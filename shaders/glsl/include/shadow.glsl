// Sun CSM lookup, shared by lighting.frag and the debug views so the two can
// never disagree about which cascade a pixel lands in.
//
// Cascade splits are view-space far distances (Globals.cascade_splits.x/y/z);
// the shadow map is a CASCADE_COUNT-layer D32_SFLOAT array sampled through a
// comparison sampler, so texture() returns the PCF-filtered visibility in [0,1].

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

// Spot shadow atlas: slot i lives in tile (i % COLS, i / COLS). texel_scale is the world
// size of one shadow texel per unit distance from the light, so the normal offset grows
// with distance the way a perspective texel does.
const float SPOT_NORMAL_BIAS_TEXELS = 1.5;

float sample_spot_shadow(sampler2DShadow t_atlas, mat4 view_proj, int slot,
                         vec3 P, vec3 N, float dist, float texel_scale) {
    vec4 clip = view_proj * vec4(P + N * (dist * texel_scale * SPOT_NORMAL_BIAS_TEXELS), 1.0);
    vec3 ndc  = clip.xyz / clip.w;
    // clamp half a texel inside the tile so the PCF footprint never reads a neighbour
    const float half_texel = 0.5 / float(SHARED_SPOT_TILE_SIZE);
    vec2 local = clamp(vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5), half_texel, 1.0 - half_texel);
    const vec2 grid = vec2(SHARED_SPOT_ATLAS_COLS, SHARED_SPOT_SHADOW_SLOTS / SHARED_SPOT_ATLAS_COLS);
    vec2 tile = vec2(slot % SHARED_SPOT_ATLAS_COLS, slot / SHARED_SPOT_ATLAS_COLS);
    return texture(t_atlas, vec3((tile + local) / grid, ndc.z));
}
