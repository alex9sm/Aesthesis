#include "vk_cubemap.hpp"
#include "vk_init.hpp"
#include "vk_memory.hpp"
#include "vk_ibl.hpp"
#include "cubemap.hpp"
#include "log.hpp"
#include "memory.hpp"

namespace vk {

	static CubemapSlot cubemaps[MAX_CUBEMAPS] = {};
	static VkSampler   cube_sampler = VK_NULL_HANDLE;

	// --- helpers ---

	static u32 mip_count_for(u32 size) {
		u32 m = size;
		u32 levels = 1;
		while (m > 1) { m >>= 1; levels++; }
		return levels;
	}

	static CubemapHandle find_free_slot() {
		for (u32 i = 0; i < MAX_CUBEMAPS; i++) {
			if (!cubemaps[i].in_use) return i;
		}
		return INVALID_CUBEMAP;
	}

	static bool create_sampler() {
		Context& c = context();

		// Cubemaps need CLAMP_TO_EDGE on all axes to avoid visible face seams
		// from the linear filter sampling into neighboring face texels.
		VkSamplerCreateInfo ci = {};
		ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		ci.magFilter = VK_FILTER_LINEAR;
		ci.minFilter = VK_FILTER_LINEAR;
		ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		ci.mipLodBias = 0.0f;
		ci.anisotropyEnable = VK_FALSE;
		ci.compareEnable = VK_FALSE;
		ci.minLod = 0.0f;
		ci.maxLod = VK_LOD_CLAMP_NONE;
		ci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
		ci.unnormalizedCoordinates = VK_FALSE;

		if (vkCreateSampler(c.device, &ci, nullptr, &cube_sampler) != VK_SUCCESS) {
			logger::fatal("Failed to create cubemap sampler");
			return false;
		}
		return true;
	}

