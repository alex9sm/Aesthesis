#include "draw2d.hpp"
#include "opengl.hpp"
#include "shader.hpp"
#include "log.hpp"

namespace draw2d {

	namespace {

		struct Vertex {
			f32 x, y;
			f32 r, g, b, a;
		};

		constexpr u32 MAX_VERTICES = 65536;

		Vertex line_verts[MAX_VERTICES];
		u32    line_count = 0;

		Vertex tri_verts[MAX_VERTICES];
		u32    tri_count = 0;

		GLuint vao = 0;
		GLuint vbo = 0;

		shader::Program shader_prog;
		GLint u_projection;

		mat4 current_projection;

		static void push_line_vert(f32 x, f32 y, vec4 c) {
			if (line_count >= MAX_VERTICES) return;
			line_verts[line_count++] = { x, y, c.x, c.y, c.z, c.w };
		}

		static void push_tri_vert(f32 x, f32 y, vec4 c) {
			if (tri_count >= MAX_VERTICES) return;
			tri_verts[tri_count++] = { x, y, c.x, c.y, c.z, c.w };
		}

		// ---- cutout text state ----

		struct CutoutVertex {
			f32 x, y;        // screen position
			f32 tu, tv;       // text-local UV
			f32 r, g, b, a;  // color
		};

		constexpr u32 MAX_CUTOUT_VERTS = 4096;
		CutoutVertex cutout_verts[MAX_CUTOUT_VERTS];
		u32          cutout_count = 0;

		GLuint cutout_vao = 0;  // separate VAO for cutout vertex layout

		shader::Program cutout_prog;
		GLint cu_projection   = -1;
		GLint cu_font_atlas   = -1;
		GLint cu_glyph_count  = -1;
		GLint cu_glyph_rect   = -1;
		GLint cu_glyph_uv     = -1;

		mat4   cutout_projection;
		GLuint cutout_atlas_texture = 0;

		vec4 cutout_glyph_rect[4];   // text-local glyph rects
		vec4 cutout_glyph_uv[4];     // atlas UV rects
		i32  cutout_glyph_count = 0;

		static void push_cutout_vert(f32 x, f32 y, f32 tu, f32 tv, vec4 c) {
			if (cutout_count >= MAX_CUTOUT_VERTS) return;
			cutout_verts[cutout_count++] = { x, y, tu, tv, c.x, c.y, c.z, c.w };
		}

	}

