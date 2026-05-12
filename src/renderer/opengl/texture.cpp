#define STB_IMAGE_IMPLEMENTATION
#include "../../dependencies/stb_image.h"

#include "texture.hpp"
#include "log.hpp"

namespace texture {

	Texture load(const char* path, bool nearest) {
		Texture tex = {};

		int w, h, channels;
		stbi_set_flip_vertically_on_load(0);
		unsigned char* data = stbi_load(path, &w, &h, &channels, 4);
		if (!data) {
			logger::error("Failed to load texture: %s", path);
			return tex;
		}

		gl::GenTextures(1, &tex.id);
		gl::BindTexture(GL_TEXTURE_2D, tex.id);
		gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

		if (nearest) {
			gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		} else {
			gl::GenerateMipmap(GL_TEXTURE_2D);
			gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		}
		gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		gl::BindTexture(GL_TEXTURE_2D, 0);

		tex.width = w;
		tex.height = h;

		stbi_image_free(data);
		return tex;
	}

	void destroy(Texture* tex) {
		if (tex->id) {
			gl::DeleteTextures(1, &tex->id);
			tex->id = 0;
		}
		tex->width = 0;
		tex->height = 0;
	}

}
