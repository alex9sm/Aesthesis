#pragma once

#include "opengl.hpp"

namespace texture {

	struct Texture {
		GLuint id;
		i32    width;
		i32    height;
	};

	Texture load(const char* path, bool nearest = false);
	void    destroy(Texture* tex);

}
