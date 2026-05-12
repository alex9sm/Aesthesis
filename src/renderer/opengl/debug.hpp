#pragma once

#include "math.hpp"

namespace debug {

	enum Mode {
		MODE_NONE,
		MODE_NO_FOW,
		MODE_COUNT
	};

	void init();
	void shutdown();
	void update();   // call every frame — handles input toggles

	Mode active_mode();
	bool is_terrain_lod_active();
	bool is_no_fow_active();

}