	bool init() {
		shader_prog = shader::load("shaders/draw2d.vert", "shaders/draw2d.frag");
		if (!shader_prog.id) return false;
		u_projection = shader::get_uniform(shader_prog, "u_projection");

		cutout_prog = shader::load("shaders/cutout.vert", "shaders/cutout.frag");
		if (cutout_prog.id) {
			cu_projection  = shader::get_uniform(cutout_prog, "u_projection");
			cu_font_atlas  = shader::get_uniform(cutout_prog, "u_font_atlas");
			cu_glyph_count = shader::get_uniform(cutout_prog, "u_glyph_count");
			cu_glyph_rect  = shader::get_uniform(cutout_prog, "u_glyph_rect[0]");
			cu_glyph_uv    = shader::get_uniform(cutout_prog, "u_glyph_uv[0]");
		}

		gl::GenVertexArrays(1, &vao);
		gl::GenBuffers(1, &vbo);
		gl::BindVertexArray(vao);
		gl::BindBuffer(GL_ARRAY_BUFFER, vbo);
		gl::BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(Vertex) * MAX_VERTICES), nullptr, GL_DYNAMIC_DRAW);

		// draw2d layout: pos(2) + color(4)
		gl::VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
		gl::EnableVertexAttribArray(0);
		gl::VertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(sizeof(f32) * 2));
		gl::EnableVertexAttribArray(1);
		gl::BindVertexArray(0);

		// cutout layout: pos(2) + texcoord(2) + color(4)
		gl::GenVertexArrays(1, &cutout_vao);
		gl::BindVertexArray(cutout_vao);
		gl::BindBuffer(GL_ARRAY_BUFFER, vbo);
		gl::VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(CutoutVertex), (void*)0);
		gl::EnableVertexAttribArray(0);
		gl::VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(CutoutVertex), (void*)(sizeof(f32) * 2));
		gl::EnableVertexAttribArray(1);
		gl::VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(CutoutVertex), (void*)(sizeof(f32) * 4));
		gl::EnableVertexAttribArray(2);
		gl::BindVertexArray(0);

		return true;
	}

	void shutdown() {
		shader::destroy(&shader_prog);
		shader::destroy(&cutout_prog);
		if (vbo) { gl::DeleteBuffers(1, &vbo); vbo = 0; }
		if (vao) { gl::DeleteVertexArrays(1, &vao); vao = 0; }
		if (cutout_vao) { gl::DeleteVertexArrays(1, &cutout_vao); cutout_vao = 0; }
	}

	void begin(mat4 projection) {
		current_projection = projection;
		line_count = 0;
		tri_count = 0;
	}

	void line(vec2 a, vec2 b, vec4 color) {
		push_line_vert(a.x, a.y, color);
		push_line_vert(b.x, b.y, color);
	}

	void line(vec2 a, vec2 b, vec4 color_a, vec4 color_b) {
		push_line_vert(a.x, a.y, color_a);
		push_line_vert(b.x, b.y, color_b);
	}

	void circle_outline(vec2 center, f32 radius, vec4 color, i32 segments) {
		for (i32 i = 0; i < segments; i++) {
			f32 a0 = TAU * (f32)i / (f32)segments;
			f32 a1 = TAU * (f32)(i + 1) / (f32)segments;
			vec2 p0 = { center.x + cosf(a0) * radius, center.y + sinf(a0) * radius };
			vec2 p1 = { center.x + cosf(a1) * radius, center.y + sinf(a1) * radius };
			push_line_vert(p0.x, p0.y, color);
			push_line_vert(p1.x, p1.y, color);
		}
	}

	void circle_filled(vec2 center, f32 radius, vec4 color, i32 segments) {
		for (i32 i = 0; i < segments; i++) {
			f32 a0 = TAU * (f32)i / (f32)segments;
			f32 a1 = TAU * (f32)(i + 1) / (f32)segments;
			push_tri_vert(center.x, center.y, color);
			push_tri_vert(center.x + cosf(a0) * radius, center.y + sinf(a0) * radius, color);
			push_tri_vert(center.x + cosf(a1) * radius, center.y + sinf(a1) * radius, color);
		}
	}

	void triangle_filled(vec2 a, vec2 b, vec2 c, vec4 color) {
		push_tri_vert(a.x, a.y, color);
		push_tri_vert(b.x, b.y, color);
		push_tri_vert(c.x, c.y, color);
	}

	void triangle_filled(vec2 a, vec2 b, vec2 c, vec4 color_a, vec4 color_b, vec4 color_c) {
		push_tri_vert(a.x, a.y, color_a);
		push_tri_vert(b.x, b.y, color_b);
		push_tri_vert(c.x, c.y, color_c);
	}

	void rect_filled(vec2 pos, vec2 size, vec4 color) {
		f32 x0 = pos.x, y0 = pos.y;
		f32 x1 = pos.x + size.x, y1 = pos.y + size.y;
		push_tri_vert(x0, y0, color);
		push_tri_vert(x1, y0, color);
		push_tri_vert(x0, y1, color);
		push_tri_vert(x1, y0, color);
		push_tri_vert(x1, y1, color);
		push_tri_vert(x0, y1, color);
	}

	void rect_outline(vec2 pos, vec2 size, vec4 color) {
		f32 x0 = pos.x, y0 = pos.y;
		f32 x1 = pos.x + size.x, y1 = pos.y + size.y;
		push_line_vert(x0, y0, color); push_line_vert(x1, y0, color);
		push_line_vert(x1, y0, color); push_line_vert(x1, y1, color);
		push_line_vert(x1, y1, color); push_line_vert(x0, y1, color);
		push_line_vert(x0, y1, color); push_line_vert(x0, y0, color);
	}

	void line_strip(vec2* positions, vec4* colors, i32 count) {
		for (i32 i = 0; i < count - 1; i++) {
			push_line_vert(positions[i].x, positions[i].y, colors[i]);
			push_line_vert(positions[i + 1].x, positions[i + 1].y, colors[i + 1]);
		}
	}

	void flush() {
		if (tri_count == 0 && line_count == 0) return;

		shader::bind(shader_prog);
		shader::set_mat4(u_projection, current_projection);
		gl::BindVertexArray(vao);
		gl::BindBuffer(GL_ARRAY_BUFFER, vbo);

		if (tri_count > 0) {
			gl::BufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(sizeof(Vertex) * tri_count), tri_verts);
			gl::DrawArrays(GL_TRIANGLES, 0, (GLsizei)tri_count);
		}

		if (line_count > 0) {
			gl::BufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(sizeof(Vertex) * line_count), line_verts);
			gl::DrawArrays(GL_LINES, 0, (GLsizei)line_count);
		}

		gl::BindVertexArray(0);
		shader::unbind();

		tri_count = 0;
		line_count = 0;
	}

	// ---- cutout text ----

	void begin_cutout(mat4 projection, const font::Atlas& atlas) {
		cutout_projection = projection;
		cutout_atlas_texture = atlas.texture_id;
		cutout_count = 0;
		cutout_glyph_count = 0;
	}

	void set_cutout_text(const font::Atlas& atlas, const char* str, f32 scale, f32* out_width, f32* out_height) {
		if (!str) return;
		cutout_glyph_count = 0;

		f32 cursor_x = 0.0f;
		f32 baseline_y = atlas.ascent * scale;

		for (const char* p = str; *p && cutout_glyph_count < 4; p++) {
			char c = *p;
			i32 index = (i32)c - font::FIRST_CHAR;
			if (index < 0 || index >= font::NUM_CHARS) continue;

			const font::GlyphInfo& g = atlas.glyphs[index];
			if (g.u0 == g.u1) {
				cursor_x += g.xadvance * scale;
				continue;
			}

			f32 x0 = cursor_x + g.xoff * scale;
			f32 y0 = baseline_y + g.yoff * scale;
			f32 x1 = cursor_x + g.xoff2 * scale;
			f32 y1 = baseline_y + g.yoff2 * scale;

			cutout_glyph_rect[cutout_glyph_count] = { x0, y0, x1, y1 };
			cutout_glyph_uv[cutout_glyph_count]   = { g.u0, g.v0, g.u1, g.v1 };
			cutout_glyph_count++;

			cursor_x += g.xadvance * scale;
		}

		if (out_width)  *out_width  = cursor_x;
		if (out_height) *out_height = (atlas.ascent - atlas.descent) * scale;
	}

	void triangle_cutout(vec2 a, vec2 b, vec2 c, vec2 uva, vec2 uvb, vec2 uvc, vec4 color) {
		push_cutout_vert(a.x, a.y, uva.x, uva.y, color);
		push_cutout_vert(b.x, b.y, uvb.x, uvb.y, color);
		push_cutout_vert(c.x, c.y, uvc.x, uvc.y, color);
	}

	void flush_cutout() {
		if (cutout_count == 0) return;

		shader::bind(cutout_prog);
		shader::set_mat4(cu_projection, cutout_projection);
		shader::set_i32(cu_font_atlas, 0);
		shader::set_i32(cu_glyph_count, cutout_glyph_count);

		if (cutout_glyph_count > 0) {
			shader::set_vec4_array(cu_glyph_rect, (const f32*)cutout_glyph_rect, cutout_glyph_count);
			shader::set_vec4_array(cu_glyph_uv,   (const f32*)cutout_glyph_uv,   cutout_glyph_count);
		}

		gl::ActiveTexture(GL_TEXTURE0);
		gl::BindTexture(GL_TEXTURE_2D, cutout_atlas_texture);

		gl::BindVertexArray(cutout_vao);
		gl::BindBuffer(GL_ARRAY_BUFFER, vbo);
		gl::BufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(sizeof(CutoutVertex) * cutout_count), cutout_verts);
		gl::DrawArrays(GL_TRIANGLES, 0, (GLsizei)cutout_count);
		gl::BindVertexArray(0);

		shader::unbind();
		cutout_count = 0;
	}

	void reload_shaders() {
		shader::Program new_prog = shader::load("shaders/draw2d.vert", "shaders/draw2d.frag");
		if (new_prog.id) {
			shader::destroy(&shader_prog);
			shader_prog  = new_prog;
			u_projection = shader::get_uniform(shader_prog, "u_projection");
		}
		shader::Program new_cutout = shader::load("shaders/cutout.vert", "shaders/cutout.frag");
		if (new_cutout.id) {
			shader::destroy(&cutout_prog);
			cutout_prog    = new_cutout;
			cu_projection  = shader::get_uniform(cutout_prog, "u_projection");
			cu_font_atlas  = shader::get_uniform(cutout_prog, "u_font_atlas");
			cu_glyph_count = shader::get_uniform(cutout_prog, "u_glyph_count");
			cu_glyph_rect  = shader::get_uniform(cutout_prog, "u_glyph_rect[0]");
			cu_glyph_uv    = shader::get_uniform(cutout_prog, "u_glyph_uv[0]");
		}
	}

}
