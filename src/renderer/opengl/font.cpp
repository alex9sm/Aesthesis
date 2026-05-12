#define STB_TRUETYPE_IMPLEMENTATION
#include "../../dependencies/stb_truetype.h"

#include "font.hpp"
#include "memory.hpp"
#include "file.hpp"
#include "log.hpp"

namespace font {

	namespace {
		constexpr i32 SDF_PADDING = 6;
		constexpr unsigned char SDF_ONEDGE = 128;
		constexpr float SDF_PIXEL_DIST_SCALE = 128.0f / (float)SDF_PADDING;
	}

	Atlas load(const char* path, f32 pixel_height) {
		Atlas atlas = {};

		u64 fsize = 0;
		if (!file::get_size(path, &fsize) || fsize == 0) {
			logger::error("Font file not found: %s", path);
			return atlas;
		}

		unsigned char* ttf_data = (unsigned char*)memory::malloc(fsize);
		file::read_file(path, ttf_data, fsize);

		stbtt_fontinfo info;
		if (!stbtt_InitFont(&info, ttf_data, 0)) {
			logger::error("Failed to init font: %s", path);
			memory::free(ttf_data);
			return atlas;
		}

		float scale = stbtt_ScaleForPixelHeight(&info, pixel_height);

		int asc, desc, lg;
		stbtt_GetFontVMetrics(&info, &asc, &desc, &lg);
		atlas.ascent   = (f32)asc * scale;
		atlas.descent  = (f32)desc * scale;
		atlas.line_gap = (f32)lg * scale;
		atlas.pixel_height = pixel_height;

		i32 tex_w = 1024;
		i32 tex_h = 1024;
		unsigned char* bitmap = (unsigned char*)memory::malloc(tex_w * tex_h);
		memory::set(bitmap, 0, tex_w * tex_h);

		i32 pen_x = 0;
		i32 pen_y = 0;
		i32 row_h = 0;

		for (i32 i = 0; i < NUM_CHARS; i++) {
			i32 codepoint = FIRST_CHAR + i;
			i32 gw = 0, gh = 0, gxoff = 0, gyoff = 0;

			unsigned char* sdf = stbtt_GetCodepointSDF(&info, scale, codepoint, SDF_PADDING, SDF_ONEDGE, SDF_PIXEL_DIST_SCALE, &gw, &gh, &gxoff, &gyoff);

			int advance_w, lsb;
			stbtt_GetCodepointHMetrics(&info, codepoint, &advance_w, &lsb);

			atlas.glyphs[i].xadvance = (f32)advance_w * scale;

			if (!sdf || gw == 0 || gh == 0) {
				atlas.glyphs[i].u0 = 0; atlas.glyphs[i].v0 = 0;
				atlas.glyphs[i].u1 = 0; atlas.glyphs[i].v1 = 0;
				atlas.glyphs[i].xoff = 0; atlas.glyphs[i].yoff = 0;
				atlas.glyphs[i].xoff2 = 0; atlas.glyphs[i].yoff2 = 0;
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
				memory::copy(bitmap + (pen_y + row) * tex_w + pen_x, sdf + row * gw, gw);
			}

			atlas.glyphs[i].u0 = (f32)pen_x / (f32)tex_w;
			atlas.glyphs[i].v0 = (f32)pen_y / (f32)tex_h;
			atlas.glyphs[i].u1 = (f32)(pen_x + gw) / (f32)tex_w;
			atlas.glyphs[i].v1 = (f32)(pen_y + gh) / (f32)tex_h;
			atlas.glyphs[i].xoff  = (f32)gxoff;
			atlas.glyphs[i].yoff  = (f32)gyoff;
			atlas.glyphs[i].xoff2 = (f32)(gxoff + gw);
			atlas.glyphs[i].yoff2 = (f32)(gyoff + gh);

			pen_x += gw + 1;
			if (gh > row_h) row_h = gh;

			stbtt_FreeSDF(sdf, nullptr);
		}

		GLuint tex = 0;
		gl::GenTextures(1, &tex);
		gl::BindTexture(GL_TEXTURE_2D, tex);
		gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
		gl::TexImage2D(GL_TEXTURE_2D, 0, GL_R8, tex_w, tex_h, 0, GL_RED, GL_UNSIGNED_BYTE, bitmap);
		gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		gl::BindTexture(GL_TEXTURE_2D, 0);

		memory::free(bitmap);
		memory::free(ttf_data);

		atlas.texture_id = tex;
		atlas.tex_w = tex_w;
		atlas.tex_h = tex_h;

		return atlas;
	}

	void destroy(Atlas* atlas) {
		if (atlas->texture_id) {
			gl::DeleteTextures(1, &atlas->texture_id);
			atlas->texture_id = 0;
		}
	}

	f32 string_width(const Atlas& atlas, const char* str, f32 scale, f32 spacing) {
		if (!str) return 0.0f;
		f32 x = 0.0f;
		for (const char* p = str; *p; p++) {
			i32 index = (i32)*p - FIRST_CHAR;
			if (index < 0 || index >= NUM_CHARS) continue;
			x += atlas.glyphs[index].xadvance * scale + spacing;
		}
		return x;
	}

}
