#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "types.hpp"

namespace vk {

	static constexpr u32 FRAMES_IN_FLIGHT = 2;

	bool init_frames();
	void shutdown_frames();

	// returns false if the frame should be skipped (e.g. swapchain out of date)
	bool begin_frame();
	bool end_frame();

	VkCommandBuffer current_cmd();
	u32 current_swapchain_image();

}
