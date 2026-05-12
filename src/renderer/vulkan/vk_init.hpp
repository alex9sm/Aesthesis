#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "types.hpp"

namespace vk {

	struct Context {
		VkInstance instance;
		VkSurfaceKHR surface;
		VkPhysicalDevice physical_device;
		VkDevice device;

		u32 graphics_queue_index;
		u32 present_queue_index;
		VkQueue graphics_queue;
		VkQueue present_queue;
	};

	bool init();
	void shutdown();

	Context& context();

}