	// Uploads 6 RGBA8 face buffers, generates a full mip chain via blit, and
	// produces a `VIEW_TYPE_CUBE` image view.
	static bool upload_cubemap(const renderer::CubemapFaces& faces, CubemapSlot* out) {
		Context& c = context();
		VmaAllocator a = allocator();

		u32 size = faces.size;
		u32 mip_levels = mip_count_for(size);
		VkDeviceSize face_bytes = (VkDeviceSize)size * size * 4;
		VkDeviceSize total_bytes = face_bytes * 6;

		// --- staging buffer holding all 6 faces packed back-to-back ---
		VkBuffer staging = VK_NULL_HANDLE;
		VmaAllocation staging_alloc = {};
		VmaAllocationInfo staging_info = {};

		VkBufferCreateInfo sb_ci = {};
		sb_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		sb_ci.size = total_bytes;
		sb_ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		sb_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo sb_alloc_ci = {};
		sb_alloc_ci.usage = VMA_MEMORY_USAGE_AUTO;
		sb_alloc_ci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
			| VMA_ALLOCATION_CREATE_MAPPED_BIT;

		if (vmaCreateBuffer(a, &sb_ci, &sb_alloc_ci, &staging, &staging_alloc, &staging_info) != VK_SUCCESS) {
			logger::error("vmaCreateBuffer (cubemap staging) failed");
			return false;
		}

		u8* dst = (u8*)staging_info.pMappedData;
		for (u32 f = 0; f < 6; f++) {
			memory::copy(dst + f * face_bytes, faces.pixels[f], (usize)face_bytes);
		}

		// --- destination image: cube-compatible, 6 array layers, full mip chain ---
		VkImageCreateInfo img_ci = {};
		img_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		img_ci.imageType = VK_IMAGE_TYPE_2D;
		img_ci.format = VK_FORMAT_R8G8B8A8_SRGB;
		img_ci.extent = { size, size, 1 };
		img_ci.mipLevels = mip_levels;
		img_ci.arrayLayers = 6;
		img_ci.samples = VK_SAMPLE_COUNT_1_BIT;
		img_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
		img_ci.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT
			| VK_IMAGE_USAGE_TRANSFER_DST_BIT
			| VK_IMAGE_USAGE_SAMPLED_BIT;
		img_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		img_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		img_ci.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

		VmaAllocationCreateInfo img_alloc_ci = {};
		img_alloc_ci.usage = VMA_MEMORY_USAGE_AUTO;

		if (vmaCreateImage(a, &img_ci, &img_alloc_ci, &out->source_image, &out->source_alloc, nullptr) != VK_SUCCESS) {
			logger::error("vmaCreateImage (cubemap source) failed");
			vmaDestroyBuffer(a, staging, staging_alloc);
			return false;
		}

		// --- transient command buffer ---
		VkCommandPool pool = VK_NULL_HANDLE;
		VkCommandPoolCreateInfo pool_ci = {};
		pool_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		pool_ci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		pool_ci.queueFamilyIndex = c.graphics_queue_index;
		vkCreateCommandPool(c.device, &pool_ci, nullptr, &pool);

		VkCommandBuffer cmd = VK_NULL_HANDLE;
		VkCommandBufferAllocateInfo ai = {};
		ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		ai.commandPool = pool;
		ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		ai.commandBufferCount = 1;
		vkAllocateCommandBuffers(c.device, &ai, &cmd);

		VkCommandBufferBeginInfo bi = {};
		bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(cmd, &bi);

		// --- transition all mips / all 6 layers UNDEFINED -> TRANSFER_DST ---
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = out->source_image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = mip_levels;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 6;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		// --- copy staging buffer -> mip 0, six regions (one per layer) ---
		VkBufferImageCopy regions[6] = {};
		for (u32 f = 0; f < 6; f++) {
			regions[f].bufferOffset = f * face_bytes;
			regions[f].bufferRowLength = 0;
			regions[f].bufferImageHeight = 0;
			regions[f].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			regions[f].imageSubresource.mipLevel = 0;
			regions[f].imageSubresource.baseArrayLayer = f;
			regions[f].imageSubresource.layerCount = 1;
			regions[f].imageOffset = { 0, 0, 0 };
			regions[f].imageExtent = { size, size, 1 };
		}
		vkCmdCopyBufferToImage(cmd, staging, out->source_image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6, regions);

		// --- generate mip chain across all 6 layers ---
		i32 src_sz = (i32)size;
		for (u32 i = 1; i < mip_levels; i++) {
			// mip[i-1] TRANSFER_DST -> TRANSFER_SRC, all 6 layers in one barrier
			barrier.subresourceRange.baseMipLevel = i - 1;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 6;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier(cmd,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);

			i32 dst_sz = (src_sz > 1) ? src_sz / 2 : 1;

			VkImageBlit blits[6] = {};
			for (u32 f = 0; f < 6; f++) {
				blits[f].srcOffsets[0] = { 0, 0, 0 };
				blits[f].srcOffsets[1] = { src_sz, src_sz, 1 };
				blits[f].srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				blits[f].srcSubresource.mipLevel = i - 1;
				blits[f].srcSubresource.baseArrayLayer = f;
				blits[f].srcSubresource.layerCount = 1;
				blits[f].dstOffsets[0] = { 0, 0, 0 };
				blits[f].dstOffsets[1] = { dst_sz, dst_sz, 1 };
				blits[f].dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				blits[f].dstSubresource.mipLevel = i;
				blits[f].dstSubresource.baseArrayLayer = f;
				blits[f].dstSubresource.layerCount = 1;
			}
			vkCmdBlitImage(cmd,
				out->source_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				out->source_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				6, blits, VK_FILTER_LINEAR);

			// mip[i-1] TRANSFER_SRC -> SHADER_READ_ONLY
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(cmd,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);

			src_sz = dst_sz;
		}

		// --- last mip is still TRANSFER_DST -> SHADER_READ_ONLY (all 6 layers) ---
		barrier.subresourceRange.baseMipLevel = mip_levels - 1;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 6;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		vkEndCommandBuffer(cmd);

		VkSubmitInfo submit = {};
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &cmd;
		vkQueueSubmit(c.graphics_queue, 1, &submit, VK_NULL_HANDLE);
		vkQueueWaitIdle(c.graphics_queue);

		vkDestroyCommandPool(c.device, pool, nullptr);
		vmaDestroyBuffer(a, staging, staging_alloc);

		// --- cube image view (all 6 layers, all mips) ---
		VkImageViewCreateInfo view_ci = {};
		view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view_ci.image = out->source_image;
		view_ci.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		view_ci.format = VK_FORMAT_R8G8B8A8_SRGB;
		view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		view_ci.subresourceRange.baseMipLevel = 0;
		view_ci.subresourceRange.levelCount = mip_levels;
		view_ci.subresourceRange.baseArrayLayer = 0;
		view_ci.subresourceRange.layerCount = 6;
		if (vkCreateImageView(c.device, &view_ci, nullptr, &out->source_view) != VK_SUCCESS) {
			logger::error("vkCreateImageView (cubemap) failed");
			vmaDestroyImage(a, out->source_image, out->source_alloc);
			out->source_image = VK_NULL_HANDLE;
			out->source_alloc = {};
			return false;
		}

		out->size = size;
		out->mip_levels = mip_levels;
		return true;
	}

