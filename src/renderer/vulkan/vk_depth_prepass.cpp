#include "vk_pch.hpp"
#include "vk_depth_prepass.hpp"
#include "vk_init.hpp"
#include "vk_memory.hpp"
#include "vk_swapchain.hpp"
#include "vk_pipeline.hpp"
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
		Targets& t = targets();

		// vertex input matches gbuffer exactly so the same vertex buffer binds
		// for both; attrs 1-3 are fetched but the vertex shader ignores them.
		VkVertexInputBindingDescription binding = {};
		binding.binding = 0;
		binding.stride = sizeof(renderer::Vertex);
		binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription attrs[4] = {};
		attrs[0].location = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;    attrs[0].offset = (u32)offsetof(renderer::Vertex, position);
		attrs[1].location = 1; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;    attrs[1].offset = (u32)offsetof(renderer::Vertex, normal);
		attrs[2].location = 2; attrs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[2].offset = (u32)offsetof(renderer::Vertex, tangent);
		attrs[3].location = 3; attrs[3].format = VK_FORMAT_R32G32_SFLOAT;       attrs[3].offset = (u32)offsetof(renderer::Vertex, uv);

		VkDescriptorSetLayout set_layouts[] = { global_set_layout() };

		// vertex stage only: no color attachments and no fragment shader, so
		// Vulkan rasterizes and writes depth without invoking a fragment shader.
		GraphicsPipelineSpec spec = {};
		spec.vs_path = "shaders/spv/depth_prepass.vert.spv";
		spec.vertex_binding = &binding;
		spec.vertex_attrs = attrs;
		spec.vertex_attr_count = 4;
		spec.cull = VK_CULL_MODE_BACK_BIT;
		spec.depth_test = VK_TRUE;
		spec.depth_write = VK_TRUE;
		spec.depth_compare = VK_COMPARE_OP_LESS;
		spec.depth_format = t.depth.format;
		spec.set_layouts = set_layouts;
		spec.set_layout_count = 1;

		return create_graphics_pipeline(spec, &pipeline, &pipeline_layout);
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
