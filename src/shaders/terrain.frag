#version 330 core

in vec3  FragPos;
in vec3  Normal;
in vec2  TexCoords;
in float WorldY;

out vec4 FragColor;

uniform vec3  lightDir;
uniform vec3  viewPos;
uniform float minHeight;
uniform float maxHeight;

// ── Utility ─────────────────────────────────────────────────────────────────
float remap(float v, float lo, float hi) {
    return clamp((v - lo) / (hi - lo), 0.0, 1.0);
}

// Classic smooth hash noise (no textures needed)
float hash(vec2 p) {
    p = fract(p * vec2(443.897, 441.423));
    p += dot(p, p + 19.19);
    return fract(p.x * p.y);
}
float noise2(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i),           hash(i + vec2(1,0)), f.x),
               mix(hash(i+vec2(0,1)), hash(i + vec2(1,1)), f.x), f.y);
}
float fbm(vec2 p, int oct) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < oct; i++) { v += a * (noise2(p) * 2.0 - 1.0); p *= 2.1; a *= 0.5; }
    return v;
}

void main()
{
    vec3 N = normalize(Normal);
    if (N.y < 0.0) N = -N;

    float range = max(maxHeight - minHeight, 0.001);
    float t  = clamp((WorldY - minHeight) / range, 0.0, 1.0);  // 0=low, 1=Kasbah peak

    // ── Slope / steepness ─────────────────────────────────────────────────────
    float slope = 1.0 - abs(N.y);        // 0=flat, 1=vertical
    float slopeSq = slope * slope;

    // ── Macro colour zones (warm 1930s North-African palette) ─────────────────
    //   t < 0.08  coastal plain / port esplanade  — pale buff sand
    //   t < 0.30  lower medina / colonial quarter  — warm sandy ochre
    //   t < 0.60  upper medina plateau             — deeper ochre
    //   t < 0.85  Kasbah flanks                   — dry terracotta stone
    //   t ≥ 0.85  Kasbah summit                   — bleached limestone grey

    vec3 c_coast   = vec3(0.85, 0.80, 0.63);   // pale buff
    vec3 c_plain   = vec3(0.80, 0.72, 0.54);   // warm sandy ochre
    vec3 c_medina  = vec3(0.72, 0.63, 0.44);   // deeper ochre
    vec3 c_kasbah  = vec3(0.62, 0.52, 0.38);   // dry terracotta
    vec3 c_summit  = vec3(0.75, 0.70, 0.62);   // bleached limestone

    vec3 col;
    if      (t < 0.08) col = mix(c_coast,  c_plain,  remap(t,  0.0,  0.08));
    else if (t < 0.30) col = mix(c_plain,  c_medina, remap(t,  0.08, 0.30));
    else if (t < 0.60) col = mix(c_medina, c_kasbah, remap(t,  0.30, 0.60));
    else if (t < 0.85) col = mix(c_kasbah, c_summit, remap(t,  0.60, 0.85));
    else               col = c_summit;

    // ── Rock on steep slopes ──────────────────────────────────────────────────
    vec3 c_rock = vec3(0.50, 0.44, 0.36);   // dark stone
    float rockBlend = smoothstep(0.35, 0.65, slope);
    col = mix(col, c_rock, rockBlend * 0.75);

    // ── Procedural colour variation (breaks up flat banding) ──────────────────
    // Large-scale: warm/cool patches every ~200m
    float macro = fbm(FragPos.xz * 0.004, 3);
    col *= (1.0 + macro * 0.12);

    // Fine-scale sandy speckle every ~15m
    float speckle = noise2(FragPos.xz * 0.07) * 2.0 - 1.0;
    col += vec3(speckle * 0.025, speckle * 0.020, speckle * 0.010);

    // ── Directional lighting ─────────────────────────────────────────────────
    vec3 L = normalize(lightDir);

    // Soften NdL with a Valve-style half-Lambert wrap
    float wrap     = 0.35;
    float NdL_raw  = dot(N, L);
    float NdL      = clamp((NdL_raw + wrap) / (1.0 + wrap), 0.0, 1.0);
    NdL            = NdL * NdL;     // square for nicer falloff

    vec3 ambient  = col * 0.58;
    vec3 diffuse  = col * NdL * 0.55;

    // Specular — slightly glossy limestone on high slopes
    vec3  V    = normalize(viewPos - FragPos);
    vec3  H    = normalize(L + V);
    float gloss = mix(8.0, 32.0, rockBlend);
    float spec  = pow(max(dot(N, H), 0.0), gloss) * mix(0.02, 0.06, rockBlend);

    // Rim light from the sky (subtle blue-grey fill from above)
    float rim = max(0.0, 1.0 - dot(N, V));
    vec3 rimCol = vec3(0.55, 0.58, 0.65) * rim * rim * 0.10;

    vec3 final = ambient + diffuse + vec3(spec) + rimCol;

    // ── Distance haze — warm Saharan haze colour ──────────────────────────────
    float dist   = length(viewPos - FragPos);
    float fogAmt = 1.0 - exp(-dist * 0.00012);
    vec3  fogCol = vec3(0.80, 0.76, 0.67);  // warm pale haze
    final = mix(final, fogCol, clamp(fogAmt, 0.0, 0.75));

    FragColor = vec4(clamp(final, 0.0, 1.0), 1.0);
}
