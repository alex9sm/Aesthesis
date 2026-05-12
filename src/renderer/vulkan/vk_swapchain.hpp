#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "types.hpp"

namespace vk {

	static constexpr u32 MAX_SWAPCHAIN_IMAGES = 8;

	struct Swapchain {
		VkSwapchainKHR handle;
		VkFormat format;
		VkColorSpaceKHR color_space;
		VkExtent2D extent;
		VkPresentModeKHR present_mode;
		u32 image_count;
		VkImage images[MAX_SWAPCHAIN_IMAGES];
		VkImageView image_views[MAX_SWAPCHAIN_IMAGES];
	};

	bool create_swapchain();
	void destroy_swapchain();
	bool recreate_swapchain();

	Swapchain& swapchain();

}
