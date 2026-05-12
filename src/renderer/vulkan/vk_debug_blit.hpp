#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "types.hpp"

namespace vk {

	// blits the g-buffer albedo onto the swapchain image, transitioning
	// the swapchain image UNDEFINED -> TRANSFER_DST -> PRESENT around it.
	// expects gb.albedo to already be in TRANSFER_SRC_OPTIMAL.
	void execute_debug_blit(VkCommandBuffer cmd, u32 swapchain_image_index);

}
