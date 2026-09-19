#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in mat3 TBN;

uniform sampler2D texture_diffuse1;
uniform sampler2D texture_diffuse2;
uniform sampler2D texture_normal1;
uniform float blendWeight;
uniform bool hasTexture;
uniform bool hasTexture2;
uniform bool hasNormalMap;

uniform vec3 lightDir;
uniform vec3 viewPos;
uniform vec3 lightColor;
uniform vec3 ambientColor;
uniform vec3 diffuseColor;

void main() {
    // 1. Linearize textures (sRGB to Linear)
    vec4 baseColor;
    if (hasTexture) {
        vec4 col1 = texture(texture_diffuse1, TexCoords);
        col1.rgb = pow(col1.rgb, vec3(2.2)); // Linearize
        if (hasTexture2) {
            vec4 col2 = texture(texture_diffuse2, TexCoords);
            col2.rgb = pow(col2.rgb, vec3(2.2)); // Linearize
            baseColor = mix(col1, col2, blendWeight);
        } else {
            baseColor = col1;
        }
    } else {
        baseColor = vec4(pow(diffuseColor, vec3(2.2)), 1.0);
    }
    if (baseColor.a < 0.5) discard;

    // 2. Normal Mapping
    vec3 norm;
    if (hasNormalMap) {
        norm = texture(texture_normal1, TexCoords).rgb;
        norm = normalize(norm * 2.0 - 1.0);
        norm = normalize(TBN * norm);
    } else {
        norm = normalize(Normal);
    }

    vec3 V = normalize(viewPos - FragPos);
    vec3 L = normalize(lightDir);
    float NdV = max(dot(norm, V), 0.0);
    float NdL = max(dot(norm, L), 0.0);
    
    // 3. Hemisphere Ambient (Mediterranean Sky/Ground)
    float dayBoost = smoothstep(-0.1, 0.3, L.y);
    vec3 skyCol = vec3(0.4, 0.6, 1.0) * mix(0.1, 0.6, dayBoost);
    vec3 groundCol = vec3(0.3, 0.2, 0.1) * mix(0.05, 0.3, dayBoost);
    vec3 ambient = mix(groundCol, skyCol, norm.y * 0.5 + 0.5) * baseColor.rgb;

    // 4. Direct Diffuse (Sunlight)
    // Sunlight in Tunisia is intense (1.4x boost)
    vec3 directDiffuse = NdL * (lightColor * 1.4) * baseColor.rgb * dayBoost;

    // 5. Specular (Tight sharp highlights for stone)
    vec3 halfwayDir = normalize(L + V);
    float spec = pow(max(dot(norm, halfwayDir), 0.0), 128.0);
    vec3 specular = spec * 0.15 * lightColor * dayBoost;
    
    // 6. Fresnel Effect (Makes edges pop)
    float fresnel = pow(1.0 - NdV, 4.0) * 0.1;
    specular += fresnel * skyCol * dayBoost;

    // 7. Combine & Tonemap
    vec3 result = ambient + directDiffuse + specular;
    
    // Simple ACES-lite Tonemapping
    result = (result * (2.51 * result + 0.03)) / (result * (2.43 * result + 0.59) + 0.14);

    // 8. Gamma Correction (Linear to sRGB)
    result = pow(result, vec3(1.0 / 2.2));

    // 9. Fog (Match terrain)
    float dist = length(viewPos - FragPos);
    float fogAmt = clamp(1.0 - exp(-dist * 0.00015), 0.0, 0.85);
    vec3 fogCol = mix(vec3(0.6, 0.62, 0.65), vec3(0.85, 0.83, 0.78), dayBoost);

    FragColor = vec4(mix(result, fogCol, fogAmt), 1.0);
}
