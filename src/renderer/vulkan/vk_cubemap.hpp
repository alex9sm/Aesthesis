#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"

#include "types.hpp"

namespace vk {

	using CubemapHandle = u32;
	static constexpr CubemapHandle INVALID_CUBEMAP = (CubemapHandle)~0u;
	static constexpr u32 MAX_CUBEMAPS = 32;

	// Per-cubemap GPU state.
	//   - source: original loaded cubemap (R8G8B8A8_SRGB + mip chain)
	//   - irradiance: 32×32×6 RGBA16F diffuse convolution (baked at load)
	//   - prefilter: 128×128×6 RGBA16F, 5 mips, per-roughness GGX convolution
	struct CubemapSlot {
		// source cubemap (R8G8B8A8_SRGB, 6 layers + mip chain)
		VkImage        source_image;
		VmaAllocation  source_alloc;
		VkImageView    source_view;

		// diffuse irradiance cubemap (32×32×6, RGBA16F, single mip)
		VkImage        irradiance_image;
		VmaAllocation  irradiance_alloc;
		VkImageView    irradiance_view;

		// prefiltered specular cubemap (PREFILTER_SIZE base, RGBA16F, PREFILTER_MIP_COUNT mips)
		VkImage        prefilter_image;
		VmaAllocation  prefilter_alloc;
		VkImageView    prefilter_view;   // full view (all mips), used for sampling

		u32  size;        // base source mip width = height
		u32  mip_levels;  // source mip count
		f32  intensity;   // per-load LDR brightness multiplier (both IBL bakes)

		// --- async bake bookkeeping ---
		// Single cmd buffer records both irradiance + prefilter; submitted
		// without a CPU wait. bake_fence fires when the GPU is done with
		// all of slot's bake work. unload_cubemap and shutdown_cubemaps wait
		// on it before tearing the transient resources down.
		VkFence          bake_fence;
		VkCommandPool    bake_cmd_pool;
		VkDescriptorPool bake_desc_pool;
		VkImageView      bake_pref_mip_views[5]; // PREFILTER_MIP_COUNT per-mip storage views

		bool ibl_baked;   // true once both irradiance and prefilter are ready
		bool in_use;
	};

	bool init_cubemaps();
	void shutdown_cubemaps();

	// Loads 6 PNG faces and uploads as a cubemap. Returns INVALID_CUBEMAP on
	// failure. `intensity` is stored on the slot for later use by the
	// prefilter bake (Phase F4); has no effect yet.
	CubemapHandle load_cubemap(const char* name, f32 intensity);
	void          unload_cubemap(CubemapHandle handle);

	const CubemapSlot* get_cubemap(CubemapHandle handle);

}
