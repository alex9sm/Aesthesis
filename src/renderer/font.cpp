#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "font.hpp"
#include "memory.hpp"
#include "file.hpp"
#include "log.hpp"

namespace font {

	namespace {
		constexpr i32 SDF_PADDING            = 6;
		constexpr unsigned char SDF_ONEDGE   = 128;
		constexpr float SDF_PIXEL_DIST_SCALE = 128.0f / (float)SDF_PADDING;
	}

	bool generate_atlas(const char* path, f32 pixel_height, Atlas* out) {
		*out = {};

		u64 fsize = 0;
		if (!file::get_size(path, &fsize) || fsize == 0) {
			logger::error("Font file not found: %s", path);
			return false;
		}

		unsigned char* ttf_data = (unsigned char*)memory::malloc(fsize);
		file::read_file(path, ttf_data, fsize);

		stbtt_fontinfo info;
		if (!stbtt_InitFont(&info, ttf_data, 0)) {
			logger::error("Failed to init font: %s", path);
			memory::free(ttf_data);
			return false;
		}

		float scale = stbtt_ScaleForPixelHeight(&info, pixel_height);

		int asc, desc, lg;
		stbtt_GetFontVMetrics(&info, &asc, &desc, &lg);
		out->ascent       = (f32)asc * scale;
		out->descent      = (f32)desc * scale;
		out->line_gap     = (f32)lg * scale;
		out->pixel_height = pixel_height;

		const i32 tex_w = 1024;
		const i32 tex_h = 1024;
		u8* gray = (u8*)memory::malloc(tex_w * tex_h);
		memory::set(gray, 0, tex_w * tex_h);

		i32 pen_x = 0;
		i32 pen_y = 0;
		i32 row_h = 0;

		for (i32 i = 0; i < NUM_CHARS; i++) {
			i32 codepoint = FIRST_CHAR + i;
			i32 gw = 0, gh = 0, gxoff = 0, gyoff = 0;
			unsigned char* sdf = stbtt_GetCodepointSDF(&info, scale, codepoint,
				SDF_PADDING, SDF_ONEDGE, SDF_PIXEL_DIST_SCALE,
				&gw, &gh, &gxoff, &gyoff);

			int advance_w, lsb;
			stbtt_GetCodepointHMetrics(&info, codepoint, &advance_w, &lsb);
			out->glyphs[i].xadvance = (f32)advance_w * scale;

			if (!sdf || gw == 0 || gh == 0) {
				out->glyphs[i].u0 = 0; out->glyphs[i].v0 = 0;
				out->glyphs[i].u1 = 0; out->glyphs[i].v1 = 0;
				out->glyphs[i].xoff = 0; out->glyphs[i].yoff = 0;
				out->glyphs[i].xoff2 = 0; out->glyphs[i].yoff2 = 0;
				if (sdf) stbtt_FreeSDF(sdf, nullptr);
				continue;
			}

			if (pen_x + gw > tex_w) {
				pen_x = 0;
				pen_y += row_h + 1;
				row_h = 0;
			}
			if (pen_y + gh > tex_h) {
				stbtt_FreeSDF(sdf, nullptr);
				continue;
			}

			for (i32 row = 0; row < gh; row++) {
				memory::copy(gray + (pen_y + row) * tex_w + pen_x, sdf + row * gw, gw);
			}

			out->glyphs[i].u0 = (f32)pen_x / (f32)tex_w;
			out->glyphs[i].v0 = (f32)pen_y / (f32)tex_h;
			out->glyphs[i].u1 = (f32)(pen_x + gw) / (f32)tex_w;
			out->glyphs[i].v1 = (f32)(pen_y + gh) / (f32)tex_h;
			out->glyphs[i].xoff  = (f32)gxoff;
			out->glyphs[i].yoff  = (f32)gyoff;
			out->glyphs[i].xoff2 = (f32)(gxoff + gw);
			out->glyphs[i].yoff2 = (f32)(gyoff + gh);

			pen_x += gw + 1;
			if (gh > row_h) row_h = gh;

			stbtt_FreeSDF(sdf, nullptr);
		}

		// expand 8-bit SDF -> RGBA8 (replicated in every channel). the shader
		// samples .r and uses it as the SDF distance.
		u8* rgba = (u8*)memory::malloc((usize)tex_w * tex_h * 4);
		for (i32 i = 0; i < tex_w * tex_h; i++) {
			u8 v = gray[i];
			rgba[i * 4 + 0] = v;
			rgba[i * 4 + 1] = v;
			rgba[i * 4 + 2] = v;
			rgba[i * 4 + 3] = v;
		}
		memory::free(gray);
		memory::free(ttf_data);

		out->bitmap = rgba;
		out->tex_w  = tex_w;
		out->tex_h  = tex_h;
		return true;
	}

	void destroy(Atlas* atlas) {
		if (atlas->bitmap) {
			memory::free(atlas->bitmap);
			atlas->bitmap = nullptr;
		}
	}

	f32 string_width(const Atlas& atlas, const char* str, f32 scale) {
		if (!str) return 0.0f;
		f32 x = 0.0f;
		for (const char* p = str; *p; p++) {
			i32 index = (i32)*p - FIRST_CHAR;
			if (index < 0 || index >= NUM_CHARS) continue;
			x += atlas.glyphs[index].xadvance * scale;
		}
		return x;
	}

}
