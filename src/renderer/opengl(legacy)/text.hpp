#pragma once

#include "font.hpp"
#include "math.hpp"

namespace text {

	bool init();
	void shutdown();

	void begin(mat4 projection);
	void draw_string(const font::Atlas& atlas, const char* str, f32 x, f32 y, vec4 color, f32 scale = 1.0f, f32 spacing = 0.0f);
	void flush();

	void reload_shaders();

}
