#pragma once

#include "types.hpp"
#include "math.hpp"

namespace renderer { struct GltfEntity; }

namespace level {

	bool load(const char* path);    // outside begin_frame/end_frame
	void unload();
	void submit(vec3 camera_pos);   // between begin_frame/end_frame

	// feet position and facing yaw (camera convention: 0 looks down -Z)
	bool player_spawn(vec3* feet, f32* yaw);

	// every node tagged `entity` and/or `model`, in file order, valid until unload.
	// read custom properties with renderer::gltf_extras_value(e.extras, key, ...)
	const renderer::GltfEntity* entities(u32* count);

}
