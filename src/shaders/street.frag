#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;

uniform vec3 lightDir;

// Hash for procedural variation
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(hash(i + vec2(0,0)), hash(i + vec2(1,0)), f.x),
        mix(hash(i + vec2(0,1)), hash(i + vec2(1,1)), f.x),
        f.y
    );
}

void main() {
    vec2 uv = FragPos.xz;

    // Packed earth / beaten-track street colors
    vec3 cBase   = vec3(0.68, 0.58, 0.42);  // Dry sandy earth
    vec3 cDarker = vec3(0.52, 0.44, 0.32);  // Worn / compacted earth
    vec3 cLight  = vec3(0.78, 0.72, 0.56);  // Lighter sandy patches

    // Noise for variation
    float nMicro = valueNoise(uv * 0.015);
    float nMacro = valueNoise(uv * 0.003);

    // Create natural variation between worn center and edges
    vec3 color = mix(cBase, cDarker, nMacro * 0.6);
    color = mix(color, cLight, nMicro * 0.3);

    // Very subtle rut/groove pattern along the street
    float ruts = valueNoise(uv * vec2(0.05, 0.02)) * 0.04;
    color -= ruts;

    // Lighting
    vec3 norm = normalize(Normal);
    vec3 L = normalize(lightDir);
    float NdotL = max(dot(norm, L), 0.0);
    float ambient = 0.40;
    float diff = ambient + NdotL * 0.60;

    FragColor = vec4(color * diff, 1.0);
}
