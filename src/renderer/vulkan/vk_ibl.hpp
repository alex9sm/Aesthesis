#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "types.hpp"
#include "vk_cubemap.hpp"

namespace vk {

	// Creates the global BRDF LUT (PNG load with compute-bake fallback), the
	// irradiance compute pipeline, and neutral 1×1×6 placeholder cubemaps,
	// then writes set-0 bindings 4/5/6 for every frame. Must run after
	// init_globals.
	bool init_ibl();
	void shutdown_ibl();

	// Records both the irradiance bake and the per-mip prefilter bakes into a
	// single command buffer, then submits ASYNCHRONOUSLY (no CPU wait) tied
	// to slot.bake_fence. Source cubemap must already be in
	// SHADER_READ_ONLY_OPTIMAL. The slot's `intensity` is applied during
	// both bakes as an LDR brightness compensation multiplier. On success,
	// slot.ibl_baked = true and slot.bake_fence / bake_cmd_pool /
	// bake_desc_pool / bake_pref_mip_views own the transient GPU work.
	bool bake_ibl(CubemapHandle handle);

	// Waits on slot->bake_fence (if any), destroys all transient bake
	// resources stored on the slot, and nulls them. Idempotent.
	void release_bake_resources(CubemapSlot* slot);

	// Selects which cubemap drives IBL diffuse (binding 4) + specular
	// (binding 5). Pure descriptor write across all frames-in-flight.
	// INVALID_CUBEMAP reverts to neutral placeholders. Must be called outside
	// begin_frame / end_frame.
	void          set_environment_cubemap(CubemapHandle handle);
	CubemapHandle active_environment();

}
