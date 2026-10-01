#ifndef LIGHTS_GLSL
#define LIGHTS_GLSL

#include "shared.glsl"

// must match vk::LightGPU (vk_lights.hpp)
struct Light {
    vec4 position_range;     // xyz = world pos, w = falloff range
    vec4 color_source;       // rgb = color * intensity, w = source radius
    vec4 direction_cos_out;  // xyz = spot direction, w = cos(outer half-angle)
    vec4 params;             // x = 1 / (cos_inner - cos_outer), y = shadow slot (-1 = none)
};

layout(set = 0, binding = BIND_LIGHTS, std430) readonly buffer Lights {
    Light lights[];
} light_buf;

// UE4-style windowed inverse-square falloff times spot cone falloff. 0 = doesn't reach.
// to_light = light pos - P. point lights encode a cone that always evaluates to 1.
float light_attenuation(Light l, vec3 to_light, float dist2) {
    float r2   = l.position_range.w * l.position_range.w;
    float win  = clamp(1.0 - dist2 / r2, 0.0, 1.0);
    float att  = (win * win) / max(dist2, 1e-4);

    float cd   = dot(-to_light * inversesqrt(max(dist2, 1e-8)), l.direction_cos_out.xyz);
    float cone = clamp((cd - l.direction_cos_out.w) * l.params.x, 0.0, 1.0);
    return att * cone * cone;
}

#endif
