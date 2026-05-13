#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

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

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 normal_matrix;
    vec4 color;
} pc;

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec4 v_color;

void main() {
    gl_Position = g.proj * g.view * pc.model * vec4(in_position, 1.0);
    v_normal = mat3(pc.normal_matrix) * in_normal;
    v_color = pc.color;
}
