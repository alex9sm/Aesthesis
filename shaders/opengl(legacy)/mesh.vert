#version 460 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_texcoord;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;
uniform mat4 u_shadow_vp[3];

out vec3 v_normal;
out vec3 v_world_pos;
out vec2 v_texcoord;
out vec4 v_shadow_coord[3];

void main() {
    vec4 world_pos = u_model * vec4(a_position, 1.0);
    v_world_pos = world_pos.xyz;
    v_normal = mat3(u_model) * a_normal;
    v_texcoord = a_texcoord;

    for (int i = 0; i < 3; i++) {
        v_shadow_coord[i] = u_shadow_vp[i] * world_pos;
    }

    gl_Position = u_projection * u_view * world_pos;
}
