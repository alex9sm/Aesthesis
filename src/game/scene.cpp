#include "scene.hpp"
#include "api.hpp"
#include "log.hpp"

namespace scene {

	static renderer::ModelHandle   test_model     = renderer::INVALID_MODEL;
	static renderer::CubemapHandle env_cubemap    = renderer::INVALID_CUBEMAP;

	// stress grid: 20x10 = 200 instances of the same model.
	// designed to validate phase D batching — all 200 should collapse into
	// one vkCmdDrawIndexed call per node in the model.
	static constexpr u32 GRID_COLS = 20;
	static constexpr u32 GRID_ROWS = 10;
	static constexpr f32 GRID_SPACING = 3.0f;

	bool init() {
		test_model = renderer::load_model("assets/models/damagedhelmet/DamagedHelmet.gltf");
		if (test_model == renderer::INVALID_MODEL) {
			logger::error("Failed to load test model");
			return false;
		}

		// directional sun: up and slightly forward/right of the origin.
		renderer::set_sun({ -0.5f, 1.0f, -0.3f }, { 1.0f, 1.0f, 1.0f }, 0.0f);

		// IBL environment: drives diffuse (and later specular) ambient.
		// load_cubemap reads assets/textures/global/sky/{px,nx,py,ny,pz,nz}.png
		// and bakes irradiance at load time; set_environment_cubemap is then a
		// pure descriptor swap.
		env_cubemap = renderer::load_cubemap("sky");
		if (env_cubemap != renderer::INVALID_CUBEMAP) {
			renderer::set_environment_cubemap(env_cubemap);
		}
		return true;
	}

	void shutdown() {
		if (env_cubemap != renderer::INVALID_CUBEMAP) {
			renderer::clear_environment_cubemap();
			renderer::unload_cubemap(env_cubemap);
		}
		renderer::unload_model(test_model);
	}

	void submit() {
		if (test_model == renderer::INVALID_MODEL) return;

		const f32 x_origin = -((f32)(GRID_COLS - 1) * 0.5f) * GRID_SPACING;
		const f32 z_origin = -((f32)(GRID_ROWS - 1) * 0.5f) * GRID_SPACING;

		for (u32 r = 0; r < GRID_ROWS; r++) {
			for (u32 c = 0; c < GRID_COLS; c++) {
				vec3 pos = {
					x_origin + (f32)c * GRID_SPACING,
					0.0f,
					z_origin + (f32)r * GRID_SPACING,
				};
				renderer::submit_model(test_model, mat4_translate(pos));
			}
		}
	}

}
