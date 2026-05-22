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

	// Bakes the irradiance cubemap into slot.irradiance_* using the irradiance
	// compute pipeline. The source cubemap must already be in
	// SHADER_READ_ONLY_OPTIMAL. Called during vk::load_cubemap before
	// bake_prefilter; together they populate slot.ibl_baked.
	bool bake_irradiance(CubemapHandle handle);

	// Bakes the prefiltered specular cubemap (PREFILTER_SIZE base, 5 mips)
	// into slot.prefilter_* with one compute dispatch per mip, each at the
	// roughness `mip / (mips - 1)`. The slot's `intensity` is applied as an
	// LDR brightness compensation multiplier during accumulation. Source must
	// already be SHADER_READ_ONLY_OPTIMAL. On success, slot.ibl_baked = true.
	bool bake_prefilter(CubemapHandle handle);

	// Selects which cubemap drives IBL diffuse (binding 4) + specular
	// (binding 5). Pure descriptor write across all frames-in-flight.
	// INVALID_CUBEMAP reverts to neutral placeholders. Must be called outside
	// begin_frame / end_frame.
	void set_environment_cubemap(CubemapHandle handle);

}
