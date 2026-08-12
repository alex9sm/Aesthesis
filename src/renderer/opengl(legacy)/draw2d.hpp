#pragma once

#include "types.hpp"
#include "math.hpp"
#include "font.hpp"

namespace draw2d {

	bool init();
	void shutdown();

	void begin(mat4 projection);

	void line(vec2 a, vec2 b, vec4 color);
	void line(vec2 a, vec2 b, vec4 color_a, vec4 color_b);

	void circle_outline(vec2 center, f32 radius, vec4 color, i32 segments = 64);
	void circle_filled(vec2 center, f32 radius, vec4 color, i32 segments = 64);

	void triangle_filled(vec2 a, vec2 b, vec2 c, vec4 color);
	void triangle_filled(vec2 a, vec2 b, vec2 c, vec4 color_a, vec4 color_b, vec4 color_c);

	void rect_filled(vec2 pos, vec2 size, vec4 color);
	void rect_outline(vec2 pos, vec2 size, vec4 color);

	void line_strip(vec2* positions, vec4* colors, i32 count);

	void flush();

	// cutout text — draws triangles with SDF font glyphs punched out as transparent
	// text is oriented by per-vertex UVs so it follows perspective distortion
	void begin_cutout(mat4 projection, const font::Atlas& atlas);
	void set_cutout_text(const font::Atlas& atlas, const char* str, f32 scale, f32* out_width, f32* out_height);
	void triangle_cutout(vec2 a, vec2 b, vec2 c, vec2 uva, vec2 uvb, vec2 uvc, vec4 color);
	void flush_cutout();

	void reload_shaders();

}
