#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"

#include "types.hpp"

namespace vk {

	bool init_memory();
	void shutdown_memory();

	VmaAllocator allocator();

}
