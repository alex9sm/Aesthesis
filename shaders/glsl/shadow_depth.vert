#version 450
#include "include/globals.glsl"

// position-only shadow depth render (sun cascades and spot atlas tiles).
// mirrors depth_prepass.vert but projects with the pushed light-space matrix.
layout(location = 0) in vec3 in_position;

layout(push_constant) uniform PC {
    mat4 view_proj;
} pc;

void main() {
    InstanceData id = inst.instances[gl_InstanceIndex];
    // parenthesised right-to-left: matrix*vector each step. without the parens
    // GLSL's left associativity forces a full mat4*mat4 per vertex.
    gl_Position = pc.view_proj * (id.model * vec4(in_position, 1.0));
}
