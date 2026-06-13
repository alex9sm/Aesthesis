#include "vk_pch.hpp"
#include "vk_depth_prepass.hpp"
#include "vk_init.hpp"
#include "vk_memory.hpp"
#include "vk_swapchain.hpp"
#include "vk_shader.hpp"
#include "vk_targets.hpp"
#include "vk_globals.hpp"
#include "vk_frame.hpp"
#include "vk_mesh.hpp"
#include "gltf.hpp"
#include "log.hpp"

#include <stddef.h>  // offsetof

namespace vk {

	static VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	static VkPipeline pipeline = VK_NULL_HANDLE;

	static bool create_pipeline() {
		Context& c = context();
		Targets& t = targets();

		VkShaderModule vs = load_shader_module("shaders/spv/depth_prepass.vert.spv");
		if (!vs) {
			logger::fatal("Failed to load depth_prepass shader");
			return false;
		}

		// only the vertex stage is bound. with no color attachments and no
		// fragment shader Vulkan rasterizes-and-tests-depth without ever
		// invoking a fragment shader — exactly what we want.
		VkPipelineShaderStageCreateInfo stage = {};
		stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
		stage.module = vs;
		stage.pName = "main";

		// vertex input matches gbuffer exactly so the same vertex buffer can
		// be bound; attrs 1-3 are fetched but the vertex shader doesn't read
		// them. keeping the layout identical avoids per-mesh re-binding
		// gymnastics later.
		VkVertexInputBindingDescription binding = {};
		binding.binding = 0;
		binding.stride = sizeof(renderer::Vertex);
		binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription attrs[4] = {};
		attrs[0].location = 0;
		attrs[0].binding = 0;
		attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		attrs[0].offset = (u32)offsetof(renderer::Vertex, position);
		attrs[1].location = 1;
		attrs[1].binding = 0;
		attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		attrs[1].offset = (u32)offsetof(renderer::Vertex, normal);
		attrs[2].location = 2;
		attrs[2].binding = 0;
		attrs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		attrs[2].offset = (u32)offsetof(renderer::Vertex, tangent);
		attrs[3].location = 3;
		attrs[3].binding = 0;
		attrs[3].format = VK_FORMAT_R32G32_SFLOAT;
		attrs[3].offset = (u32)offsetof(renderer::Vertex, uv);

		VkPipelineVertexInputStateCreateInfo vi = {};
		vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vi.vertexBindingDescriptionCount = 1;
		vi.pVertexBindingDescriptions = &binding;
		vi.vertexAttributeDescriptionCount = 4;
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
		ds.depthTestEnable  = VK_TRUE;
		ds.depthWriteEnable = VK_TRUE;
		ds.depthCompareOp   = VK_COMPARE_OP_LESS;

		// no color attachments, no blend state needed (pNext stays null).
		VkPipelineColorBlendStateCreateInfo cb = {};
		cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		cb.attachmentCount = 0;
		cb.pAttachments = nullptr;

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

		if (vkCreatePipelineLayout(c.device, &layout_ci, nullptr, &pipeline_layout) != VK_SUCCESS) {
			logger::fatal("Failed to create depth prepass pipeline layout");
			return false;
		}

		VkPipelineRenderingCreateInfo rendering_ci = {};
		rendering_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		rendering_ci.colorAttachmentCount = 0;
		rendering_ci.pColorAttachmentFormats = nullptr;
		rendering_ci.depthAttachmentFormat = t.depth.format;

		VkGraphicsPipelineCreateInfo pipeline_ci = {};
		pipeline_ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeline_ci.pNext = &rendering_ci;
		pipeline_ci.stageCount = 1;
		pipeline_ci.pStages = &stage;
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

		if (r != VK_SUCCESS) {
			logger::fatal("Failed to create depth prepass pipeline");
			return false;
		}
		return true;
	}

	bool init_depth_prepass() {
		return create_pipeline();
	}

	void shutdown_depth_prepass() {
		Context& c = context();
		if (pipeline) vkDestroyPipeline(c.device, pipeline, nullptr);
		if (pipeline_layout) vkDestroyPipelineLayout(c.device, pipeline_layout, nullptr);
		pipeline = VK_NULL_HANDLE;
		pipeline_layout = VK_NULL_HANDLE;
	}

	void execute_depth_prepass(VkCommandBuffer cmd,
		const DrawBatch* batches, u32 batch_count)
	{
		Targets& t = targets();

		// depth target persists across frames; discard previous-frame contents.
		// this is the first writer of depth in the frame (was previously gbuffer).
		transition_discard(cmd, t.depth, ResState::DepthWrite);

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
		info.colorAttachmentCount = 0;
		info.pColorAttachments = nullptr;
		info.pDepthAttachment = &depth_attachment;

		vkCmdBeginRendering(cmd, &info);

		// Y-flipped viewport — same convention as gbuffer so projection math
		// stays consistent. EQUAL test in gbuffer relies on both passes
		// producing bit-identical gl_Position.
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

		for (u32 b = 0; b < batch_count; b++) {
			const DrawBatch& batch = batches[b];
			const MeshGPU* m = get_mesh(batch.mesh);
			if (!m) continue;

			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(cmd, 0, 1, &m->vertex_buffer, &offset);
			vkCmdBindIndexBuffer(cmd, m->index_buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(cmd, m->index_count, batch.instance_count, 0, 0, batch.first_instance);
		}

		vkCmdEndRendering(cmd);

		// hand the depth target off to the gbuffer pass for an EQUAL read.
		// stays in DEPTH_ATTACHMENT_OPTIMAL — only the access scope changes.
		transition(cmd, t.depth, ResState::DepthRead);
	}

}
