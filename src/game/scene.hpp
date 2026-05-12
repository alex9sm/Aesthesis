#pragma once

#include "types.hpp"
#include "math.hpp"

namespace scene {

	bool init();
	void shutdown();

	// queues all of the scene's meshes onto the renderer for the current frame.
	void submit();

}
