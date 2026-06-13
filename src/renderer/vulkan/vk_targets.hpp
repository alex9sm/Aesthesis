#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"

#include "types.hpp"

namespace vk {

	// semantic resource state: each maps to a {layout, access, stage} triple
	// (see state_info in vk_targets.cpp). Passes declare intent, not raw masks.
	enum class ResState {
		Undefined,   // discard / initial
		ColorWrite,  // color attachment output
		DepthWrite,  // depth attachment read+write
		DepthRead,   // depth attachment read (EQUAL test, no write)
		ShaderRead,  // sampled in a fragment shader
		Present,     // ready for vkQueuePresent
	};

	struct RenderImage {
		VkImage       image;
		VkImageView   view;
		VmaAllocation alloc;
		VkFormat      format;
		ResState      state; // tracked across passes
	};

	struct Targets {
		RenderImage albedo;     // RGBA8_UNORM
		RenderImage normal;     // RG16F (octahedral-encoded world normal)
		RenderImage material;   // RG8_UNORM
		RenderImage depth;      // D32_SFLOAT
		RenderImage scene_hdr;  // RGBA16F   (lighting output)
		VkExtent2D  extent;
	};

	bool init_targets(VkExtent2D extent);
	void shutdown_targets();
	bool resize_targets(VkExtent2D extent);

	Targets& targets();

	// resize-callback registry: passes that sample render targets register a
	// refresh hook (rewrites their descriptor sets) at init. recreate_swapchain
	// runs them after resize_targets, so swapchain code never names a pass.
	void register_target_consumer(void(*refresh)());
	void refresh_target_consumers();

	// transition a tracked image to a new state. old state, layouts, access
	// and stage masks are all derived from img.state -> new_state.
	void transition(VkCommandBuffer cmd, RenderImage& img, ResState new_state);

	// same, but discards previous contents (oldLayout = UNDEFINED). use when
	// the image is about to be fully overwritten and its old data is dead.
	void transition_discard(VkCommandBuffer cmd, RenderImage& img, ResState new_state);

	// transition an untracked image (e.g. swapchain) between explicit states.
	void transition_raw(VkCommandBuffer cmd, VkImage image, ResState from, ResState to);

}
