#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "types.hpp"
#include "math.hpp"

namespace vk {

	struct GlobalUBO {
		mat4 view;
		mat4 proj;
		mat4 inv_view;
		mat4 inv_proj;
		vec4 cam_pos;        // xyz world-space camera position; w = z_near
		vec4 sun_dir;        // xyz world-space direction TO light; w = z_far
		vec4 sun_color;      // rgb radiance; w intensity multiplier
		vec4 viewport_size;  // x=width, y=height, z=1/w, w=1/h
	};

	bool init_globals();
	void shutdown_globals();
	void update_globals(const GlobalUBO& data);

	VkDescriptorSetLayout global_set_layout();
	VkDescriptorSet       current_global_set();
	VkDescriptorSet       global_set_for_frame(u32 frame_index);

}
