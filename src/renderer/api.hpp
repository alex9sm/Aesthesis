#pragma once

#include "types.hpp"
#include "math.hpp"

namespace renderer {

	bool init();
	void shutdown();
	void begin_frame(const mat4& view, const mat4& projection);
	void end_frame();

}
