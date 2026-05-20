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

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec4 v_tint;
layout(location = 2) flat out uint v_material_id;

void main() {
    InstanceData id = inst.instances[gl_InstanceIndex];
    gl_Position = g.proj * g.view * id.model * vec4(in_position, 1.0);
    v_normal = mat3(id.normal_matrix) * in_normal;
    v_tint = id.tint;
    v_material_id = id.material_id;
}
