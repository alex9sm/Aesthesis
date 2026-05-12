#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "types.hpp"

namespace vk {

	VkShaderModule load_shader_module(const char* spv_path);
	void destroy_shader_module(VkShaderModule module);

}
