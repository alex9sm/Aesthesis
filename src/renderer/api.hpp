#pragma once

#include "types.hpp"
#include "math.hpp"

namespace renderer {

	using MeshHandle = u32;
	static constexpr MeshHandle INVALID_MESH = 0;

	using TextureHandle = u32;
	static constexpr TextureHandle INVALID_TEXTURE = (TextureHandle)~0u;

	// engine-provided reserved texture slots, always populated
	static constexpr TextureHandle DEFAULT_ALBEDO = 0;  // 1x1 white
	static constexpr TextureHandle DEFAULT_NORMAL = 1;  // 1x1 flat-normal
	static constexpr TextureHandle DEFAULT_ORM    = 2;  // 1x1 ORM neutral

	enum DebugMode : u32 {
		DEBUG_FINAL    = 0,  // Reinhard(scene_hdr) + gamma 2.2
		DEBUG_ALBEDO   = 1,
		DEBUG_NORMAL   = 2,
		DEBUG_MATERIAL = 3,
		DEBUG_DEPTH    = 4,
		DEBUG_HDR_RAW  = 5,
		DEBUG_COUNT    = 6
	};

	bool init();
	void shutdown();

	// resource management
	MeshHandle load_mesh(const char* path);
	void unload_mesh(MeshHandle handle);

	TextureHandle load_texture(const char* path);
	void unload_texture(TextureHandle handle);

	// frame
	void begin_frame(const mat4& view, const mat4& projection);
	void submit_mesh(MeshHandle mesh, const mat4& model, vec4 color);
	void end_frame();

	// debug
	void cycle_debug_mode();
	void set_debug_mode(u32 mode);
	u32  debug_mode();

}
