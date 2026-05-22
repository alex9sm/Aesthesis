#version 450

// vertex layout: pos (vec2 pixels), color (vec4)
layout(location = 0) in vec2  in_pos;
layout(location = 1) in vec4  in_color;

layout(location = 0) out vec4 v_color;

layout(set = 0, binding = 0) uniform Globals {
	mat4 view;
	mat4 proj;
	mat4 inv_view;
	mat4 inv_proj;
	vec4 cam_pos;
	vec4 sun_dir;
	vec4 sun_color;
	vec4 viewport_size;  // x=width, y=height, z=1/w, w=1/h
} g;

void main() {
	// pixel-space (0,0 top-left, +y down) -> NDC ([-1,1], +y down to match Vulkan)
	vec2 ndc = in_pos * vec2(g.viewport_size.z, g.viewport_size.w) * 2.0 - 1.0;
	gl_Position = vec4(ndc, 0.0, 1.0);
	v_color = in_color;
}
