#pragma once

#include "types.hpp"
#include "math.hpp"

namespace level {

	bool load(const char* path);   // outside begin_frame/end_frame
	void unload();
	void submit();                 // between begin_frame/end_frame

	// feet position and facing yaw (camera convention: 0 looks down -Z)
	bool player_spawn(vec3* feet, f32* yaw);

}