	// --- init / shutdown ---

	bool init_cubemaps() {
		memory::set(cubemaps, 0, sizeof(cubemaps));
		if (!create_sampler()) return false;
		return true;
	}

	void shutdown_cubemaps() {
		Context& c = context();
		VmaAllocator a = allocator();

		for (u32 i = 0; i < MAX_CUBEMAPS; i++) {
			CubemapSlot& s = cubemaps[i];
			if (s.irradiance_view)  vkDestroyImageView(c.device, s.irradiance_view, nullptr);
			if (s.irradiance_image) vmaDestroyImage(a, s.irradiance_image, s.irradiance_alloc);
			if (s.source_view)      vkDestroyImageView(c.device, s.source_view, nullptr);
			if (s.source_image)     vmaDestroyImage(a, s.source_image, s.source_alloc);
		}
		memory::set(cubemaps, 0, sizeof(cubemaps));

		if (cube_sampler) {
			vkDestroySampler(c.device, cube_sampler, nullptr);
			cube_sampler = VK_NULL_HANDLE;
		}
	}

	// --- public ---

	CubemapHandle load_cubemap(const char* name, f32 intensity) {
		renderer::CubemapFaces faces = {};
		if (!renderer::load_cubemap_faces(name, &faces)) {
			return INVALID_CUBEMAP;
		}

		CubemapHandle slot = find_free_slot();
		if (slot == INVALID_CUBEMAP) {
			logger::error("Out of cubemap slots");
			renderer::free_cubemap_faces(&faces);
			return INVALID_CUBEMAP;
		}

		CubemapSlot& cm = cubemaps[slot];
		if (!upload_cubemap(faces, &cm)) {
			renderer::free_cubemap_faces(&faces);
			return INVALID_CUBEMAP;
		}
		cm.intensity = intensity;
		cm.in_use = true;

		renderer::free_cubemap_faces(&faces);

		// IBL bake: irradiance now, prefilter once Phase F4 lands. Source is
		// already SHADER_READ_ONLY_OPTIMAL coming out of upload_cubemap.
		if (!bake_irradiance(slot)) {
			logger::error("IBL irradiance bake failed for cubemap '%s'", name);
			unload_cubemap(slot);
			return INVALID_CUBEMAP;
		}

		logger::info("Loaded cubemap '%s' [%u]: %ux%u, %u mips, intensity=%.2f",
			name, slot, cm.size, cm.size, cm.mip_levels, intensity);
		return slot;
	}

	void unload_cubemap(CubemapHandle handle) {
		if (handle == INVALID_CUBEMAP || handle >= MAX_CUBEMAPS) return;
		CubemapSlot& s = cubemaps[handle];
		if (!s.in_use) return;

		Context& c = context();
		VmaAllocator a = allocator();
		// Wait for any in-flight frame that might still be sampling this
		// cubemap's irradiance via set-0 binding 4.
		vkDeviceWaitIdle(c.device);

		if (s.irradiance_view)  vkDestroyImageView(c.device, s.irradiance_view, nullptr);
		if (s.irradiance_image) vmaDestroyImage(a, s.irradiance_image, s.irradiance_alloc);
		if (s.source_view)      vkDestroyImageView(c.device, s.source_view, nullptr);
		if (s.source_image)     vmaDestroyImage(a, s.source_image, s.source_alloc);
		s = {};
	}

	const CubemapSlot* get_cubemap(CubemapHandle handle) {
		if (handle == INVALID_CUBEMAP || handle >= MAX_CUBEMAPS) return nullptr;
		if (!cubemaps[handle].in_use) return nullptr;
		return &cubemaps[handle];
	}

}
