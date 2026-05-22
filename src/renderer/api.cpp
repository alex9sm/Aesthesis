#include "api.hpp"
#include "gltf.hpp"
#include "font.hpp"
#include "vk_init.hpp"
#include "vk_frame.hpp"
#include "vk_mesh.hpp"
#include "vk_gbuffer.hpp"
#include "vk_lighting.hpp"
#include "vk_debug.hpp"
#include "vk_draw2d.hpp"
#include "vk_globals.hpp"
#include "vk_instance.hpp"
#include "vk_texture.hpp"
#include "vk_cubemap.hpp"
#include "vk_ibl.hpp"
#include "vk_material.hpp"
#include "vk_swapchain.hpp"
#include "memory.hpp"
#include "log.hpp"

namespace renderer {

	struct PendingDraw {
		MeshHandle     mesh;
		MaterialHandle material;
		mat4           model;
		vec4           tint;
	};

	static PendingDraw draw_queue[vk::MAX_DRAWS_PER_FRAME];
	static u32 draw_count = 0;

	static mat4 frame_view = {};
	static mat4 frame_projection = {};
	static bool frame_active = false;

	static u32 g_debug_mode = DEBUG_FINAL;

	// persistent sun state. defaults provide a sane fallback if the scene
	// never calls set_sun (white light, straight down, low intensity).
	static vec3 g_sun_dir       = { 0.0f, 1.0f, 0.0f };
	static vec3 g_sun_color     = { 1.0f, 1.0f, 1.0f };
	static f32  g_sun_intensity = 1.0f;

	// --- model slot table ---

	static constexpr u32 MAX_MODELS = 64;

	struct ModelNode {
		MeshHandle     mesh;
		MaterialHandle material;
		mat4           local_transform;
	};

	struct ModelInternal {
		ModelNode* nodes;
		u32        node_count;
		bool       in_use;
	};

	static ModelInternal models[MAX_MODELS] = {};

	// --- font slot table ---

	static constexpr u32 MAX_FONTS = 8;

	struct FontInternal {
		bool          in_use;
		TextureHandle atlas_tex;     // bindless texture slot of the SDF atlas
		f32           pixel_height;
		f32           ascent;
		f32           descent;
		f32           line_gap;
		i32           tex_w;
		i32           tex_h;
		font::GlyphInfo glyphs[font::NUM_CHARS];
	};

	static FontInternal fonts[MAX_FONTS] = {};

	static ModelHandle alloc_model_slot() {
		for (u32 i = 0; i < MAX_MODELS; i++) {
			if (!models[i].in_use) return i;
		}
		return INVALID_MODEL;
	}

	static FontHandle alloc_font_slot() {
		for (u32 i = 0; i < MAX_FONTS; i++) {
			if (!fonts[i].in_use) return i;
		}
		return INVALID_FONT;
	}

	// --- init / shutdown ---

	bool init() {
		if (!vk::init()) {
			logger::fatal("Failed to initialize Vulkan");
			return false;
		}
		memory::set(models, 0, sizeof(models));
		memory::set(fonts, 0, sizeof(fonts));
		logger::info("Renderer initialized");
		return true;
	}

	void shutdown() {
		for (u32 i = 0; i < MAX_MODELS; i++) {
			if (models[i].in_use && models[i].nodes) {
				memory::free(models[i].nodes);
			}
		}
		memory::set(models, 0, sizeof(models));

		// fonts release their bindless texture slot only — vk::shutdown will
		// tear down all remaining textures regardless.
		memory::set(fonts, 0, sizeof(fonts));

		vk::shutdown();
		logger::info("Renderer shutdown");
	}

	// --- meshes ---

	MeshHandle load_mesh(const char* path) {
		MeshData data = {};
		if (!load_gltf(path, &data)) return INVALID_MESH;

		MeshHandle handle = vk::create_mesh(data);
		free_mesh_data(&data);
		return handle;
	}

	void unload_mesh(MeshHandle handle) {
		vk::destroy_mesh(handle);
	}

	// --- textures ---

	TextureHandle load_texture(const char* path) {
		return vk::load_texture(path);
	}

	void unload_texture(TextureHandle handle) {
		vk::unload_texture(handle);
	}

	// --- cubemaps ---

	CubemapHandle load_cubemap(const char* name, f32 intensity) {
		return vk::load_cubemap(name, intensity);
	}

	void unload_cubemap(CubemapHandle handle) {
		vk::unload_cubemap(handle);
	}

	void set_environment_cubemap(CubemapHandle handle) {
		vk::set_environment_cubemap(handle);
	}

