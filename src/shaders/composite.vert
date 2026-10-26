#version 460 core

// Fullscreen triangle generated from gl_VertexID -- no vertex buffer or attributes.
// Vertex ids 0,1,2 map to (0,0), (2,0), (0,2) in UV space, covering [0,1]x[0,1] with a
// single triangle that overhangs the screen and is clipped. UV matches NDC (both
// bottom-left origin), so the sampled texture is not flipped.
out vec2 vUV;

void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
