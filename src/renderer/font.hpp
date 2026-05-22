#pragma once

#include "types.hpp"

namespace font {

	// printable ASCII range covered by the atlas
	constexpr i32 FIRST_CHAR = 32;
	constexpr i32 NUM_CHARS  = 96;

	struct GlyphInfo {
		f32 u0, v0, u1, v1;     // atlas UVs
		f32 xoff, yoff;         // top-left offset from cursor (pixels, glyph height)
		f32 xoff2, yoff2;       // bottom-right offset from cursor
		f32 xadvance;           // horizontal pen advance after this glyph
	};

	// CPU-side atlas. owns nothing GPU-related — the renderer is responsible
	// for uploading `bitmap` into a texture and releasing it with destroy().
	struct Atlas {
		u8*       bitmap;      // RGBA8, tex_w * tex_h * 4 bytes; SDF replicated in all channels
		i32       tex_w;
		i32       tex_h;
		f32       pixel_height;
		f32       ascent;
		f32       descent;
		f32       line_gap;
		GlyphInfo glyphs[NUM_CHARS];
	};

	// loads a TTF, rasterizes ASCII 32..126 as SDF glyphs into an RGBA8 atlas
	// bitmap, and fills the glyph metrics. returns false on failure.
	bool generate_atlas(const char* path, f32 pixel_height, Atlas* out);

	// frees the CPU bitmap. safe to call on an already-destroyed atlas.
	void destroy(Atlas* atlas);

	f32 string_width(const Atlas& atlas, const char* str, f32 scale);

}
