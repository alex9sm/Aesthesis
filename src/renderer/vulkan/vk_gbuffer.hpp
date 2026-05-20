#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "types.hpp"
#include "math.hpp"
#include "vk_mesh.hpp"

namespace vk {

	bool init_gbuffer();
	void shutdown_gbuffer();

	// `meshes` is parallel to the instance SSBO: meshes[i] is drawn with
	// firstInstance = i. caller is responsible for writing instance data
	// to the SSBO before this is recorded.
	void execute_gbuffer_pass(VkCommandBuffer cmd,
		const MeshHandle* meshes, u32 draw_count);

}
