#version 450
#include "include/globals.glsl"

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec4 in_tangent;   // .xyz tangent, .w bitangent sign
layout(location = 3) in vec2 in_uv;

layout(location = 0) out vec2  v_uv;
layout(location = 1) out vec3  v_normal_ws;
layout(location = 2) out vec3  v_tangent_ws;
layout(location = 3) out float v_tangent_sign;
layout(location = 4) out vec4  v_tint;
layout(location = 5) flat out uint v_material_id;

void main() {
    InstanceData id = inst.instances[gl_InstanceIndex];
    gl_Position = g.proj * (g.view * (id.model * vec4(in_position, 1.0)));

    v_uv           = in_uv;
    v_normal_ws    = mat3(id.normal_matrix) * in_normal;
    // tangent is a true tangent vector (contravariant), so use the model matrix
    // directly rather than the inverse-transpose.
    v_tangent_ws   = mat3(id.model) * in_tangent.xyz;
    v_tangent_sign = in_tangent.w;
    v_tint         = id.tint;
    v_material_id  = id.material_id;
}
