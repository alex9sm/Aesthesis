#pragma once

#include "types.hpp"
#include "math.hpp"

namespace renderer {

	using MeshHandle = u32;
	static constexpr MeshHandle INVALID_MESH = 0;

	using TextureHandle = u32;
	static constexpr TextureHandle INVALID_TEXTURE = (TextureHandle)~0u;

	using MaterialHandle = u32;
	static constexpr MaterialHandle INVALID_MATERIAL = (MaterialHandle)~0u;
	// slot 0 is the engine-provided default material (flat grey, roughness=1, metallic=0).
	static constexpr MaterialHandle DEFAULT_MATERIAL_HANDLE = 0;

	using ModelHandle = u32;
	static constexpr ModelHandle INVALID_MODEL = (ModelHandle)~0u;

	using CubemapHandle = u32;
	static constexpr CubemapHandle INVALID_CUBEMAP = (CubemapHandle)~0u;

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

	// developer-facing material description. unset texture handles default to
	// the engine reserved slots (white/flat-normal/ORM-neutral).
	struct MaterialDesc {
		TextureHandle albedo            = DEFAULT_ALBEDO;
		TextureHandle normal            = DEFAULT_NORMAL;
		TextureHandle orm               = DEFAULT_ORM;
		vec4          base_color_factor = { 1.0f, 1.0f, 1.0f, 1.0f };
		f32           metallic_factor   = 1.0f;
		f32           roughness_factor  = 1.0f;
	};

	bool init();
	void shutdown();

	// resource management
	MeshHandle load_mesh(const char* path);
	void unload_mesh(MeshHandle handle);

	TextureHandle load_texture(const char* path);
	void unload_texture(TextureHandle handle);

	MaterialHandle create_material(const MaterialDesc& desc);
	void unload_material(MaterialHandle handle);
	MaterialHandle default_material();

	ModelHandle load_model(const char* path);
	void unload_model(ModelHandle handle);

	// loads a single PNG at assets/textures/global/<name>.png containing the
	// 6 cube faces as a horizontal strip (left-to-right: +X, -X, +Y, -Y, +Z, -Z).
	// The image must be 6N wide × N tall.
	// `intensity` is a per-cubemap LDR brightness multiplier applied during the
	// IBL prefilter bake (Phase F4); ignored at load time, 1.0 = neutral.
	// load_cubemap also bakes diffuse irradiance for use as an environment.
	// Must be called outside begin_frame / end_frame.
	CubemapHandle load_cubemap(const char* name, f32 intensity = 1.0f);
	void          unload_cubemap(CubemapHandle handle);

	// IBL environment selection. set_environment_cubemap drives the diffuse
	// (and, from Phase F4, specular) IBL terms in the lighting pass. Pure
	// descriptor swap — no GPU work. Pass INVALID_CUBEMAP / call the clear
	// variant to revert to neutral mid-grey placeholders.
	// Conventionally called between frames; ownership is not transferred.
	void set_environment_cubemap(CubemapHandle handle);
	void clear_environment_cubemap();

	// lighting (persistent state; set whenever the scene changes it)
	void set_sun(vec3 direction, vec3 color, f32 intensity);

	// frame
	void begin_frame(const mat4& view, const mat4& projection);
	void submit_mesh(MeshHandle mesh, MaterialHandle material,
		const mat4& model, vec4 tint = { 1.0f, 1.0f, 1.0f, 1.0f });
	void submit_model(ModelHandle model, const mat4& transform,
		vec4 tint = { 1.0f, 1.0f, 1.0f, 1.0f });
	void end_frame();

	// debug
	void cycle_debug_mode();
	void set_debug_mode(u32 mode);
	u32  debug_mode();

}
