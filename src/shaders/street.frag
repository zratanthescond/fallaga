#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in float RoadType;

uniform vec3 lightDir;

// Procedural noise for natural micro-texture
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
    float u = TexCoords.x; // 0.0 at left edge, 0.5 at center, 1.0 at right edge
    float v = TexCoords.y; // metric distance along road

    // Dual wheel tracks: symmetrically at u ~0.28 and u ~0.72
    float distToLeftRut = abs(u - 0.28);
    float distToRightRut = abs(u - 0.72);
    float minRutDist = min(distToLeftRut, distToRightRut);
    float rutFactor = smoothstep(0.12, 0.02, minRutDist);

    // Center crown/median between ruts
    float centerFactor = smoothstep(0.18, 0.0, abs(u - 0.5));

    // Outer edge falloff (for soft fading into terrain shoulders)
    float edgeDist = min(u, 1.0 - u);
    float edgeAlpha = smoothstep(0.005, 0.09, edgeDist);

    // Micro and macro surface noise
    vec2 worldXZ = FragPos.xz;
    float nMicro = valueNoise(worldXZ * 1.5) * 0.5 + valueNoise(worldXZ * 4.0) * 0.5;
    float nMacro = valueNoise(worldXZ * 0.05);

    vec3 roadColor;
    float roughness = 0.85;

    bool isUrban = (RoadType < 1.5); // Colonial Avenue (0) or Urban Street (1)

    if (isUrban) {
        // --- URBAN COLONIAL TARRED MACADAM / CRUSHED STONE ---
        vec3 cTar = vec3(0.35, 0.33, 0.31);       // Warm weathered macadam
        vec3 cCompacted = vec3(0.24, 0.23, 0.22); // Smooth compacted wheel paths
        vec3 cGravel = vec3(0.48, 0.45, 0.40);    // Exposed crushed limestone aggregate
        vec3 cDust = vec3(0.62, 0.56, 0.46);      // Warm Mediterranean dust & sand

        roadColor = mix(cTar, cGravel, nMicro * 0.40);
        roadColor = mix(roadColor, cCompacted, rutFactor * 0.65);
        roadColor = mix(roadColor, cDust, centerFactor * 0.35 + (1.0 - edgeAlpha) * 0.45);
        roughness = mix(0.70, 0.45, rutFactor);
    } else {
        // --- RURAL COUNTRY DIRT ROAD & CART TRACK ---
        vec3 cDirt = vec3(0.60, 0.50, 0.36);      // Sun-baked Mediterranean earth
        vec3 cRut = vec3(0.42, 0.34, 0.24);       // Dark compressed moist ruts
        vec3 cDust = vec3(0.72, 0.64, 0.50);      // Fine powdery dust on center ridge
        vec3 cPebble = vec3(0.76, 0.70, 0.58);    // Light limestone pebbles on shoulders

        roadColor = mix(cDirt, cPebble, nMicro * 0.35);
        roadColor = mix(roadColor, cRut, rutFactor * 0.75);
        roadColor = mix(roadColor, cDust, centerFactor * 0.50);
        roadColor *= mix(0.92, 1.08, nMacro);
        roughness = mix(0.90, 0.65, rutFactor);
    }

    // Directional lighting + ambient
    vec3 N = normalize(Normal);
    vec3 L = normalize(lightDir);
    float NdotL = max(dot(N, L), 0.0);
    float ambient = 0.42;
    vec3 finalColor = roadColor * (ambient + NdotL * 0.58);

    if (edgeAlpha < 0.02) discard;
    FragColor = vec4(finalColor, edgeAlpha);
}
