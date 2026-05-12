#include "scene.hpp"
#include "api.hpp"
#include "log.hpp"

namespace scene {

	static renderer::MeshHandle test_mesh = renderer::INVALID_MESH;

	bool init() {
		test_mesh = renderer::load_mesh("assets/models/monkey.gltf");
		if (test_mesh == renderer::INVALID_MESH) {
			logger::error("Failed to load test mesh");
			return false;
		}
		return true;
	}

	void shutdown() {
		if (test_mesh != renderer::INVALID_MESH) {
			renderer::unload_mesh(test_mesh);
			test_mesh = renderer::INVALID_MESH;
		}
	}

	void submit() {
		if (test_mesh == renderer::INVALID_MESH) return;
		renderer::submit_mesh(test_mesh, mat4_identity(), { 0.8f, 0.4f, 0.2f, 1.0f });
	}

}
