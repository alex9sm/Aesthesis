#include "api.hpp"
#include "gltf.hpp"
#include "vk_init.hpp"
#include "vk_frame.hpp"
#include "vk_mesh.hpp"
#include "vk_gbuffer.hpp"
#include "vk_debug_blit.hpp"
#include "log.hpp"

namespace renderer {

	static constexpr u32 MAX_DRAWS_PER_FRAME = 1024;

	static vk::GBufferDraw draw_queue[MAX_DRAWS_PER_FRAME];
	static u32 draw_count = 0;

	static mat4 frame_view = {};
	static mat4 frame_projection = {};
	static bool frame_active = false;

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
	}

	void submit_mesh(MeshHandle mesh, const mat4& model, vec4 color) {
		if (!frame_active) return;
		if (draw_count >= MAX_DRAWS_PER_FRAME) return;
		draw_queue[draw_count++] = { mesh, model, color };
	}

	void end_frame() {
		if (!frame_active) return;

		VkCommandBuffer cmd = vk::current_cmd();
		u32 image_index = vk::current_swapchain_image();

		vk::execute_gbuffer_pass(cmd, frame_view, frame_projection, draw_queue, draw_count);
		vk::execute_debug_blit(cmd, image_index);

		vk::end_frame();
		frame_active = false;
	}

}
