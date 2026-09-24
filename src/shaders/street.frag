#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in float RoadType;

uniform vec3 lightDir;
uniform vec3 viewPos;

uniform int u_debugUV;   // FALLAGA_DEBUGVIEW: 1 = checker grid on the UVs

// ---------------------------------------------------------------------------
//  Procedural GTA-style road surface.
//    u = 0..1 across road (0 left, 0.5 center, 1 right)
//    v = metric distance along road (repeated)
// ---------------------------------------------------------------------------

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

// Smooth band: 1.0 when |x - center| < halfW, 0 outside with AA ramp
float mark(float x, float center, float halfW, float aa) {
    return smoothstep(halfW, halfW - aa, abs(x - center));
}

void main() {
    float u = TexCoords.x;
    float v = TexCoords.y;
    int   type = int(RoadType + 0.5);
    bool  paved = (type == 0 || type == 1 || type == 4);
    float dist  = length(viewPos - FragPos);
    vec2  wxz   = FragPos.xz;

    // ---- noise layers ---------------------------------------------------
    float nFine  = fbm(wxz * 7.0);      // tarmac aggregate
    float nMid   = fbm(wxz * 2.2);      // wear / dust streaks
    float nMacro = fbm(wxz * 0.5);      // large age mottling
    float nRust  = valueNoise(wxz * 11.0);

    // soft fade at the road shoulder (blend into dirt)
    float edgeDist  = min(u, 1.0 - u);
    float edgeAlpha = smoothstep(0.005, 0.08, edgeDist);
    float dust = (1.0 - edgeAlpha) * 0.9 +
                 pow(smoothstep(0.55, 1.0, nMacro), 3.0) * 0.1;

    // markings fade with distance to cut shimmering (GTA LOD behaviour)
    float markFade = 1.0 - smoothstep(140.0, 420.0, dist);

    vec3 albedo;
    float gloss = 0.0;

    if (paved) {
        // ---------------- ASPHALT (GTA dark blue-grey tarmac) -------------
        vec3 cTar    = vec3(0.147, 0.156, 0.172);   // core asphalt
        vec3 cWorn   = vec3(0.190, 0.184, 0.174);   // polished wheel paths
        vec3 cPatch  = vec3(0.100, 0.108, 0.122);   // tar repair patches
        vec3 cGrit   = vec3(0.212, 0.205, 0.195);   // exposed chippings
        vec3 cGrime  = vec3(0.46, 0.41, 0.34);      // blown dust / dirt

        float rut = smoothstep(0.13, 0.02,
                              min(abs(u - 0.28), abs(u - 0.72)));
        float patch = smoothstep(0.68, 0.92, nMacro);
        float grit  = nFine * 0.6 + nRust * 0.4;

        albedo = mix(cTar, cWorn, rut * 0.55);
        albedo = mix(albedo, cPatch, patch * 0.45);
        albedo = mix(albedo, cGrit, (grit - 0.5) * 0.05);
        albedo = mix(albedo, cGrime, dust * 0.35);
        gloss = 0.25;

        // solid white edge lines (inside ribs at u ~0.067..0.103)
        float edgeLineAA = 0.007;
        float lEdge = mark(u, 0.085, 0.018, edgeLineAA);
        float rEdge = mark(u, 0.915, 0.018, edgeLineAA);

        // concrete curb band + gutter shadow at the road rim
        float curb = mark(u, 0.028, 0.024, 0.008) +
                     mark(u, 0.972, 0.024, 0.008);
        vec3 cCurb = mix(vec3(0.60, 0.585, 0.545),
                         vec3(0.76, 0.735, 0.68), nMid * 0.3);
        albedo = mix(albedo, cCurb, curb * 0.9);
        albedo = mix(albedo, cTar * 0.6,
                     max(mark(u, 0.062, 0.012, 0.007),
                         mark(u, 0.938, 0.012, 0.007)));
        gloss = mix(gloss, 0.05, clamp(curb, 0.0, 1.0));

        // center markings
        vec3 lineWhite = vec3(0.865, 0.855, 0.83);
        vec3 lineYel   = vec3(0.80, 0.635, 0.16);
        float lineAA   = 0.006;

        // fade/age the paint
        float wear = 1.0 - (nMid * nMid) * 0.22 -
                     smoothstep(0.90, 0.98, nFine) * 0.45;

        if (type == 0) {
            // Colonial Avenue: solid double yellow center line
            float yL = mark(u, 0.475, 0.020, lineAA);
            float yR = mark(u, 0.525, 0.020, lineAA);
            float y  = clamp(yL + yR, 0.0, 1.0) * markFade * wear;
            albedo = mix(albedo, lineYel, y);
            gloss = mix(gloss, 0.45, y);
        } else {
            // dashed white single center line
            float dashEdge = smoothstep(0.26, 0.22,
                                        fract(v / 2.3)); // ~11.5 m cycle
            float ctr = mark(u, 0.5, 0.018, lineAA) * dashEdge;
            ctr *= markFade * wear;
            albedo = mix(albedo, lineWhite, ctr);
            gloss = mix(gloss, 0.45, ctr);
        }

        // edge lines
        float edgeLines = clamp(lEdge + rEdge, 0.0, 1.0) * markFade;
        albedo = mix(albedo, lineWhite, edgeLines);
        gloss  = mix(gloss, 0.45, edgeLines * 0.6);
    } else {
        // ---------------- PACKED EARTH / DIRT TRACK ------------------------
        vec3 cPacked = vec3(0.50, 0.44, 0.33);
        vec3 cRut    = vec3(0.36, 0.30, 0.22);
        vec3 cDust   = vec3(0.66, 0.60, 0.47);
        vec3 cPebble = vec3(0.72, 0.66, 0.54);

        float rut = smoothstep(0.13, 0.02,
                              min(abs(u - 0.25), abs(u - 0.75)));
        albedo = mix(cPacked, cPebble, (nFine - 0.5) * 0.4);
        albedo = mix(albedo, cRut, rut * 0.8);
        albedo = mix(albedo, cDust, smoothstep(0.6, 0.9, nMacro) * 0.5);
        albedo *= mix(0.9, 1.1, nMacro);
        gloss = 0.05;
    }

    // ---- lighting ---------------------------------------------------------
    vec3 N = normalize(Normal);
    vec3 L = normalize(lightDir);
    float NdotL = max(dot(N, L), 0.0);
    vec3 V = normalize(viewPos - FragPos);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 48.0);

    float ambient = 0.42;
    vec3 finalColor = albedo * (ambient + NdotL * 0.58);
    finalColor += vec3(0.26, 0.24, 0.21) * NdotL * spec * gloss;

    if (edgeAlpha < 0.02) discard;

    // UV inspection: alternating cells + grid lines reveal arc-length spacing
    // (v) and cross-road mapping (u); uniform cell sizes mean no stretching.
    if (u_debugUV != 0) {
        vec2 cells = TexCoords * vec2(8.0, 16.0);
        vec2 f = fract(cells);
        vec2 c = floor(cells);
        float cell = fract(c.x * 0.5 + c.y * 0.5);
        vec3 base = mix(vec3(0.08, 0.10, 0.42), vec3(0.45, 0.10, 0.10), cell);
        float grid = 1.0 - smoothstep(0.0, 0.06, min(f.x, f.y));
        FragColor = vec4(mix(base, vec3(0.95, 0.93, 0.88), grid * 0.9), 1.0);
        return;
    }

    FragColor = vec4(finalColor, edgeAlpha);
}