#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "types.hpp"
#include "math.hpp"

namespace vk {

	static constexpr u32 MAX_LIGHTS = 64;

	// must match Light in shaders/glsl/include/lights.glsl. a point light is a spot
	// with cos_outer = -2 and cone_scale = 1, so the cone term is always 1.
	struct LightGPU {
		vec4 position_range;     // xyz = world pos, w = falloff range
		vec4 color_source;       // rgb = color * intensity, w = source radius
		vec4 direction_cos_out;  // xyz = spot direction, w = cos(outer half-angle)
		vec4 params;             // x = 1 / (cos_inner - cos_outer), y = shadow slot (-1 = none)
	};
	static_assert(sizeof(LightGPU) == 64, "LightGPU std430 size mismatch");

	bool init_lights();
	void shutdown_lights();

	// reset the current frame's write cursor to 0. call once per frame.
	void reset_lights();

	// append a light and return its index. returns UINT32_MAX on overflow.
	u32 push_light(const LightGPU& data);

	// number of lights pushed this frame (for uploading into UBO misc.x).
	u32 light_count();

}
