#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "types.hpp"
#include "math.hpp"

namespace vk {

	using MaterialHandle = u32;
	static constexpr MaterialHandle INVALID_MATERIAL = (MaterialHandle)~0u;
	static constexpr u32 MAX_MATERIALS = 64;

	// slot 0 is reserved for the engine default material.
	static constexpr MaterialHandle DEFAULT_MATERIAL = 0;

	// must match std430 layout in gbuffer shaders.
	struct MaterialGPU {
		vec4 base_color_factor;
		vec4 mr_factors;     // .x = metallic, .y = roughness, .zw unused
		u32  albedo_idx;
		u32  normal_idx;
		u32  orm_idx;
		u32  _pad;
	};
	static_assert(sizeof(MaterialGPU) == 48, "MaterialGPU std430 size mismatch");

	// developer-facing material description, mirrors renderer::MaterialDesc but
	// uses vk-side texture handles. api.cpp performs the conversion.
	struct MaterialDescGPU {
		u32  albedo_idx;
		u32  normal_idx;
		u32  orm_idx;
		vec4 base_color_factor;
		f32  metallic_factor;
		f32  roughness_factor;
	};

	bool init_materials();
	void shutdown_materials();

	MaterialHandle create_material(const MaterialDescGPU& desc);
	void           unload_material(MaterialHandle handle);

}
