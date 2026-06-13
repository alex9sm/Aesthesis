#include "vk_pch.hpp"
#include "vk_lighting.hpp"
#include "vk_init.hpp"
#include "vk_targets.hpp"
#include "vk_globals.hpp"
#include "vk_pipeline.hpp"
#include "log.hpp"

namespace vk {

	static VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
	static VkDescriptorPool      pool       = VK_NULL_HANDLE;
	static VkDescriptorSet       set        = VK_NULL_HANDLE;
	static VkSampler             sampler    = VK_NULL_HANDLE;
	static VkPipelineLayout      pipeline_layout = VK_NULL_HANDLE;
	static VkPipeline            pipeline   = VK_NULL_HANDLE;

	static bool create_sampler() {
		Context& c = context();
		VkSamplerCreateInfo s = {};
		s.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		s.magFilter = VK_FILTER_NEAREST;
		s.minFilter = VK_FILTER_NEAREST;
		s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		s.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		s.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		s.maxLod = 1.0f;
		s.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
		if (vkCreateSampler(c.device, &s, nullptr, &sampler) != VK_SUCCESS) {
			logger::fatal("Failed to create lighting sampler");
			return false;
		}
		return true;
	}

	static bool create_descriptor_resources() {
		Context& c = context();

		VkDescriptorSetLayoutBinding bindings[4] = {};
		for (u32 i = 0; i < 4; i++) {
			bindings[i].binding = i;
			bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[i].descriptorCount = 1;
			bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		}

		VkDescriptorSetLayoutCreateInfo lci = {};
		lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		lci.bindingCount = 4;
		lci.pBindings = bindings;

		if (vkCreateDescriptorSetLayout(c.device, &lci, nullptr, &set_layout) != VK_SUCCESS) {
			logger::fatal("Failed to create lighting descriptor set layout");
			return false;
		}

		VkDescriptorPoolSize size = {};
		size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		size.descriptorCount = 4;

		VkDescriptorPoolCreateInfo pci = {};
		pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pci.maxSets = 1;
		pci.poolSizeCount = 1;
		pci.pPoolSizes = &size;

		if (vkCreateDescriptorPool(c.device, &pci, nullptr, &pool) != VK_SUCCESS) {
			logger::fatal("Failed to create lighting descriptor pool");
			return false;
		}

		VkDescriptorSetAllocateInfo ai = {};
		ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		ai.descriptorPool = pool;
		ai.descriptorSetCount = 1;
		ai.pSetLayouts = &set_layout;

		if (vkAllocateDescriptorSets(c.device, &ai, &set) != VK_SUCCESS) {
			logger::fatal("Failed to allocate lighting descriptor set");
			return false;
		}
		return true;
	}

	static bool create_pipeline() {
		Targets& t = targets();

		VkFormat color_format = t.scene_hdr.format;
		VkDescriptorSetLayout layouts[] = { global_set_layout(), set_layout };

		// fullscreen triangle; the vertex shader generates positions, so no
		// vertex input and no culling.
		GraphicsPipelineSpec spec = {};
		spec.vs_path = "shaders/spv/lighting.vert.spv";
		spec.fs_path = "shaders/spv/lighting.frag.spv";
		spec.color_formats = &color_format;
		spec.color_count = 1;
		spec.set_layouts = layouts;
		spec.set_layout_count = 2;

		return create_graphics_pipeline(spec, &pipeline, &pipeline_layout);
	}

	bool init_lighting() {
		if (!create_sampler()) return false;
		if (!create_descriptor_resources()) return false;
		if (!create_pipeline()) return false;
		return true;
	}

	void shutdown_lighting() {
		Context& c = context();
		if (pipeline)        vkDestroyPipeline(c.device, pipeline, nullptr);
		if (pipeline_layout) vkDestroyPipelineLayout(c.device, pipeline_layout, nullptr);
		if (pool)            vkDestroyDescriptorPool(c.device, pool, nullptr);
		if (set_layout)      vkDestroyDescriptorSetLayout(c.device, set_layout, nullptr);
		if (sampler)         vkDestroySampler(c.device, sampler, nullptr);
		pipeline = VK_NULL_HANDLE;
		pipeline_layout = VK_NULL_HANDLE;
		pool = VK_NULL_HANDLE;
		set = VK_NULL_HANDLE;
		set_layout = VK_NULL_HANDLE;
		sampler = VK_NULL_HANDLE;
	}

	void lighting_refresh_descriptors() {
		Context& c = context();
		Targets& t = targets();

		VkDescriptorImageInfo infos[4] = {};
		infos[0].sampler = sampler;
		infos[0].imageView = t.albedo.view;
		infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		infos[1].sampler = sampler;
		infos[1].imageView = t.normal.view;
		infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		infos[2].sampler = sampler;
		infos[2].imageView = t.material.view;
		infos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		infos[3].sampler = sampler;
		infos[3].imageView = t.depth.view;
		infos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet writes[4] = {};
		for (u32 i = 0; i < 4; i++) {
			writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[i].dstSet = set;
			writes[i].dstBinding = i;
			writes[i].descriptorCount = 1;
			writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[i].pImageInfo = &infos[i];
		}
		vkUpdateDescriptorSets(c.device, 4, writes, 0, nullptr);
	}

	void execute_lighting_pass(VkCommandBuffer cmd) {
		Targets& t = targets();

		// scene_hdr → color write (don't care about previous contents)
		transition_discard(cmd, t.scene_hdr, ResState::ColorWrite);

		VkRenderingAttachmentInfo color = {};
		color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		color.imageView = t.scene_hdr.view;
		color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color.clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

		VkRenderingInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		info.renderArea.offset = { 0, 0 };
		info.renderArea.extent = t.extent;
		info.layerCount = 1;
		info.colorAttachmentCount = 1;
		info.pColorAttachments = &color;

		vkCmdBeginRendering(cmd, &info);

		VkViewport viewport = {};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = (f32)t.extent.width;
		viewport.height = (f32)t.extent.height;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(cmd, 0, 1, &viewport);

		VkRect2D scissor = {};
		scissor.offset = { 0, 0 };
		scissor.extent = t.extent;
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

		VkDescriptorSet sets[2] = { current_global_set(), set };
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
			0, 2, sets, 0, nullptr);

		vkCmdDraw(cmd, 3, 1, 0, 0);

		vkCmdEndRendering(cmd);

		// scene_hdr → shader read for the debug/composite pass
		transition(cmd, t.scene_hdr, ResState::ShaderRead);
	}

}
