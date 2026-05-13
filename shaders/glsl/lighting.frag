#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

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

layout(set = 1, binding = 0) uniform sampler2D t_albedo;
layout(set = 1, binding = 1) uniform sampler2D t_normal;
layout(set = 1, binding = 2) uniform sampler2D t_material;
layout(set = 1, binding = 3) uniform sampler2D t_depth;

void main() {
    vec3 albedo = texture(t_albedo, v_uv).rgb;
    vec3 n      = texture(t_normal, v_uv).rgb * 2.0 - 1.0;
    float d     = texture(t_depth, v_uv).r;

    // sky / un-rendered: depth at far plane
    if (d >= 1.0) {
        out_color = vec4(0.05, 0.07, 0.10, 1.0);
        return;
    }

    // metallic/roughness stub (unused this milestone)
    // vec2 mat = texture(t_material, v_uv).rg;

    vec3 L = normalize(g.sun_dir.xyz);
    float ndl = max(dot(normalize(n), L), 0.0);
    vec3 ambient = vec3(0.05);
    vec3 lit = albedo * (ambient + g.sun_color.rgb * g.sun_color.w * ndl);

    out_color = vec4(lit, 1.0);
}
