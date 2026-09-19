#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;

out vec3 WorldPos;
out vec3 Normal;
out vec2 UV;

uniform mat4 model, view, projection;
uniform vec4 clipPlane = vec4(0.0, -1.0, 0.0, 100000.0);

void main() {
    WorldPos = vec3(model * vec4(aPos, 1.0));
    Normal = normalize(mat3(model) * aNormal);
    UV = aUV;
    gl_ClipDistance[0] = dot(vec4(WorldPos, 1.0), clipPlane);
    gl_Position = projection * view * vec4(WorldPos, 1.0);
}
