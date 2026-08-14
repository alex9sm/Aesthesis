// Sun CSM lookup, shared by lighting.frag and the debug views so the two can
// never disagree about which cascade a pixel lands in.
//
// Cascade splits are view-space far distances (Globals.cascade_splits.x/y/z);
// the shadow map is a CASCADE_COUNT-layer D32_SFLOAT array sampled through a
// comparison sampler, so texture() returns the PCF-filtered visibility in [0,1].

const int SHADOW_CASCADE_COUNT = 3;

int select_cascade(float view_depth, vec4 splits) {
    return (view_depth < splits.x) ? 0
         : (view_depth < splits.y) ? 1
         : 2;
}

// Normal-offset bias: push the sampled point off the surface along its normal
// before projecting into light space. Format-independent (unlike rasterizer
// depthBias, whose scale depends on the depth format — D32_SFLOAT doesn't
// behave like the fixed-point formats those constants assume) and peter-pans
// less than an equivalent constant-depth bias.
const float SHADOW_NORMAL_BIAS = 0.05;

float sample_shadow(sampler2DArrayShadow t_shadow, mat4 cascade_view_proj,
                    int cascade, vec3 P, vec3 N) {
    vec4 light_clip = cascade_view_proj * vec4(P + N * SHADOW_NORMAL_BIAS, 1.0);
    vec3 light_ndc  = light_clip.xyz / light_clip.w;
    // the shadow map is rendered with a Y-flipped viewport (matching the
    // depth_prepass winding convention), so v is inverted relative to the
    // naive ndc->uv mapping.
    vec2 uv = vec2(light_ndc.x * 0.5 + 0.5, 0.5 - light_ndc.y * 0.5);
    return texture(t_shadow, vec4(uv, float(cascade), light_ndc.z));
}
