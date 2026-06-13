#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "types.hpp"

namespace vk {

	bool init_debug();
	void shutdown_debug();

	// composites the selected debug mode onto the swapchain image. leaves the
	// swapchain image in COLOR_ATTACHMENT_OPTIMAL — the overlay pass handles
	// the final transition to PRESENT_SRC_KHR.
	void execute_debug_pass(VkCommandBuffer cmd, u32 swapchain_image_index, u32 mode);

}
