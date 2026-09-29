#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;
layout (location = 3) in float aType;
layout (location = 4) in float aMarkMask;
layout (location = 5) in float aSurfacePriority;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;
out float RoadType;
out float MarkMask;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    TexCoords = aUV;
    RoadType = aType;
    MarkMask = aMarkMask;
    vec4 clipPos = projection * view * vec4(FragPos, 1.0);
    // Ground roads share nearly the same height. Terrain-conform interpolation can
    // leave tiny depth differences between overlapping ribbons, so draw order alone
    // does not reliably select the dominant surface. Bias only clip-space depth (not
    // FragPos/world Y) so the prioritized road owns overlap pixels without a raised
    // asphalt patch or a visible height step.
    float roadPriorityDepth = aSurfacePriority * 0.000001;
    clipPos.z -= 2.0 * roadPriorityDepth * clipPos.w;
    gl_Position = clipPos;
}