	void clear_environment_cubemap() {
		vk::set_environment_cubemap(vk::INVALID_CUBEMAP);
	}

	// --- materials ---

	MaterialHandle create_material(const MaterialDesc& desc) {
		vk::MaterialDescGPU g = {};
		g.albedo_idx = (desc.albedo == INVALID_TEXTURE) ? DEFAULT_ALBEDO : desc.albedo;
		g.normal_idx = (desc.normal == INVALID_TEXTURE) ? DEFAULT_NORMAL : desc.normal;
		g.orm_idx    = (desc.orm    == INVALID_TEXTURE) ? DEFAULT_ORM    : desc.orm;
		g.base_color_factor = desc.base_color_factor;
		g.metallic_factor   = desc.metallic_factor;
		g.roughness_factor  = desc.roughness_factor;
		return vk::create_material(g);
	}

	void unload_material(MaterialHandle handle) {
		vk::unload_material(handle);
	}

	MaterialHandle default_material() {
		return DEFAULT_MATERIAL_HANDLE;
	}

	// --- models ---

	ModelHandle load_model(const char* path) {
		GltfModel gm = {};
		if (!load_gltf_model(path, &gm)) {
			return INVALID_MODEL;
		}

		ModelHandle slot = alloc_model_slot();
		if (slot == INVALID_MODEL) {
			logger::error("Out of model slots");
			free_gltf_model(&gm);
			return INVALID_MODEL;
		}

		// 1. textures (in order; index i in gm becomes texture_handles[i])
		TextureHandle* texture_handles = nullptr;
		if (gm.texture_count > 0) {
			texture_handles = (TextureHandle*)memory::malloc(sizeof(TextureHandle) * gm.texture_count);
			for (u32 i = 0; i < gm.texture_count; i++) {
				texture_handles[i] = load_texture(gm.textures[i].path);
				if (texture_handles[i] == INVALID_TEXTURE) {
					// fall back to default albedo; doesn't matter which since the
					// material will quote it via whichever slot it referenced.
					texture_handles[i] = DEFAULT_ALBEDO;
				}
			}
		}

		// 2. materials
		MaterialHandle* material_handles = nullptr;
		if (gm.material_count > 0) {
			material_handles = (MaterialHandle*)memory::malloc(sizeof(MaterialHandle) * gm.material_count);
			for (u32 i = 0; i < gm.material_count; i++) {
				const GltfMaterial& src = gm.materials[i];
				MaterialDesc desc = {};
				desc.albedo = (src.albedo_index == (u32)~0u) ? DEFAULT_ALBEDO : texture_handles[src.albedo_index];
				desc.normal = (src.normal_index == (u32)~0u) ? DEFAULT_NORMAL : texture_handles[src.normal_index];
				desc.orm    = (src.orm_index    == (u32)~0u) ? DEFAULT_ORM    : texture_handles[src.orm_index];
				desc.base_color_factor = src.base_color_factor;
				desc.metallic_factor   = src.metallic_factor;
				desc.roughness_factor  = src.roughness_factor;
				material_handles[i] = create_material(desc);
				if (material_handles[i] == INVALID_MATERIAL) {
					material_handles[i] = DEFAULT_MATERIAL_HANDLE;
				}
			}
		}

		// 3. meshes (per primitive)
		MeshHandle* prim_handles = nullptr;
		if (gm.primitive_count > 0) {
			prim_handles = (MeshHandle*)memory::malloc(sizeof(MeshHandle) * gm.primitive_count);
			for (u32 i = 0; i < gm.primitive_count; i++) {
				if (gm.primitives[i].mesh.vertex_count == 0) {
					prim_handles[i] = INVALID_MESH;
					continue;
				}
				prim_handles[i] = vk::create_mesh(gm.primitives[i].mesh);
			}
		}

		// 4. nodes — collapse to {mesh, material, transform} triples
		ModelInternal& mi = models[slot];
		mi.in_use = true;
		mi.node_count = 0;
		mi.nodes = (gm.node_count > 0)
			? (ModelNode*)memory::malloc(sizeof(ModelNode) * gm.node_count)
			: nullptr;

		for (u32 i = 0; i < gm.node_count; i++) {
			const GltfNode& gn = gm.nodes[i];
			if (gn.primitive_index >= gm.primitive_count) continue;
			MeshHandle mh = prim_handles ? prim_handles[gn.primitive_index] : INVALID_MESH;
			if (mh == INVALID_MESH) continue;

			MaterialHandle matp = DEFAULT_MATERIAL_HANDLE;
			if (gn.material_index != (u32)~0u && material_handles) {
				matp = material_handles[gn.material_index];
			}

			mi.nodes[mi.node_count++] = { mh, matp, gn.world_transform };
		}

		if (texture_handles)  memory::free(texture_handles);
		if (material_handles) memory::free(material_handles);
		if (prim_handles)     memory::free(prim_handles);

		free_gltf_model(&gm);
		return slot;
	}

