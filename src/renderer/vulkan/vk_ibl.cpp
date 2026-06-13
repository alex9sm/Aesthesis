#include "vk_pch.hpp"

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "vk_ibl.hpp"
#include "vk_init.hpp"
#include "vk_memory.hpp"
#include "vk_frame.hpp"
#include "vk_globals.hpp"
#include "vk_pipeline.hpp"
#include "vk_mem_alloc.h"
#include "log.hpp"
#include "memory.hpp"

// stb_image's IMPLEMENTATION is in vk_texture.cpp.
#include "stb_image.h"

namespace vk {

	static constexpr u32 BRDF_LUT_SIZE       = 256;
	static constexpr u32 IRRADIANCE_SIZE     = 32;
	static constexpr u32 PREFILTER_SIZE      = 128;
	static constexpr u32 PREFILTER_MIP_COUNT = 5;
	static constexpr const char* BRDF_LUT_PNG = "assets/textures/global/brdf_lut.png";

	struct PrefilterPC {
		f32 roughness;
		u32 mip_size;
		f32 intensity;
	};

	struct IrradiancePC {
		f32 intensity;
	};

	struct IblImage {
		VkImage       image;
		VmaAllocation alloc;
		VkImageView   view;
	};

	static IblImage  brdf_lut          = {};
	static IblImage  placeholder_irr   = {};
	static IblImage  placeholder_pref  = {};
	static VkSampler ibl_sampler       = VK_NULL_HANDLE;

	// persistent irradiance bake compute pipeline (built once at init_ibl)
	static VkDescriptorSetLayout irr_set_layout      = VK_NULL_HANDLE;
	static VkPipelineLayout      irr_pipeline_layout = VK_NULL_HANDLE;
	static VkPipeline            irr_pipeline        = VK_NULL_HANDLE;

	// persistent prefilter bake compute pipeline (built once at init_ibl)
	static VkDescriptorSetLayout pref_set_layout      = VK_NULL_HANDLE;
	static VkPipelineLayout      pref_pipeline_layout = VK_NULL_HANDLE;
	static VkPipeline            pref_pipeline        = VK_NULL_HANDLE;

	// currently active environment cubemap; INVALID_CUBEMAP -> use placeholders
	static CubemapHandle active_env = INVALID_CUBEMAP;

	// --- helpers ---

	// fp32 -> IEEE 754 half-float (fp16). Subnormals and special encodings are
	// flushed to zero / inf; sufficient for typical BRDF-LUT values in [0,1].
	static u16 float_to_half(f32 f) {
		union { u32 u; f32 f; } v;
		v.f = f;
		u32 b = v.u;
		u32 sign = (b >> 16) & 0x8000;
		i32 e = (i32)((b >> 23) & 0xFF) - 127 + 15;
		u32 m = b & 0x7FFFFF;
		if (e <= 0) return (u16)sign;          // flush subnormals to zero
		if (e >= 31) return (u16)(sign | 0x7C00); // overflow to inf
		return (u16)(sign | ((u32)e << 10) | (m >> 13));
	}

	static bool create_sampler() {
		Context& c = context();
		VkSamplerCreateInfo s = {};
		s.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		s.magFilter = VK_FILTER_LINEAR;
		s.minFilter = VK_FILTER_LINEAR;
		s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		s.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		s.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		s.minLod = 0.0f;
		s.maxLod = VK_LOD_CLAMP_NONE;
		s.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
		if (vkCreateSampler(c.device, &s, nullptr, &ibl_sampler) != VK_SUCCESS) {
			logger::fatal("Failed to create IBL sampler");
			return false;
		}
		return true;
	}

	static VkCommandBuffer begin_one_shot(VkCommandPool* out_pool) {
		Context& c = context();

		VkCommandPoolCreateInfo pci = {};
		pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		pci.queueFamilyIndex = c.graphics_queue_index;
		vkCreateCommandPool(c.device, &pci, nullptr, out_pool);

		VkCommandBufferAllocateInfo ai = {};
		ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		ai.commandPool = *out_pool;
		ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		ai.commandBufferCount = 1;
		VkCommandBuffer cmd = VK_NULL_HANDLE;
		vkAllocateCommandBuffers(c.device, &ai, &cmd);

		VkCommandBufferBeginInfo bi = {};
		bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(cmd, &bi);
		return cmd;
	}

	static void end_one_shot(VkCommandBuffer cmd, VkCommandPool pool) {
		Context& c = context();
		vkEndCommandBuffer(cmd);

		VkSubmitInfo si = {};
		si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		si.commandBufferCount = 1;
		si.pCommandBuffers = &cmd;
		vkQueueSubmit(c.graphics_queue, 1, &si, VK_NULL_HANDLE);
		vkQueueWaitIdle(c.graphics_queue);
		vkDestroyCommandPool(c.device, pool, nullptr);
	}

