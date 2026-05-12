#pragma once

#include "opengl.hpp"

namespace font {

	constexpr i32 FIRST_CHAR = 32;
	constexpr i32 NUM_CHARS  = 96;

	struct GlyphInfo {
		f32 u0, v0, u1, v1;
		f32 xoff, yoff;
		f32 xoff2, yoff2;
		f32 xadvance;
	};

	struct Atlas {
		GLuint     texture_id;
		i32        tex_w;
		i32        tex_h;
		f32        pixel_height;
		f32        ascent;
		f32        descent;
		f32        line_gap;
		GlyphInfo  glyphs[NUM_CHARS];
	};

	Atlas load(const char* path, f32 pixel_height);
	void  destroy(Atlas* atlas);

	f32 string_width(const Atlas& atlas, const char* str, f32 scale = 1.0f, f32 spacing = 0.0f);

}
