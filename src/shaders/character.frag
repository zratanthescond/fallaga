#version 330 core

// ----------------------------------------------------
// Input from Vertex Shader
// ----------------------------------------------------
in vec2 TexCoords;
in vec3 Normal;
in vec3 FragPos;

// ----------------------------------------------------
// Uniforms (from Character::render)
// ----------------------------------------------------
uniform sampler2D ourTexture; 
uniform vec3 lightPos;
uniform vec3 viewPos;
uniform vec3 lightColor;

// ----------------------------------------------------
// Output
// ----------------------------------------------------
out vec4 FragColor;

void main()
{
    vec4 texColor = texture(ourTexture, TexCoords);
    
    // Fallback: If texture is missing/black, use a default color 
    vec3 objectColor = (texColor.a < 0.1) ? vec3(0.5) : texColor.rgb;

    // Scale lights by sun height (proxy for day/night intensity)
    float sunVisibility = smoothstep(-0.2, 0.2, normalize(lightPos).y);
    vec3 animatedLightColor = lightColor * mix(0.2, 1.0, sunVisibility);

    // 1. Ambient Light - Dim at night
    float ambientStrength = mix(0.1, 0.4, sunVisibility);
    vec3 ambient = ambientStrength * animatedLightColor;

    // 2. Diffuse Light
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * animatedLightColor;

    // 3. Specular Light
    float specularStrength = 0.2;
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);  
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);
    vec3 specular = specularStrength * spec * animatedLightColor;  
        
    vec3 result = (ambient + diffuse + specular) * objectColor;
    FragColor = vec4(result, 1.0);
}