#include "assets.hpp"
#include "gltf.hpp"
#include "log.hpp"
#include "string.hpp"

namespace assets {

	static constexpr u32 MAX_SHARED_MODELS = 64;

	struct SharedModel {
		char                  name[128];
		u32                   refs;
		renderer::ModelHandle model;
		renderer::GltfModel   gltf;   // CPU geometry, kept only when the model has physics
	};

	static SharedModel models[MAX_SHARED_MODELS] = {};

	static bool has_physics(const renderer::GltfModel& gm) {
		for (u32 i = 0; i < gm.object_count; i++) {
			if (gm.objects[i].physics.type != renderer::PhysicsType::None) return true;
		}
		return false;
	}

	ModelRef acquire_model(const char* name) {
		ModelRef slot = INVALID_MODEL_REF;
		for (u32 i = 0; i < MAX_SHARED_MODELS; i++) {
			if (models[i].refs > 0 && str::equal(models[i].name, name)) {
				models[i].refs++;
				return i;
			}
			if (models[i].refs == 0 && slot == INVALID_MODEL_REF) slot = i;
		}
		if (slot == INVALID_MODEL_REF) {
			logger::error("assets: out of shared model slots (%u)", MAX_SHARED_MODELS);
			return INVALID_MODEL_REF;
		}

		const char* leaf = name;
		for (const char* p = name; *p; p++) if (*p == '/') leaf = p + 1;
		char path[512];
		str::format(path, sizeof(path), "assets/shared/%s/%s.gltf", name, leaf);

		SharedModel& m = models[slot];
		if (!renderer::load_gltf_model(path, &m.gltf)) {
			logger::error("assets: no shared model '%s' (%s)", name, path);
			return INVALID_MODEL_REF;
		}
		m.model = renderer::load_model(m.gltf);
		if (!has_physics(m.gltf)) renderer::free_gltf_model(&m.gltf);
		str::copy(m.name, name, sizeof(m.name));
		m.refs = 1;
		return slot;
	}

	void release_model(ModelRef ref) {
		if (ref >= MAX_SHARED_MODELS || models[ref].refs == 0) return;
		SharedModel& m = models[ref];
		if (--m.refs > 0) return;
		renderer::unload_model(m.model);
		renderer::free_gltf_model(&m.gltf);
		m = {};
	}

	renderer::ModelHandle render_model(ModelRef ref) {
		return ref < MAX_SHARED_MODELS ? models[ref].model : renderer::INVALID_MODEL;
	}

	u32 create_bodies(ModelRef ref, const mat4& transform, physics::ObjectBody* out, u32 max) {
		if (ref >= MAX_SHARED_MODELS || models[ref].gltf.object_count == 0) return 0;
		return physics::create_bodies(models[ref].gltf, transform, out, max);
	}

}
