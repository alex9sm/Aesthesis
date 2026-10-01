#ifndef GLOBALS_GLSL
#define GLOBALS_GLSL

#include "shared.glsl"

// must match vk::GlobalUBO (vk_globals.hpp)
layout(set = 0, binding = BIND_GLOBALS) uniform Globals {
    mat4 view;
    mat4 proj;
    mat4 inv_view;
    mat4 inv_proj;
    vec4 cam_pos;        // w = z_near
    vec4 sun_dir;        // w = z_far
    vec4 sun_color;      // w = intensity
    vec4 viewport_size;  // x=width, y=height, z=1/w, w=1/h
    vec4 misc;           // x = point_light_count
    mat4 cascade_view_proj[SHARED_CASCADE_COUNT];
    vec4 cascade_splits; // x/y/z = view-space far distance of cascades 0/1/2
} g;

// must match vk::InstanceData (vk_instance.hpp)
struct InstanceData {
    mat4 model;
    mat4 normal_matrix;
    vec4 tint;
    uint material_id;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

layout(set = 0, binding = BIND_INSTANCES, std430) readonly buffer Instances {
    InstanceData instances[];
} inst;

#endif
