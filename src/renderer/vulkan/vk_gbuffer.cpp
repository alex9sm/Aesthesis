#include "vk_gbuffer.hpp"
#include "vk_init.hpp"
#include "vk_memory.hpp"
#include "vk_swapchain.hpp"
#include "vk_shader.hpp"
#include "gltf.hpp"
#include "log.hpp"
#include "memory.hpp"

#include <stddef.h>  // offsetof

namespace vk {

	struct PushConstants {
		mat4 mvp;
		vec4 color;
	};

	static GBuffer gb = {};
	static VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
	static VkPipeline pipeline = VK_NULL_HANDLE;

	GBuffer& gbuffer() { return gb; }

	// --- helpers ---

	static bool create_image(GBufferImage* out, VkFormat format, VkExtent2D extent,
		VkImageUsageFlags usage, VkImageAspectFlags aspect)
	{
		Context& c = context();
		VmaAllocator a = allocator();

		VkImageCreateInfo image_ci = {};
		image_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		image_ci.imageType = VK_IMAGE_TYPE_2D;
		image_ci.format = format;
		image_ci.extent = { extent.width, extent.height, 1 };
		image_ci.mipLevels = 1;
		image_ci.arrayLayers = 1;
		image_ci.samples = VK_SAMPLE_COUNT_1_BIT;
		image_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
		image_ci.usage = usage;
		image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VmaAllocationCreateInfo alloc_ci = {};
		alloc_ci.usage = VMA_MEMORY_USAGE_AUTO;

		if (vmaCreateImage(a, &image_ci, &alloc_ci, &out->image, &out->alloc, nullptr) != VK_SUCCESS) {
			logger::error("vmaCreateImage failed");
			return false;
		}

		VkImageViewCreateInfo view_ci = {};
		view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view_ci.image = out->image;
		view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view_ci.format = format;
		view_ci.subresourceRange.aspectMask = aspect;
		view_ci.subresourceRange.baseMipLevel = 0;
		view_ci.subresourceRange.levelCount = 1;
		view_ci.subresourceRange.baseArrayLayer = 0;
		view_ci.subresourceRange.layerCount = 1;

		if (vkCreateImageView(c.device, &view_ci, nullptr, &out->view) != VK_SUCCESS) {
			logger::error("vkCreateImageView failed");
			return false;
		}

		out->format = format;
		return true;
	}

	static void destroy_image(GBufferImage* img) {
		Context& c = context();
		if (img->view)  vkDestroyImageView(c.device, img->view, nullptr);
		if (img->image) vmaDestroyImage(allocator(), img->image, img->alloc);
		*img = {};
	}

	static bool create_attachments(VkExtent2D extent) {
		VkImageUsageFlags color_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
			| VK_IMAGE_USAGE_SAMPLED_BIT
			| VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		VkImageUsageFlags depth_usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
			| VK_IMAGE_USAGE_SAMPLED_BIT;

		if (!create_image(&gb.albedo,   VK_FORMAT_R8G8B8A8_UNORM,     extent, color_usage, VK_IMAGE_ASPECT_COLOR_BIT)) return false;
		if (!create_image(&gb.normal,   VK_FORMAT_R16G16B16A16_SFLOAT, extent, color_usage, VK_IMAGE_ASPECT_COLOR_BIT)) return false;
		if (!create_image(&gb.material, VK_FORMAT_R8G8_UNORM,         extent, color_usage, VK_IMAGE_ASPECT_COLOR_BIT)) return false;
		if (!create_image(&gb.depth,    VK_FORMAT_D32_SFLOAT,         extent, depth_usage, VK_IMAGE_ASPECT_DEPTH_BIT)) return false;

		gb.extent = extent;
		return true;
	}

	static void destroy_attachments() {
		destroy_image(&gb.albedo);
		destroy_image(&gb.normal);
		destroy_image(&gb.material);
		destroy_image(&gb.depth);
		gb.extent = {};
	}

	// --- pipeline ---

	static bool create_pipeline() {
		Context& c = context();

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

		// pipeline layout with push constants
		VkPushConstantRange push_range = {};
		push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		push_range.offset = 0;
		push_range.size = sizeof(PushConstants);

		VkPipelineLayoutCreateInfo layout_ci = {};
		layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layout_ci.pushConstantRangeCount = 1;
		layout_ci.pPushConstantRanges = &push_range;

		if (vkCreatePipelineLayout(c.device, &layout_ci, nullptr, &pipeline_layout) != VK_SUCCESS) {
			logger::fatal("Failed to create pipeline layout");
			return false;
		}

		// dynamic rendering: declare attachment formats
		VkFormat color_formats[3] = {
			gb.albedo.format,
			gb.normal.format,
			gb.material.format,
		};

		VkPipelineRenderingCreateInfo rendering_ci = {};
		rendering_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		rendering_ci.colorAttachmentCount = 3;
		rendering_ci.pColorAttachmentFormats = color_formats;
		rendering_ci.depthAttachmentFormat = gb.depth.format;

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
		Swapchain& sc = swapchain();
		if (!create_attachments(sc.extent)) return false;
		if (!create_pipeline()) return false;
		return true;
	}

