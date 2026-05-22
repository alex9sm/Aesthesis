#version 450

#include "include/octahedral.glsl"
#include "include/ibl.glsl"

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

// set 0 bindings 4/5/6 = IBL (irradiance / prefilter / BRDF LUT). Prefilter
// + BRDF LUT remain placeholders here until Phase F4 wires specular IBL.
layout(set = 0, binding = 4) uniform samplerCube t_irradiance;

layout(set = 1, binding = 0) uniform sampler2D t_albedo;
layout(set = 1, binding = 1) uniform sampler2D t_normal;
layout(set = 1, binding = 2) uniform sampler2D t_material;
layout(set = 1, binding = 3) uniform sampler2D t_depth;

const float PI = 3.14159265359;

float ndf_ggx(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float geo_smith(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

vec3 fresnel_schlick(float cos_theta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cos_theta, 5.0);
}

void main() {
    float d = texture(t_depth, v_uv).r;
    if (d >= 1.0) {
        out_color = vec4(0.05, 0.07, 0.10, 1.0);
        return;
    }

    vec4  albedo_ao = texture(t_albedo,   v_uv);
    vec2  enc_n     = texture(t_normal,   v_uv).rg;
    vec2  mr        = texture(t_material, v_uv).rg;

    vec3  albedo    = albedo_ao.rgb;
    float ao        = albedo_ao.a;
    float metallic  = mr.r;
    // floor roughness so the specular lobe stays sane; mirror-perfect dielectrics
    // explode the NDF and produce fireflies otherwise.
    float roughness = max(mr.g, 0.05);

    vec3 N = decode_octahedral(enc_n);

    // reconstruct world-space position from depth.
    // gbuffer is rendered with a Y-flipped viewport, so the NDC.y that produced
    // this pixel is (1 - 2*v_uv.y), not (2*v_uv.y - 1).
    vec4 clip      = vec4(v_uv.x * 2.0 - 1.0, 1.0 - v_uv.y * 2.0, d, 1.0);
    vec4 view_pos  = g.inv_proj * clip;
    view_pos      /= view_pos.w;
    vec3 P         = (g.inv_view * view_pos).xyz;

    vec3 V = normalize(g.cam_pos.xyz - P);
    vec3 L = normalize(g.sun_dir.xyz);
    vec3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    vec3  F0  = mix(vec3(0.04), albedo, metallic);
    vec3  F   = fresnel_schlick(VdotH, F0);
    float NDF = ndf_ggx(NdotH, roughness);
    float G   = geo_smith(NdotV, NdotL, roughness);

    vec3 spec    = (NDF * G * F) / max(4.0 * NdotL * NdotV, 1e-4);
    vec3 kS      = F;
    vec3 kD      = (vec3(1.0) - kS) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    vec3 radiance = g.sun_color.rgb * g.sun_color.w;
    vec3 direct   = (diffuse + spec) * radiance * NdotL;

    // --- diffuse IBL (Phase F3) ---
    // Irradiance is baked as (Σ L)/N from cosine-weighted importance sampling,
    // which absorbs the (1/pi) Lambertian factor; multiplying by albedo is
    // energy-correct without an extra pi divide. Specular IBL still pending
    // until Phase F4 — until then the ambient term is diffuse-only.
    vec3 irradiance = texture(t_irradiance, N).rgb;
    vec3 kS_ibl     = fresnel_schlick_roughness(NdotV, F0, roughness);
    vec3 kD_ibl     = (vec3(1.0) - kS_ibl) * (1.0 - metallic);
    vec3 diffuse_ibl = kD_ibl * irradiance * albedo;

    vec3 ambient = diffuse_ibl * ao;

    out_color = vec4(direct + ambient, 1.0);
}
