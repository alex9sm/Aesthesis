#include "vk_pch.hpp"
#include "vk_draw2d.hpp"
#include "vk_init.hpp"
#include "vk_swapchain.hpp"
#include "vk_targets.hpp"
#include "vk_memory.hpp"
#include "vk_frame.hpp"
#include "vk_globals.hpp"
#include "vk_pipeline.hpp"
#include "log.hpp"
#include "memory.hpp"

#include <stddef.h>

namespace vk {

	struct RectVertex {
		f32 x, y;
		f32 r, g, b, a;
	};

	struct TextVertex {
		f32 x, y;
		f32 u, v;
		f32 r, g, b, a;
	};

	// per-text-batch: a run of TextVertex sharing the same atlas slot.
	struct TextBatch {
		u32 first_vertex;
		u32 vertex_count;
		TextureHandle atlas_idx;
	};

	static constexpr u32 MAX_RECT_VERTS  = 4096;   // 682 rects/frame
	static constexpr u32 MAX_TEXT_VERTS  = 8192;   // ~1365 glyphs/frame
	static constexpr u32 MAX_TEXT_BATCHES = 32;

	struct PerFrame {
		VkBuffer       rect_vb;
		VmaAllocation  rect_alloc;
		RectVertex*    rect_mapped;

		VkBuffer       text_vb;
		VmaAllocation  text_alloc;
		TextVertex*    text_mapped;
	};

	static PerFrame frames[FRAMES_IN_FLIGHT] = {};

	// CPU staging (per frame)
	static RectVertex rect_verts[MAX_RECT_VERTS];
	static u32        rect_count = 0;

	static TextVertex text_verts[MAX_TEXT_VERTS];
	static u32        text_count = 0;

	static TextBatch  text_batches[MAX_TEXT_BATCHES];
	static u32        text_batch_count = 0;

	// pipelines
	static VkPipelineLayout rect_layout    = VK_NULL_HANDLE;
	static VkPipeline       rect_pipeline  = VK_NULL_HANDLE;
	static VkPipelineLayout text_layout    = VK_NULL_HANDLE;
	static VkPipeline       text_pipeline  = VK_NULL_HANDLE;

	// --- pipeline creation ---

	static bool create_rect_pipeline() {
		Swapchain& sc = swapchain();

		VkVertexInputBindingDescription vb = {};
		vb.binding = 0;
		vb.stride = sizeof(RectVertex);
		vb.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription va[2] = {};
		va[0].location = 0; va[0].format = VK_FORMAT_R32G32_SFLOAT;       va[0].offset = offsetof(RectVertex, x);
		va[1].location = 1; va[1].format = VK_FORMAT_R32G32B32A32_SFLOAT; va[1].offset = offsetof(RectVertex, r);

		VkFormat color_format = sc.format;
		VkDescriptorSetLayout layouts[] = { global_set_layout() };

		GraphicsPipelineSpec spec = {};
		spec.vs_path = "shaders/spv/draw2d_rect.vert.spv";
		spec.fs_path = "shaders/spv/draw2d_rect.frag.spv";
		spec.vertex_binding = &vb;
		spec.vertex_attrs = va;
		spec.vertex_attr_count = 2;
		spec.color_formats = &color_format;
		spec.color_count = 1;
		spec.blend = BlendMode::AlphaBlend;
		spec.set_layouts = layouts;
		spec.set_layout_count = 1;

		return create_graphics_pipeline(spec, &rect_pipeline, &rect_layout);
	}

	static bool create_text_pipeline() {
		Swapchain& sc = swapchain();

		VkVertexInputBindingDescription vb = {};
		vb.binding = 0;
		vb.stride = sizeof(TextVertex);
		vb.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription va[3] = {};
		va[0].location = 0; va[0].format = VK_FORMAT_R32G32_SFLOAT;       va[0].offset = offsetof(TextVertex, x);
		va[1].location = 1; va[1].format = VK_FORMAT_R32G32_SFLOAT;       va[1].offset = offsetof(TextVertex, u);
		va[2].location = 2; va[2].format = VK_FORMAT_R32G32B32A32_SFLOAT; va[2].offset = offsetof(TextVertex, r);

		VkPushConstantRange push_range = {};
		push_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		push_range.offset = 0;
		push_range.size = sizeof(u32);

		VkFormat color_format = sc.format;
		VkDescriptorSetLayout layouts[] = { global_set_layout() };

		GraphicsPipelineSpec spec = {};
		spec.vs_path = "shaders/spv/draw2d_text.vert.spv";
		spec.fs_path = "shaders/spv/draw2d_text.frag.spv";
		spec.vertex_binding = &vb;
		spec.vertex_attrs = va;
		spec.vertex_attr_count = 3;
		spec.color_formats = &color_format;
		spec.color_count = 1;
		spec.blend = BlendMode::AlphaBlend;
		spec.set_layouts = layouts;
		spec.set_layout_count = 1;
		spec.push_constant = &push_range;

		return create_graphics_pipeline(spec, &text_pipeline, &text_layout);
	}

