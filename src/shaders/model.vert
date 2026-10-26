#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 7) in vec3 aTangent;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;
out mat3 TBN;
out float vLocalHeight;

layout (location = 3) in mat4 instanceModel; // Instanced Model Matrix (Locations 3, 4, 5, 6)

uniform mat4 view;
uniform mat4 projection;
uniform vec3 viewPos;
uniform bool isInstanced;
uniform mat4 model;
uniform float time;
uniform float vegetation;
uniform float grassLod;
uniform float windStrength;
uniform float windSpeed;
uniform bool treeWind;

uniform bool deformToTerrain;
uniform sampler2D heightmap;
uniform float worldMinX;
uniform float worldMaxX;
uniform float worldMinZ;
uniform float worldMaxZ;
uniform float minHeight;
uniform float maxHeight;
uniform float baseWorldY;

vec3 safeNormalize(vec3 value, vec3 fallback) {
    float magnitudeSquared = dot(value, value);
    if (magnitudeSquared > 1e-8 && !any(isnan(value)) && !any(isinf(value)))
        return value * inversesqrt(magnitudeSquared);
    return fallback;
}

void main() {
    mat4 finalModel = isInstanced ? instanceModel : model;
    
    vec4 localPos = vec4(aPos, 1.0);
    // Shrink distant grass cards smoothly around their root before rasterizing
    // them. Near-bank meshes stay full size; the far field keeps its color cue
    // while using fewer alpha-tested pixels and less overdraw.
    if (grassLod > 0.5) {
        float plantDistance = length(viewPos.xz - finalModel[3].xz);
        float lodScale = mix(1.0, 0.45, smoothstep(28.0, 76.0, plantDistance));
        localPos.xyz *= lodScale;
    }
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

    // Foliage pivots at its base. Trees use a height-normalized, capped canopy
    // bend driven by a broad gust field; grass keeps the faster local flutter.
    // The two need different displacement scales because tree vertices are much
    // higher above their roots than grass vertices.
    vLocalHeight = max(worldPos.y - finalModel[3].y, 0.0);
    if (vegetation > 0.5) {
        const vec2 windDir = vec2(0.86, 0.51);
        float bend;
        if (treeWind) {
            float phase = dot(finalModel[3].xz, vec2(0.018, 0.011)) +
                          time * 0.65 * windSpeed;
            float gust = sin(phase) + 0.22 * sin(phase * 0.57 + 1.7);
            float canopyWeight = smoothstep(2.0, 22.0, vLocalHeight);
            bend = 0.55 * canopyWeight * canopyWeight * gust * windStrength;
        } else {
            float phase = dot(worldPos.xz, vec2(0.18, 0.13)) +
                          time * 1.9 * windSpeed;
            float gust = sin(phase) + 0.45 * sin(phase * 2.37 + 1.7);
            bend = (0.035 + 0.05 * vLocalHeight) * vLocalHeight * gust *
                   windStrength;
        }
        worldPos.xz += windDir * bend;
        worldPos.y -= abs(bend) * (treeWind ? 0.025 : 0.20);
    }

    FragPos = worldPos;
    
    mat3 normalMatrix = mat3(transpose(inverse(finalModel)));
    // Keep malformed or missing per-vertex attributes from propagating NaNs
    // into the fragment shader's lighting and tangent-space normal map.
    vec3 N = safeNormalize(normalMatrix * aNormal, vec3(0.0, 1.0, 0.0));
    vec3 tangentFallback = abs(N.z) < 0.9
        ? safeNormalize(cross(N, vec3(0.0, 0.0, 1.0)), vec3(1.0, 0.0, 0.0))
        : safeNormalize(cross(N, vec3(1.0, 0.0, 0.0)), vec3(0.0, 1.0, 0.0));
    vec3 T = safeNormalize(normalMatrix * aTangent, tangentFallback);
    // Gram-Schmidt orthogonalization, with a valid tangent for missing or
    // collinear attributes.
    T = safeNormalize(T - dot(T, N) * N, tangentFallback);
    vec3 B = safeNormalize(cross(N, T), vec3(0.0, 0.0, 1.0));
    TBN = mat3(T, B, N);

    Normal = N;
    TexCoords = aTexCoords;
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
