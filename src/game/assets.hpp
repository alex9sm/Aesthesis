#pragma once

#include "types.hpp"
#include "math.hpp"
#include "api.hpp"
#include "physics.hpp"

// shared models: one copy in memory however many levels/placements/game systems use it
namespace assets {

	using ModelRef = u32;
	static constexpr ModelRef INVALID_MODEL_REF = (ModelRef)~0u;

	// name is a path under assets/shared/: "props/crate" -> assets/shared/props/crate/crate.gltf.
	// refcounted: the first acquire loads, the last release unloads. outside begin/end_frame
	ModelRef acquire_model(const char* name);
	void     release_model(ModelRef ref);

	renderer::ModelHandle render_model(ModelRef ref);

	// bodies for one placement, from the physics flags in the model's own file
	u32 create_bodies(ModelRef ref, const mat4& transform, physics::ObjectBody* out, u32 max);

}
