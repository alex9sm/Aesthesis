#version 450
#include "include/globals.glsl"

// position-only cascade depth render. mirrors depth_prepass.vert but projects
// with a light-space cascade matrix (selected by push constant) instead of
// the camera's view/proj.
layout(location = 0) in vec3 in_position;

layout(push_constant) uniform PC {
    uint cascade_index;
} pc;

void main() {
    InstanceData id = inst.instances[gl_InstanceIndex];
    // parenthesised right-to-left: matrix*vector each step. without the parens
    // GLSL's left associativity forces a full mat4*mat4 per vertex.
    gl_Position = g.cascade_view_proj[pc.cascade_index] * (id.model * vec4(in_position, 1.0));
}
