#pragma once

#include "opengl.hpp"
#include "math.hpp"

namespace shader {

	struct Program {
		GLuint id;
	};

	Program compile(const char* vert_source, const char* frag_source);
	Program load(const char* vert_path, const char* frag_path);
	void    destroy(Program* program);

	void bind(Program program);
	void unbind();

	GLint get_uniform(Program program, const char* name);

	void set_i32(GLint loc, i32 val);
	void set_f32(GLint loc, f32 val);
	void set_vec2(GLint loc, vec2 v);
	void set_vec3(GLint loc, vec3 v);
	void set_vec4(GLint loc, vec4 v);
	void set_mat4(GLint loc, const mat4& m);
	void set_f32_array(GLint loc, const f32* data, i32 count);
	void set_vec3_array(GLint loc, const f32* data, i32 count);
	void set_vec4_array(GLint loc, const f32* data, i32 count);

}
