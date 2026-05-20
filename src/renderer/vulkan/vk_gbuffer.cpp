#include "vk_gbuffer.hpp"
#include "vk_init.hpp"
#include "vk_memory.hpp"
#include "vk_swapchain.hpp"
#include "vk_shader.hpp"
#include "vk_targets.hpp"
#include "vk_globals.hpp"
#include "vk_frame.hpp"
#include "gltf.hpp"
#include "log.hpp"
#include "memory.hpp"

#include <stddef.h>  // offsetof

namespace vk {

	static VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	static VkPipeline pipeline = VK_NULL_HANDLE;

	// --- pipeline ---

	static bool create_pipeline() {
		Context& c = context();
		Targets& t = targets();

		VkShaderModule vs = load_shader_module("shaders/spv/gbuffer.vert.spv");
		VkShaderModule fs = load_shader_module("shaders/spv/gbuffer.frag.spv");
		if (!vs || !fs) {
			logger::fatal("Failed to load gbuffer shaders");
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

		// vertex input: pos at location 0, normal at location 1
		VkVertexInputBindingDescription binding = {};
		binding.binding = 0;
		binding.stride = sizeof(renderer::Vertex);
		binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription attrs[2] = {};
		attrs[0].location = 0;
		attrs[0].binding = 0;
		attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		attrs[0].offset = (u32)offsetof(renderer::Vertex, position);
		attrs[1].location = 1;
		attrs[1].binding = 0;
		attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		attrs[1].offset = (u32)offsetof(renderer::Vertex, normal);

		VkPipelineVertexInputStateCreateInfo vi = {};
		vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vi.vertexBindingDescriptionCount = 1;
		vi.pVertexBindingDescriptions = &binding;
		vi.vertexAttributeDescriptionCount = 2;
		vi.pVertexAttributeDescriptions = attrs;

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
		rs.cullMode = VK_CULL_MODE_BACK_BIT;
		rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rs.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo ms = {};
		ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineDepthStencilStateCreateInfo ds = {};
		ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		ds.depthTestEnable = VK_TRUE;
		ds.depthWriteEnable = VK_TRUE;
		ds.depthCompareOp = VK_COMPARE_OP_LESS;

		// three color attachments, no blending
		VkPipelineColorBlendAttachmentState cb_attachments[3] = {};
		for (int i = 0; i < 3; i++) {
			cb_attachments[i].colorWriteMask =
				VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
				VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			cb_attachments[i].blendEnable = VK_FALSE;
		}

		VkPipelineColorBlendStateCreateInfo cb = {};
		cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		cb.attachmentCount = 3;
		cb.pAttachments = cb_attachments;

		VkDynamicState dyn_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dyn = {};
		dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dyn.dynamicStateCount = 2;
		dyn.pDynamicStates = dyn_states;

		VkDescriptorSetLayout set_layouts[] = { global_set_layout() };

		VkPipelineLayoutCreateInfo layout_ci = {};
		layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layout_ci.setLayoutCount = 1;
		layout_ci.pSetLayouts = set_layouts;
		layout_ci.pushConstantRangeCount = 0;
		layout_ci.pPushConstantRanges = nullptr;

		if (vkCreatePipelineLayout(c.device, &layout_ci, nullptr, &pipeline_layout) != VK_SUCCESS) {
			logger::fatal("Failed to create pipeline layout");
			return false;
		}

		// dynamic rendering: declare attachment formats
		VkFormat color_formats[3] = {
			t.albedo.format,
			t.normal.format,
			t.material.format,
		};

		VkPipelineRenderingCreateInfo rendering_ci = {};
		rendering_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		rendering_ci.colorAttachmentCount = 3;
		rendering_ci.pColorAttachmentFormats = color_formats;
		rendering_ci.depthAttachmentFormat = t.depth.format;

		VkGraphicsPipelineCreateInfo pipeline_ci = {};
		pipeline_ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeline_ci.pNext = &rendering_ci;
		pipeline_ci.stageCount = 2;
		pipeline_ci.pStages = stages;
		pipeline_ci.pVertexInputState = &vi;
		pipeline_ci.pInputAssemblyState = &ia;
		pipeline_ci.pViewportState = &vp;
		pipeline_ci.pRasterizationState = &rs;
		pipeline_ci.pMultisampleState = &ms;
		pipeline_ci.pDepthStencilState = &ds;
		pipeline_ci.pColorBlendState = &cb;
		pipeline_ci.pDynamicState = &dyn;
		pipeline_ci.layout = pipeline_layout;

		VkResult r = vkCreateGraphicsPipelines(c.device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &pipeline);

		vkDestroyShaderModule(c.device, vs, nullptr);
		vkDestroyShaderModule(c.device, fs, nullptr);

		if (r != VK_SUCCESS) {
			logger::fatal("Failed to create gbuffer pipeline");
			return false;
		}
		return true;
	}

	// --- public ---

	bool init_gbuffer() {
		if (!create_pipeline()) return false;
		return true;
	}

	void shutdown_gbuffer() {
		Context& c = context();
		if (pipeline) vkDestroyPipeline(c.device, pipeline, nullptr);
		if (pipeline_layout) vkDestroyPipelineLayout(c.device, pipeline_layout, nullptr);
		pipeline = VK_NULL_HANDLE;
		pipeline_layout = VK_NULL_HANDLE;
	}

	void execute_gbuffer_pass(VkCommandBuffer cmd,
		const MeshHandle* meshes, u32 draw_count)
	{
		Targets& t = targets();

		// reset layouts at the top of each frame so transitions begin from a known state.
		// (the targets persist across frames; we discard contents by transitioning from UNDEFINED.)
		t.albedo.layout   = VK_IMAGE_LAYOUT_UNDEFINED;
		t.normal.layout   = VK_IMAGE_LAYOUT_UNDEFINED;
		t.material.layout = VK_IMAGE_LAYOUT_UNDEFINED;
		t.depth.layout    = VK_IMAGE_LAYOUT_UNDEFINED;

		transition_image(cmd, t.albedo, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
		transition_image(cmd, t.normal, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
		transition_image(cmd, t.material, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
		transition_image(cmd, t.depth, VK_IMAGE_ASPECT_DEPTH_BIT,
			VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);

		// rendering attachments
		VkRenderingAttachmentInfo color_attachments[3] = {};
		color_attachments[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		color_attachments[0].imageView = t.albedo.view;
		color_attachments[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		color_attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color_attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color_attachments[0].clearValue.color = { { 0.05f, 0.07f, 0.10f, 1.0f } };

		color_attachments[1].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		color_attachments[1].imageView = t.normal.view;
		color_attachments[1].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		color_attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color_attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color_attachments[1].clearValue.color = { { 0.5f, 0.5f, 1.0f, 0.0f } };

		color_attachments[2].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		color_attachments[2].imageView = t.material.view;
		color_attachments[2].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		color_attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color_attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color_attachments[2].clearValue.color = { { 0.0f, 0.5f, 0.0f, 0.0f } };

		VkRenderingAttachmentInfo depth_attachment = {};
		depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depth_attachment.imageView = t.depth.view;
		depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depth_attachment.clearValue.depthStencil = { 1.0f, 0 };

		VkRenderingInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		info.renderArea.offset = { 0, 0 };
		info.renderArea.extent = t.extent;
		info.layerCount = 1;
		info.colorAttachmentCount = 3;
		info.pColorAttachments = color_attachments;
		info.pDepthAttachment = &depth_attachment;

		vkCmdBeginRendering(cmd, &info);

		// viewport/scissor (flip viewport for Vulkan to match GL-style projection)
		VkViewport viewport = {};
		viewport.x = 0.0f;
		viewport.y = (f32)t.extent.height;
		viewport.width = (f32)t.extent.width;
		viewport.height = -(f32)t.extent.height;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(cmd, 0, 1, &viewport);

		VkRect2D scissor = {};
		scissor.offset = { 0, 0 };
		scissor.extent = t.extent;
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

		VkDescriptorSet global_set = current_global_set();
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
			0, 1, &global_set, 0, nullptr);

		for (u32 i = 0; i < draw_count; i++) {
			const MeshGPU* m = get_mesh(meshes[i]);
			if (!m) continue;

			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(cmd, 0, 1, &m->vertex_buffer, &offset);
			vkCmdBindIndexBuffer(cmd, m->index_buffer, 0, VK_INDEX_TYPE_UINT32);
			// firstInstance = i selects instances[i] from the SSBO via gl_InstanceIndex
			vkCmdDrawIndexed(cmd, m->index_count, 1, 0, 0, i);
		}

		vkCmdEndRendering(cmd);

		// transition all attachments to SHADER_READ_ONLY for the lighting pass
		transition_image(cmd, t.albedo, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
		transition_image(cmd, t.normal, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
		transition_image(cmd, t.material, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
		transition_image(cmd, t.depth, VK_IMAGE_ASPECT_DEPTH_BIT,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	}

}
