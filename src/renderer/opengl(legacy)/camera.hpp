#pragma once

#include "math.hpp"
#include "types.hpp"

struct Camera {
	vec3 position;
	vec3 target_position;
	f32  zoom;
	f32  target_zoom;   // smooth zoom target — zoom lerps toward this each frame
	f32  pitch;         // angle from horizontal in radians
	f32  yaw;           // horizontal rotation in radians
	f32  min_zoom;
	f32  max_zoom;
	f32  pan_speed;
	i32  prev_mouse_x;
	i32  prev_mouse_y;
	f32  target_pitch;
	f32  target_yaw;
};

Camera camera_create();
void   camera_update(Camera& cam, f32 dt);
mat4   camera_view(const Camera& cam);
vec3   camera_eye(const Camera& cam);
mat4   camera_projection();
