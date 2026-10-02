#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "types.hpp"
#include "math.hpp"
#include "shared.glsl"

namespace vk {

	struct GlobalUBO {
		mat4 view;
		mat4 proj;
		mat4 inv_view;
		mat4 inv_proj;
		vec4 cam_pos;
		vec4 sun_dir;
		vec4 sun_color;
		vec4 viewport_size;
		vec4 misc;
		mat4 cascade_view_proj[SHARED_CASCADE_COUNT];
		vec4 cascade_splits; // x/y/z = view-space far distance of cascades 0/1/2
		mat4 spot_shadow_vp[SHARED_SPOT_SHADOW_SLOTS];
	};

	bool init_globals();
	void shutdown_globals();
	void update_globals(const GlobalUBO& data);
	void patch_globals_misc(const vec4& misc);
	void patch_globals_spot_shadows(const mat4* view_proj, u32 count);

	VkDescriptorSetLayout global_set_layout();
	VkDescriptorSet       current_global_set();
	VkDescriptorSet       global_set_for_frame(u32 frame_index);

}
