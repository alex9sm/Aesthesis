#version 450

// position-only prepass. the vertex buffer still streams normal/tangent/uv
// (they're part of the shared Vertex layout) — those attributes are simply
// not declared here, so the GPU fetches them but the shader ignores them.
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
} g;

// must match the gbuffer.vert InstanceData layout exactly — both shaders
// index the same SSBO via gl_InstanceIndex. depth EQUAL test in gbuffer
// requires bit-identical gl_Position math here.
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

void main() {
    InstanceData id = inst.instances[gl_InstanceIndex];
    gl_Position = g.proj * g.view * id.model * vec4(in_position, 1.0);
}
