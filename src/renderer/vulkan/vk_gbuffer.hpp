#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "types.hpp"
#include "math.hpp"
#include "vk_mesh.hpp"

namespace vk {

	struct GBufferDraw {
		MeshHandle mesh;
		mat4 model;
		vec4 color;
	};

	bool init_gbuffer();
	void shutdown_gbuffer();

	void execute_gbuffer_pass(VkCommandBuffer cmd,
		const GBufferDraw* draws, u32 draw_count);

}
