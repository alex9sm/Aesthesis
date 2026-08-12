#version 460 core

out vec2 v_uv;

void main() {
    // oversized triangle that covers the full screen
    // vertex 0: (-1, -1), vertex 1: (3, -1), vertex 2: (-1, 3)
    float x = float((gl_VertexID & 1) << 2) - 1.0;
    float y = float((gl_VertexID & 2) << 1) - 1.0;

    v_uv = vec2(x, y) * 0.5 + 0.5;
    gl_Position = vec4(x, y, 1.0, 1.0);
}
