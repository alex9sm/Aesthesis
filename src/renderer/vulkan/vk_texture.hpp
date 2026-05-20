#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"

#include "types.hpp"

namespace vk {

	using TextureHandle = u32;
	static constexpr TextureHandle INVALID_TEXTURE = (TextureHandle)~0u;
	static constexpr u32 MAX_TEXTURES = 256;

	// reserved slots, populated at init time
	static constexpr TextureHandle TEX_DEFAULT_ALBEDO = 0;  // 1x1 white
	static constexpr TextureHandle TEX_DEFAULT_NORMAL = 1;  // 1x1 flat-normal (0.5, 0.5, 1)
	static constexpr TextureHandle TEX_DEFAULT_ORM    = 2;  // 1x1 ORM neutral (AO=1, R=1, M=0)

	bool init_textures();
	void shutdown_textures();

	// loads an RGBA8 image from disk (jpg/png/etc via stb_image). returns
	// INVALID_TEXTURE on failure. caller owns the returned handle and must
	// eventually call unload_texture.
	TextureHandle load_texture(const char* path);

	// loads from raw RGBA8 pixel data. exposed mainly for procedural/reserved
	// textures (the engine itself uses this for the default slots).
	TextureHandle load_texture_pixels(const u8* rgba, u32 width, u32 height);

	void unload_texture(TextureHandle handle);

}
