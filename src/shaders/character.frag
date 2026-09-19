#version 330 core

// ----------------------------------------------------
// Input from Vertex Shader
// ----------------------------------------------------
in vec2 TexCoords;
in vec3 Normal;
in vec3 FragPos;
in float HeightAboveFeet;
in float DistFromCenter;
in vec3 LocalPos;

// ----------------------------------------------------
// Uniforms (from Character::render)
// ----------------------------------------------------
uniform sampler2D ourTexture; 
uniform vec3 lightDir;
uniform vec3 viewPos;
uniform vec3 lightColor;

// ----------------------------------------------------
// Output
// ----------------------------------------------------
out vec4 FragColor;
uniform bool hasTexture;

void main()
{
    vec4 texColor = texture(ourTexture, TexCoords);
    
    // Fallback: If texture is missing/unbound/black (e.g. mainCharacter.fbx without embedded texture)
    bool isMissing = (!hasTexture || texColor.a < 0.1 || dot(texColor.rgb, vec3(1.0)) < 0.02);
    
    vec3 baseColor;
    float roughness = 0.65;
    if (isMissing) {
        // Natural Tunisian Rebel / Desert Adventurer color palette
        vec3 skinTone     = vec3(0.76, 0.58, 0.46); // Warm olive Mediterranean skin
        vec3 skinWarmth   = vec3(0.82, 0.52, 0.42); // Flushed subcutaneous tone
        vec3 shortsTone   = vec3(0.38, 0.35, 0.29); // Rugged desert khaki shorts
        vec3 beltTone     = vec3(0.18, 0.12, 0.08); // Weathered brown leather belt
        vec3 buckleTone   = vec3(0.75, 0.65, 0.35); // Brass buckle
        vec3 sandalTone   = vec3(0.24, 0.17, 0.12); // Leather sandals / wraps
        vec3 hairTone     = vec3(0.12, 0.10, 0.09); // Dark North African hair

        // Micro-fabric / skin pore noise
        float skinPore = sin(FragPos.x * 250.0) * sin(FragPos.z * 250.0) * 0.02;
        float clothWeave = (sin(FragPos.x * 160.0) + cos(FragPos.y * 160.0)) * 0.03;

        // Anatomical zones based on vertical height above ground:
        float h = HeightAboveFeet;
        float d = DistFromCenter;

        // Check if point is on arm/hand (hanging at side)
        bool isArmOrHand = (h < 1.45 && h > 0.65 && d > 0.27);

        if (h < 0.10) {
            // Sandals / Foot wraps
            baseColor = sandalTone + skinPore;
            roughness = 0.85;
        } else if (h >= 0.76 && h <= 1.05 && !isArmOrHand) {
            // Shorts and Belt
            if (h >= 0.99 && h <= 1.05) {
                // Belt band
                if (abs(LocalPos.x) < 0.04 && LocalPos.z > 0.04) {
                    baseColor = buckleTone; // Front brass buckle
                    roughness = 0.30;
                } else {
                    baseColor = beltTone;
                    roughness = 0.60;
                }
            } else {
                // Khaki shorts cloth
                baseColor = shortsTone + clothWeave;
                roughness = 0.80;
            }
        } else if (h > 1.68 && (Normal.y > 0.20 || Normal.z < -0.15)) {
            // Hair on scalp and back of head
            baseColor = hairTone;
            roughness = 0.90;
        } else {
            // Skin (Face, Neck, Torso, Arms, Hands, Legs)
            vec3 sCol = skinTone;

            // Subtle 5 o'clock beard stubble on chin and jaw
            if (h >= 1.52 && h <= 1.63 && Normal.z > 0.15 && abs(LocalPos.x) < 0.10) {
                sCol = mix(sCol, hairTone, 0.40);
            }

            // Flushed warmth on cheeks, nose, and shoulders
            if ((h >= 1.58 && h <= 1.66 && Normal.z > 0.35) || (h >= 1.42 && h <= 1.52 && Normal.y > 0.4)) {
                sCol = mix(sCol, skinWarmth, 0.35);
            }

            baseColor = sCol + skinPore;
            roughness = 0.55;
        }
    } else {
        baseColor = pow(texColor.rgb, vec3(2.2));
    }

    vec3 norm = normalize(Normal);
    vec3 V = normalize(viewPos - FragPos);
    vec3 L = normalize(lightDir);
    float NdV = max(dot(norm, V), 0.0);
    float NdL = max(dot(norm, L), 0.0);

    // 2. Hemisphere Ambient (Mediterranean Sky & Warm Desert Sand Bounce)
    float dayBoost = smoothstep(-0.1, 0.3, L.y);
    vec3 skyCol = vec3(0.50, 0.68, 0.95) * 0.45;
    vec3 groundCol = vec3(0.72, 0.58, 0.44) * 0.42; // Warm desert sand reflection
    vec3 ambient = mix(groundCol, skyCol, norm.y * 0.5 + 0.5) * baseColor.rgb * dayBoost;

    // 3. Direct Diffuse with Half-Lambert wrap lighting (soft subsurface skin falloff)
    float wrapNdL = max(dot(norm, L) * 0.70 + 0.30, 0.0);
    vec3 directDiffuse = wrapNdL * (lightColor * 1.35) * baseColor.rgb * dayBoost;

    // 4. Specular & Subsurface Scattering
    vec3 halfwayDir = normalize(L + V);
    float shininess = mix(64.0, 8.0, roughness);
    float spec = pow(max(dot(norm, halfwayDir), 0.0), shininess);
    vec3 specular = spec * mix(0.18, 0.03, roughness) * lightColor * dayBoost;

    // Subcutaneous translucency (warm red glow through skin on backlighting)
    float sss = pow(clamp(dot(V, -L), 0.0, 1.0), 3.0) * (1.0 - roughness);
    vec3 sssColor = vec3(0.85, 0.32, 0.16) * sss * 0.30 * dayBoost;

    // 5. Combine
    vec3 result = ambient + directDiffuse + specular + sssColor;

    // 6. Simple ACES-lite Tonemapping
    result = (result * (2.51 * result + 0.03)) / (result * (2.43 * result + 0.59) + 0.14);

    // 7. Gamma Correction (Linear to sRGB)
    result = pow(result, vec3(1.0 / 2.2));

    // 8. Fog (Match terrain)
    float dist = length(viewPos - FragPos);
    float fogAmt = clamp(1.0 - exp(-dist * 0.00015), 0.0, 0.85);
    vec3 fogCol = mix(vec3(0.6, 0.62, 0.65), vec3(0.85, 0.83, 0.78), dayBoost);

    FragColor = vec4(mix(result, fogCol, fogAmt), 1.0);
}