	static void simple_barrier(VkCommandBuffer cmd, VkImage image,
		VkImageLayout from, VkImageLayout to,
		VkAccessFlags src_access, VkAccessFlags dst_access,
		VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage,
		u32 layer_count = 1)
	{
		VkImageMemoryBarrier b = {};
		b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.image = image;
		b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		b.subresourceRange.baseMipLevel = 0;
		b.subresourceRange.levelCount = 1;
		b.subresourceRange.baseArrayLayer = 0;
		b.subresourceRange.layerCount = layer_count;
		b.oldLayout = from;
		b.newLayout = to;
		b.srcAccessMask = src_access;
		b.dstAccessMask = dst_access;
		vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &b);
	}

	// --- 2D image (BRDF LUT) ---

	static bool create_brdf_lut_image() {
		VmaAllocator a = allocator();
		Context& c = context();

		VkImageCreateInfo ci = {};
		ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		ci.imageType = VK_IMAGE_TYPE_2D;
		ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		ci.extent = { BRDF_LUT_SIZE, BRDF_LUT_SIZE, 1 };
		ci.mipLevels = 1;
		ci.arrayLayers = 1;
		ci.samples = VK_SAMPLE_COUNT_1_BIT;
		ci.tiling = VK_IMAGE_TILING_OPTIMAL;
		ci.usage = VK_IMAGE_USAGE_STORAGE_BIT
			| VK_IMAGE_USAGE_TRANSFER_DST_BIT
			| VK_IMAGE_USAGE_SAMPLED_BIT;
		ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VmaAllocationCreateInfo aci = {};
		aci.usage = VMA_MEMORY_USAGE_AUTO;
		if (vmaCreateImage(a, &ci, &aci, &brdf_lut.image, &brdf_lut.alloc, nullptr) != VK_SUCCESS) {
			logger::fatal("Failed to create BRDF LUT image");
			return false;
		}

		VkImageViewCreateInfo vci = {};
		vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		vci.image = brdf_lut.image;
		vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		vci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		vci.subresourceRange.baseMipLevel = 0;
		vci.subresourceRange.levelCount = 1;
		vci.subresourceRange.baseArrayLayer = 0;
		vci.subresourceRange.layerCount = 1;
		if (vkCreateImageView(c.device, &vci, nullptr, &brdf_lut.view) != VK_SUCCESS) {
			logger::fatal("Failed to create BRDF LUT view");
			return false;
		}
		return true;
	}

	// Try to load the shipped BRDF LUT PNG. PNG R/G channels carry scale/bias,
	// B/A are ignored. Returns false if the file is missing or unexpected size.
	static bool try_load_brdf_lut_png() {
		int w = 0, h = 0, ch = 0;
		stbi_uc* data = stbi_load(BRDF_LUT_PNG, &w, &h, &ch, 4);
		if (!data) {
			return false;
		}
		if (w != (int)BRDF_LUT_SIZE || h != (int)BRDF_LUT_SIZE) {
			logger::warn("BRDF LUT PNG has unexpected size %dx%d (expected %ux%u); falling back to compute bake",
				w, h, BRDF_LUT_SIZE, BRDF_LUT_SIZE);
			stbi_image_free(data);
			return false;
		}

		// build an RGBA16F buffer from RGBA8 (only R/G are meaningful)
		VkDeviceSize byte_size = (VkDeviceSize)w * h * 8; // 4 channels * 2 bytes
		VmaAllocator a = allocator();
		Context& c = context();

		VkBuffer staging = VK_NULL_HANDLE;
		VmaAllocation staging_alloc = {};
		VmaAllocationInfo staging_info = {};

		VkBufferCreateInfo bci = {};
		bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bci.size = byte_size;
		bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo sb_aci = {};
		sb_aci.usage = VMA_MEMORY_USAGE_AUTO;
		sb_aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
			| VMA_ALLOCATION_CREATE_MAPPED_BIT;

		if (vmaCreateBuffer(a, &bci, &sb_aci, &staging, &staging_alloc, &staging_info) != VK_SUCCESS) {
			logger::error("BRDF LUT PNG staging buffer create failed");
			stbi_image_free(data);
			return false;
		}

		u16* dst = (u16*)staging_info.pMappedData;
		const f32 inv = 1.0f / 255.0f;
		u32 texels = (u32)(w * h);
		for (u32 i = 0; i < texels; i++) {
			f32 r = (f32)data[i * 4 + 0] * inv;
			f32 g = (f32)data[i * 4 + 1] * inv;
			dst[i * 4 + 0] = float_to_half(r);
			dst[i * 4 + 1] = float_to_half(g);
			dst[i * 4 + 2] = 0;
			dst[i * 4 + 3] = 0;
		}
		stbi_image_free(data);

		VkCommandPool pool = VK_NULL_HANDLE;
		VkCommandBuffer cmd = begin_one_shot(&pool);

		simple_barrier(cmd, brdf_lut.image,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			0, VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

		VkBufferImageCopy region = {};
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.layerCount = 1;
		region.imageExtent = { BRDF_LUT_SIZE, BRDF_LUT_SIZE, 1 };
		vkCmdCopyBufferToImage(cmd, staging, brdf_lut.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		simple_barrier(cmd, brdf_lut.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

		end_one_shot(cmd, pool);
		vmaDestroyBuffer(a, staging, staging_alloc);

		logger::info("BRDF LUT loaded from %s (%ux%u)", BRDF_LUT_PNG, BRDF_LUT_SIZE, BRDF_LUT_SIZE);
		return true;
	}

	// Compute fallback: bake the BRDF LUT with brdf_lut.comp.
	static bool bake_brdf_lut_compute() {
		Context& c = context();

		// descriptor set layout: single storage image
		VkDescriptorSetLayoutBinding b = {};
		b.binding = 0;
		b.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		b.descriptorCount = 1;
		b.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		VkDescriptorSetLayoutCreateInfo lci = {};
		lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		lci.bindingCount = 1;
		lci.pBindings = &b;
		VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
		if (vkCreateDescriptorSetLayout(c.device, &lci, nullptr, &set_layout) != VK_SUCCESS) {
			logger::fatal("BRDF LUT bake: create descriptor set layout failed");
			return false;
		}

		ComputePipelineSpec cspec = {};
		cspec.cs_path = "shaders/spv/brdf_lut.comp.spv";
		cspec.set_layouts = &set_layout;
		cspec.set_layout_count = 1;
		VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
		VkPipeline pipeline = VK_NULL_HANDLE;
		if (!create_compute_pipeline(cspec, &pipeline, &pipeline_layout)) {
			vkDestroyDescriptorSetLayout(c.device, set_layout, nullptr);
			return false;
		}

		// descriptor pool + set
		VkDescriptorPoolSize ps = {};
		ps.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		ps.descriptorCount = 1;
		VkDescriptorPoolCreateInfo pci = {};
		pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pci.maxSets = 1;
		pci.poolSizeCount = 1;
		pci.pPoolSizes = &ps;
		VkDescriptorPool pool = VK_NULL_HANDLE;
		vkCreateDescriptorPool(c.device, &pci, nullptr, &pool);

		VkDescriptorSetAllocateInfo ai = {};
		ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		ai.descriptorPool = pool;
		ai.descriptorSetCount = 1;
		ai.pSetLayouts = &set_layout;
		VkDescriptorSet set = VK_NULL_HANDLE;
		vkAllocateDescriptorSets(c.device, &ai, &set);

		VkDescriptorImageInfo ii = {};
		ii.imageView = brdf_lut.view;
		ii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		VkWriteDescriptorSet w = {};
		w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		w.dstSet = set;
		w.dstBinding = 0;
		w.descriptorCount = 1;
		w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		w.pImageInfo = &ii;
		vkUpdateDescriptorSets(c.device, 1, &w, 0, nullptr);

		// dispatch
		VkCommandPool cmd_pool = VK_NULL_HANDLE;
		VkCommandBuffer cmd = begin_one_shot(&cmd_pool);

		simple_barrier(cmd, brdf_lut.image,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
			0, VK_ACCESS_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout,
			0, 1, &set, 0, nullptr);
		// 8×8 workgroups across the 256×256 LUT
		vkCmdDispatch(cmd, BRDF_LUT_SIZE / 8, BRDF_LUT_SIZE / 8, 1);

		simple_barrier(cmd, brdf_lut.image,
			VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

		end_one_shot(cmd, cmd_pool);

		// cleanup one-shot bake resources
		vkDestroyDescriptorPool(c.device, pool, nullptr);
		vkDestroyPipeline(c.device, pipeline, nullptr);
		vkDestroyPipelineLayout(c.device, pipeline_layout, nullptr);
		vkDestroyDescriptorSetLayout(c.device, set_layout, nullptr);

		logger::info("BRDF LUT baked via compute (%ux%u)", BRDF_LUT_SIZE, BRDF_LUT_SIZE);
		return true;
	}

	// --- placeholder cubemaps ---

	// Creates a 1×1×6 RGBA16F cubemap filled with `value` (RGB), alpha=1.
	// Used as the neutral pre-set_environment default for irradiance + prefilter.
	static bool create_placeholder_cube(IblImage* out, f32 value) {
		Context& c = context();
		VmaAllocator a = allocator();

		VkImageCreateInfo ci = {};
		ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		ci.imageType = VK_IMAGE_TYPE_2D;
		ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		ci.extent = { 1, 1, 1 };
		ci.mipLevels = 1;
		ci.arrayLayers = 6;
		ci.samples = VK_SAMPLE_COUNT_1_BIT;
		ci.tiling = VK_IMAGE_TILING_OPTIMAL;
		ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		ci.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

		VmaAllocationCreateInfo aci = {};
		aci.usage = VMA_MEMORY_USAGE_AUTO;
		if (vmaCreateImage(a, &ci, &aci, &out->image, &out->alloc, nullptr) != VK_SUCCESS) {
			logger::fatal("Failed to create IBL placeholder cubemap");
			return false;
		}

		// upload one RGBA16F texel per face
		u16 texel[4] = {
			float_to_half(value),
			float_to_half(value),
			float_to_half(value),
			float_to_half(1.0f),
		};
		VkDeviceSize bytes_per_face = sizeof(texel);
		VkDeviceSize total = bytes_per_face * 6;

		VkBuffer staging = VK_NULL_HANDLE;
		VmaAllocation staging_alloc = {};
		VmaAllocationInfo staging_info = {};

		VkBufferCreateInfo bci = {};
		bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bci.size = total;
		bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo sb = {};
		sb.usage = VMA_MEMORY_USAGE_AUTO;
		sb.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
			| VMA_ALLOCATION_CREATE_MAPPED_BIT;
		vmaCreateBuffer(a, &bci, &sb, &staging, &staging_alloc, &staging_info);

		u8* dst = (u8*)staging_info.pMappedData;
		for (u32 f = 0; f < 6; f++) {
			memory::copy(dst + f * bytes_per_face, texel, (usize)bytes_per_face);
		}

		VkCommandPool pool = VK_NULL_HANDLE;
		VkCommandBuffer cmd = begin_one_shot(&pool);

		simple_barrier(cmd, out->image,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			0, VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			6);

		VkBufferImageCopy regions[6] = {};
		for (u32 f = 0; f < 6; f++) {
			regions[f].bufferOffset = f * bytes_per_face;
			regions[f].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			regions[f].imageSubresource.mipLevel = 0;
			regions[f].imageSubresource.baseArrayLayer = f;
			regions[f].imageSubresource.layerCount = 1;
			regions[f].imageExtent = { 1, 1, 1 };
		}
		vkCmdCopyBufferToImage(cmd, staging, out->image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6, regions);

		simple_barrier(cmd, out->image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			6);

		end_one_shot(cmd, pool);
		vmaDestroyBuffer(a, staging, staging_alloc);

		VkImageViewCreateInfo vci = {};
		vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		vci.image = out->image;
		vci.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		vci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		vci.subresourceRange.baseMipLevel = 0;
		vci.subresourceRange.levelCount = 1;
		vci.subresourceRange.baseArrayLayer = 0;
		vci.subresourceRange.layerCount = 6;
		if (vkCreateImageView(c.device, &vci, nullptr, &out->view) != VK_SUCCESS) {
			logger::fatal("Failed to create IBL placeholder cube view");
			return false;
		}
		return true;
	}

	// --- descriptor writes ---

	static void write_descriptors() {
		Context& c = context();

		// Pick irradiance + prefilter views from the active environment's slot,
		// falling back to the neutral placeholders when nothing is set.
		VkImageView irr_view  = placeholder_irr.view;
		VkImageView pref_view = placeholder_pref.view;
		if (active_env != INVALID_CUBEMAP) {
			const CubemapSlot* slot = get_cubemap(active_env);
			if (slot && slot->ibl_baked) {
				if (slot->irradiance_view) irr_view  = slot->irradiance_view;
				if (slot->prefilter_view)  pref_view = slot->prefilter_view;
			}
		}

		VkDescriptorImageInfo irr_i = {};
		irr_i.sampler = ibl_sampler;
		irr_i.imageView = irr_view;
		irr_i.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkDescriptorImageInfo pref_i = {};
		pref_i.sampler = ibl_sampler;
		pref_i.imageView = pref_view;
		pref_i.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkDescriptorImageInfo lut_i = {};
		lut_i.sampler = ibl_sampler;
		lut_i.imageView = brdf_lut.view;
		lut_i.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		for (u32 fi = 0; fi < FRAMES_IN_FLIGHT; fi++) {
			VkWriteDescriptorSet writes[3] = {};
			VkDescriptorSet dst = global_set_for_frame(fi);

			writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[0].dstSet = dst;
			writes[0].dstBinding = 4;
			writes[0].descriptorCount = 1;
			writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[0].pImageInfo = &irr_i;

			writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[1].dstSet = dst;
			writes[1].dstBinding = 5;
			writes[1].descriptorCount = 1;
			writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[1].pImageInfo = &pref_i;

			writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[2].dstSet = dst;
			writes[2].dstBinding = 6;
			writes[2].descriptorCount = 1;
			writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[2].pImageInfo = &lut_i;

			vkUpdateDescriptorSets(c.device, 3, writes, 0, nullptr);
		}
	}

	// --- irradiance compute pipeline (persistent) ---

	static bool create_irradiance_pipeline() {
		Context& c = context();

		// binding 0: input samplerCube (source)
		// binding 1: output imageCube  (irradiance)
		VkDescriptorSetLayoutBinding bs[2] = {};
		bs[0].binding = 0;
		bs[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bs[0].descriptorCount = 1;
		bs[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		bs[1].binding = 1;
		bs[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		bs[1].descriptorCount = 1;
		bs[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		VkDescriptorSetLayoutCreateInfo lci = {};
		lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		lci.bindingCount = 2;
		lci.pBindings = bs;
		if (vkCreateDescriptorSetLayout(c.device, &lci, nullptr, &irr_set_layout) != VK_SUCCESS) {
			logger::fatal("Failed to create irradiance descriptor set layout");
			return false;
		}

		VkPushConstantRange ipcr = {};
		ipcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		ipcr.offset = 0;
		ipcr.size = sizeof(IrradiancePC);

		ComputePipelineSpec cspec = {};
		cspec.cs_path = "shaders/spv/irradiance.comp.spv";
		cspec.set_layouts = &irr_set_layout;
		cspec.set_layout_count = 1;
		cspec.push_constant = &ipcr;
		if (!create_compute_pipeline(cspec, &irr_pipeline, &irr_pipeline_layout)) {
			return false;
		}
		return true;
	}

	// --- internal helpers for the combined bake_ibl path ---

	// Allocate the irradiance image + view on the slot.
	static bool create_irradiance_image(CubemapSlot* slot) {
		Context&    c = context();
		VmaAllocator a = allocator();

		VkImageCreateInfo ici = {};
		ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		ici.imageType = VK_IMAGE_TYPE_2D;
		ici.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		ici.extent = { IRRADIANCE_SIZE, IRRADIANCE_SIZE, 1 };
		ici.mipLevels = 1;
		ici.arrayLayers = 6;
		ici.samples = VK_SAMPLE_COUNT_1_BIT;
		ici.tiling = VK_IMAGE_TILING_OPTIMAL;
		ici.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		ici.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

		VmaAllocationCreateInfo aci = {};
		aci.usage = VMA_MEMORY_USAGE_AUTO;
		if (vmaCreateImage(a, &ici, &aci, &slot->irradiance_image, &slot->irradiance_alloc, nullptr) != VK_SUCCESS) {
			logger::error("bake_ibl: irradiance vmaCreateImage failed");
			return false;
		}

		VkImageViewCreateInfo vci = {};
		vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		vci.image = slot->irradiance_image;
		vci.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		vci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		vci.subresourceRange.baseMipLevel = 0;
		vci.subresourceRange.levelCount = 1;
		vci.subresourceRange.baseArrayLayer = 0;
		vci.subresourceRange.layerCount = 6;
		if (vkCreateImageView(c.device, &vci, nullptr, &slot->irradiance_view) != VK_SUCCESS) {
			vmaDestroyImage(a, slot->irradiance_image, slot->irradiance_alloc);
			slot->irradiance_image = VK_NULL_HANDLE;
			slot->irradiance_alloc = {};
			logger::error("bake_ibl: irradiance vkCreateImageView failed");
			return false;
		}
		return true;
	}

	// Allocate the prefilter image, its full sampling view, and one per-mip
	// storage view (stored on slot->bake_pref_mip_views — released later by
	// release_bake_resources once the bake fence signals).
	static bool create_prefilter_image(CubemapSlot* slot) {
		Context&    c = context();
		VmaAllocator a = allocator();

		VkImageCreateInfo ici = {};
		ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		ici.imageType = VK_IMAGE_TYPE_2D;
		ici.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		ici.extent = { PREFILTER_SIZE, PREFILTER_SIZE, 1 };
		ici.mipLevels = PREFILTER_MIP_COUNT;
		ici.arrayLayers = 6;
		ici.samples = VK_SAMPLE_COUNT_1_BIT;
		ici.tiling = VK_IMAGE_TILING_OPTIMAL;
		ici.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		ici.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

		VmaAllocationCreateInfo aci = {};
		aci.usage = VMA_MEMORY_USAGE_AUTO;
		if (vmaCreateImage(a, &ici, &aci, &slot->prefilter_image, &slot->prefilter_alloc, nullptr) != VK_SUCCESS) {
			logger::error("bake_ibl: prefilter vmaCreateImage failed");
			return false;
		}

		VkImageViewCreateInfo full_vci = {};
		full_vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		full_vci.image = slot->prefilter_image;
		full_vci.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		full_vci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		full_vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		full_vci.subresourceRange.baseMipLevel = 0;
		full_vci.subresourceRange.levelCount = PREFILTER_MIP_COUNT;
		full_vci.subresourceRange.baseArrayLayer = 0;
		full_vci.subresourceRange.layerCount = 6;
		if (vkCreateImageView(c.device, &full_vci, nullptr, &slot->prefilter_view) != VK_SUCCESS) {
			vmaDestroyImage(a, slot->prefilter_image, slot->prefilter_alloc);
			slot->prefilter_image = VK_NULL_HANDLE;
			slot->prefilter_alloc = {};
			logger::error("bake_ibl: prefilter vkCreateImageView (full) failed");
			return false;
		}

		for (u32 m = 0; m < PREFILTER_MIP_COUNT; m++) {
			VkImageViewCreateInfo mvci = full_vci;
			mvci.subresourceRange.baseMipLevel = m;
			mvci.subresourceRange.levelCount = 1;
			if (vkCreateImageView(c.device, &mvci, nullptr, &slot->bake_pref_mip_views[m]) != VK_SUCCESS) {
				logger::error("bake_ibl: prefilter per-mip view %u failed", m);
				return false;
			}
		}
		return true;
	}

	// Combined irradiance + prefilter bake: single command buffer, async
	// submit with slot->bake_fence. The transient cmd pool / descriptor pool
	// / per-mip views are stored on the slot and released by
	// release_bake_resources once the fence signals.
	bool bake_ibl(CubemapHandle handle) {
		const CubemapSlot* slot_c = get_cubemap(handle);
		if (!slot_c) {
			logger::error("bake_ibl: invalid cubemap handle");
			return false;
		}
		CubemapSlot* slot = (CubemapSlot*)slot_c;

		Context& c = context();

		if (!create_irradiance_image(slot)) return false;
		if (!create_prefilter_image(slot))  return false;

		// --- one descriptor pool sized for 1 irradiance + PREFILTER_MIP_COUNT prefilter sets ---
		VkDescriptorPoolSize ps[2] = {};
		ps[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		ps[0].descriptorCount = 1 + PREFILTER_MIP_COUNT;
		ps[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		ps[1].descriptorCount = 1 + PREFILTER_MIP_COUNT;

		VkDescriptorPoolCreateInfo pci = {};
		pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pci.maxSets = 1 + PREFILTER_MIP_COUNT;
		pci.poolSizeCount = 2;
		pci.pPoolSizes = ps;
		if (vkCreateDescriptorPool(c.device, &pci, nullptr, &slot->bake_desc_pool) != VK_SUCCESS) {
			logger::error("bake_ibl: descriptor pool creation failed");
			return false;
		}

		// --- allocate irradiance set + prefilter mip sets ---
		VkDescriptorSet irr_set = VK_NULL_HANDLE;
		{
			VkDescriptorSetAllocateInfo dsai = {};
			dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			dsai.descriptorPool = slot->bake_desc_pool;
			dsai.descriptorSetCount = 1;
			dsai.pSetLayouts = &irr_set_layout;
			vkAllocateDescriptorSets(c.device, &dsai, &irr_set);

			VkDescriptorImageInfo src_i = {};
			src_i.sampler = ibl_sampler;
			src_i.imageView = slot->source_view;
			src_i.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			VkDescriptorImageInfo dst_i = {};
			dst_i.imageView = slot->irradiance_view;
			dst_i.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

			VkWriteDescriptorSet ws[2] = {};
			ws[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			ws[0].dstSet = irr_set;
			ws[0].dstBinding = 0;
			ws[0].descriptorCount = 1;
			ws[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			ws[0].pImageInfo = &src_i;
			ws[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			ws[1].dstSet = irr_set;
			ws[1].dstBinding = 1;
			ws[1].descriptorCount = 1;
			ws[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			ws[1].pImageInfo = &dst_i;
			vkUpdateDescriptorSets(c.device, 2, ws, 0, nullptr);
		}

		VkDescriptorSet pref_sets[PREFILTER_MIP_COUNT] = {};
		{
			VkDescriptorSetLayout layouts[PREFILTER_MIP_COUNT];
			for (u32 m = 0; m < PREFILTER_MIP_COUNT; m++) layouts[m] = pref_set_layout;
			VkDescriptorSetAllocateInfo dsai = {};
			dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			dsai.descriptorPool = slot->bake_desc_pool;
			dsai.descriptorSetCount = PREFILTER_MIP_COUNT;
			dsai.pSetLayouts = layouts;
			vkAllocateDescriptorSets(c.device, &dsai, pref_sets);

			VkDescriptorImageInfo src_infos[PREFILTER_MIP_COUNT] = {};
			VkDescriptorImageInfo dst_infos[PREFILTER_MIP_COUNT] = {};
			VkWriteDescriptorSet  writes[PREFILTER_MIP_COUNT * 2] = {};
			for (u32 m = 0; m < PREFILTER_MIP_COUNT; m++) {
				src_infos[m].sampler = ibl_sampler;
				src_infos[m].imageView = slot->source_view;
				src_infos[m].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				dst_infos[m].imageView = slot->bake_pref_mip_views[m];
				dst_infos[m].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

				writes[m * 2 + 0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[m * 2 + 0].dstSet = pref_sets[m];
				writes[m * 2 + 0].dstBinding = 0;
				writes[m * 2 + 0].descriptorCount = 1;
				writes[m * 2 + 0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writes[m * 2 + 0].pImageInfo = &src_infos[m];

				writes[m * 2 + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[m * 2 + 1].dstSet = pref_sets[m];
				writes[m * 2 + 1].dstBinding = 1;
				writes[m * 2 + 1].descriptorCount = 1;
				writes[m * 2 + 1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				writes[m * 2 + 1].pImageInfo = &dst_infos[m];
			}
			vkUpdateDescriptorSets(c.device, PREFILTER_MIP_COUNT * 2, writes, 0, nullptr);
		}

		// --- cmd pool + cmd buffer (kept alive on the slot until the fence signals) ---
		VkCommandPoolCreateInfo cpci = {};
		cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		cpci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		cpci.queueFamilyIndex = c.graphics_queue_index;
		vkCreateCommandPool(c.device, &cpci, nullptr, &slot->bake_cmd_pool);

		VkCommandBufferAllocateInfo cbai = {};
		cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cbai.commandPool = slot->bake_cmd_pool;
		cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cbai.commandBufferCount = 1;
		VkCommandBuffer cmd = VK_NULL_HANDLE;
		vkAllocateCommandBuffers(c.device, &cbai, &cmd);

		VkCommandBufferBeginInfo bi = {};
		bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(cmd, &bi);

		// --- irradiance: UNDEFINED -> GENERAL, dispatch, GENERAL -> SHADER_READ ---
		simple_barrier(cmd, slot->irradiance_image,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
			0, VK_ACCESS_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			6);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, irr_pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, irr_pipeline_layout,
			0, 1, &irr_set, 0, nullptr);
		{
			IrradiancePC ipc = {};
			ipc.intensity = slot->intensity;
			vkCmdPushConstants(cmd, irr_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
				0, sizeof(IrradiancePC), &ipc);
		}
		vkCmdDispatch(cmd, IRRADIANCE_SIZE / 8, IRRADIANCE_SIZE / 8, 6);

		simple_barrier(cmd, slot->irradiance_image,
			VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			6);

		// --- prefilter: all mips UNDEFINED -> GENERAL ---
		{
			VkImageMemoryBarrier b = {};
			b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			b.image = slot->prefilter_image;
			b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			b.subresourceRange.baseMipLevel = 0;
			b.subresourceRange.levelCount = PREFILTER_MIP_COUNT;
			b.subresourceRange.baseArrayLayer = 0;
			b.subresourceRange.layerCount = 6;
			b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
			b.srcAccessMask = 0;
			b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			vkCmdPipelineBarrier(cmd,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &b);
		}

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pref_pipeline);
		for (u32 m = 0; m < PREFILTER_MIP_COUNT; m++) {
			u32 mip_size = PREFILTER_SIZE >> m;
			f32 roughness = (f32)m / (f32)(PREFILTER_MIP_COUNT - 1);

			PrefilterPC pc = {};
			pc.roughness = roughness;
			pc.mip_size  = mip_size;
			pc.intensity = slot->intensity;
			vkCmdPushConstants(cmd, pref_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
				0, sizeof(PrefilterPC), &pc);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pref_pipeline_layout,
				0, 1, &pref_sets[m], 0, nullptr);

			u32 groups_xy = (mip_size + 7) / 8;
			vkCmdDispatch(cmd, groups_xy, groups_xy, 6);
		}

		// --- prefilter: all mips GENERAL -> SHADER_READ ---
		{
			VkImageMemoryBarrier b = {};
			b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			b.image = slot->prefilter_image;
			b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			b.subresourceRange.baseMipLevel = 0;
			b.subresourceRange.levelCount = PREFILTER_MIP_COUNT;
			b.subresourceRange.baseArrayLayer = 0;
			b.subresourceRange.layerCount = 6;
			b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
			b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			vkCmdPipelineBarrier(cmd,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &b);
		}

		vkEndCommandBuffer(cmd);

		// --- async submit with the slot's bake fence (no CPU wait) ---
		VkFenceCreateInfo fci = {};
		fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		if (vkCreateFence(c.device, &fci, nullptr, &slot->bake_fence) != VK_SUCCESS) {
			logger::error("bake_ibl: fence creation failed");
			return false;
		}

		VkSubmitInfo si = {};
		si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		si.commandBufferCount = 1;
		si.pCommandBuffers = &cmd;
		vkQueueSubmit(c.graphics_queue, 1, &si, slot->bake_fence);

		slot->ibl_baked = true;
		return true;
	}

	void release_bake_resources(CubemapSlot* slot) {
		if (!slot) return;
		Context& c = context();

		if (slot->bake_fence) {
			// Wait up to 1 s for the bake to complete; cubemap bakes finish in
			// a few ms so this is effectively a non-wait at unload time.
			vkWaitForFences(c.device, 1, &slot->bake_fence, VK_TRUE, 1'000'000'000ull);
			vkDestroyFence(c.device, slot->bake_fence, nullptr);
			slot->bake_fence = VK_NULL_HANDLE;
		}
		if (slot->bake_desc_pool) {
			vkDestroyDescriptorPool(c.device, slot->bake_desc_pool, nullptr);
			slot->bake_desc_pool = VK_NULL_HANDLE;
		}
		if (slot->bake_cmd_pool) {
			vkDestroyCommandPool(c.device, slot->bake_cmd_pool, nullptr);
			slot->bake_cmd_pool = VK_NULL_HANDLE;
		}
		for (u32 m = 0; m < 5; m++) {
			if (slot->bake_pref_mip_views[m]) {
				vkDestroyImageView(c.device, slot->bake_pref_mip_views[m], nullptr);
				slot->bake_pref_mip_views[m] = VK_NULL_HANDLE;
			}
		}
	}

	// --- prefilter compute pipeline (persistent) ---

	static bool create_prefilter_pipeline() {
		Context& c = context();

		// binding 0: input samplerCube (source)
		// binding 1: output imageCube  (single mip of prefilter)
		VkDescriptorSetLayoutBinding bs[2] = {};
		bs[0].binding = 0;
		bs[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bs[0].descriptorCount = 1;
		bs[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		bs[1].binding = 1;
		bs[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		bs[1].descriptorCount = 1;
		bs[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		VkDescriptorSetLayoutCreateInfo lci = {};
		lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		lci.bindingCount = 2;
		lci.pBindings = bs;
		if (vkCreateDescriptorSetLayout(c.device, &lci, nullptr, &pref_set_layout) != VK_SUCCESS) {
			logger::fatal("Failed to create prefilter descriptor set layout");
			return false;
		}

		VkPushConstantRange pcr = {};
		pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		pcr.offset = 0;
		pcr.size = sizeof(PrefilterPC);

		ComputePipelineSpec cspec = {};
		cspec.cs_path = "shaders/spv/prefilter.comp.spv";
		cspec.set_layouts = &pref_set_layout;
		cspec.set_layout_count = 1;
		cspec.push_constant = &pcr;
		if (!create_compute_pipeline(cspec, &pref_pipeline, &pref_pipeline_layout)) {
			return false;
		}
		return true;
	}

	// --- public ---

	bool init_ibl() {
		if (!create_sampler()) return false;
		if (!create_brdf_lut_image()) return false;

		// PNG load path first; fall back to compute bake if the asset is missing
		// or the wrong size. Either way the LUT ends up in SHADER_READ_ONLY.
		if (!try_load_brdf_lut_png()) {
			if (!bake_brdf_lut_compute()) return false;
		}

		// neutral mid-grey placeholders so set-0 bindings 4 & 5 always sample
		// something well-defined before the first set_environment_cubemap call.
		if (!create_placeholder_cube(&placeholder_irr,  0.5f)) return false;
		if (!create_placeholder_cube(&placeholder_pref, 0.5f)) return false;

		if (!create_irradiance_pipeline()) return false;
		if (!create_prefilter_pipeline()) return false;

		write_descriptors();
		return true;
	}

	void set_environment_cubemap(CubemapHandle handle) {
		if (handle != INVALID_CUBEMAP) {
			const CubemapSlot* slot = get_cubemap(handle);
			if (!slot || !slot->ibl_baked) {
				logger::error("set_environment_cubemap: handle %u is not a valid baked cubemap", handle);
				return;
			}
		}
		active_env = handle;

		// Descriptor sets may have been used by an in-flight frame; flush so
		// the rewrite is safe. set_environment_cubemap is conventionally a
		// between-frames operation, so the wait is brief.
		vkDeviceWaitIdle(context().device);
		write_descriptors();
	}

	CubemapHandle active_environment() {
		return active_env;
	}

	static void destroy_ibl_image(IblImage& img) {
		Context& c = context();
		VmaAllocator a = allocator();
		if (img.view)  vkDestroyImageView(c.device, img.view, nullptr);
		if (img.image) vmaDestroyImage(a, img.image, img.alloc);
		img = {};
	}

	void shutdown_ibl() {
		Context& c = context();

		if (pref_pipeline)        vkDestroyPipeline(c.device, pref_pipeline, nullptr);
		if (pref_pipeline_layout) vkDestroyPipelineLayout(c.device, pref_pipeline_layout, nullptr);
		if (pref_set_layout)      vkDestroyDescriptorSetLayout(c.device, pref_set_layout, nullptr);
		pref_pipeline        = VK_NULL_HANDLE;
		pref_pipeline_layout = VK_NULL_HANDLE;
		pref_set_layout      = VK_NULL_HANDLE;

		if (irr_pipeline)        vkDestroyPipeline(c.device, irr_pipeline, nullptr);
		if (irr_pipeline_layout) vkDestroyPipelineLayout(c.device, irr_pipeline_layout, nullptr);
		if (irr_set_layout)      vkDestroyDescriptorSetLayout(c.device, irr_set_layout, nullptr);
		irr_pipeline        = VK_NULL_HANDLE;
		irr_pipeline_layout = VK_NULL_HANDLE;
		irr_set_layout      = VK_NULL_HANDLE;
		active_env          = INVALID_CUBEMAP;

		destroy_ibl_image(brdf_lut);
		destroy_ibl_image(placeholder_irr);
		destroy_ibl_image(placeholder_pref);

		if (ibl_sampler) {
			vkDestroySampler(c.device, ibl_sampler, nullptr);
			ibl_sampler = VK_NULL_HANDLE;
		}
	}

}
