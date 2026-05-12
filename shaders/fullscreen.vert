#version 460 core

out vec2 v_uv;

// Fullscreen triangle — no vertex buffer needed
void main() {
    // gl_VertexID 0,1,2 -> covers full screen
    v_uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(v_uv * 2.0 - 1.0, 0.0, 1.0);
}
