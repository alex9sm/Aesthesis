#version 450
#include "include/globals.glsl"

// position-only prepass. the pipeline binds only the position stream (a tight
// 12 B/vertex buffer, binding 0); the normal/tangent/uv attribute stream is
// never bound here, so no wasted vertex fetch.
layout(location = 0) in vec3 in_position;

// gbuffer's depth EQUAL test requires bit-identical gl_Position math to
// gbuffer.vert, parenthesisation included.
void main() {
    InstanceData id = inst.instances[gl_InstanceIndex];
    gl_Position = g.proj * (g.view * (id.model * vec4(in_position, 1.0)));
}