	void unload_model(ModelHandle handle) {
		if (handle >= MAX_MODELS) return;
		ModelInternal& mi = models[handle];
		if (!mi.in_use) return;
		if (mi.nodes) memory::free(mi.nodes);
		mi.nodes = nullptr;
		mi.node_count = 0;
		mi.in_use = false;
	}

	// --- lighting ---

	void set_sun(vec3 direction, vec3 color, f32 intensity) {
		f32 len_sq = direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;
		g_sun_dir = (len_sq > 0.0f) ? normalize(direction) : vec3{ 0.0f, 1.0f, 0.0f };
		g_sun_color = color;
		g_sun_intensity = intensity;
	}

	// --- frame ---

	void begin_frame(const mat4& view, const mat4& projection) {
		frame_view = view;
		frame_projection = projection;
		draw_count = 0;
		frame_active = vk::begin_frame();

		if (frame_active) {
			vk::Swapchain& sc = vk::swapchain();

			vk::GlobalUBO g = {};
			g.view = view;
			g.proj = projection;
			g.inv_view = mat4_inverse(view);
			g.inv_proj = mat4_inverse(projection);

			f32 z_near, z_far;
			mat4_extract_perspective_vk(projection, &z_near, &z_far);

			// camera world position is the translation column of inv_view; pack z_near in .w
			g.cam_pos   = { g.inv_view.col[3][0], g.inv_view.col[3][1], g.inv_view.col[3][2], z_near };
			g.sun_dir   = { g_sun_dir.x, g_sun_dir.y, g_sun_dir.z, z_far };
			g.sun_color = { g_sun_color.x, g_sun_color.y, g_sun_color.z, g_sun_intensity };

			f32 w = (f32)sc.extent.width;
			f32 h = (f32)sc.extent.height;
			g.viewport_size = { w, h, w > 0 ? 1.0f / w : 0.0f, h > 0 ? 1.0f / h : 0.0f };

			vk::update_globals(g);
		}
	}

	void submit_mesh(MeshHandle mesh, MaterialHandle material,
		const mat4& model, vec4 tint)
	{
		if (!frame_active) return;
		if (draw_count >= vk::MAX_DRAWS_PER_FRAME) return;
		MaterialHandle m = (material == INVALID_MATERIAL) ? DEFAULT_MATERIAL_HANDLE : material;
		draw_queue[draw_count++] = { mesh, m, model, tint };
	}

	void submit_model(ModelHandle model, const mat4& transform, vec4 tint) {
		if (!frame_active) return;
		if (model >= MAX_MODELS) return;
		const ModelInternal& mi = models[model];
		if (!mi.in_use) return;

		for (u32 i = 0; i < mi.node_count; i++) {
			const ModelNode& n = mi.nodes[i];
			submit_mesh(n.mesh, n.material, transform * n.local_transform, tint);
		}
	}

	void end_frame() {
		if (!frame_active) return;

		VkCommandBuffer cmd = vk::current_cmd();
		u32 image_index = vk::current_swapchain_image();

		// --- mesh-first stable sort ---
		//
		// bindless materials make per-instance material switches free, so the
		// optimal grouping is by mesh: collapse runs of identical mesh handles
		// into a single vkCmdDrawIndexed with instanceCount=N. counting sort
		// over the bounded MeshHandle range is O(n + MAX_MESHES) and stable.
		static PendingDraw sorted[vk::MAX_DRAWS_PER_FRAME];
		u32 bucket[vk::MAX_MESHES] = {};

		for (u32 i = 0; i < draw_count; i++) {
			MeshHandle m = draw_queue[i].mesh;
			if (m == INVALID_MESH || m >= vk::MAX_MESHES) continue;
			bucket[m]++;
		}
		// prefix-sum the bucket counts into write offsets.
		u32 running = 0;
		for (u32 m = 0; m < vk::MAX_MESHES; m++) {
			u32 c = bucket[m];
			bucket[m] = running;
			running += c;
		}
		// scatter, preserving original order within each bucket (stable).
		u32 sorted_count = 0;
		for (u32 i = 0; i < draw_count; i++) {
			MeshHandle m = draw_queue[i].mesh;
			if (m == INVALID_MESH || m >= vk::MAX_MESHES) continue;
			sorted[bucket[m]++] = draw_queue[i];
			sorted_count++;
		}

		// --- write SSBO + build batches ---

		vk::reset_instances();
		static vk::DrawBatch batches[vk::MAX_DRAWS_PER_FRAME];
		u32 batch_count = 0;

		u32 run_start = 0;
		while (run_start < sorted_count) {
			MeshHandle run_mesh = sorted[run_start].mesh;
			u32 run_end = run_start + 1;
			while (run_end < sorted_count && sorted[run_end].mesh == run_mesh) {
				run_end++;
			}

			for (u32 i = run_start; i < run_end; i++) {
				vk::InstanceData id = {};
				id.model = sorted[i].model;
				id.normal_matrix = vk::compute_normal_matrix(sorted[i].model);
				id.tint = sorted[i].tint;
				id.material_id = sorted[i].material;
				vk::push_instance(id);
			}

			batches[batch_count++] = { run_mesh, run_start, run_end - run_start };
			run_start = run_end;
		}

		vk::execute_gbuffer_pass(cmd, batches, batch_count);
		vk::execute_lighting_pass(cmd);
		vk::execute_debug_pass(cmd, image_index, g_debug_mode);
		vk::execute_overlay_pass(cmd, image_index);

		vk::end_frame();
		frame_active = false;
	}

