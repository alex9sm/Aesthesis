#version 450
#include "include/globals.glsl"

#include "include/octahedral.glsl"
#include "include/ibl.glsl"
#include "include/shadow.glsl"
#include "include/lights.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

// IBL: diffuse irradiance, prefiltered specular, BRDF LUT.
layout(set = 0, binding = BIND_IRRADIANCE) uniform samplerCube t_irradiance;
layout(set = 0, binding = BIND_PREFILTER) uniform samplerCube t_prefilter;
layout(set = 0, binding = BIND_BRDF_LUT) uniform sampler2D   t_brdf_lut;

// comparison sampler; texture() returns the PCF-filtered compare result in [0,1].
layout(set = 0, binding = BIND_SHADOW) uniform sampler2DArrayShadow t_shadow;
layout(set = 0, binding = BIND_SPOT_SHADOW) uniform sampler2DShadow t_spot_shadow;

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

    // --- CSM: pick a cascade by view-space depth, sample with hardware PCF ---
    float view_depth = -(g.view * vec4(P, 1.0)).z;
    int   cascade    = select_cascade(view_depth, g.cascade_splits);
    float shadow     = sample_shadow(t_shadow, g.cascade_view_proj[cascade], cascade, P, N);

    vec3 radiance = g.sun_color.rgb * g.sun_color.w;
    vec3 direct   = (diffuse + spec) * radiance * NdotL * shadow;

    // --- point + spot lights ---
    vec3  R_spec = reflect(-V, N);
    float alpha  = roughness * roughness;
    uint num_lights = uint(g.misc.x);
    for (uint i = 0; i < num_lights; i++) {
        Light l      = light_buf.lights[i];
        vec3  Lp     = l.position_range.xyz - P;
        float dist2  = dot(Lp, Lp);
        float att    = light_attenuation(l, Lp, dist2);
        if (att <= 0.0) continue;

        float dist   = sqrt(dist2);
        if (l.params.y >= 0.0) {
            int slot = int(l.params.y);
            att *= sample_spot_shadow(t_spot_shadow, g.spot_shadow_vp[slot], slot, P, N, dist, l.params.z);
            if (att <= 0.0) continue;
        }
        vec3  Ll     = Lp / max(dist, 1e-4);
        float pNdotL = max(dot(N, Ll), 0.0);

        // sphere light (Karis 2013): specular uses the point on the emitter sphere
        // closest to the reflection ray, renormalized for the widened lobe.
        // source radius 0 reduces exactly to a point light.
        float src    = l.color_source.w;
        vec3  to_ray = dot(Lp, R_spec) * R_spec - Lp;
        vec3  Ls     = normalize(Lp + to_ray * clamp(src / max(length(to_ray), 1e-4), 0.0, 1.0));
        float energy = alpha / clamp(alpha + 0.5 * clamp(src / max(dist, 1e-4), 0.0, 1.0), 0.0, 1.0);
        energy      *= energy;

        vec3  Hs     = normalize(V + Ls);
        float sNdotL = max(dot(N, Ls), 0.0);
        float sNdotH = max(dot(N, Hs), 0.0);
        float sVdotH = max(dot(V, Hs), 0.0);

        vec3  pF   = fresnel_schlick(sVdotH, F0);
        float pNDF = ndf_ggx(sNdotH, roughness);
        float pG   = geo_smith(NdotV, sNdotL, roughness);

        vec3 pSpec = (pNDF * pG * pF) / max(4.0 * sNdotL * NdotV, 1e-4) * energy;
        vec3 pkD   = (vec3(1.0) - pF) * (1.0 - metallic);
        vec3 pDiff = pkD * albedo / PI;

        direct += (pDiff * pNdotL + pSpec * sNdotL) * l.color_source.rgb * att;
    }

    // --- IBL ambient (Phase F3 diffuse + Phase F4 specular, split-sum) ---
    // Irradiance is baked as (Σ L)/N from cosine-weighted importance sampling,
    // which absorbs the (1/pi) Lambertian factor; multiplying by albedo is
    // energy-correct without an extra pi divide.
    vec3 irradiance  = texture(t_irradiance, N).rgb;
    vec3 kS_ibl      = fresnel_schlick_roughness(NdotV, F0, roughness);
    vec3 kD_ibl      = (vec3(1.0) - kS_ibl) * (1.0 - metallic);
    vec3 diffuse_ibl = kD_ibl * irradiance * albedo;

    // Prefiltered radiance along the reflection direction; mip selects
    // roughness. MAX_LOD = PREFILTER_MIP_COUNT - 1.
    vec3  R           = reflect(-V, N);
    const float MAX_LOD = float(SHARED_PREFILTER_MIP_COUNT - 1);
    vec3  prefiltered = textureLod(t_prefilter, R, roughness * MAX_LOD).rgb;
    vec2  envBRDF     = texture(t_brdf_lut, vec2(NdotV, roughness)).rg;
    vec3  specular_ibl = prefiltered * (F0 * envBRDF.x + envBRDF.y);

    vec3 ambient = (diffuse_ibl + specular_ibl) * ao;

    out_color = vec4(direct + ambient, 1.0);
}
