#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "vk_ibl.hpp"
#include "vk_init.hpp"
#include "vk_memory.hpp"
#include "vk_frame.hpp"
#include "vk_globals.hpp"
#include "vk_shader.hpp"
#include "vk_mem_alloc.h"
#include "log.hpp"
#include "memory.hpp"

// stb_image's IMPLEMENTATION is in vk_texture.cpp.
#include "stb_image.h"

namespace vk {

	static constexpr u32 BRDF_LUT_SIZE   = 256;
	static constexpr u32 IRRADIANCE_SIZE = 32;
	static constexpr const char* BRDF_LUT_PNG = "assets/textures/global/brdf_lut.png";

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

		VkShaderModule sm = load_shader_module("shaders/spv/brdf_lut.comp.spv");
		if (!sm) {
			logger::fatal("Failed to load brdf_lut.comp.spv");
			return false;
		}

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
			vkDestroyShaderModule(c.device, sm, nullptr);
			logger::fatal("BRDF LUT bake: create descriptor set layout failed");
			return false;
		}

		VkPipelineLayoutCreateInfo pli = {};
		pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pli.setLayoutCount = 1;
		pli.pSetLayouts = &set_layout;
		VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
		if (vkCreatePipelineLayout(c.device, &pli, nullptr, &pipeline_layout) != VK_SUCCESS) {
			vkDestroyDescriptorSetLayout(c.device, set_layout, nullptr);
			vkDestroyShaderModule(c.device, sm, nullptr);
			logger::fatal("BRDF LUT bake: create pipeline layout failed");
			return false;
		}

		VkPipelineShaderStageCreateInfo stage = {};
		stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stage.module = sm;
		stage.pName = "main";

		VkComputePipelineCreateInfo cpi = {};
		cpi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		cpi.stage = stage;
		cpi.layout = pipeline_layout;

		VkPipeline pipeline = VK_NULL_HANDLE;
		if (vkCreateComputePipelines(c.device, VK_NULL_HANDLE, 1, &cpi, nullptr, &pipeline) != VK_SUCCESS) {
			vkDestroyPipelineLayout(c.device, pipeline_layout, nullptr);
			vkDestroyDescriptorSetLayout(c.device, set_layout, nullptr);
			vkDestroyShaderModule(c.device, sm, nullptr);
			logger::fatal("BRDF LUT bake: create compute pipeline failed");
			return false;
		}
		vkDestroyShaderModule(c.device, sm, nullptr);

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

		// Pick the irradiance view from the active environment's slot, falling
		// back to the neutral placeholder when nothing is set. Prefilter is
		// always the placeholder until Phase F4 wires it up.
		VkImageView irr_view = placeholder_irr.view;
		if (active_env != INVALID_CUBEMAP) {
			const CubemapSlot* slot = get_cubemap(active_env);
			if (slot && slot->ibl_baked && slot->irradiance_view) {
				irr_view = slot->irradiance_view;
			}
		}

		VkDescriptorImageInfo irr_i = {};
		irr_i.sampler = ibl_sampler;
		irr_i.imageView = irr_view;
		irr_i.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkDescriptorImageInfo pref_i = {};
		pref_i.sampler = ibl_sampler;
		pref_i.imageView = placeholder_pref.view;
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

		VkShaderModule sm = load_shader_module("shaders/spv/irradiance.comp.spv");
		if (!sm) {
			logger::fatal("Failed to load irradiance.comp.spv");
			return false;
		}

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
			vkDestroyShaderModule(c.device, sm, nullptr);
			logger::fatal("Failed to create irradiance descriptor set layout");
			return false;
		}

		VkPipelineLayoutCreateInfo pli = {};
		pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pli.setLayoutCount = 1;
		pli.pSetLayouts = &irr_set_layout;
		if (vkCreatePipelineLayout(c.device, &pli, nullptr, &irr_pipeline_layout) != VK_SUCCESS) {
			vkDestroyShaderModule(c.device, sm, nullptr);
			logger::fatal("Failed to create irradiance pipeline layout");
			return false;
		}

		VkPipelineShaderStageCreateInfo stage = {};
		stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stage.module = sm;
		stage.pName = "main";

		VkComputePipelineCreateInfo cpi = {};
		cpi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		cpi.stage = stage;
		cpi.layout = irr_pipeline_layout;

		VkResult r = vkCreateComputePipelines(c.device, VK_NULL_HANDLE, 1, &cpi, nullptr, &irr_pipeline);
		vkDestroyShaderModule(c.device, sm, nullptr);
		if (r != VK_SUCCESS) {
			logger::fatal("Failed to create irradiance compute pipeline");
			return false;
		}
		return true;
	}

	// Allocates the irradiance cubemap on a slot, then dispatches the bake.
	// Source cubemap must already be SHADER_READ_ONLY_OPTIMAL on entry.
	bool bake_irradiance(CubemapHandle handle) {
		const CubemapSlot* slot_c = get_cubemap(handle);
		if (!slot_c) {
			logger::error("bake_irradiance: invalid cubemap handle");
			return false;
		}
		// We need a mutable view of the slot to store the new resources.
		CubemapSlot* slot = (CubemapSlot*)slot_c;

		Context&    c = context();
		VmaAllocator a = allocator();

		// --- create the irradiance cubemap (32×32×6, RGBA16F, single mip) ---
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
			logger::error("bake_irradiance: vmaCreateImage failed");
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
			logger::error("bake_irradiance: vkCreateImageView failed");
			return false;
		}

		// --- transient descriptor pool + set referencing source + irradiance ---
		VkDescriptorPoolSize ps[2] = {};
		ps[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		ps[0].descriptorCount = 1;
		ps[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		ps[1].descriptorCount = 1;

		VkDescriptorPoolCreateInfo pci = {};
		pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pci.maxSets = 1;
		pci.poolSizeCount = 2;
		pci.pPoolSizes = ps;

		VkDescriptorPool pool = VK_NULL_HANDLE;
		vkCreateDescriptorPool(c.device, &pci, nullptr, &pool);

		VkDescriptorSetAllocateInfo dsai = {};
		dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		dsai.descriptorPool = pool;
		dsai.descriptorSetCount = 1;
		dsai.pSetLayouts = &irr_set_layout;
		VkDescriptorSet set = VK_NULL_HANDLE;
		vkAllocateDescriptorSets(c.device, &dsai, &set);

		VkDescriptorImageInfo src_i = {};
		src_i.sampler = ibl_sampler;
		src_i.imageView = slot->source_view;
		src_i.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkDescriptorImageInfo dst_i = {};
		dst_i.imageView = slot->irradiance_view;
		dst_i.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		VkWriteDescriptorSet ws[2] = {};
		ws[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		ws[0].dstSet = set;
		ws[0].dstBinding = 0;
		ws[0].descriptorCount = 1;
		ws[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		ws[0].pImageInfo = &src_i;
		ws[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		ws[1].dstSet = set;
		ws[1].dstBinding = 1;
		ws[1].descriptorCount = 1;
		ws[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		ws[1].pImageInfo = &dst_i;
		vkUpdateDescriptorSets(c.device, 2, ws, 0, nullptr);

		// --- record + submit dispatch ---
		VkCommandPool cmd_pool = VK_NULL_HANDLE;
		VkCommandBuffer cmd = begin_one_shot(&cmd_pool);

		simple_barrier(cmd, slot->irradiance_image,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
			0, VK_ACCESS_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			6);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, irr_pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, irr_pipeline_layout,
			0, 1, &set, 0, nullptr);
		// 8×8 workgroups across 32×32, one z-slice per face
		vkCmdDispatch(cmd, IRRADIANCE_SIZE / 8, IRRADIANCE_SIZE / 8, 6);

		simple_barrier(cmd, slot->irradiance_image,
			VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			6);

		end_one_shot(cmd, cmd_pool);

		vkDestroyDescriptorPool(c.device, pool, nullptr);

		slot->ibl_baked = true;
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

	static void destroy_ibl_image(IblImage& img) {
		Context& c = context();
		VmaAllocator a = allocator();
		if (img.view)  vkDestroyImageView(c.device, img.view, nullptr);
		if (img.image) vmaDestroyImage(a, img.image, img.alloc);
		img = {};
	}

	void shutdown_ibl() {
		Context& c = context();

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
