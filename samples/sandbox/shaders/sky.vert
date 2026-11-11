#version 450

// Full-screen triangle generated from the vertex index: no vertex buffer.
layout(location = 0) out vec2 outNdc;

void main() {
    vec2 position = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
    outNdc = position;
    gl_Position = vec4(position, 1.0, 1.0);  // on the far plane
}
