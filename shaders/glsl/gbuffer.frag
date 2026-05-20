#version 450
#extension GL_EXT_nonuniform_qualifier : require

#include "include/octahedral.glsl"

layout(location = 0) in vec2  v_uv;
layout(location = 1) in vec3  v_normal_ws;
layout(location = 2) in vec3  v_tangent_ws;
layout(location = 3) in float v_tangent_sign;
layout(location = 4) in vec4  v_tint;
layout(location = 5) flat in uint v_material_id;

layout(location = 0) out vec4 out_albedo;    // RGBA8 UNORM: rgb=albedo, a=AO
layout(location = 1) out vec2 out_normal;    // RG16F: octahedral-encoded world normal
layout(location = 2) out vec2 out_material;  // RG8 UNORM: metallic, roughness

struct Material {
    vec4 base_color_factor;
    vec4 mr_factors;     // .x=metallic, .y=roughness, .zw unused
    uint albedo_idx;
    uint normal_idx;
    uint orm_idx;
    uint _pad;
};

layout(set = 0, binding = 2, std430) readonly buffer Materials {
    Material materials[];
} mat;

layout(set = 0, binding = 3) uniform sampler2D u_textures[256];

void main() {
    Material m = mat.materials[v_material_id];

    // material_id is `flat`, but its value comes from the instance SSBO and may
    // vary across primitives in a batched draw, so the texture index is not
    // dynamically uniform — nonuniformEXT is required.
    vec4 base  = texture(u_textures[nonuniformEXT(m.albedo_idx)], v_uv);
    vec3 orm   = texture(u_textures[nonuniformEXT(m.orm_idx)],    v_uv).rgb;
    vec3 n_tex = texture(u_textures[nonuniformEXT(m.normal_idx)], v_uv).xyz * 2.0 - 1.0;

    vec3 albedo = base.rgb * m.base_color_factor.rgb * v_tint.rgb;
    float ao = orm.r;
    float metallic  = orm.b * m.mr_factors.x;
    float roughness = orm.g * m.mr_factors.y;

    // Gram-Schmidt re-orthogonalize the interpolated tangent to the normal.
    vec3 N = normalize(v_normal_ws);
    vec3 T = normalize(v_tangent_ws - dot(v_tangent_ws, N) * N);
    vec3 B = cross(N, T) * v_tangent_sign;
    vec3 world_n = normalize(mat3(T, B, N) * n_tex);

    out_albedo   = vec4(albedo, ao);
    out_normal   = encode_octahedral(world_n);
    out_material = vec2(metallic, roughness);
}