	void shutdown_gbuffer() {
		Context& c = context();
		if (pipeline) vkDestroyPipeline(c.device, pipeline, nullptr);
		if (pipeline_layout) vkDestroyPipelineLayout(c.device, pipeline_layout, nullptr);
		pipeline = VK_NULL_HANDLE;
		pipeline_layout = VK_NULL_HANDLE;
		destroy_attachments();
	}

	bool resize_gbuffer(VkExtent2D extent) {
		Context& c = context();
		vkDeviceWaitIdle(c.device);
		destroy_attachments();
		return create_attachments(extent);
	}

	static void transition(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
		VkImageLayout from, VkImageLayout to,
		VkAccessFlags src_access, VkAccessFlags dst_access,
		VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage)
	{
		VkImageMemoryBarrier b = {};
		b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		b.oldLayout = from;
		b.newLayout = to;
		b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		b.image = image;
		b.subresourceRange.aspectMask = aspect;
		b.subresourceRange.levelCount = 1;
		b.subresourceRange.layerCount = 1;
		b.srcAccessMask = src_access;
		b.dstAccessMask = dst_access;
		vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &b);
	}

	void execute_gbuffer_pass(VkCommandBuffer cmd,
		const mat4& view, const mat4& projection,
		const GBufferDraw* draws, u32 draw_count)
	{
		// transition all attachments to writable
		transition(cmd, gb.albedo.image, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
		transition(cmd, gb.normal.image, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
		transition(cmd, gb.material.image, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
		transition(cmd, gb.depth.image, VK_IMAGE_ASPECT_DEPTH_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);

		// rendering attachments
		VkRenderingAttachmentInfo color_attachments[3] = {};
		color_attachments[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		color_attachments[0].imageView = gb.albedo.view;
		color_attachments[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		color_attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color_attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color_attachments[0].clearValue.color = { { 0.05f, 0.07f, 0.10f, 1.0f } };

		color_attachments[1].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		color_attachments[1].imageView = gb.normal.view;
		color_attachments[1].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		color_attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color_attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color_attachments[1].clearValue.color = { { 0.5f, 0.5f, 1.0f, 0.0f } };

		color_attachments[2].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		color_attachments[2].imageView = gb.material.view;
		color_attachments[2].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		color_attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		color_attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		color_attachments[2].clearValue.color = { { 0.0f, 0.5f, 0.0f, 0.0f } };

		VkRenderingAttachmentInfo depth_attachment = {};
		depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depth_attachment.imageView = gb.depth.view;
		depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depth_attachment.clearValue.depthStencil = { 1.0f, 0 };

		VkRenderingInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		info.renderArea.offset = { 0, 0 };
		info.renderArea.extent = gb.extent;
		info.layerCount = 1;
		info.colorAttachmentCount = 3;
		info.pColorAttachments = color_attachments;
		info.pDepthAttachment = &depth_attachment;

		vkCmdBeginRendering(cmd, &info);

		// viewport/scissor (flip viewport for Vulkan to match GL-style projection)
		VkViewport viewport = {};
		viewport.x = 0.0f;
		viewport.y = (f32)gb.extent.height;
		viewport.width = (f32)gb.extent.width;
		viewport.height = -(f32)gb.extent.height;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(cmd, 0, 1, &viewport);

		VkRect2D scissor = {};
		scissor.offset = { 0, 0 };
		scissor.extent = gb.extent;
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

		mat4 view_proj = projection * view;

		for (u32 i = 0; i < draw_count; i++) {
			const MeshGPU* m = get_mesh(draws[i].mesh);
			if (!m) continue;

			PushConstants pc;
			pc.mvp = view_proj * draws[i].model;
			pc.color = draws[i].color;
			vkCmdPushConstants(cmd, pipeline_layout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0, sizeof(pc), &pc);

			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(cmd, 0, 1, &m->vertex_buffer, &offset);
			vkCmdBindIndexBuffer(cmd, m->index_buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(cmd, m->index_count, 1, 0, 0, 0);
		}

		vkCmdEndRendering(cmd);

		// transition albedo to TRANSFER_SRC for the debug blit that follows
		transition(cmd, gb.albedo.image, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
	}

}
