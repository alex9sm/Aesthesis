#version 460 core

in vec2 v_uv;

uniform mat4 u_inv_view_proj;
uniform samplerCube u_skybox;

out vec4 frag_color;

void main() {
    vec2 ndc = v_uv * 2.0 - 1.0;
    vec4 clip_near = vec4(ndc, -1.0, 1.0);
    vec4 clip_far  = vec4(ndc,  1.0, 1.0);

    vec4 world_near = u_inv_view_proj * clip_near;
    vec4 world_far  = u_inv_view_proj * clip_far;
    world_near /= world_near.w;
    world_far  /= world_far.w;

    vec3 ray_dir = normalize(world_far.xyz - world_near.xyz);

    // rotate 90 degrees on X axis to match cubemap orientation
    vec3 sample_dir = vec3(ray_dir.x, -ray_dir.z, ray_dir.y);

    frag_color = texture(u_skybox, sample_dir);
}
