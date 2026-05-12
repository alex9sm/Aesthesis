#pragma once

#include "math.hpp"
#include "mesh.hpp"
#include "shader.hpp"
#include "opengl.hpp"

namespace render3d {

	bool init();
	void shutdown();

	void begin(mat4 view, mat4 projection);
	void set_sun_dir(vec3 dir);
	void set_camera_pos(vec3 pos);
	void set_fog(vec3 color, f32 start, f32 end);
	void set_shadow(const mat4* shadow_vps, const f32* shadow_splits, vec3 shadow_center);
	void set_alpha(f32 a);
	void draw_mesh(const mesh::Mesh& m, mat4 model, vec4 color);
	void draw_mesh_textured(const mesh::Mesh& m, mat4 model, GLuint diffuse_tex, GLuint normal_tex = 0);
	void flush();

}
