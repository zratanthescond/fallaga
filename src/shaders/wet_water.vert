#version 460 core
// Wadi pools + trickles. One vertex = one point of the water surface (see WetWaterMesh.h, struct Vertex).
layout (location = 0) in vec3  aPos;     // world position, y = water surface
layout (location = 1) in float aBedY;    // terrain height under this vertex
layout (location = 2) in vec2  aFlow;    // flow direction * speed (m/s), 0 = still
layout (location = 3) in float aKind;    // 2 = pool, 3 = trickle
layout (location = 4) in float aLife;    // 1 = full, 0 = water dies out here

uniform mat4  view;
uniform mat4  projection;
uniform float time;

out vec3  vWorld;
out float vBedY;
out vec2  vFlow;
out float vKind;
out float vLife;

void main() {
    vec3 p = aPos;
    // tiny breathing of still water so the surface is never perfectly rigid (a few millimetres)
    if (aKind < 2.5) p.y += 0.004 * sin(time * 1.3 + aPos.x * 0.35 + aPos.z * 0.27);
    vWorld = p;
    vBedY = aBedY;
    vFlow = aFlow;
    vKind = aKind;
    vLife = aLife;
    gl_Position = projection * view * vec4(p, 1.0);
}
