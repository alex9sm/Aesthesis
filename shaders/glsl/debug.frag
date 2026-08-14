#version 450

#include "include/octahedral.glsl"
#include "include/shadow.glsl"

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
    vec4 misc;
    mat4 cascade_view_proj[3];
    vec4 cascade_splits;
} g;

layout(set = 0, binding = 8) uniform sampler2DArrayShadow t_shadow;

layout(set = 1, binding = 0) uniform sampler2D t_scene;
layout(set = 1, binding = 1) uniform sampler2D t_albedo;
layout(set = 1, binding = 2) uniform sampler2D t_normal;
layout(set = 1, binding = 3) uniform sampler2D t_material;
layout(set = 1, binding = 4) uniform sampler2D t_depth;

layout(push_constant) uniform PC { uint mode; } pc;

// view-space position from the g-buffer depth. the g-buffer was rendered with
// a Y-flipped viewport, so the NDC.y that produced this pixel is 1 - 2*uv.y.
vec3 view_pos_at(vec2 uv, float d) {
    vec4 p = g.inv_proj * vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, d, 1.0);
    return p.xyz / p.w;
}

const vec3 SKY = vec3(0.05, 0.07, 0.10);

// DEBUG_FINAL=0, DEBUG_ALBEDO=1, DEBUG_NORMAL=2, DEBUG_MATERIAL=3,
// DEBUG_DEPTH=4, DEBUG_CASCADES=5, DEBUG_SHADOW=6
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
    } else if (pc.mode == 4u) {
        // depth linearized via inv_proj, normalized against the camera's [near, far].
        float linear_z = -view_pos_at(v_uv, texture(t_depth, v_uv).r).z;
        float z_near = g.cam_pos.w;
        float z_far  = g.sun_dir.w;
        float range  = max(z_far - z_near, 1e-6);
        col = vec3(clamp((linear_z - z_near) / range, 0.0, 1.0));
    } else if (pc.mode == 5u) {
        // CSM cascade index. tinting the lit scene rather than filling flat
        // colour keeps silhouettes readable, so you can see where on the
        // geometry a split actually lands.
        float d = texture(t_depth, v_uv).r;
        if (d >= 1.0) { out_color = vec4(SKY, 1.0); return; }

        int cascade = select_cascade(-view_pos_at(v_uv, d).z, g.cascade_splits);
        const vec3 TINT[3] = vec3[3](
            vec3(1.0, 0.2, 0.2), vec3(0.2, 1.0, 0.2), vec3(0.3, 0.4, 1.0));

        float luma = dot(clamp(texture(t_scene, v_uv).rgb, 0.0, 1.0),
                         vec3(0.299, 0.587, 0.114));
        col = TINT[cascade] * (0.25 + 0.75 * luma);
    } else {
        // raw CSM visibility term, exactly as lighting.frag samples it:
        // white = lit, black = occluded.
        float d = texture(t_depth, v_uv).r;
        if (d >= 1.0) { out_color = vec4(SKY, 1.0); return; }

        vec3 vp      = view_pos_at(v_uv, d);
        vec3 P       = (g.inv_view * vec4(vp, 1.0)).xyz;
        vec3 N       = decode_octahedral(texture(t_normal, v_uv).rg);
        int  cascade = select_cascade(-vp.z, g.cascade_splits);

        col = vec3(sample_shadow(t_shadow, g.cascade_view_proj[cascade], cascade, P, N));
    }

    out_color = vec4(col, 1.0);
}
