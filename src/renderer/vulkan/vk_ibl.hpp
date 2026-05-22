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
	// SHADER_READ_ONLY_OPTIMAL. On success, slot.ibl_baked is set true.
	// Called once per cubemap during vk::load_cubemap.
	bool bake_irradiance(CubemapHandle handle);

	// Selects which cubemap drives IBL diffuse (binding 4) + specular
	// (binding 5; prefilter still placeholder until Phase F4). Pure descriptor
	// write across all frames-in-flight. INVALID_CUBEMAP reverts to neutral
	// placeholders. Must be called outside begin_frame / end_frame.
	void set_environment_cubemap(CubemapHandle handle);

}
