#include "vk_lighting.hpp"
#include "vk_init.hpp"
#include "vk_targets.hpp"
#include "vk_globals.hpp"
#include "vk_shader.hpp"
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
		Context& c = context();
		Targets& t = targets();

		VkShaderModule vs = load_shader_module("shaders/spv/lighting.vert.spv");
		VkShaderModule fs = load_shader_module("shaders/spv/lighting.frag.spv");
		if (!vs || !fs) {
			logger::fatal("Failed to load lighting shaders");
			return false;
		}

		VkPipelineShaderStageCreateInfo stages[2] = {};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vs;
		stages[0].pName = "main";
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = fs;
		stages[1].pName = "main";

		VkPipelineVertexInputStateCreateInfo vi = {};
		vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

		VkPipelineInputAssemblyStateCreateInfo ia = {};
		ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineViewportStateCreateInfo vp = {};
		vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		vp.viewportCount = 1;
		vp.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo rs = {};
		rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rs.polygonMode = VK_POLYGON_MODE_FILL;
		rs.cullMode = VK_CULL_MODE_NONE;
		rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo ms = {};
		ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineColorBlendAttachmentState cba = {};
		cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
			| VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		VkPipelineColorBlendStateCreateInfo cb = {};
		cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		cb.attachmentCount = 1;
		cb.pAttachments = &cba;

		VkDynamicState dyn_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dyn = {};
		dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dyn.dynamicStateCount = 2;
		dyn.pDynamicStates = dyn_states;

		VkDescriptorSetLayout layouts[] = { global_set_layout(), set_layout };

		VkPipelineLayoutCreateInfo layout_ci = {};
		layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layout_ci.setLayoutCount = 2;
		layout_ci.pSetLayouts = layouts;

		if (vkCreatePipelineLayout(c.device, &layout_ci, nullptr, &pipeline_layout) != VK_SUCCESS) {
			logger::fatal("Failed to create lighting pipeline layout");
			return false;
		}

		VkFormat color_format = t.scene_hdr.format;

		VkPipelineRenderingCreateInfo rendering_ci = {};
		rendering_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		rendering_ci.colorAttachmentCount = 1;
		rendering_ci.pColorAttachmentFormats = &color_format;

		VkGraphicsPipelineCreateInfo pci = {};
		pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pci.pNext = &rendering_ci;
		pci.stageCount = 2;
		pci.pStages = stages;
		pci.pVertexInputState = &vi;
		pci.pInputAssemblyState = &ia;
		pci.pViewportState = &vp;
		pci.pRasterizationState = &rs;
		pci.pMultisampleState = &ms;
		pci.pColorBlendState = &cb;
		pci.pDynamicState = &dyn;
		pci.layout = pipeline_layout;

		VkResult r = vkCreateGraphicsPipelines(c.device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline);

		vkDestroyShaderModule(c.device, vs, nullptr);
		vkDestroyShaderModule(c.device, fs, nullptr);

		if (r != VK_SUCCESS) {
			logger::fatal("Failed to create lighting pipeline");
			return false;
		}
		return true;
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

		// scene_hdr → COLOR_ATTACHMENT_OPTIMAL (don't care about previous contents)
		t.scene_hdr.layout = VK_IMAGE_LAYOUT_UNDEFINED;
		transition_image(cmd, t.scene_hdr, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

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

		// scene_hdr → SHADER_READ_ONLY for the debug/composite pass
		transition_image(cmd, t.scene_hdr, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	}

}
