#version 330 core
layout (location = 0) in vec2 aPos;
out vec3 viewDir;

uniform mat4 invView;
uniform mat4 invProj;

void main() {
    gl_Position = vec4(aPos, 1.0, 1.0); // Render at far plane (z=1)
    
    // Calculate world-space view direction for each pixel
    vec4 clipPos = vec4(aPos, 1.0, 1.0);
    vec4 viewPos = invProj * clipPos;
    viewPos /= viewPos.w;
    vec3 worldDir = vec3(invView * vec4(viewPos.xyz, 0.0));
    viewDir = normalize(worldDir);
}
