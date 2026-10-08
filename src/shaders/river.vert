#version 330 core
layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec2 aFlowDirection;
layout (location = 2) in vec2 aUV;
layout (location = 3) in float aFlowSpeed;
layout (location = 4) in float aDepth;

out vec3 FragPos;
out vec2 FlowDirection;
out vec2 RiverUV;
out float FlowSpeed;
out float RiverDepth;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    FragPos = vec3(model * vec4(aPosition, 1.0));
    FlowDirection = normalize(aFlowDirection);
    RiverUV = aUV;
    FlowSpeed = aFlowSpeed;
    RiverDepth = aDepth;
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
