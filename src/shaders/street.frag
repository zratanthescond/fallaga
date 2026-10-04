#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in float RoadType;
in float MarkMask;   // 1 = paint markings, 0 = bare asphalt (junction pad / merged road)
in vec2 MetricUV;    // metres across and along the road, for the surface texture

uniform vec3 lightDir;
uniform vec3 viewPos;

// Tarred gravel surface. Sampled with MetricUV so the aggregate keeps a constant
// physical size instead of stretching with the road width the way TexCoords would.
uniform sampler2D roadSurfaceTex;
uniform int  roadSurfaceValid;  // 0 = texture unavailable, fall back to procedural
uniform float roadSurfaceScale; // metres per texture repeat
// The tarred gravel scan is authored dark (mean sRGB ~0.23, i.e. ~0.047 linear), and
// the albedo is multiplied by `ambient + NdotL*0.58` below, so used raw it renders
// almost black. Gain brings it back to the brightness the procedural asphalt had
// (linear ~0.15); the tint keeps it from reading as flat grey.
uniform float roadSurfaceGain;
uniform vec3  roadSurfaceTint;
// Gravel relief. Sampled with the same metric UV as the colour, so the stones stay
// in step with the aggregate. OpenGL-convention map (green = +Y).
uniform sampler2D roadNormalTex;
uniform int   roadNormalValid;
uniform float roadNormalStrength;

uniform int u_debugUV;   // FALLAGA_DEBUGVIEW: 1 = checker grid on the UVs
uniform int u_debugEdges; // FALLAGA_EDGEONLY: 1 = road edges only, no surface
uniform int u_joinStrokeCount;
uniform vec4 u_joinCurvePoints[128]; // four xz control points per shader-painted stroke
uniform vec4 u_joinStrokeInfo[32];  // half-width, 1 for pavement/curb, 0 for white edge
uniform int u_joinContactCount;
uniform vec4 u_joinContactCenterT[32]; // host center xz and tangent xz
uniform vec4 u_joinContactInfo[32];    // host side, white mouth half-span, pavement half-span, radius
uniform vec4 u_joinContactDims[32];    // host road width

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

float bezierStrokeDistance(vec2 q, vec2 p0, vec2 p1, vec2 p2, vec2 p3) {
    float best = 1e20;
    vec2 previous = p0;
    for (int step = 1; step <= 16; ++step) {
        float t = float(step) / 16.0;
        float s = 1.0 - t;
        vec2 current = p0 * (s*s*s) + p1 * (3.0*s*s*t) +
                       p2 * (3.0*s*t*t) + p3 * (t*t*t);
        vec2 d = current - previous;
        float d2 = dot(d, d);
        float along = (d2 > 1e-8) ? clamp(dot(q - previous, d) / d2, 0.0, 1.0) : 0.0;
        best = min(best, length(q - (previous + d * along)));
        previous = current;
    }
    return best;
}

