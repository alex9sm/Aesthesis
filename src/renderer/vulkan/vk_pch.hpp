#pragma once

// Precompiled header for the Vulkan backend.
// Only stable, slow-to-parse, near-never-changing headers belong here.
// Adding anything that changes frequently invalidates the PCH for all 16 TUs.

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"

#include "types.hpp"
#include "log.hpp"
