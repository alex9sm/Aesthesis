#include "sky.hpp"
#include "shader.hpp"
#include "opengl.hpp"
#include "log.hpp"
#include "memory.hpp"

// stb_image is already implemented in texture.cpp, just declare what we need
extern "C" {
	unsigned char* stbi_load(const char*, int*, int*, int*, int);
	void stbi_image_free(void*);
}

namespace sky {

	namespace {
		shader::Program program;
		GLuint empty_vao;
		GLuint cubemap_tex;
		GLint u_inv_view_proj;
		GLint u_skybox;
	}

	bool init(const char* cubemap_path) {
		program = shader::load("shaders/sky.vert", "shaders/sky.frag");
		if (!program.id) {
			logger::error("Failed to load sky shaders");
			return false;
		}

		u_inv_view_proj = shader::get_uniform(program, "u_inv_view_proj");
		u_skybox        = shader::get_uniform(program, "u_skybox");

		gl::GenVertexArrays(1, &empty_vao);

		// load 1x6 strip cubemap: faces arranged horizontally as +X -X +Y -Y +Z -Z
		i32 img_w, img_h, channels;
		unsigned char* data = stbi_load(cubemap_path, &img_w, &img_h, &channels, 4);
		if (!data) {
			logger::error("Failed to load cubemap: %s", cubemap_path);
			return false;
		}

		i32 face_size = img_h; // each face is face_size x face_size, strip is 6*face_size wide
		if (img_w != face_size * 6) {
			logger::error("Cubemap strip has wrong dimensions: %dx%d (expected %dx%d)", img_w, img_h, face_size * 6, face_size);
			stbi_image_free(data);
			return false;
		}

		gl::GenTextures(1, &cubemap_tex);
		gl::BindTexture(GL_TEXTURE_CUBE_MAP, cubemap_tex);

		// extract each face from the horizontal strip
		i32 face_bytes = face_size * face_size * 4;
		unsigned char* face_data = (unsigned char*)memory::malloc(face_bytes);

		for (i32 face = 0; face < 6; face++) {
			// copy face pixels row by row from the strip
			for (i32 row = 0; row < face_size; row++) {
				unsigned char* src = data + (row * img_w + face * face_size) * 4;
				unsigned char* dst = face_data + row * face_size * 4;
				memory::copy(dst, src, face_size * 4);
			}
			gl::TexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA8,
			               face_size, face_size, 0, GL_RGBA, GL_UNSIGNED_BYTE, face_data);
		}

		memory::free(face_data);
		stbi_image_free(data);

		gl::TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		gl::TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		gl::TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		gl::TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		gl::TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

		gl::BindTexture(GL_TEXTURE_CUBE_MAP, 0);
		return true;
	}

	void shutdown() {
		shader::destroy(&program);
		if (empty_vao) {
			gl::DeleteVertexArrays(1, &empty_vao);
			empty_vao = 0;
		}
		if (cubemap_tex) {
			gl::DeleteTextures(1, &cubemap_tex);
			cubemap_tex = 0;
		}
	}

	void render(mat4 inv_view_proj) {
		gl::DepthMask(GL_FALSE);
		gl::DepthFunc(GL_LEQUAL);

		shader::bind(program);
		shader::set_mat4(u_inv_view_proj, inv_view_proj);

		gl::ActiveTexture(GL_TEXTURE0);
		gl::BindTexture(GL_TEXTURE_CUBE_MAP, cubemap_tex);
		shader::set_i32(u_skybox, 0);

		gl::BindVertexArray(empty_vao);
		gl::DrawArrays(GL_TRIANGLES, 0, 3);
		gl::BindVertexArray(0);

		gl::BindTexture(GL_TEXTURE_CUBE_MAP, 0);
		shader::unbind();

		gl::DepthFunc(GL_LESS);
		gl::DepthMask(GL_TRUE);
	}

}
