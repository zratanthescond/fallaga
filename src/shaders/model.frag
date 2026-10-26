#version 460 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in mat3 TBN;
in float vLocalHeight;

uniform sampler2D texture_diffuse1;
uniform sampler2D texture_diffuse2;
uniform sampler2D texture_normal1;
uniform float blendWeight;
uniform bool hasTexture;
uniform bool textureIsSRGB;
uniform bool hasTexture2;
uniform bool hasNormalMap;

uniform vec3 lightDir;
uniform vec3 viewPos;
uniform vec3 lightColor;
uniform vec3 ambientColor;
uniform vec3 diffuseColor;
uniform float vegetation;
uniform float alphaCutoff;

vec3 safeNormalize(vec3 value, vec3 fallback) {
    float magnitudeSquared = dot(value, value);
    if (magnitudeSquared > 1e-8 && !any(isnan(value)) && !any(isinf(value)))
        return value * inversesqrt(magnitudeSquared);
    return fallback;
}

void main() {
    // 1. Linearize textures (sRGB to Linear)
    vec4 baseColor;
    if (hasTexture) {
        vec4 col1 = texture(texture_diffuse1, TexCoords);
        if (!textureIsSRGB) col1.rgb = pow(col1.rgb, vec3(2.2));
        if (hasTexture2) {
            vec4 col2 = texture(texture_diffuse2, TexCoords);
            if (!textureIsSRGB) col2.rgb = pow(col2.rgb, vec3(2.2));
            baseColor = mix(col1, col2, blendWeight);
        } else {
            baseColor = col1;
        }
    } else {
        baseColor = vec4(pow(diffuseColor, vec3(2.2)), 1.0);
    }
    if (vegetation > 0.5) {
        // Preserve the glTF MASK cutoff. The C++ loader supplies this per
        // material; forcing a higher minimum removes thin scanned leaves.
        if (baseColor.a < clamp(alphaCutoff, 0.0, 1.0)) discard;

        // Per-plant variation and a soft base occlusion stop the field from
        // reading as one flat green sheet and anchor roots into the ground.
        float variation = fract(sin(dot(FragPos.xz, vec2(12.9898, 78.233)))
                                * 43758.5453);
        // Keep the scanned albedo recognizable; vary it gently instead of
        // washing every species into the same yellow-green tint.
        vec3 tint = mix(vec3(0.93, 0.97, 0.89), vec3(1.04, 1.02, 0.96),
                        variation);
        baseColor.rgb *= tint;
        // Small flowers and ground cover should not stay in the dark root band.
        float baseOcclusion = mix(0.62, 1.0,
                                  smoothstep(0.0, 0.10, vLocalHeight));
        baseColor.rgb *= baseOcclusion;
    } else {
        if (baseColor.a < 0.5) discard;
    }

    // 2. Normal Mapping
    vec3 norm;
    if (hasNormalMap) {
        vec3 tangentNormal = texture(texture_normal1, TexCoords).rgb * 2.0 - 1.0;
        tangentNormal = safeNormalize(tangentNormal, vec3(0.0, 0.0, 1.0));
        norm = safeNormalize(TBN * tangentNormal,
                             safeNormalize(Normal, vec3(0.0, 1.0, 0.0)));
    } else {
        norm = safeNormalize(Normal, vec3(0.0, 1.0, 0.0));
    }

    vec3 V = normalize(viewPos - FragPos);
    vec3 L = normalize(lightDir);

    // Two-sided normal for thin foliage leaves
    if (vegetation > 0.5 && dot(norm, V) < 0.0) {
        norm = -norm;
    }

    float NdV = max(dot(norm, V), 0.0);
    float NdL = max(dot(norm, L), 0.0);
    
    // 3. Hemisphere Ambient (Mediterranean Sky/Ground)
    float dayBoost = smoothstep(-0.1, 0.3, L.y);
    vec3 skyCol = vec3(0.4, 0.6, 1.0) * mix(0.1, 0.6, dayBoost);
    vec3 groundCol = vec3(0.3, 0.2, 0.1) * mix(0.05, 0.3, dayBoost);
    vec3 ambient = mix(groundCol, skyCol, norm.y * 0.5 + 0.5) * baseColor.rgb;
    if (vegetation > 0.5) ambient *= 1.6;

    // 4. Direct Diffuse & Specular
    vec3 directDiffuse;
    vec3 specular = vec3(0.0);

    if (vegetation > 0.5) {
        // Subsurface transmission / wrap lighting for translucent foliage
        float wrapTerm = 0.35;
        float wrapNdL = clamp((dot(norm, L) + wrapTerm) / (1.0 + wrapTerm), 0.0, 1.0);

        // Subsurface back-light transmission (glowing translucent leaves when looking toward the sun)
        float backLight = max(dot(-V, L), 0.0);
        vec3 sss = baseColor.rgb * vec3(1.15, 1.25, 0.55) * pow(backLight, 3.0) * 0.40;
        directDiffuse = (wrapNdL * baseColor.rgb + sss) * (lightColor * 1.25) * dayBoost;

        // Soft waxy leaf sheen instead of harsh stone specular
        vec3 halfwayDir = normalize(L + V);
        float vegSpec = pow(max(dot(norm, halfwayDir), 0.0), 24.0) * 0.035;
        specular = vec3(vegSpec) * lightColor * dayBoost;
    } else {
        // Architecture / Props (Tunisian sunlight)
        directDiffuse = NdL * (lightColor * 1.4) * baseColor.rgb * dayBoost;

        vec3 halfwayDir = normalize(L + V);
        float spec = pow(max(dot(norm, halfwayDir), 0.0), 128.0);
        specular = spec * 0.15 * lightColor * dayBoost;

        // Fresnel Effect (makes stone edges pop)
        float fresnel = pow(1.0 - NdV, 4.0) * 0.1;
        specular += fresnel * skyCol * dayBoost;
    }

    // 7. Combine & Tonemap
    vec3 result = ambient + directDiffuse + specular;
    
    // Simple ACES-lite Tonemapping
    result = (result * (2.51 * result + 0.03)) / (result * (2.43 * result + 0.59) + 0.14);

    // 8. Gamma Correction (Linear to sRGB)
    result = pow(result, vec3(1.0 / 2.2));

    // 9. Forced perspective haze (matches terrain_chunk.frag exactly so buildings,
    //    carriages and terrain dissolve into the same horizon).
    float dist = length(viewPos - FragPos);
    float sunTilt = L.y;
    vec3 dayBottom    = vec3(0.5, 0.8, 1.0);
    vec3 sunsetBottom = vec3(1.0, 0.4, 0.2);
    vec3 nightBottom  = vec3(0.05, 0.1, 0.2);
    vec3 fogCol;
    if (sunTilt > 0.1) {
        fogCol = mix(sunsetBottom, dayBottom, smoothstep(0.1, 0.5, sunTilt));
    } else if (sunTilt > -0.1) {
        fogCol = mix(nightBottom, sunsetBottom, smoothstep(-0.1, 0.1, sunTilt));
    } else {
        fogCol = nightBottom;
    }
    vec3 sunHazeColor = mix(vec3(1.0, 0.65, 0.35), vec3(1.0, 0.95, 0.85), clamp(sunTilt * 2.0, 0.0, 1.0));
    fogCol = mix(fogCol, sunHazeColor, pow(max(dot(V, -L), 0.0), 4.0) * 0.25 * smoothstep(-0.05, 0.2, sunTilt));
    // Haze held off until ~1.2 km and kept faint, matching terrain_chunk.frag.
    float fogAmt = clamp(1.0 - exp(-max(dist - 1200.0, 0.0) * 0.0004), 0.0, 1.0);

    FragColor = vec4(mix(result, fogCol, fogAmt), 1.0);
}
