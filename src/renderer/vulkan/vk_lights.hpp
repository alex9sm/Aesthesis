#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "types.hpp"
#include "math.hpp"

namespace vk {

	static constexpr u32 MAX_POINT_LIGHTS = 64;

	// must match std430 layout in lighting.frag
	struct PointLightGPU {
		vec4 position_radius;   // xyz = world pos, w = radius
		vec4 color_intensity;   // rgb = color, w = intensity
	};
	static_assert(sizeof(PointLightGPU) == 32, "PointLightGPU std430 size mismatch");

	bool init_lights();
	void shutdown_lights();

	// reset the current frame's write cursor to 0. call once per frame.
	void reset_lights();

	// append a point light and return its index. returns UINT32_MAX on overflow.
	u32 push_light(const PointLightGPU& data);

	// number of lights pushed this frame (for uploading into UBO misc.x).
	u32 light_count();

}
