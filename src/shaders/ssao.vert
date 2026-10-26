#version 460 core

// Same gl_VertexID fullscreen triangle as composite.vert: one triangle covering [0,1]^2.
out vec2 vUV;

void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
