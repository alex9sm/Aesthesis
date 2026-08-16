#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "types.hpp"
#include "math.hpp"
#include "vk_gbuffer.hpp"

namespace vk {

	static constexpr u32 CASCADE_COUNT   = 3;
	static constexpr u32 SHADOW_MAP_SIZE = 2048;

	bool init_shadow();
	void shutdown_shadow();

	void compute_cascades(const mat4& camera_view, const mat4& camera_proj,
		vec3 sun_dir, mat4 out_view_proj[CASCADE_COUNT], vec4& out_splits);

	// renders `batches` into all cascade layers
	void execute_shadow_pass(VkCommandBuffer cmd,
		const DrawBatch* batches, u32 batch_count);

}
