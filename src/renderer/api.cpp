#include "api.hpp"
#include "gltf.hpp"
#include "vk_init.hpp"
#include "vk_frame.hpp"
#include "vk_mesh.hpp"
#include "vk_gbuffer.hpp"
#include "vk_lighting.hpp"
#include "vk_debug.hpp"
#include "vk_globals.hpp"
#include "vk_instance.hpp"
#include "vk_texture.hpp"
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

	static ModelHandle alloc_model_slot() {
		for (u32 i = 0; i < MAX_MODELS; i++) {
			if (!models[i].in_use) return i;
		}
		return INVALID_MODEL;
	}

	// --- init / shutdown ---

	bool init() {
		if (!vk::init()) {
			logger::fatal("Failed to initialize Vulkan");
			return false;
		}
		memory::set(models, 0, sizeof(models));
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
			// extract z_near / z_far from the Vulkan-corrected projection matrix
			// (see mat4_perspective_vk: col[2][2] = -f/(f-n), col[3][2] = -n*f/(f-n))
			f32 A = projection.col[2][2];
			f32 B = projection.col[3][2];
			f32 z_near = (A != 0.0f) ? B / A : 0.1f;
			f32 z_far  = ((1.0f + A) != 0.0f) ? (A * z_near) / (1.0f + A) : 1000.0f;

			// camera world position is the translation column of inv_view; pack z_near in .w
			g.cam_pos = { g.inv_view.col[3][0], g.inv_view.col[3][1], g.inv_view.col[3][2], z_near };
			// direction TO light (light is up and slightly forward/right of origin); pack z_far in .w
			vec3 sd = normalize(vec3{ -0.5f, 1.0f, -0.3f });
			g.sun_dir   = { sd.x, sd.y, sd.z, z_far };
			g.sun_color = { 1.0f, 1.0f, 1.0f, 3.0f };
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

		vk::end_frame();
		frame_active = false;
	}

	void cycle_debug_mode() {
		g_debug_mode = (g_debug_mode + 1) % DEBUG_COUNT;
	}

	void set_debug_mode(u32 mode) {
		if (mode < DEBUG_COUNT) g_debug_mode = mode;
	}

	u32 debug_mode() { return g_debug_mode; }

}