	static bool create_vertex_buffers() {
		VmaAllocator a = allocator();

		for (u32 i = 0; i < FRAMES_IN_FLIGHT; i++) {
			// rect VB
			{
				VkBufferCreateInfo bci = {};
				bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
				bci.size = sizeof(RectVertex) * MAX_RECT_VERTS;
				bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
				bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

				VmaAllocationCreateInfo aci = {};
				aci.usage = VMA_MEMORY_USAGE_AUTO;
				aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
					| VMA_ALLOCATION_CREATE_MAPPED_BIT;

				VmaAllocationInfo info = {};
				if (vmaCreateBuffer(a, &bci, &aci, &frames[i].rect_vb, &frames[i].rect_alloc, &info) != VK_SUCCESS) {
					logger::fatal("Failed to create draw2d rect VB");
					return false;
				}
				frames[i].rect_mapped = (RectVertex*)info.pMappedData;
			}
			// text VB
			{
				VkBufferCreateInfo bci = {};
				bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
				bci.size = sizeof(TextVertex) * MAX_TEXT_VERTS;
				bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
				bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

				VmaAllocationCreateInfo aci = {};
				aci.usage = VMA_MEMORY_USAGE_AUTO;
				aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
					| VMA_ALLOCATION_CREATE_MAPPED_BIT;

				VmaAllocationInfo info = {};
				if (vmaCreateBuffer(a, &bci, &aci, &frames[i].text_vb, &frames[i].text_alloc, &info) != VK_SUCCESS) {
					logger::fatal("Failed to create draw2d text VB");
					return false;
				}
				frames[i].text_mapped = (TextVertex*)info.pMappedData;
			}
		}
		return true;
	}

	bool init_draw2d() {
		if (!create_vertex_buffers()) return false;
		if (!create_rect_pipeline()) return false;
		if (!create_text_pipeline()) return false;
		return true;
	}

	void shutdown_draw2d() {
		Context& c = context();
		VmaAllocator a = allocator();

		for (u32 i = 0; i < FRAMES_IN_FLIGHT; i++) {
			if (frames[i].rect_vb) vmaDestroyBuffer(a, frames[i].rect_vb, frames[i].rect_alloc);
			if (frames[i].text_vb) vmaDestroyBuffer(a, frames[i].text_vb, frames[i].text_alloc);
		}
		memory::set(frames, 0, sizeof(frames));

		if (text_pipeline) vkDestroyPipeline(c.device, text_pipeline, nullptr);
		if (text_layout)   vkDestroyPipelineLayout(c.device, text_layout, nullptr);
		if (rect_pipeline) vkDestroyPipeline(c.device, rect_pipeline, nullptr);
		if (rect_layout)   vkDestroyPipelineLayout(c.device, rect_layout, nullptr);
		text_pipeline = VK_NULL_HANDLE;
		text_layout   = VK_NULL_HANDLE;
		rect_pipeline = VK_NULL_HANDLE;
		rect_layout   = VK_NULL_HANDLE;
	}

	// --- queue ---

	void draw2d_push_rect(f32 x, f32 y, f32 w, f32 h, vec4 color) {
		if (rect_count + 6 > MAX_RECT_VERTS) return;
		f32 x0 = x, y0 = y, x1 = x + w, y1 = y + h;
		RectVertex c = { 0, 0, color.x, color.y, color.z, color.w };
		RectVertex* v = &rect_verts[rect_count];
		v[0] = c; v[0].x = x0; v[0].y = y0;
		v[1] = c; v[1].x = x1; v[1].y = y0;
		v[2] = c; v[2].x = x0; v[2].y = y1;
		v[3] = c; v[3].x = x1; v[3].y = y0;
		v[4] = c; v[4].x = x1; v[4].y = y1;
		v[5] = c; v[5].x = x0; v[5].y = y1;
		rect_count += 6;
	}

