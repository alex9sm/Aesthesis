#version 450

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec4 v_color;

layout(location = 0) out vec4 out_albedo;    // RGBA8 UNORM
layout(location = 1) out vec4 out_normal;    // RGBA16F
layout(location = 2) out vec2 out_material;  // RG8 UNORM (metallic, roughness)

void main() {
    out_albedo = v_color;
    vec3 n = normalize(v_normal);
    // world-space normal encoded into [0,1]
    out_normal = vec4(n * 0.5 + 0.5, 0.0);
    // default material: non-metal, mid roughness
    out_material = vec2(0.0, 0.5);
}
