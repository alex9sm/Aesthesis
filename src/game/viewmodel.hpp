#pragma once

#include "types.hpp"
#include "math.hpp"

// first-person arms + weapon: an animated skinned model drawn in view space
namespace viewmodel {

	// a shared model name, e.g. "weapons/sidearm". outside begin_frame/end_frame
	bool load(const char* name);
	void unload();
	struct Input {
		bool fire;      // pressed this frame
		bool reload;    // pressed this frame
		bool toggle;    // pressed this frame: stow, or draw when stowed
		bool walking;   // moving on the ground
	};

	// returns true when a shot fired this frame
	bool update(f32 dt, const Input& in);
	void submit(const mat4& view, f32 fov_y);   // between begin_frame/end_frame

}
