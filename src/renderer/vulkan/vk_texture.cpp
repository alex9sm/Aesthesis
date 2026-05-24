#include "vk_pch.hpp"

// stb_image uses fopen/sscanf etc. which MSVC flags as deprecated.
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "vk_texture.hpp"
#include "vk_init.hpp"
#include "vk_memory.hpp"
#include "vk_frame.hpp"
#include "vk_globals.hpp"
#include "log.hpp"
#include "memory.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace vk {

	struct TextureGPU {
		VkImage        image;
		VmaAllocation  alloc;
		VkImageView    view;
		u32            width;
		u32            height;
		u32            mip_levels;
	};

	static TextureGPU textures[MAX_TEXTURES] = {};
	static VkSampler  default_sampler = VK_NULL_HANDLE;

	// --- helpers ---

	static u32 mip_count_for(u32 width, u32 height) {
		u32 m = (width > height) ? width : height;
		u32 levels = 1;
		while (m > 1) { m >>= 1; levels++; }
		return levels;
	}

	static TextureHandle find_free_slot() {
		// slots 0..2 are reserved for engine defaults; allocate user textures from 3+
		for (u32 i = 3; i < MAX_TEXTURES; i++) {
			if (textures[i].image == VK_NULL_HANDLE) return i;
		}
		return INVALID_TEXTURE;
	}

	static bool create_sampler() {
		Context& c = context();

		VkSamplerCreateInfo ci = {};
		ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		ci.magFilter = VK_FILTER_LINEAR;
		ci.minFilter = VK_FILTER_LINEAR;
		ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		ci.mipLodBias = 0.0f;
		ci.anisotropyEnable = VK_FALSE;
		ci.compareEnable = VK_FALSE;
		ci.minLod = 0.0f;
		ci.maxLod = VK_LOD_CLAMP_NONE;
		ci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
		ci.unnormalizedCoordinates = VK_FALSE;

		if (vkCreateSampler(c.device, &ci, nullptr, &default_sampler) != VK_SUCCESS) {
			logger::fatal("Failed to create default sampler");
			return false;
		}
		return true;
	}

	// writes textures[slot] into binding 3 array element `slot` for every frame's global set.
	static void write_descriptor(TextureHandle slot) {
		Context& c = context();
		VkDescriptorImageInfo ii = {};
		ii.sampler = default_sampler;
		ii.imageView = textures[slot].view;
		ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		for (u32 fi = 0; fi < FRAMES_IN_FLIGHT; fi++) {
			VkWriteDescriptorSet w = {};
			w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			w.dstSet = global_set_for_frame(fi);
			w.dstBinding = 3;
			w.dstArrayElement = slot;
			w.descriptorCount = 1;
			w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			w.pImageInfo = &ii;
			vkUpdateDescriptorSets(c.device, 1, &w, 0, nullptr);
		}
	}

	// staging upload + image creation + mip chain generation via vkCmdBlitImage.
	static bool upload_image(const u8* rgba, u32 width, u32 height, TextureGPU* out) {
		Context& c = context();
		VmaAllocator a = allocator();

		u32 mip_levels = mip_count_for(width, height);
		VkDeviceSize byte_size = (VkDeviceSize)width * height * 4;

		// staging buffer (host-visible)
		VkBuffer staging = VK_NULL_HANDLE;
		VmaAllocation staging_alloc = {};
		VmaAllocationInfo staging_info = {};

		VkBufferCreateInfo sb_ci = {};
		sb_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		sb_ci.size = byte_size;
		sb_ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		sb_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo sb_alloc_ci = {};
		sb_alloc_ci.usage = VMA_MEMORY_USAGE_AUTO;
		sb_alloc_ci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
			| VMA_ALLOCATION_CREATE_MAPPED_BIT;

		if (vmaCreateBuffer(a, &sb_ci, &sb_alloc_ci, &staging, &staging_alloc, &staging_info) != VK_SUCCESS) {
			logger::error("vmaCreateBuffer (texture staging) failed");
			return false;
		}
		memory::copy(staging_info.pMappedData, rgba, (usize)byte_size);

		// destination image
		VkImageCreateInfo img_ci = {};
		img_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		img_ci.imageType = VK_IMAGE_TYPE_2D;
		img_ci.format = VK_FORMAT_R8G8B8A8_UNORM;
		img_ci.extent = { width, height, 1 };
		img_ci.mipLevels = mip_levels;
		img_ci.arrayLayers = 1;
		img_ci.samples = VK_SAMPLE_COUNT_1_BIT;
		img_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
		img_ci.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT  // for blit-down mip chain
			| VK_IMAGE_USAGE_TRANSFER_DST_BIT
			| VK_IMAGE_USAGE_SAMPLED_BIT;
		img_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		img_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VmaAllocationCreateInfo img_alloc_ci = {};
		img_alloc_ci.usage = VMA_MEMORY_USAGE_AUTO;

		if (vmaCreateImage(a, &img_ci, &img_alloc_ci, &out->image, &out->alloc, nullptr) != VK_SUCCESS) {
			logger::error("vmaCreateImage (texture) failed");
			vmaDestroyBuffer(a, staging, staging_alloc);
			return false;
		}

		// transient command buffer
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

		// transition all mips: UNDEFINED -> TRANSFER_DST
		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = out->image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = mip_levels;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &barrier);

		// copy staging buffer -> mip 0
		VkBufferImageCopy region = {};
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageOffset = { 0, 0, 0 };
		region.imageExtent = { width, height, 1 };
		vkCmdCopyBufferToImage(cmd, staging, out->image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		// generate mip chain: blit mip[i-1] -> mip[i]
		i32 src_w = (i32)width;
		i32 src_h = (i32)height;
		for (u32 i = 1; i < mip_levels; i++) {
			// transition mip[i-1] from TRANSFER_DST -> TRANSFER_SRC
			barrier.subresourceRange.baseMipLevel = i - 1;
			barrier.subresourceRange.levelCount = 1;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			vkCmdPipelineBarrier(cmd,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);

			i32 dst_w = (src_w > 1) ? src_w / 2 : 1;
			i32 dst_h = (src_h > 1) ? src_h / 2 : 1;

			VkImageBlit blit = {};
			blit.srcOffsets[0] = { 0, 0, 0 };
			blit.srcOffsets[1] = { src_w, src_h, 1 };
			blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.srcSubresource.mipLevel = i - 1;
			blit.srcSubresource.baseArrayLayer = 0;
			blit.srcSubresource.layerCount = 1;
			blit.dstOffsets[0] = { 0, 0, 0 };
			blit.dstOffsets[1] = { dst_w, dst_h, 1 };
			blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.dstSubresource.mipLevel = i;
			blit.dstSubresource.baseArrayLayer = 0;
			blit.dstSubresource.layerCount = 1;
			vkCmdBlitImage(cmd,
				out->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				out->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &blit, VK_FILTER_LINEAR);

			// mip[i-1] -> SHADER_READ_ONLY
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(cmd,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);

			src_w = dst_w;
			src_h = dst_h;
		}

		// last mip is still TRANSFER_DST -> SHADER_READ_ONLY
		barrier.subresourceRange.baseMipLevel = mip_levels - 1;
		barrier.subresourceRange.levelCount = 1;
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

		// image view (all mip levels)
		VkImageViewCreateInfo view_ci = {};
		view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view_ci.image = out->image;
		view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view_ci.format = VK_FORMAT_R8G8B8A8_UNORM;
		view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		view_ci.subresourceRange.baseMipLevel = 0;
		view_ci.subresourceRange.levelCount = mip_levels;
		view_ci.subresourceRange.baseArrayLayer = 0;
		view_ci.subresourceRange.layerCount = 1;
		if (vkCreateImageView(c.device, &view_ci, nullptr, &out->view) != VK_SUCCESS) {
			logger::error("vkCreateImageView (texture) failed");
			vmaDestroyImage(a, out->image, out->alloc);
			out->image = VK_NULL_HANDLE;
			out->alloc = {};
			return false;
		}

		out->width = width;
		out->height = height;
		out->mip_levels = mip_levels;
		return true;
	}

	static TextureHandle create_in_slot(TextureHandle slot, const u8* rgba, u32 w, u32 h) {
		if (!upload_image(rgba, w, h, &textures[slot])) {
			return INVALID_TEXTURE;
		}
		write_descriptor(slot);
		return slot;
	}

	// --- init/shutdown ---

	static bool create_reserved_textures() {
		// slot 0: 1x1 white (default albedo)
		u8 white[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
		if (create_in_slot(TEX_DEFAULT_ALBEDO, white, 1, 1) == INVALID_TEXTURE) return false;

		// slot 1: 1x1 flat-normal (0.5, 0.5, 1) -> (128, 128, 255)
		u8 flat_normal[4] = { 0x80, 0x80, 0xFF, 0xFF };
		if (create_in_slot(TEX_DEFAULT_NORMAL, flat_normal, 1, 1) == INVALID_TEXTURE) return false;

		// slot 2: 1x1 ORM neutral: AO=1 in R, roughness=1 in G, metallic=0 in B
		u8 orm[4] = { 0xFF, 0xFF, 0x00, 0xFF };
		if (create_in_slot(TEX_DEFAULT_ORM, orm, 1, 1) == INVALID_TEXTURE) return false;

		return true;
	}

	bool init_textures() {
		memory::set(textures, 0, sizeof(textures));
		if (!create_sampler()) return false;
		if (!create_reserved_textures()) return false;
		return true;
	}

	void shutdown_textures() {
		Context& c = context();
		VmaAllocator a = allocator();

		for (u32 i = 0; i < MAX_TEXTURES; i++) {
			if (textures[i].view)  vkDestroyImageView(c.device, textures[i].view, nullptr);
			if (textures[i].image) vmaDestroyImage(a, textures[i].image, textures[i].alloc);
		}
		memory::set(textures, 0, sizeof(textures));

		if (default_sampler) {
			vkDestroySampler(c.device, default_sampler, nullptr);
			default_sampler = VK_NULL_HANDLE;
		}
	}

	// --- public ---

	TextureHandle load_texture(const char* path) {
		int w = 0, h = 0, ch = 0;
		stbi_uc* data = stbi_load(path, &w, &h, &ch, 4);
		if (!data) {
			logger::error("stbi_load failed: %s", path);
			return INVALID_TEXTURE;
		}
		TextureHandle handle = load_texture_pixels(data, (u32)w, (u32)h);
		stbi_image_free(data);
		return handle;
	}

	TextureHandle load_texture_pixels(const u8* rgba, u32 width, u32 height) {
		if (!rgba || width == 0 || height == 0) return INVALID_TEXTURE;
		TextureHandle slot = find_free_slot();
		if (slot == INVALID_TEXTURE) {
			logger::error("Out of texture slots");
			return INVALID_TEXTURE;
		}
		return create_in_slot(slot, rgba, width, height);
	}

	void unload_texture(TextureHandle handle) {
		if (handle == INVALID_TEXTURE || handle >= MAX_TEXTURES) return;
		if (handle < 3) {
			logger::error("Attempt to unload reserved texture slot %u", handle);
			return;
		}
		Context& c = context();
		VmaAllocator a = allocator();
		TextureGPU& t = textures[handle];
		if (t.view)  vkDestroyImageView(c.device, t.view, nullptr);
		if (t.image) vmaDestroyImage(a, t.image, t.alloc);
		t = {};
	}

}
