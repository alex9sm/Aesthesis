#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "types.hpp"
#include "vk_gbuffer.hpp"   // DrawBatch

namespace vk {

	bool init_depth_prepass();
	void shutdown_depth_prepass();

	// renders position-only into the shared depth target with CLEAR/STORE.
	// runs before the g-buffer pass; gbuffer then loads the depth and
	// performs an EQUAL test with no depth writes.
	void execute_depth_prepass(VkCommandBuffer cmd,
		const DrawBatch* batches, u32 batch_count);

}
