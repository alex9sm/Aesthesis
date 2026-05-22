#pragma once

#include "types.hpp"
#include "math.hpp"

namespace scene {

	bool init();
	void shutdown();

	// queues all of the scene's meshes onto the renderer for the current frame.
	// `dt` is wall-clock seconds since the previous submit (used for the FPS HUD).
	void submit(f32 dt);

}
