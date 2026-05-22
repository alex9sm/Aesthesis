// Shared helpers for IBL bakes and IBL evaluation in lighting.frag.
//
// - Hammersley sequence for low-discrepancy 2D samples
// - GGX importance sampling (Epic 2013 split-sum)
// - IBL-flavored Smith / Schlick-GGX geometry term (uses k = a^2 / 2,
//   different from direct lighting's k = (a+1)^2 / 8)
// - Roughness-aware Schlick fresnel (Lagarde) used for IBL diffuse weighting

const float IBL_PI = 3.14159265359;

// Van der Corput radical-inverse, base 2.
float radical_inverse_vdc(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u)  | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u)  | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u)  | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u)  | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint i, uint N) {
    return vec2(float(i) / float(N), radical_inverse_vdc(i));
}

// GGX importance sample: maps uniform Xi in [0,1)^2 to a half-vector H
// distributed according to the GGX NDF for the given roughness, oriented to N.
vec3 importance_sample_ggx(vec2 Xi, vec3 N, float roughness) {
    float a = roughness * roughness;
    float phi       = 2.0 * IBL_PI * Xi.x;
    float cos_theta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
    float sin_theta = sqrt(max(1.0 - cos_theta * cos_theta, 0.0));

    // tangent-space half vector
    vec3 H = vec3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);

    // build a TBN around N
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 T  = normalize(cross(up, N));
    vec3 B  = cross(N, T);

    return normalize(T * H.x + B * H.y + N * H.z);
}

// IBL variant of Smith Schlick-GGX geometry term. Uses k = a^2 / 2 rather than
// direct lighting's k = (a + 1)^2 / 8.
float geometry_schlick_ggx_ibl(float NdotV, float roughness) {
    float a = roughness;
    float k = (a * a) / 2.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometry_smith_ibl(float NdotV, float NdotL, float roughness) {
    return geometry_schlick_ggx_ibl(NdotV, roughness)
         * geometry_schlick_ggx_ibl(NdotL, roughness);
}

// Sébastien Lagarde's roughness-aware Schlick fresnel — used to compute the
// IBL diffuse weighting (kS) so it shrinks at high roughness instead of always
// snapping to F0.
vec3 fresnel_schlick_roughness(float cos_theta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cos_theta, 5.0);
}
