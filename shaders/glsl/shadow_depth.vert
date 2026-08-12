#version 450

// position-only cascade depth render. mirrors depth_prepass.vert but projects
// with a light-space cascade matrix (selected by push constant) instead of
// the camera's view/proj.
layout(location = 0) in vec3 in_position;

layout(set = 0, binding = 0) uniform Globals {
    mat4 view;
    mat4 proj;
    mat4 inv_view;
    mat4 inv_proj;
    vec4 cam_pos;
    vec4 sun_dir;
    vec4 sun_color;
    vec4 viewport_size;
    vec4 misc;
    mat4 cascade_view_proj[3];
    vec4 cascade_splits;
} g;

struct InstanceData {
    mat4 model;
    mat4 normal_matrix;
    vec4 tint;
    uint material_id;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

layout(set = 0, binding = 1, std430) readonly buffer Instances {
    InstanceData instances[];
} inst;

layout(push_constant) uniform PC {
    uint cascade_index;
} pc;

void main() {
    InstanceData id = inst.instances[gl_InstanceIndex];
    gl_Position = g.cascade_view_proj[pc.cascade_index] * id.model * vec4(in_position, 1.0);
}
