#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "types.hpp"
#include "math.hpp"
#include "vk_mesh.hpp"

// compute pre-skinning: each skinned mesh instance owns a copy of its source mesh whose
// vertex streams a compute pass re-poses once per frame. every later pass draws the copy
// as an ordinary mesh.
namespace vk {

	static constexpr u32 MAX_SKINNED_MESHES = 64;
	static constexpr u32 MAX_SKIN_JOINTS_PER_FRAME = 2048;

	bool init_skinning();
	void shutdown_skinning();

	// src must carry a skin stream. records the copy on the upload batch
	MeshHandle create_skinned_mesh(MeshHandle src);
	// caller waits for the GPU to go idle first
	void destroy_skinned_mesh(MeshHandle mesh);

	// this frame's model-space skin matrices. returns their offset, UINT32_MAX when full
	u32 push_joints(const mat4* joints, u32 count);
	void queue_skinning(MeshHandle mesh, u32 joint_offset);

	// dispatches every queued mesh; record before any pass that draws them
	void execute_skinning(VkCommandBuffer cmd);

}
