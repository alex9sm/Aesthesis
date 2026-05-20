#version 450

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec4 v_tint;
layout(location = 2) flat in uint v_material_id;

layout(location = 0) out vec4 out_albedo;    // RGBA8 UNORM
layout(location = 1) out vec4 out_normal;    // RGBA16F
layout(location = 2) out vec2 out_material;  // RG8 UNORM (metallic, roughness)

struct Material {
    vec4 base_color_factor;
    vec4 mr_factors;  // .x=metallic, .y=roughness, .zw unused
    uint albedo_idx;
    uint normal_idx;
    uint orm_idx;
    uint _pad;
};

layout(set = 0, binding = 2, std430) readonly buffer Materials {
    Material materials[];
} mat;

void main() {
    Material m = mat.materials[v_material_id];

    vec3 albedo = m.base_color_factor.rgb * v_tint.rgb;
    out_albedo = vec4(albedo, m.base_color_factor.a * v_tint.a);

    vec3 n = normalize(v_normal);
    // world-space normal encoded into [0,1]
    out_normal = vec4(n * 0.5 + 0.5, 0.0);
    // material factors only (no texture sampling yet — phase E)
    out_material = vec2(m.mr_factors.x, m.mr_factors.y);
}
