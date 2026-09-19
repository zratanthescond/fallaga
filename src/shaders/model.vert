#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 7) in vec3 aTangent;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;
out mat3 TBN;

layout (location = 3) in mat4 instanceModel; // Instanced Model Matrix (Locations 3, 4, 5, 6)

uniform mat4 view;
uniform mat4 projection;
uniform bool isInstanced;
uniform mat4 model;

uniform bool deformToTerrain;
uniform sampler2D heightmap;
uniform float worldMinX;
uniform float worldMaxX;
uniform float worldMinZ;
uniform float worldMaxZ;
uniform float minHeight;
uniform float maxHeight;
uniform float baseWorldY;

void main() {
    mat4 finalModel = isInstanced ? instanceModel : model;
    
    vec4 localPos = vec4(aPos, 1.0);
    vec3 worldPos = vec3(finalModel * localPos);
    
    if (deformToTerrain) {
        vec2 uv;
        uv.x = (worldPos.x - worldMinX) / (worldMaxX - worldMinX);
        uv.y = (worldPos.z - worldMinZ) / (worldMaxZ - worldMinZ);
        uv = clamp(uv, 0.0, 1.0);
        
        float hSample = texture(heightmap, uv).r;
        float actualHeight = minHeight + hSample * (maxHeight - minHeight);
        
        // Anchor the wall base directly to the ground height, preserving its vertical height above the base
        worldPos.y = (worldPos.y - baseWorldY) + actualHeight;
    }

    FragPos = worldPos;
    
    mat3 normalMatrix = mat3(transpose(inverse(finalModel)));
    vec3 N = normalize(normalMatrix * aNormal);
    vec3 T = normalize(normalMatrix * aTangent);
    
    // Gram-Schmidt orthogonalization with collinearity check
    if (abs(dot(T, N)) > 0.99) {
        T = normalize(cross(N, vec3(0.0, 0.0, 1.0)));
        if (length(T) < 0.01) T = normalize(cross(N, vec3(1.0, 0.0, 0.0)));
    }
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T);
    TBN = mat3(T, B, N);

    Normal = N;
    TexCoords = aTexCoords;
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
