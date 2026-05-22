#version 450

#include "include/octahedral.glsl"

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

layout(set = 1, binding = 0) uniform sampler2D t_scene;
layout(set = 1, binding = 1) uniform sampler2D t_albedo;
layout(set = 1, binding = 2) uniform sampler2D t_normal;
layout(set = 1, binding = 3) uniform sampler2D t_material;
layout(set = 1, binding = 4) uniform sampler2D t_depth;

layout(push_constant) uniform PC { uint mode; } pc;

// DEBUG_FINAL=0, DEBUG_ALBEDO=1, DEBUG_NORMAL=2, DEBUG_MATERIAL=3, DEBUG_DEPTH=4
void main() {
    vec3 col = vec3(0.0);

    if (pc.mode == 0u) {
        // Linear scene_hdr clipped to display range. The swapchain attachment
        // is VK_FORMAT_B8G8R8A8_SRGB, so hardware applies linear->sRGB encoding
        // on store. No tonemap — values above 1.0 just clip.
        col = clamp(texture(t_scene, v_uv).rgb, 0.0, 1.0);
    } else if (pc.mode == 1u) {
        col = texture(t_albedo, v_uv).rgb;
    } else if (pc.mode == 2u) {
        // normal stored octahedral-encoded in RG16F
        vec2 enc = texture(t_normal, v_uv).rg;
        vec3 n = decode_octahedral(enc);
        col = n * 0.5 + 0.5;
    } else if (pc.mode == 3u) {
        vec2 m = texture(t_material, v_uv).rg;
        col = vec3(m, 0.0);
    } else {
        // depth linearized via inv_proj, normalized against the camera's [near, far].
        // gbuffer used a Y-flipped viewport, so NDC.y = 1 - 2*v_uv.y.
        float d = texture(t_depth, v_uv).r;
        vec4 ndc = vec4(v_uv.x * 2.0 - 1.0, 1.0 - v_uv.y * 2.0, d, 1.0);
        vec4 view_pos = g.inv_proj * ndc;
        view_pos /= view_pos.w;
        float linear_z = -view_pos.z;
        float z_near = g.cam_pos.w;
        float z_far  = g.sun_dir.w;
        float range  = max(z_far - z_near, 1e-6);
        col = vec3(clamp((linear_z - z_near) / range, 0.0, 1.0));
    }

    out_color = vec4(col, 1.0);
}
