#include "text.hpp"
#include "shader.hpp"
#include "opengl.hpp"

namespace text {

	namespace {

		struct Vertex {
			f32 x, y;
			f32 u, v;
			f32 r, g, b, a;
		};

		constexpr i32 MAX_CHARS = 2048;
		constexpr i32 MAX_VERTS = MAX_CHARS * 6;

		GLuint vao = 0;
		GLuint vbo = 0;
		shader::Program text_shader;
		GLint u_projection = -1;
		GLint u_texture = -1;

		Vertex vertices[MAX_VERTS];
		i32 vertex_count = 0;

		GLuint bound_texture = 0;

	}

	bool init() {
		text_shader = shader::load("shaders/text.vert", "shaders/text.frag");
		if (!text_shader.id) return false;

		u_projection = shader::get_uniform(text_shader, "u_projection");
		u_texture    = shader::get_uniform(text_shader, "u_texture");

		gl::GenVertexArrays(1, &vao);
		gl::GenBuffers(1, &vbo);
		gl::BindVertexArray(vao);
		gl::BindBuffer(GL_ARRAY_BUFFER, vbo);
		gl::BufferData(GL_ARRAY_BUFFER, sizeof(vertices), nullptr, GL_DYNAMIC_DRAW);

		gl::VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
		gl::EnableVertexAttribArray(0);
		gl::VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(sizeof(f32) * 2));
		gl::EnableVertexAttribArray(1);
		gl::VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(sizeof(f32) * 4));
		gl::EnableVertexAttribArray(2);

		gl::BindVertexArray(0);
		return true;
	}

	void shutdown() {
		shader::destroy(&text_shader);
		if (vbo) { gl::DeleteBuffers(1, &vbo); vbo = 0; }
		if (vao) { gl::DeleteVertexArrays(1, &vao); vao = 0; }
	}

	void begin(mat4 projection) {
		vertex_count = 0;
		bound_texture = 0;
		shader::bind(text_shader);
		shader::set_mat4(u_projection, projection);
		shader::set_i32(u_texture, 0);
	}

	void draw_string(const font::Atlas& atlas, const char* str, f32 x, f32 y, vec4 color, f32 scale, f32 spacing) {
		if (!str) return;

		if (atlas.texture_id != bound_texture) {
			if (vertex_count > 0) flush();
			bound_texture = atlas.texture_id;
		}

		f32 start_x = x;
		f32 cursor_x = x;
		f32 baseline_y = y + atlas.ascent * scale;

		for (const char* p = str; *p; p++) {
			char c = *p;
			if (c == '\n') {
				cursor_x = start_x;
				baseline_y += (atlas.ascent - atlas.descent + atlas.line_gap) * scale;
				continue;
			}

			i32 index = (i32)c - font::FIRST_CHAR;
			if (index < 0 || index >= font::NUM_CHARS) continue;

			const font::GlyphInfo& g = atlas.glyphs[index];

			if (g.u0 == g.u1) {
				cursor_x += g.xadvance * scale + spacing;
				continue;
			}

			if (vertex_count + 6 > MAX_VERTS) flush();

			f32 x0 = cursor_x + g.xoff * scale;
			f32 y0 = baseline_y + g.yoff * scale;
			f32 x1 = cursor_x + g.xoff2 * scale;
			f32 y1 = baseline_y + g.yoff2 * scale;

			Vertex* v = &vertices[vertex_count];
			v[0] = { x0, y0, g.u0, g.v0, color.x, color.y, color.z, color.w };
			v[1] = { x1, y0, g.u1, g.v0, color.x, color.y, color.z, color.w };
			v[2] = { x0, y1, g.u0, g.v1, color.x, color.y, color.z, color.w };
			v[3] = { x1, y0, g.u1, g.v0, color.x, color.y, color.z, color.w };
			v[4] = { x1, y1, g.u1, g.v1, color.x, color.y, color.z, color.w };
			v[5] = { x0, y1, g.u0, g.v1, color.x, color.y, color.z, color.w };

			vertex_count += 6;
			cursor_x += g.xadvance * scale + spacing;
		}
	}

	void flush() {
		if (vertex_count <= 0) return;

		gl::Enable(GL_BLEND);
		gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		gl::ActiveTexture(GL_TEXTURE0);
		gl::BindTexture(GL_TEXTURE_2D, bound_texture);

		gl::BindBuffer(GL_ARRAY_BUFFER, vbo);
		gl::BufferSubData(GL_ARRAY_BUFFER, 0, vertex_count * (GLsizeiptr)sizeof(Vertex), vertices);

		gl::BindVertexArray(vao);
		gl::DrawArrays(GL_TRIANGLES, 0, vertex_count);
		gl::BindVertexArray(0);

		gl::Disable(GL_BLEND);

		vertex_count = 0;
	}

	void reload_shaders() {
		shader::Program new_shader = shader::load("shaders/text.vert", "shaders/text.frag");
		if (new_shader.id) {
			shader::destroy(&text_shader);
			text_shader  = new_shader;
			u_projection = shader::get_uniform(text_shader, "u_projection");
			u_texture    = shader::get_uniform(text_shader, "u_texture");
		}
	}

}
