#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "types.hpp"

namespace vk {

	bool init_debug();
	void shutdown_debug();

	// re-binds gbuffer + scene_hdr image views into the debug descriptor set.
	// must be called once after init_targets and again after every resize_targets.
	void debug_refresh_descriptors();

	// composites the selected debug mode onto the swapchain image and
	// transitions the swapchain image into PRESENT_SRC_KHR.
	void execute_debug_pass(VkCommandBuffer cmd, u32 swapchain_image_index, u32 mode);

}
