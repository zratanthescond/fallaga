#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in float RoadType;
in float MarkMask;
in vec2 MetricUV;

uniform vec3 lightDir;
uniform vec3 viewPos;

uniform sampler2D roadSurfaceTex;
uniform int  roadSurfaceValid;
uniform float roadSurfaceScale;
uniform float roadSurfaceGain;
uniform vec3  roadSurfaceTint;

uniform sampler2D roadNormalTex;
uniform int   roadNormalValid;
uniform float roadNormalStrength;

uniform int u_debugUV;
uniform int u_debugEdges;

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

float fbm(vec2 p) {
    float s = 0.0;
    s += valueNoise(p) * 0.5;
    s += valueNoise(p * 2.0) * 0.25;
    s += valueNoise(p * 4.0) * 0.125;
    return s;
}

void main() {
    float edgeDist = clamp(TexCoords.x, 0.0, 1.0);
    float bakedAO  = clamp(TexCoords.y, 0.0, 1.0);
    int type = int(RoadType + 0.5);
    bool paved = (type != 1); // 1 = Dirt, others = Paved

    // FALLAGA_EDGEONLY inspection
    if (u_debugEdges != 0) {
        float isCurb = smoothstep(0.16, 0.14, edgeDist);
        float isOuter = smoothstep(0.02, 0.0, edgeDist);
        vec3 c = mix(vec3(0.1, 0.8, 1.0), vec3(1.0, 0.9, 0.1), isCurb);
        c = mix(c, vec3(1.0, 0.2, 0.2), isOuter);
        FragColor = vec4(c, 1.0);
        return;
    }

    // 1. Per-tile hash variation (4m grid cells)
    vec2 tilePos = floor((MetricUV - vec2(-1000.0, -4000.0)) / 4.0);
    float tileHash = hash(tilePos);
    vec3 tileTint = vec3(0.96 + 0.08 * tileHash,
                         0.96 + 0.07 * fract(tileHash * 13.71),
                         0.95 + 0.08 * fract(tileHash * 29.33));

    // 2. Cobblestone albedo & surface
    vec2 cobbleUV = MetricUV / roadSurfaceScale;
    vec3 cobbleAlbedo;
    float gloss = 0.12;

    if (roadSurfaceValid != 0) {
        vec3 tex = texture(roadSurfaceTex, cobbleUV).rgb;
        cobbleAlbedo = tex * roadSurfaceGain * roadSurfaceTint * tileTint;
    } else {
        // Procedural cobble pattern
        vec2 p = MetricUV * 1.5;
        vec2 ip = floor(p);
        vec2 fp = fract(p);
        float stoneH = hash(ip);
        float stoneEdge = smoothstep(0.04, 0.08, min(fp.x, fp.y)) *
                          smoothstep(0.04, 0.08, min(1.0 - fp.x, 1.0 - fp.y));
        vec3 cStone = mix(vec3(0.19, 0.18, 0.17), vec3(0.28, 0.26, 0.24), stoneH);
        vec3 cMortar = vec3(0.12, 0.11, 0.10);
        cobbleAlbedo = mix(cMortar, cStone, stoneEdge) * tileTint;
    }

    // 3. Cut-stone curb
    // Lighter, rectangular cut stone blocks along the curb
    float curbJointX = smoothstep(0.03, 0.0, abs(fract(MetricUV.x / 0.70) - 0.5) - 0.47);
    float curbJointZ = smoothstep(0.03, 0.0, abs(fract(MetricUV.y / 0.70) - 0.5) - 0.47);
    float curbJoint = max(curbJointX, curbJointZ);
    float curbStoneH = hash(floor(MetricUV * 1.4));
    vec3 curbBase = mix(vec3(0.64, 0.61, 0.56), vec3(0.76, 0.73, 0.68), curbStoneH * 0.35);
    vec3 curbAlbedo = curbBase * (1.0 - curbJoint * 0.25) * tileTint;

    vec3 albedo;
    if (paved) {
        // Blend cobble & curb using edgeDist: 0 at outer curb, 0.15 at road margin
        float curbFactor = 1.0 - smoothstep(0.0, 0.15, edgeDist);
        albedo = mix(cobbleAlbedo, curbAlbedo, curbFactor);
        gloss = mix(0.12, 0.05, curbFactor);
    } else {
        // Packed earth / dirt track
        vec3 cPacked = vec3(0.48, 0.42, 0.32);
        vec3 cRut    = vec3(0.35, 0.29, 0.21);
        float rutNoise = fbm(MetricUV * 0.8);
        float rut = smoothstep(0.12, 0.02, min(abs(edgeDist - 0.35), abs(edgeDist - 0.65)));
        albedo = mix(cPacked, cRut, rut * 0.7) * tileTint * mix(0.9, 1.1, rutNoise);
        gloss = 0.04;
    }

    // 4. Ground blend at outer edge: soft transition to grass (no hard seam)
    vec3 grassColor = vec3(0.24, 0.30, 0.14);
    float groundBlend = smoothstep(0.0, 0.04, edgeDist);
    albedo = mix(grassColor, albedo, groundBlend);

    // 5. Normal mapping
    vec3 N = normalize(Normal);
    if (roadNormalValid != 0 && paved) {
        vec2 uvN = cobbleUV;
        vec3 nm = texture(roadNormalTex, uvN).xyz * 2.0 - 1.0;
        vec3 T = normalize(vec3(1.0, 0.0, 0.0) - N * N.x);
        vec3 B = cross(N, T);
        vec3 perturbed = normalize(T * nm.x * roadNormalStrength +
                                   B * nm.y * roadNormalStrength +
                                   N * max(nm.z, 0.05));
        N = normalize(mix(N, perturbed, 0.75 * smoothstep(0.0, 0.15, edgeDist)));
    }

    // 6. Lighting with baked AO
    vec3 L = normalize(lightDir);
    float NdotL = max(dot(N, L), 0.0);
    vec3 V = normalize(viewPos - FragPos);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 32.0);

    float ambient = 0.40;
    vec3 finalColor = albedo * ((ambient + NdotL * 0.60) * bakedAO);
    finalColor += vec3(0.28, 0.26, 0.23) * NdotL * spec * gloss * bakedAO;

    // UV debug checker
    if (u_debugUV != 0) {
        vec2 cells = MetricUV * 0.5;
        vec2 f = fract(cells);
        float checker = mod(floor(cells.x) + floor(cells.y), 2.0);
        vec3 dbgColor = mix(vec3(0.2, 0.4, 0.8), vec3(0.9, 0.8, 0.2), checker);
        FragColor = vec4(dbgColor, 1.0);
        return;
    }

    FragColor = vec4(finalColor, 1.0);
}