	// --- fonts ---

	FontHandle load_font(const char* path, f32 pixel_height) {
		FontHandle slot = alloc_font_slot();
		if (slot == INVALID_FONT) {
			logger::error("Out of font slots");
			return INVALID_FONT;
		}

		font::Atlas atlas = {};
		if (!font::generate_atlas(path, pixel_height, &atlas)) {
			return INVALID_FONT;
		}

		TextureHandle tex = vk::load_texture_pixels(atlas.bitmap, (u32)atlas.tex_w, (u32)atlas.tex_h);
		if (tex == INVALID_TEXTURE) {
			font::destroy(&atlas);
			logger::error("Failed to upload font atlas texture");
			return INVALID_FONT;
		}

		FontInternal& f = fonts[slot];
		f.in_use       = true;
		f.atlas_tex    = tex;
		f.pixel_height = atlas.pixel_height;
		f.ascent       = atlas.ascent;
		f.descent      = atlas.descent;
		f.line_gap     = atlas.line_gap;
		f.tex_w        = atlas.tex_w;
		f.tex_h        = atlas.tex_h;
		for (i32 i = 0; i < font::NUM_CHARS; i++) {
			f.glyphs[i] = atlas.glyphs[i];
		}

		font::destroy(&atlas);
		return slot;
	}

	void unload_font(FontHandle handle) {
		if (handle >= MAX_FONTS) return;
		FontInternal& f = fonts[handle];
		if (!f.in_use) return;
		if (f.atlas_tex != INVALID_TEXTURE) {
			vk::unload_texture(f.atlas_tex);
		}
		memory::set(&f, 0, sizeof(f));
	}

	// --- 2D overlay ---

	void draw_2d_rect(f32 x, f32 y, f32 w, f32 h, vec4 color) {
		if (!frame_active) return;
		vk::draw2d_push_rect(x, y, w, h, color);
	}

	void draw_text(FontHandle font, const char* str, f32 x, f32 y, f32 scale, vec4 color) {
		if (!frame_active) return;
		if (!str) return;
		if (font >= MAX_FONTS) return;
		const FontInternal& f = fonts[font];
		if (!f.in_use) return;

		// baseline sits `ascent` pixels below the requested top-left pen origin
		f32 pen_x = x;
		f32 baseline_y = y + f.ascent * scale;

		for (const char* p = str; *p; p++) {
			i32 idx = (i32)(u8)*p - font::FIRST_CHAR;
			if (idx < 0 || idx >= font::NUM_CHARS) continue;

			const font::GlyphInfo& g = f.glyphs[idx];
			f32 x0 = pen_x      + g.xoff  * scale;
			f32 y0 = baseline_y + g.yoff  * scale;
			f32 x1 = pen_x      + g.xoff2 * scale;
			f32 y1 = baseline_y + g.yoff2 * scale;

			if (g.u1 > g.u0 && g.v1 > g.v0) {
				vk::draw2d_push_text_quad(x0, y0, x1, y1,
					g.u0, g.v0, g.u1, g.v1, color, f.atlas_tex);
			}

			pen_x += g.xadvance * scale;
		}
	}

	void cycle_debug_mode() {
		g_debug_mode = (g_debug_mode + 1) % DEBUG_COUNT;
	}

	void set_debug_mode(u32 mode) {
		if (mode < DEBUG_COUNT) g_debug_mode = mode;
	}

	u32 debug_mode() { return g_debug_mode; }

}
