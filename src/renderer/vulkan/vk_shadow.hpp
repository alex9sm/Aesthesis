#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "types.hpp"
#include "math.hpp"
#include "shared.glsl"
#include "vk_gbuffer.hpp"

namespace vk {

	static constexpr u32 CASCADE_COUNT   = SHARED_CASCADE_COUNT;
	static constexpr u32 SHADOW_MAP_SIZE = 2048;

	bool init_shadow();
	void shutdown_shadow();

	void compute_cascades(const mat4& camera_view, const mat4& camera_proj,
		vec3 sun_dir, mat4 out_view_proj[CASCADE_COUNT], vec4& out_splits);

	static constexpr u32 SPOT_SHADOW_SLOTS = SHARED_SPOT_SHADOW_SLOTS;
	static constexpr u32 SPOT_ATLAS_COLS   = SHARED_SPOT_ATLAS_COLS;
	static constexpr u32 SPOT_TILE_SIZE    = SHARED_SPOT_TILE_SIZE;
	static constexpr f32 SPOT_SHADOW_NEAR  = (f32)SHARED_SPOT_SHADOW_NEAR;

	// one shadow-casting view: its matrix and its caster list, already culled against it.
	struct ShadowView {
		mat4             view_proj;
		const DrawBatch* batches;
		u32              count;
	};

	// renders each cascade's own caster list into its layer. nullptr = sun off: skip
	// rendering, only keep the map sampleable.
	void execute_shadow_pass(VkCommandBuffer cmd, const ShadowView* cascades);

	// renders spot slot i into atlas tile i. count may be 0 (atlas is still made sampleable).
	void execute_spot_shadow_pass(VkCommandBuffer cmd, const ShadowView* spots, u32 count);

}
