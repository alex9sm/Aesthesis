#version 460 core

// per-instance attributes
layout(location = 0) in vec3  a_position;
layout(location = 1) in float a_size;
layout(location = 2) in float a_alpha;
layout(location = 3) in float a_rotation;
layout(location = 4) in vec2  a_uv_offset;  // spritesheet sub-rect origin
layout(location = 5) in vec2  a_uv_scale;   // spritesheet sub-rect size

uniform mat4 u_view;
uniform mat4 u_proj;
uniform bool u_face_up;  // true = flat on XZ plane, false = camera billboard

out float v_alpha;
out vec2 v_uv;
out vec3 v_world_pos;
out vec2 v_uv_offset;
out vec2 v_uv_scale;

void main() {
	// quad corners from gl_VertexID (triangle strip: 0,1,2,3)
	float x = (gl_VertexID & 1) == 0 ? -1.0 : 1.0;
	float y = (gl_VertexID & 2) == 0 ? -1.0 : 1.0;

	// UVs are unrotated — texture stays fixed
	v_uv = vec2(x, y);

	// rotate the corner offset around the billboard center
	float cs = cos(a_rotation);
	float sn = sin(a_rotation);
	float rx = x * cs - y * sn;
	float ry = x * sn + y * cs;

	vec3 world_pos;
	if (u_face_up) {
		// flat quad on XZ plane — rx maps to X, ry maps to Z
		world_pos = a_position + vec3(rx, 0.0, ry) * a_size * 0.5;
	} else {
		// camera-facing billboard
		vec3 cam_right = vec3(u_view[0][0], u_view[1][0], u_view[2][0]);
		vec3 cam_up    = vec3(u_view[0][1], u_view[1][1], u_view[2][1]);
		world_pos = a_position + (cam_right * rx + cam_up * ry) * a_size * 0.5;
	}

	v_alpha = a_alpha;
	v_world_pos = world_pos;
	v_uv_offset = a_uv_offset;
	v_uv_scale = a_uv_scale;

	gl_Position = u_proj * u_view * vec4(world_pos, 1.0);
}
