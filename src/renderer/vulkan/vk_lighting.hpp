#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "types.hpp"

namespace vk {

	bool init_lighting();
	void shutdown_lighting();

	// re-binds gbuffer image views into the lighting descriptor set.
	// must be called once after init_targets and again after every resize_targets.
	void lighting_refresh_descriptors();

	void execute_lighting_pass(VkCommandBuffer cmd);

}
