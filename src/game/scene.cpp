#include "scene.hpp"
#include "api.hpp"
#include "log.hpp"

namespace scene {

	static renderer::ModelHandle test_model = renderer::INVALID_MODEL;

	bool init() {
		test_model = renderer::load_model("assets/models/damagedhelmet/DamagedHelmet.gltf");
		if (test_model == renderer::INVALID_MODEL) {
			logger::error("Failed to load test model");
			return false;
		}
		return true;
	}

	void shutdown() {
		if (test_model != renderer::INVALID_MODEL) {
			renderer::unload_model(test_model);
			test_model = renderer::INVALID_MODEL;
		}
	}

	void submit() {
		if (test_model == renderer::INVALID_MODEL) return;
		renderer::submit_model(test_model, mat4_identity());
	}

}
