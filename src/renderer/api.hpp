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

	using FontHandle = u32;
	static constexpr FontHandle INVALID_FONT = (FontHandle)~0u;

	// engine-provided reserved texture slots, always populated
	static constexpr TextureHandle DEFAULT_ALBEDO = 0;  // 1x1 white
	static constexpr TextureHandle DEFAULT_NORMAL = 1;  // 1x1 flat-normal
	static constexpr TextureHandle DEFAULT_ORM    = 2;  // 1x1 ORM neutral

	enum DebugMode : u32 {
		DEBUG_FINAL    = 0,
		DEBUG_ALBEDO   = 1,
		DEBUG_NORMAL   = 2,
		DEBUG_MATERIAL = 3,
		DEBUG_DEPTH    = 4,
		DEBUG_CASCADES = 5,
		DEBUG_COUNT    = 6
	};

	// developer-facing material description. unset texture handles default to
	// the engine reserved slots (white/flat-normal/ORM-neutral).
	struct MaterialDesc {
		TextureHandle albedo            = DEFAULT_ALBEDO;
		TextureHandle normal            = DEFAULT_NORMAL;
		TextureHandle orm               = DEFAULT_ORM;
		vec4          base_color_factor = { 1.0f, 1.0f, 1.0f, 1.0f };
		f32           metallic_factor   = 0.0f;
		f32           roughness_factor  = 1.0f;
		f32           normal_scale      = 1.0f;
	};

	bool init();
	void shutdown();

	// resource management
	MeshHandle load_mesh(const char* path);
	void unload_mesh(MeshHandle handle);

	TextureHandle load_texture(const char* path, bool srgb = false);
	void unload_texture(TextureHandle handle);

	MaterialHandle create_material(const MaterialDesc& desc);
	void unload_material(MaterialHandle handle);
	MaterialHandle default_material();

	struct GltfModel;

	ModelHandle load_model(const char* path);
	ModelHandle load_model(const GltfModel& model);   // caller keeps ownership (e.g. to build physics bodies)
	void unload_model(ModelHandle handle);

	// draws one object of a model at an absolute world transform (e.g. a dynamic body)
	struct ObjectOverride {
		u32  object_index;
		mat4 world;
	};

	// Loads 6 PNG faces from assets/textures/global/<name>/{px,nx,py,ny,pz,nz}.png
	CubemapHandle load_cubemap(const char* name, f32 intensity = 1.0f);

	// Releases the cubemap's GPU resources
	void unload_cubemap(CubemapHandle handle);

	void set_environment_cubemap(CubemapHandle handle);
	void clear_environment_cubemap();

	void set_sun(vec3 direction, vec3 color, f32 intensity);

	// per-frame lights. range = falloff cutoff; source_radius = physical size of the
	// emitter (softens specular highlights). spot cone angles are half-angles in degrees.
	// shadow-casting spots take an atlas slot (8 per frame, first come) and clamp outer to 80.
	void submit_point_light(vec3 position, vec3 color, f32 range, f32 intensity,
		f32 source_radius = 0.05f);
	void submit_spot_light(vec3 position, vec3 color, f32 range, f32 intensity,
		f32 source_radius, vec3 direction, f32 inner_deg, f32 outer_deg, bool casts_shadow = false);

	// frame
	void begin_frame(const mat4& view, const mat4& projection);
	void submit_mesh(MeshHandle mesh, MaterialHandle material,
		const mat4& model, vec4 tint = { 1.0f, 1.0f, 1.0f, 1.0f });
	void submit_model(ModelHandle model, const mat4& transform = mat4_identity(),
		vec4 tint = { 1.0f, 1.0f, 1.0f, 1.0f },
		const ObjectOverride* overrides = nullptr, u32 override_count = 0);
	void end_frame();

	// fonts — bakes an SDF atlas from a TTF and uploads it as a texture
	FontHandle load_font(const char* path, f32 pixel_height);
	void       unload_font(FontHandle handle);

	// 2D overlay (drawn after the 3D scene, no depth)
	void draw_2d_rect(f32 x, f32 y, f32 w, f32 h, vec4 color);
	void draw_text(FontHandle font, const char* str, f32 x, f32 y, f32 scale, vec4 color);

	// linear multiplier on scene HDR before the AgX tonemap. default 1.0
	void set_exposure(f32 exposure);

	// debug
	void cycle_debug_mode();
	void set_debug_mode(u32 mode);
	u32  debug_mode();

}
