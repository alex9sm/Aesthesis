#ifndef SHARED_GLSL
#define SHARED_GLSL

// constants shared by C++ (vk_*) and GLSL. plain #defines only.
// sizes are SHARED_-prefixed so they don't clobber the C++ constexprs built from them.

// set 0 binding indices
#define BIND_GLOBALS     0
#define BIND_INSTANCES   1
#define BIND_MATERIALS   2
#define BIND_TEXTURES    3
#define BIND_IRRADIANCE  4
#define BIND_PREFILTER   5
#define BIND_BRDF_LUT    6
#define BIND_LIGHTS      7
#define BIND_SHADOW      8
#define BIND_SPOT_SHADOW 9
#define BIND_COUNT       10

#define SHARED_MAX_TEXTURES         256
#define SHARED_CASCADE_COUNT        3
#define SHARED_PREFILTER_MIP_COUNT  5

// reserved bindless texture slot for the flat default normal
#define SHARED_TEX_DEFAULT_NORMAL   1

// spot shadow atlas: SLOTS tiles of TILE_SIZE^2, COLS per row
#define SHARED_SPOT_SHADOW_SLOTS    8
#define SHARED_SPOT_ATLAS_COLS      4
#define SHARED_SPOT_TILE_SIZE       1024
#define SHARED_SPOT_SHADOW_NEAR     0.05

#endif