	void draw2d_push_text_quad(f32 x0, f32 y0, f32 x1, f32 y1,
		f32 u0, f32 v0, f32 u1, f32 v1, vec4 color, TextureHandle atlas_idx)
	{
		if (text_count + 6 > MAX_TEXT_VERTS) return;

		// open a new batch when the atlas changes (or first quad)
		if (text_batch_count == 0 ||
			text_batches[text_batch_count - 1].atlas_idx != atlas_idx)
		{
			if (text_batch_count >= MAX_TEXT_BATCHES) return;
			text_batches[text_batch_count++] = { text_count, 0, atlas_idx };
		}
		TextBatch& b = text_batches[text_batch_count - 1];

		TextVertex* v = &text_verts[text_count];
		auto fill = [&](TextVertex& q, f32 px, f32 py, f32 tu, f32 tv) {
			q.x = px; q.y = py; q.u = tu; q.v = tv;
			q.r = color.x; q.g = color.y; q.b = color.z; q.a = color.w;
		};
		fill(v[0], x0, y0, u0, v0);
		fill(v[1], x1, y0, u1, v0);
		fill(v[2], x0, y1, u0, v1);
		fill(v[3], x1, y0, u1, v0);
		fill(v[4], x1, y1, u1, v1);
		fill(v[5], x0, y1, u0, v1);
		text_count += 6;
		b.vertex_count += 6;
	}

	// --- execute ---

	void execute_overlay_pass(VkCommandBuffer cmd, u32 swapchain_image_index) {
		Swapchain& sc = swapchain();
		u32 frame_idx = current_frame_index();
		PerFrame& f = frames[frame_idx];

		// upload CPU-staged verts into this frame's mapped VB (linear copy)
		if (rect_count > 0) {
			memory::copy(f.rect_mapped, rect_verts, sizeof(RectVertex) * rect_count);
		}
		if (text_count > 0) {
			memory::copy(f.text_mapped, text_verts, sizeof(TextVertex) * text_count);
		}

		bool has_work = (rect_count > 0) || (text_count > 0);
		VkImage dst = sc.images[swapchain_image_index];
		VkImageView dst_view = sc.image_views[swapchain_image_index];

		if (has_work) {
			VkRenderingAttachmentInfo color = {};
			color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			color.imageView = dst_view;
			color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;     // preserve debug pass output
			color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

			VkRenderingInfo info = {};
			info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
			info.renderArea.offset = { 0, 0 };
			info.renderArea.extent = sc.extent;
			info.layerCount = 1;
			info.colorAttachmentCount = 1;
			info.pColorAttachments = &color;

			vkCmdBeginRendering(cmd, &info);

			VkViewport viewport = {};
			viewport.x = 0.0f;
			viewport.y = 0.0f;
			viewport.width = (f32)sc.extent.width;
			viewport.height = (f32)sc.extent.height;
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;
			vkCmdSetViewport(cmd, 0, 1, &viewport);

			VkRect2D scissor = {};
			scissor.offset = { 0, 0 };
			scissor.extent = sc.extent;
			vkCmdSetScissor(cmd, 0, 1, &scissor);

			VkDescriptorSet g_set = current_global_set();

			// rects first
			if (rect_count > 0) {
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rect_pipeline);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rect_layout,
					0, 1, &g_set, 0, nullptr);

				VkBuffer vb = f.rect_vb;
				VkDeviceSize offs = 0;
				vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offs);
				vkCmdDraw(cmd, rect_count, 1, 0, 0);
			}

			// text — per batch
			if (text_count > 0) {
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, text_pipeline);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, text_layout,
					0, 1, &g_set, 0, nullptr);

				VkBuffer vb = f.text_vb;
				VkDeviceSize offs = 0;
				vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offs);

				for (u32 i = 0; i < text_batch_count; i++) {
					const TextBatch& b = text_batches[i];
					u32 atlas = b.atlas_idx;
					vkCmdPushConstants(cmd, text_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
						0, sizeof(u32), &atlas);
					vkCmdDraw(cmd, b.vertex_count, 1, b.first_vertex, 0);
				}
			}

			vkCmdEndRendering(cmd);
		}

		// always transition to PRESENT_SRC — overlay is the single owner of this transition
		transition_raw(cmd, dst, ResState::ColorWrite, ResState::Present);

		// clear CPU queues for next frame
		rect_count = 0;
		text_count = 0;
		text_batch_count = 0;
	}

}
