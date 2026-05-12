#pragma once

#include "math.hpp"
#include "types.hpp"
#include "shader.hpp"
#include "opengl.hpp"

namespace shadow {

	constexpr i32 CASCADE_COUNT = 3;
	constexpr i32 MAP_SIZE = 2048;

	bool init();
	void shutdown();

	void update(vec3 sun_dir, vec3 camera_pos, f32 camera_zoom);

	void begin_cascade(i32 index);
	void end_cascade();
	void end(i32 screen_w, i32 screen_h);

	void bind_maps();

	shader::Program get_depth_program();
	GLint get_u_mvp();
	mat4 get_light_vp(i32 cascade);
	f32 get_split(i32 cascade);
	vec3 get_center();

}
