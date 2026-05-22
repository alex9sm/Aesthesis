#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "types.hpp"
#include "math.hpp"
#include "vk_texture.hpp"  // for TextureHandle

namespace vk {

	bool init_draw2d();
	void shutdown_draw2d();

	// queue a colored rectangle in pixel-space (top-left origin)
	void draw2d_push_rect(f32 x, f32 y, f32 w, f32 h, vec4 color);

	// queue a textured quad (text glyph). atlas_idx is a bindless texture slot.
	void draw2d_push_text_quad(f32 x0, f32 y0, f32 x1, f32 y1,
		f32 u0, f32 v0, f32 u1, f32 v1, vec4 color, TextureHandle atlas_idx);

	// renders all queued 2D elements onto the swapchain image and transitions
	// it from COLOR_ATTACHMENT_OPTIMAL to PRESENT_SRC_KHR. always called once
	// per frame after execute_debug_pass — even when nothing was queued — so
	// the swapchain transition is single-source-of-truth here.
	void execute_overlay_pass(VkCommandBuffer cmd, u32 swapchain_image_index);

}
