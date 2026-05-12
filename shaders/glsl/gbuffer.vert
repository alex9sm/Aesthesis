#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 color;
} pc;

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec4 v_color;

void main() {
    gl_Position = pc.mvp * vec4(in_position, 1.0);
    v_normal = in_normal;
    v_color = pc.color;
}