void main() {
    float u = TexCoords.x;
    float v = TexCoords.y;
    int   type = int(RoadType + 0.5);
    bool  paved = (type == 0 || type == 1 || type == 4);
    float dist  = length(viewPos - FragPos);
    vec2  wxz   = FragPos.xz;

    // Junction / merge suppression. Interpolated, so paint fades out over the length of
    // one cross-section instead of snapping on at a hard line.
    float paint = clamp(MarkMask, 0.0, 1.0);

    // ---- edge-only inspection (FALLAGA_EDGEONLY) ----------------------------
    //  Keeps just the two ribbon boundaries and throws the entire surface away, so
    //  deformation is read straight off the outline instead of through tarmac shading:
    //  a pinched, twisted or self-overlapping ribbon shows up as the two edges crossing
    //  or bunching. This has to run BEFORE the shoulder discard further down, which
    //  drops exactly the u~0 / u~1 band being shown here, and it ignores markFade,
    //  which would otherwise erase the painted lines past 140 m. Unlit, so the colour
    //  is flat and the geometry is not disguised by lighting.
    if (u_debugEdges != 0) {
        // Cyan = the true geometric rim of the ribbon (u = 0 and u = 1).
        float rim = clamp(mark(u, 0.0, 0.010, 0.004) + mark(u, 1.0, 0.010, 0.004),
                          0.0, 1.0);
        // Yellow = the painted edge lines, for reference against the real rim.
        float line = clamp(mark(u, 0.085, 0.016, 0.006) + mark(u, 0.915, 0.016, 0.006),
                           0.0, 1.0);
        float a = clamp(rim + line * 0.5, 0.0, 1.0);
        if (a < 0.05) discard;
        vec3 c = mix(vec3(0.10, 0.90, 1.00), vec3(1.00, 0.92, 0.15),
                     clamp(line * 0.5 / a, 0.0, 1.0));
        FragColor = vec4(c, 1.0);
        return;
    }

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
    float markFade = (1.0 - smoothstep(140.0, 420.0, dist)) * paint;


    vec3 albedo;
    float gloss = 0.0;

    if (paved) {
        // ---------------- PAVED: tarred gravel surface texture -------------
        // The saved surface is tar over gravel, so the texture carries the colour and
        // the aggregate. The procedural layers above are kept for wear, rutting,
        // patches and dust, so the road still reads as used rather than tiled. The
        // old flat asphalt colours survive only as the fallback for a missing texture.
        vec3 cTar    = vec3(0.147, 0.156, 0.172);   // fallback core asphalt
        vec3 cWorn   = vec3(0.190, 0.184, 0.174);   // polished wheel paths
        vec3 cPatch  = vec3(0.100, 0.108, 0.122);   // tar repair patches
        vec3 cGrit   = vec3(0.212, 0.205, 0.195);   // exposed chippings
        vec3 cGrime  = vec3(0.46, 0.41, 0.34);      // blown dust / dirt

        float rut = smoothstep(0.13, 0.02,
                              min(abs(u - 0.28), abs(u - 0.72))) * paint;
        float patch = smoothstep(0.68, 0.92, nMacro);
        float grit  = nFine * 0.6 + nRust * 0.4;

        if (roadSurfaceValid != 0) {
            // Isotropic tiling: MetricUV is metres on both axes, so one repeat spans
            // roadSurfaceScale metres in every direction.
            vec3 tex = texture(roadSurfaceTex, MetricUV / roadSurfaceScale).rgb;
            albedo = tex * roadSurfaceGain * roadSurfaceTint;
            // Wheel-path polish, tar patches and blown dust on top of the texture.
            albedo = mix(albedo, albedo * 1.18, rut * 0.55);
            albedo = mix(albedo, albedo * 0.72, patch * 0.45);
            albedo = mix(albedo, albedo * mix(1.25, 0.85, grit), dust * 0.30);
            albedo = mix(albedo, cGrime, dust * 0.12);
            gloss = 0.12;
        } else {
            // ---------------- ASPHALT fallback (no texture bound) -----------
            albedo = mix(cTar, cWorn, rut * 0.55);
            albedo = mix(albedo, cPatch, patch * 0.45);
            albedo = mix(albedo, cGrit, (grit - 0.5) * 0.05);
            albedo = mix(albedo, cGrime, dust * 0.35);
            gloss = 0.25;
        }

        // solid white edge lines (inside ribs at u ~0.067..0.103)
        float edgeLineAA = 0.007;
        float lEdge = mark(u, 0.085, 0.018, edgeLineAA);
        float rEdge = mark(u, 0.915, 0.018, edgeLineAA);

        // Concrete pavement strip from the ribbon edge to just outside the white
        // edge stripe. Junction curves use the same width and material below.
        float curb = mark(u, 0.0335, 0.0335, 0.006) +
                     mark(u, 0.9665, 0.0335, 0.006);
        curb *= paint;
        float whiteSuppression = 0.0;
        float pavementSuppression = 0.0;
        for (int j = 0; j < 32; ++j) {
            if (j >= u_joinContactCount) break;
            vec4 centerT = u_joinContactCenterT[j];
            vec4 info = u_joinContactInfo[j];
            float hostWidth = u_joinContactDims[j].x;
            vec2 hostCenter = centerT.xy;
            vec2 hostTangent = normalize(centerT.zw);
            vec2 hostRight = vec2(hostTangent.y, -hostTangent.x);
            float along = dot(wxz - hostCenter, hostTangent);
            float outward = dot(wxz - hostCenter, hostRight) * info.x;
            if (abs(along) < info.y && outward > hostWidth * 0.30) {
                whiteSuppression = 1.0;
                pavementSuppression = 1.0;
            }
        }
        curb *= 1.0 - pavementSuppression;
        vec3 cCurb = mix(vec3(0.60, 0.585, 0.545),
                         vec3(0.76, 0.735, 0.68), nMid * 0.3);
        albedo = mix(albedo, cCurb, curb * 0.9);
        albedo = mix(albedo, cTar * 0.6,
                     max(mark(u, 0.062, 0.012, 0.007),
                         mark(u, 0.938, 0.012, 0.007)) * paint *
                         (1.0 - pavementSuppression));
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
        float edgeLines = clamp(lEdge + rEdge, 0.0, 1.0) * markFade * paint *
                          (1.0 - whiteSuppression);
        albedo = mix(albedo, lineWhite, edgeLines);
        gloss  = mix(gloss, 0.45, edgeLines * 0.6);

        // Paint rounded junction transitions with this road's own material. The
        // curves are world-space UV guides; no separate line mesh can z-fight or
        // introduce a second asphalt texture at the join.
        float joinWhite = 0.0;
        float joinCurb = 0.0;
        for (int j = 0; j < 32; ++j) {
            if (j >= u_joinStrokeCount) break;
            int base = j * 4;
            float d = bezierStrokeDistance(wxz,
                u_joinCurvePoints[base + 0].xy,
                u_joinCurvePoints[base + 1].xy,
                u_joinCurvePoints[base + 2].xy,
                u_joinCurvePoints[base + 3].xy);
            float halfWidth = u_joinStrokeInfo[j].x;
            float coverage = 1.0 - smoothstep(halfWidth - 0.025,
                                               halfWidth + 0.025, d);
            if (u_joinStrokeInfo[j].y > 0.5)
                joinCurb = max(joinCurb, coverage);
            else
                joinWhite = max(joinWhite, coverage);
        }
        vec3 joinedCurb = mix(vec3(0.60, 0.585, 0.545),
                              vec3(0.76, 0.735, 0.68), nMid * 0.3);
        albedo = mix(albedo, joinedCurb, joinCurb);
        // Join strokes already replace the branch and host lines inside the
        // endpoint suppression zone. Do not reuse markFade here: MarkMask is
        // intentionally zero at attached endpoints, and using it made every
        // rounded white connector disappear just before it met the host stripe.
        float joinLineFade = 1.0 - smoothstep(140.0, 420.0, dist);
        albedo = mix(albedo, lineWhite, joinWhite * joinLineFade * wear);
        gloss = mix(gloss, 0.05, joinCurb);
        gloss = mix(gloss, 0.45, joinWhite * 0.6);
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
    // Gravel relief. MetricUV is metres across/along the road, which is world XZ, so
    // the tangent frame is just that basis flattened and orthonormalised against the
    // vertex normal -- no tangent attribute needed. Degenerate only if the normal is
    // exactly world-up, which the cross with (1,0,0) below cannot produce.
    if (roadNormalValid != 0) {
        vec2 uvN = MetricUV / roadSurfaceScale;
        vec3 nm = texture(roadNormalTex, uvN).xyz * 2.0 - 1.0;
        vec3 T = normalize(vec3(1.0, 0.0, 0.0) - N * N.x);
        vec3 B = cross(N, T);
        vec3 perturbed = normalize(T * nm.x * roadNormalStrength +
                                   B * nm.y * roadNormalStrength +
                                   N * max(nm.z, 0.05));
        N = normalize(mix(N, perturbed, 0.85));
    }
    vec3 L = normalize(lightDir);
    float NdotL = max(dot(N, L), 0.0);
    vec3 V = normalize(viewPos - FragPos);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 48.0);

    float ambient = 0.42;
    vec3 finalColor = albedo * (ambient + NdotL * 0.58);
    finalColor += vec3(0.26, 0.24, 0.21) * NdotL * spec * gloss;

    // Keep shoulder fragments opaque. Screen-space discard made each road's edge
    // punch holes through the prioritized top surface, revealing the overlapping
    // road beneath as black/white speckling at joins. `dust` above still shades the
    // outer band so it fades visually into the terrain without fragment holes.

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

    // Opaque: the road ribbon is drawn with blending off, so the shoulder has already
    // been dithered away above and every surviving fragment owns its depth.
    FragColor = vec4(finalColor, 1.0);
}
