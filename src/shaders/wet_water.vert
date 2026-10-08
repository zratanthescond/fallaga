#version 330 core

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec2 aFlowDirection;
layout (location = 2) in vec2 aWorldXZ;
layout (location = 3) in vec2 aPatchCenter;
layout (location = 4) in float aPatchRadius;
layout (location = 5) in float aMaxDepth;
layout (location = 6) in float aFlowSpeed;
layout (location = 7) in float aKind;
layout (location = 8) in float aFlowFade;

uniform mat4 projection;
uniform mat4 view;

out vec3 vWorldPosition;
out vec2 vWorldXZ;
out vec2 vFlowDirection;
out vec2 vPatchCenter;
out float vPatchRadius;
out float vMaxDepth;
out float vFlowSpeed;
out float vKind;
out float vFlowFade;

void main() {
    vWorldPosition = aPosition;
    vWorldXZ = aWorldXZ;
    vFlowDirection = aFlowDirection;
    vPatchCenter = aPatchCenter;
    vPatchRadius = aPatchRadius;
    vMaxDepth = aMaxDepth;
    vFlowSpeed = aFlowSpeed;
    vKind = aKind;
    vFlowFade = aFlowFade;
    gl_Position = projection * view * vec4(aPosition, 1.0);
}
