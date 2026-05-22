#version 450

// vertex layout: pos (vec2 pixels), uv (vec2), color (vec4)
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

layout(set = 0, binding = 0) uniform Globals {
	mat4 view;
	mat4 proj;
	mat4 inv_view;
	mat4 inv_proj;
	vec4 cam_pos;
	vec4 sun_dir;
	vec4 sun_color;
	vec4 viewport_size;
} g;

void main() {
	vec2 ndc = in_pos * vec2(g.viewport_size.z, g.viewport_size.w) * 2.0 - 1.0;
	gl_Position = vec4(ndc, 0.0, 1.0);
	v_uv    = in_uv;
	v_color = in_color;
}
