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
#include "vk_swapchain.hpp"
#include "log.hpp"

namespace renderer {

	struct PendingDraw {
		MeshHandle mesh;
		mat4 model;
		vec4 color;
	};

	static PendingDraw draw_queue[vk::MAX_DRAWS_PER_FRAME];
	static u32 draw_count = 0;

	static mat4 frame_view = {};
	static mat4 frame_projection = {};
	static bool frame_active = false;

	static u32 g_debug_mode = DEBUG_FINAL;

	bool init() {
		if (!vk::init()) {
			logger::fatal("Failed to initialize Vulkan");
			return false;
		}
		logger::info("Renderer initialized");
		return true;
	}

	void shutdown() {
		vk::shutdown();
		logger::info("Renderer shutdown");
	}

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

	void submit_mesh(MeshHandle mesh, const mat4& model, vec4 color) {
		if (!frame_active) return;
		if (draw_count >= vk::MAX_DRAWS_PER_FRAME) return;
		draw_queue[draw_count++] = { mesh, model, color };
	}

	void end_frame() {
		if (!frame_active) return;

		VkCommandBuffer cmd = vk::current_cmd();
		u32 image_index = vk::current_swapchain_image();

		// write per-draw data into the per-frame instance SSBO, and collect
		// the parallel mesh-handle list for the gbuffer pass.
		vk::reset_instances();
		MeshHandle meshes[vk::MAX_DRAWS_PER_FRAME];
		for (u32 i = 0; i < draw_count; i++) {
			vk::InstanceData id = {};
			id.model = draw_queue[i].model;
			id.normal_matrix = vk::compute_normal_matrix(draw_queue[i].model);
			id.tint = draw_queue[i].color;
			id.material_id = 0;
			vk::push_instance(id);
			meshes[i] = draw_queue[i].mesh;
		}

		vk::execute_gbuffer_pass(cmd, meshes, draw_count);
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
