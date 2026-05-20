#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "types.hpp"
#include "math.hpp"

namespace vk {

	static constexpr u32 MAX_DRAWS_PER_FRAME = 1024;

	// must match std430 layout in gbuffer.vert
	struct InstanceData {
		mat4 model;
		mat4 normal_matrix;
		vec4 tint;
		u32  material_id;
		u32  _pad[3];
	};
	static_assert(sizeof(InstanceData) == 160, "InstanceData std430 size mismatch");

	bool init_instances();
	void shutdown_instances();

	// reset the current frame's write cursor to 0. call once per frame
	// before pushing instances.
	void reset_instances();

	// append an instance and return its index within the current frame's
	// buffer. returns UINT32_MAX if the per-frame cap is exceeded.
	u32 push_instance(const InstanceData& data);

	// build the per-instance normal matrix (transpose(inverse(model3x3)))
	// packed into a mat4 for std430 alignment.
	mat4 compute_normal_matrix(const mat4& model);

}
