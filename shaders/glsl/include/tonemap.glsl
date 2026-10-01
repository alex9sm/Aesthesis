#ifndef TONEMAP_GLSL
#define TONEMAP_GLSL

// AgX (Sobotka; polynomial fit by Wrensch). returns LINEAR display values in [0,1];
// the sRGB swapchain encodes on store.
// look tunables, applied in AgX's display-encoded space. presets:
//   base   : slope 1,           power 1,    sat 1
//   punchy : slope 1,           power 1.35, sat 1.4
//   golden : slope (1,0.9,0.5), power 0.8,  sat 0.8
const vec3  AGX_SLOPE  = vec3(1.0);
const vec3  AGX_OFFSET = vec3(0.0);
const vec3  AGX_POWER  = vec3(1.0);
const float AGX_SAT    = 1.0;

vec3 agx_contrast(vec3 x) {
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4
         - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

vec3 tonemap_agx(vec3 c) {
    const mat3 AGX_IN = mat3(
        0.842479062253094,  0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772,  0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 AGX_OUT = mat3(
         1.19687900512017,  -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368, 1.15190312990417,   -0.0980434501171241,
        -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float MIN_EV = -12.47393;
    const float MAX_EV = 4.026069;

    c = AGX_IN * max(c, vec3(1e-10));
    c = clamp(log2(c), MIN_EV, MAX_EV);
    c = agx_contrast((c - MIN_EV) / (MAX_EV - MIN_EV));

    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = pow(max(c * AGX_SLOPE + AGX_OFFSET, 0.0), AGX_POWER);
    c = luma + AGX_SAT * (c - luma);

    // AgX output is display-encoded (~2.2 gamma); linearize for the sRGB attachment
    c = AGX_OUT * c;
    return pow(clamp(c, 0.0, 1.0), vec3(2.2));
}

#endif
