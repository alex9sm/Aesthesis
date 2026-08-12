#version 460 core

in vec2 v_texcoord;
in vec4 v_color;

uniform sampler2D u_texture;

out vec4 frag_color;

void main() {
    float d = texture(u_texture, v_texcoord).r;
    float alpha = smoothstep(0.45, 0.55, d);
    frag_color = vec4(v_color.rgb, v_color.a * alpha);
}
