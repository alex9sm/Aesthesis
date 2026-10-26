#pragma once

#include "types.hpp"
#include "math.hpp"

// first-person arms + weapon: an animated skinned model drawn in view space
namespace viewmodel {

	// a shared model name, e.g. "weapons/sidearm". outside begin_frame/end_frame
	bool load(const char* name);
	void unload();
	void update(f32 dt, bool fire);
	void submit(const mat4& view, f32 fov_y);   // between begin_frame/end_frame

}
