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
#define BIND_COUNT       9

#define SHARED_MAX_TEXTURES         256
#define SHARED_CASCADE_COUNT        3
#define SHARED_PREFILTER_MIP_COUNT  5

#endif
