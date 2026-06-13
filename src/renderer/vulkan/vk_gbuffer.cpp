#include "vk_pch.hpp"
#include "vk_gbuffer.hpp"
#include "vk_init.hpp"
#include "vk_memory.hpp"
#include "vk_swapchain.hpp"
#include "vk_pipeline.hpp"
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
		Targets& t = targets();

		// vertex input: pos / normal / tangent / uv
		VkVertexInputBindingDescription binding = {};
		binding.binding = 0;
		binding.stride = sizeof(renderer::Vertex);
		binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription attrs[4] = {};
		attrs[0].location = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;    attrs[0].offset = (u32)offsetof(renderer::Vertex, position);
		attrs[1].location = 1; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;    attrs[1].offset = (u32)offsetof(renderer::Vertex, normal);
		attrs[2].location = 2; attrs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[2].offset = (u32)offsetof(renderer::Vertex, tangent);
		attrs[3].location = 3; attrs[3].format = VK_FORMAT_R32G32_SFLOAT;       attrs[3].offset = (u32)offsetof(renderer::Vertex, uv);

		VkFormat color_formats[3] = { t.albedo.format, t.normal.format, t.material.format };
		VkDescriptorSetLayout set_layouts[] = { global_set_layout() };

		// depth prepass already filled depth with LESS; here we shade only the
		// fragments that survived. EQUAL test + no depth write = zero overdraw.
		GraphicsPipelineSpec spec = {};
		spec.vs_path = "shaders/spv/gbuffer.vert.spv";
		spec.fs_path = "shaders/spv/gbuffer.frag.spv";
		spec.vertex_binding = &binding;
		spec.vertex_attrs = attrs;
		spec.vertex_attr_count = 4;
		spec.cull = VK_CULL_MODE_BACK_BIT;
		spec.depth_test = VK_TRUE;
		spec.depth_write = VK_FALSE;
		spec.depth_compare = VK_COMPARE_OP_EQUAL;
		spec.color_formats = color_formats;
		spec.color_count = 3;
		spec.depth_format = t.depth.format;
		spec.set_layouts = set_layouts;
		spec.set_layout_count = 1;

		return create_graphics_pipeline(spec, &pipeline, &pipeline_layout);
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
		const DrawBatch* batches, u32 batch_count)
	{
		Targets& t = targets();

		// discard last frame's color contents and move to write state. depth is
		// owned by the prepass: it already sits in DepthRead (the prepass issued
		// WRITE→READ), ready to be read+tested, so we leave it untouched here.
		transition_discard(cmd, t.albedo,   ResState::ColorWrite);
		transition_discard(cmd, t.normal,   ResState::ColorWrite);
		transition_discard(cmd, t.material, ResState::ColorWrite);

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
		// RG16F octahedral; sky pixels are gated by depth in the lighting pass
		// so the cleared value is never read.
		color_attachments[1].clearValue.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

		color_attachments[2].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		color_attachments[2].imageView = t.material.view;
		color_attachments[2].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		color_attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color_attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color_attachments[2].clearValue.color = { { 0.0f, 0.5f, 0.0f, 0.0f } };

		// LOAD the depth filled by the prepass; STORE so the lighting pass
		// can still sample it. No clearValue needed for LOAD.
		VkRenderingAttachmentInfo depth_attachment = {};
		depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depth_attachment.imageView = t.depth.view;
		depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

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

		for (u32 b = 0; b < batch_count; b++) {
			const DrawBatch& batch = batches[b];
			const MeshGPU* m = get_mesh(batch.mesh);
			if (!m) continue;

			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(cmd, 0, 1, &m->vertex_buffer, &offset);
			vkCmdBindIndexBuffer(cmd, m->index_buffer, 0, VK_INDEX_TYPE_UINT32);
			// each instance within the batch picks its InstanceData via
			// gl_InstanceIndex = firstInstance + instance_within_draw.
			vkCmdDrawIndexed(cmd, m->index_count, batch.instance_count, 0, 0, batch.first_instance);
		}

		vkCmdEndRendering(cmd);

		// hand all attachments (incl. depth) to the lighting pass as textures.
		transition(cmd, t.albedo,   ResState::ShaderRead);
		transition(cmd, t.normal,   ResState::ShaderRead);
		transition(cmd, t.material, ResState::ShaderRead);
		transition(cmd, t.depth,    ResState::ShaderRead);
	}

}
