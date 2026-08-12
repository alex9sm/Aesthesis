#version 460 core

in vec2 v_uv;
uniform sampler2D u_scene;
uniform sampler2D u_bloom;
uniform float u_bloom_strength;

out vec4 frag_color;

void main() {
    vec3 scene = texture(u_scene, v_uv).rgb;
    vec3 bloom = texture(u_bloom, v_uv).rgb;
    frag_color = vec4(scene + bloom * u_bloom_strength, 1.0);
}
