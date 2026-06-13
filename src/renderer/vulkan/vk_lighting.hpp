#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "types.hpp"

namespace vk {

	bool init_lighting();
	void shutdown_lighting();

	void execute_lighting_pass(VkCommandBuffer cmd);

}
