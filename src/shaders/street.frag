#version 460 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in float RoadType;
in float MarkMask;   // 1 = paint markings/curbs, 0 = bare junction pad / merged road
in vec2 MetricUV;    // world coordinates in metres (FragPos.xz)

uniform vec3 lightDir;
uniform vec3 viewPos;

// --- Historical Period Road Textures (from bla_road_txd) ---
// 1. Cobblestone carriageway
uniform sampler2D roadSurfaceTex;
uniform int       roadSurfaceValid;
uniform float     roadSurfaceScale;
uniform float     roadSurfaceGain;
uniform vec3      roadSurfaceTint;
uniform sampler2D roadNormalTex;
uniform int       roadNormalValid;
uniform float     roadNormalStrength;

// 2. Cut stone curb blocks
uniform sampler2D roadCurbTex;
uniform int       roadCurbValid;
uniform sampler2D roadCurbNormalTex;
uniform int       roadCurbNormalValid;
uniform float     roadCurbScale;

// 3. Period red herringbone brick sidewalk
uniform sampler2D roadSidewalkTex;
uniform int       roadSidewalkValid;
uniform sampler2D roadSidewalkNormalTex;
uniform int       roadSidewalkNormalValid;
uniform float     roadSidewalkScale;

// 4. Large stone block paving (Port, Medina, Colonial Avenue)
uniform sampler2D roadPavingTex;
uniform int       roadPavingValid;
uniform sampler2D roadPavingNormalTex;
uniform int       roadPavingNormalValid;
uniform float     roadPavingScale;

// 5. Muddy dirt road & cart track
uniform sampler2D roadDirtTex;
uniform int       roadDirtValid;
uniform sampler2D roadDirtNormalTex;
uniform int       roadDirtNormalValid;
uniform float     roadDirtScale;

// 6. Sand ground (shoulders, gutters, verges)
uniform sampler2D roadSandTex;
uniform int       roadSandValid;
uniform sampler2D roadSandNormalTex;
uniform int       roadSandNormalValid;
uniform float     roadSandScale;

// Debug and junction controls
uniform int u_debugUV;
// Diagnostics for black / wrong-looking pixels: 1 = albedo only (unlit), 2 = shading normal,
// 3 = diffuse term. 0 = normal rendering.
uniform int u_debugView;
// 1: any pixel whose final colour is NaN / Inf is painted magenta (so you can SEE it instead of
// getting black). Set to 0 for release: those pixels then fall back to a dim albedo.
#define DEBUG_NAN_PINK 1
uniform int u_debugTexSlot; // 1 surface, 2 curb, 3 sidewalk, 4 paving, 5 dirt, 6 sand: shows the raw bound texture
uniform int u_debugEdges;
uniform int u_joinStrokeCount;
uniform vec4 u_joinCurvePoints[128]; // four xz control points per shader-painted stroke
uniform vec4 u_joinStrokeInfo[32];   // half-width, 1 for pavement/curb
uniform int u_joinContactCount;
uniform vec4 u_joinContactCenterT[32]; // host center xz and tangent xz
uniform vec4 u_joinContactInfo[32];    // host side, white mouth half-span, pavement half-span, radius
uniform vec4 u_joinContactDims[32];    // host road width

// ---------------------------------------------------------------------------
//  LOOK-DEV CONSTANTS
//  Tuned against the Saint-Denis reference screenshot: a street that is mostly
//  *dust*, with the cobbles showing through softly, a dark chunky curb, clean-ish
//  red brick, sand drifted against the curb and ragged grass at the road edge.
//  Every value here can be promoted to a uniform if you want live tweaking.
// ---------------------------------------------------------------------------

// Lighting mood: hazy warm sun, soft warm fill (sum ~= your old 0.44 ambient)
const vec3  SUN_TINT         = vec3(1.04, 1.03, 1.00);
const vec3  AMBIENT_COL      = vec3(0.48, 0.49, 0.49);
const float WRAP_AMOUNT      = 0.45;   // soft "powdery" diffuse wrap on dusty surfaces

// Procedural surfaces. If the texture in a slot is not what you expect (your render showed a
// brick-WALL texture on the carriageway), the shader can draw the stones itself.
//   1 = generate in-shader (ignores roadSurfaceTex / roadPavingTex / roadSidewalkTex)
//   0 = use the textures bound from C++
#define PROCEDURAL_COBBLE 0
// PROCEDURAL_BRICK: 0 = texture only, 1 = fully generated, 2 = HYBRID (recommended): keeps the colour
// of your brick texture but draws the brick / mortar pattern and relief in the shader. Use 2 when the
// sidewalk albedo is nearly flat (nbx_ground_brickherringdr_red_01_ab is almost a single red tone,
// its pattern lives in the _ma / _mb material maps, which this shader does not read).
#define PROCEDURAL_BRICK  2
#define PROCEDURAL_CURB   0

// Colour calibration. 1 = every texture is re-balanced so its AVERAGE colour equals the REF_*
// value below (measured on the Saint-Denis reference screenshot), while keeping the texture's own
// detail and contrast. This makes the look independent of how bright / saturated / tinted the
// source textures happen to be. 0 = use the textures exactly as bound.
#define MATCH_REFERENCE 1
const vec3 REF_COBBLE = vec3(0.62, 0.63, 0.58);   // grey-beige cobbles (reference mean)
const vec3 REF_BRICK  = vec3(0.46, 0.24, 0.20);   // deep red-brown sidewalk brick
const vec3 REF_CURB   = vec3(0.60, 0.60, 0.56);   // light grey cut stone

// Heading of the street in world XZ, in degrees (0 = street runs along world +X).
// Brick rows, cobble rows and curb blocks are laid parallel to it.
const float ROAD_ANGLE_DEG = 0.0;

// Dust on the carriageway
const vec3  DUST_COL         = vec3(0.66, 0.65, 0.58);   // pale grey-beige dust on stone tops
const vec3  DUST_JOINT_COL   = vec3(0.42, 0.41, 0.36);   // darker packed dirt in the joints
const float DUST_COVER       = 0.35;   // 0 = clean stone, 1 = buried (0.35 matches the reference; ~0.95 = mostly sand)
const float COBBLE_CONTRAST  = 0.85;   // 1 = raw texture contrast, lower = flatter / more worn
const float COBBLE_DESAT     = 0.0;    // pulls the texture towards a warm tan (0 = off)

// Curb
const vec3  CURB_TINT        = vec3(1.0);                 // multiplies the curb albedo (1 = as calibrated)
const float CURB_CONTRAST    = 0.55;                      // 1 = raw texture, lower = fewer stains / decals

// Distance behaviour (detail melts into an average dusty colour -> no shimmer, matches the screenshot)
const float DETAIL_FADE_NEAR = 18.0;
const float DETAIL_FADE_FAR  = 85.0;

// Road-edge grass tufts
const float TUFT_AMOUNT      = 1.0;    // 0 disables

// Texture scale multipliers on top of roadSurfaceScale / roadPavingScale / roadSidewalkScale.
// > 1 makes the pattern LARGER (fewer repeats). Aim for ~20-25 cm per cobble, ~22 cm per brick.
const float COBBLE_SCALE_MUL = 1.0;
const float BRICK_SCALE_MUL  = 1.0;

// Optional warm haze (OFF by default so the road does not seam against terrain that has no fog)
const float HAZE_DENSITY     = 0.0;    // e.g. 0.006
const vec3  HAZE_COL         = vec3(0.78, 0.72, 0.60);

// ---------------------------------------------------------------------------
//  Noise & Utility Functions
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

vec2 rot2(vec2 p, float a) {
    float c = cos(a), s = sin(a);
    return vec2(c * p.x - s * p.y, s * p.x + c * p.y);
}

float luma(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

// Re-balance a sample so the texture's average colour becomes `target` (detail is preserved).
vec3 matchMean(vec3 c, vec3 avg, vec3 target) {
    // Per-channel gain with a wide range: scanned albedos are often very dark (0.05 - 0.15).
    // Normalise by the mean luminance first, then re-tint, so a near-black channel cannot blow up.
    float la = max(luma(avg), 0.004);
    float lt = luma(target);
    vec3 tint = clamp((target / max(lt, 1e-3)) / max(avg / la, vec3(0.25)), vec3(0.5), vec3(2.0));
    return clamp(c * (lt / la) * tint, 0.0, 1.0);
}

// Smooth band: 1.0 when |x - center| < halfW, 0 outside with AA ramp
float mark(float x, float center, float halfW, float aa) {
    return 1.0 - smoothstep(halfW - aa, halfW, abs(x - center));
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

// Staggered stone / brick layout. p in metres. Returns a height-like value:
// 0 in the joint, 1 on the stone top. `id` is a per-stone random number.
float stoneField(vec2 p, vec2 size, float warp, float rad, float shrink, out float id)
{
    // domain warp -> irregular, hand-laid outlines
    vec2 w = vec2(valueNoise(p * 4.0), valueNoise(p * 4.0 + 17.3)) - 0.5;
    p += w * warp * size.y;
    float row = floor(p.y / size.y);
    float rowLen = 0.75 + 0.6 * hash(vec2(row, 9.1)) * step(0.001, warp);   // rows of longer / shorter stones
    vec2 q = vec2(p.x / (size.x * rowLen), p.y / size.y);
    q.x += hash(vec2(row, 3.7)) + 0.5 * mod(row, 2.0);   // staggered rows, random offset per row
    vec2 cell = vec2(floor(q.x), row);
    vec2 f = fract(q) - 0.5;
    id = hash(cell + 11.3);
    // every stone sits a little off-centre and is slightly skewed (hand-laid look)
    float irr = step(0.001, warp);
    f += (vec2(hash(cell + 1.7), hash(cell + 5.9)) - 0.5) * 0.06 * irr;
    f.x += f.y * (hash(cell + 5.1) - 0.5) * 0.30 * irr;
    // every stone is a slightly different size -> joints of varying width
    vec2 hb = vec2(0.5 - shrink * (0.5 + hash(cell + 7.7)),
                   0.5 - shrink * (0.5 + hash(cell + 3.3)));
    vec2 d = abs(f) - (hb - rad);
    float sdf = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - rad;
    float h = 1.0 - smoothstep(-0.14, 0.0, sdf);          // rounded edge
    h *= 0.80 + 0.20 * (1.0 - clamp(dot(f, f) * 2.0, 0.0, 1.0));   // slight dome
    return h;
}

// Height field -> tangent-space normal (same convention as the sampled normal maps:
// x along world +X, y along world -Z, z up).
vec3 heightToNormal(float h0, float hx, float hz, float e, float k)
{
    vec2 g = vec2(hx - h0, hz - h0) / e;                   // dh/dx, dh/dz
    return normalize(vec3(-g.x * k, g.y * k, 1.0));
}

// Irregular rounded cobbles with dark packed-earth joints (Saint-Denis style).
vec3 proceduralCobble(vec2 p, out float joint, out vec3 nrm)
{
    const vec2 SZ = vec2(0.24, 0.15);
    const float e = 0.004;
    float id, idd;
    const float A = radians(ROAD_ANGLE_DEG);
    float h  = stoneField(rot2(p,                -A), SZ, 0.20, 0.18, 0.07, id);
    float hx = stoneField(rot2(p + vec2(e, 0.0), -A), SZ, 0.20, 0.18, 0.07, idd);
    float hz = stoneField(rot2(p + vec2(0.0, e), -A), SZ, 0.20, 0.18, 0.07, idd);
    nrm = heightToNormal(h, hx, hz, e, 0.012);

    vec3 sA = vec3(0.68, 0.62, 0.52);   // warm sandstone
    vec3 sB = vec3(0.60, 0.57, 0.51);   // grey stone
    vec3 sC = vec3(0.54, 0.47, 0.38);   // brown stone
    vec3 sc = mix(mix(sA, sB, smoothstep(0.0, 0.5, id)), sC, smoothstep(0.55, 1.0, id) * 0.8);
    sc *= 0.88 + 0.24 * valueNoise(p * 23.0 + id * 13.0);  // grain
    sc *= 0.82 + 0.18 * h;                                   // darker bevel edges

    joint = 1.0 - smoothstep(0.0, 0.45, h);
    return mix(vec3(0.38, 0.33, 0.27), sc, 1.0 - joint);
}

// Running-bond red brick with dusty mortar.
vec3 proceduralBrick(vec2 p, out vec3 nrm, out float mortar, out float brickId)
{
    const vec2 SZ = vec2(0.21, 0.068);
    const float e = 0.003;
    const float A = radians(ROAD_ANGLE_DEG);
    float id, idd;
    float h  = stoneField(rot2(p,                -A), SZ, 0.0, 0.08, 0.022, id);
    float hx = stoneField(rot2(p + vec2(e, 0.0), -A), SZ, 0.0, 0.08, 0.022, idd);
    float hz = stoneField(rot2(p + vec2(0.0, e), -A), SZ, 0.0, 0.08, 0.022, idd);
    nrm = heightToNormal(h, hx, hz, e, 0.006);

    vec3 bA = vec3(0.56, 0.25, 0.19);   // brick red
    vec3 bB = vec3(0.47, 0.20, 0.16);   // deeper red-brown
    vec3 bC = vec3(0.62, 0.34, 0.25);   // lighter, sun-faded
    vec3 bc = mix(mix(bA, bB, smoothstep(0.0, 0.6, id)), bC, smoothstep(0.7, 1.0, id));
    bc *= 0.90 + 0.20 * valueNoise(p * 40.0 + id * 9.0);
    float m = 1.0 - smoothstep(0.0, 0.40, h);
    mortar = m;
    brickId = id;
    return mix(bc, vec3(0.46, 0.41, 0.36), m * 0.85);        // dusty grey-brown mortar
}

// Grey-blue cut-stone kerb blocks (~85 cm long) with thin joints.
vec3 proceduralCurb(vec2 p)
{
    vec2 pr = rot2(p, -radians(ROAD_ANGLE_DEG));
    float along = pr.x / 0.85;
    float id = hash(vec2(floor(along), 4.2));
    float f  = fract(along);
    float j  = 1.0 - smoothstep(0.0, 0.012, min(f, 1.0 - f));      // ~1 cm joint
    vec3 c = mix(vec3(0.46, 0.49, 0.51), vec3(0.58, 0.58, 0.56), id);
    c *= 0.90 + 0.20 * valueNoise(p * 18.0);
    return mix(c, c * 0.45, j);
}

// Sparse small pebbles / debris scattered on the sand. rgb + coverage.
vec4 pebbles(vec2 p)
{
    vec2 g = p * 5.0;                                              // 20 cm cells
    vec2 c = floor(g);
    vec2 ctr = vec2(hash(c + 7.1), hash(c + 1.9)) * 0.6 + 0.2;
    float d   = length(fract(g) - ctr);
    float rad = mix(0.04, 0.10, hash(c + 9.7));                    // 0.8 - 2 cm
    float a   = step(0.955, hash(c + 3.3)) * (1.0 - smoothstep(rad * 0.6, rad, d));
    vec3 col  = mix(vec3(0.45, 0.38, 0.29), vec3(0.62, 0.54, 0.42), hash(c + 2.2));
    return vec4(col, a);
}

// Turns a stone albedo into a dust-covered paving surface.
//   raw       : stone colour
//   avg       : average colour (blurred mip, or a constant for procedural stones)
//   jointIn   : 0..1 joint mask if known (procedural), or < 0 to derive it from texture luma
//   contrast  : 1 keeps `raw` as is, lower flattens it towards `avg`
//   rut       : 0..1, wheel-polished area (dust is pushed away there)
//   farFade   : 0..1, how far we are from the camera (detail melts away)
//   coverMul  : per-surface multiplier on DUST_COVER
//   dustAmt   : out, final dust coverage (used to flatten normals / kill gloss)
vec3 dustyStone(vec3 raw, vec3 avg, float jointIn, float contrast,
                float mMid, float mMacro, float mFine,
                float rut, float farFade, float coverMul, out float dustAmt)
{
    float lum    = luma(raw);
    float avgLum = max(luma(avg), 1e-3);

    // Joints / recesses. For textures: darker than the local average (cheap height proxy).
    float joint = (jointIn >= 0.0) ? jointIn
                                   : smoothstep(0.0, 0.45, (avgLum - lum) / avgLum);

    // 1) Flatten contrast and pull towards warm tan.
    vec3 stone = mix(avg, raw, contrast);
    stone = mix(stone, vec3(luma(stone)) * vec3(1.06, 0.98, 0.86), COBBLE_DESAT);
    stone *= 1.0 - rut * 0.03;

    // 2) Patchy but gentle dust coverage: thicker in joints, thin on wheel ruts.
    float cover = DUST_COVER * coverMul
                + (mMacro - 0.5) * 0.20
                + (mMid   - 0.5) * 0.10
                + joint * 0.25
                - rut * 0.07;
    cover = clamp(cover, 0.0, 0.95);

    // 3) Dust colour: pale on tops, darker/packed in the joints.
    vec3 dust = mix(DUST_COL * mix(0.94, 1.06, mMid), DUST_JOINT_COL, joint * 0.55);
    float k = clamp(cover * (0.85 + 0.15 * joint), 0.0, 0.95);
    vec3 col = mix(stone, dust, k);

    // 4) Fine speckle so the dust is never perfectly flat
    col *= 0.95 + 0.10 * mFine;

    // 5) Far away: melt into the average dusty colour (kills shimmer, gives the hazy look)
    vec3 farCol = mix(avg, DUST_COL, cover);
    col = mix(col, farCol, farFade * 0.6);

    dustAmt = k;
    return col;
}

void main() {
    float u = TexCoords.x;
    float v = TexCoords.y;
    int   type = int(RoadType + 0.5);
    float dist  = length(viewPos - FragPos);
    vec2  wxz   = FragPos.xz;
    float paint = clamp(MarkMask, 0.0, 1.0);
    float farFade = smoothstep(DETAIL_FADE_NEAR, DETAIL_FADE_FAR, dist);

    // Geometric (faceted) normal from screen-space derivatives: gives the raised kerb face
    // a real orientation so it is lit / shaded like a vertical stone face, not a painted band.
    vec3 geoCross = cross(dFdx(FragPos), dFdy(FragPos));
    float geoLen2 = dot(geoCross, geoCross);
    // NaN * 0.0 is still NaN, so a degenerate cross product must never reach the mix() below
    vec3 Ngeo = (geoLen2 > 1e-20) ? geoCross * inversesqrt(geoLen2) : normalize(Normal);
    if (Ngeo.y < 0.0) Ngeo = -Ngeo;

    // ---- texture slot inspection: shows what is REALLY bound to each slot ----------
    if (u_debugTexSlot != 0) {
        vec3 c = vec3(1.0, 0.0, 1.0);   // magenta = slot invalid / not bound
        if      (u_debugTexSlot == 1 && roadSurfaceValid  != 0) c = texture(roadSurfaceTex,  MetricUV / roadSurfaceScale ).rgb;
        else if (u_debugTexSlot == 2 && roadCurbValid     != 0) c = texture(roadCurbTex,     MetricUV / roadCurbScale    ).rgb;
        else if (u_debugTexSlot == 3 && roadSidewalkValid != 0) c = texture(roadSidewalkTex, MetricUV / (roadSidewalkScale * BRICK_SCALE_MUL)).rgb;
        else if (u_debugTexSlot == 4 && roadPavingValid   != 0) c = texture(roadPavingTex,   MetricUV / roadPavingScale  ).rgb;
        else if (u_debugTexSlot == 5 && roadDirtValid     != 0) c = texture(roadDirtTex,     MetricUV / roadDirtScale    ).rgb;
        else if (u_debugTexSlot == 6 && roadSandValid     != 0) c = texture(roadSandTex,     MetricUV / roadSandScale    ).rgb;
        FragColor = vec4(c, 1.0);
        return;
    }

    // ---- edge-only inspection (FALLAGA_EDGEONLY) ----------------------------
    if (u_debugEdges != 0) {
        float rim = clamp(mark(u, 0.0, 0.010, 0.004) + mark(u, 1.0, 0.010, 0.004), 0.0, 1.0);
        float line = clamp(mark(u, 0.085, 0.016, 0.006) + mark(u, 0.915, 0.016, 0.006), 0.0, 1.0);
        float a = clamp(rim + line * 0.5, 0.0, 1.0);
        if (a < 0.05) discard;
        vec3 c = mix(vec3(0.10, 0.90, 1.00), vec3(1.00, 0.92, 0.15), clamp(line * 0.5 / a, 0.0, 1.0));
        FragColor = vec4(c, 1.0);
        return;
    }

    // ---- noise layers for organic weathering and dirt variation -------------
    float nFine  = fbm(wxz * 7.0);      // stone micro-variation   (raw, 0..0.875)
    float nMid   = fbm(wxz * 2.2);      // dust / wear streaks
    float nMacro = fbm(wxz * 0.5);      // large-scale age mottling
    // normalised copies (0..1, mean 0.5) used by the new dust model
    float mFine  = nFine  / 0.875;
    float mMid   = nMid   / 0.875;
    float mMacro = nMacro / 0.875;

    // Soft fade at ribbon outer shoulder (blends into terrain ground)
    float edgeDist  = min(u, 1.0 - u);
    float edgeAlpha = smoothstep(0.005, 0.08, edgeDist);
    float dust = (1.0 - edgeAlpha) * 0.85 + pow(smoothstep(0.55, 1.0, nMacro), 3.0) * 0.15;

    // ---- junction suppression & bezier corner fillet curves ----------------
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
            pavementSuppression = 1.0;
        }
    }

    float joinCurb = 0.0;
    for (int j = 0; j < 32; ++j) {
        if (j >= u_joinStrokeCount) break;
        int base = j * 4;
        vec2 c0 = u_joinCurvePoints[base + 0].xy;
        vec2 c1 = u_joinCurvePoints[base + 1].xy;
        vec2 c2 = u_joinCurvePoints[base + 2].xy;
        vec2 c3 = u_joinCurvePoints[base + 3].xy;
        float halfWidth = u_joinStrokeInfo[j].x;
        // A bezier stays inside the hull of its control points, so a pixel outside that box
        // (grown by the stroke half-width and its AA ramp) cannot be covered: skip the
        // 16-segment distance search, which is the expensive part of a junction pixel.
        float reach = halfWidth + 0.04;
        vec2 lo = min(min(c0, c1), min(c2, c3)) - reach;
        vec2 hi = max(max(c0, c1), max(c2, c3)) + reach;
        if (wxz.x < lo.x || wxz.x > hi.x || wxz.y < lo.y || wxz.y > hi.y) continue;
        float d = bezierStrokeDistance(wxz, c0, c1, c2, c3);
        float coverage = 1.0 - smoothstep(halfWidth - 0.035, halfWidth + 0.035, d);
        joinCurb = max(joinCurb, coverage);
    }

    // Sample textures with metric UV (consistent real-world scale everywhere)
    vec2 cobbleUV  = MetricUV / (roadSurfaceScale * COBBLE_SCALE_MUL);
    vec2 pavingUV  = MetricUV / (roadPavingScale * COBBLE_SCALE_MUL);
    vec2 sidewalkUV = MetricUV / (roadSidewalkScale * BRICK_SCALE_MUL);

    vec3 colCobble = (roadSurfaceValid != 0)
        ? texture(roadSurfaceTex, cobbleUV).rgb * roadSurfaceGain * roadSurfaceTint
        : vec3(0.55, 0.52, 0.48);
    // Average colour of the texture (very high mip). Needs mipmaps on the texture; without
    // them this simply returns the raw texel and the dust model degrades gracefully.
    vec3 colCobbleAvg = (roadSurfaceValid != 0)
        ? textureLod(roadSurfaceTex, cobbleUV, 16.0).rgb * roadSurfaceGain * roadSurfaceTint
        : colCobble;
    vec3 normCobble = (roadNormalValid != 0)
        ? texture(roadNormalTex, cobbleUV).xyz * 2.0 - 1.0
        : vec3(0.0, 0.0, 1.0);

    vec3 colCurb = (roadCurbValid != 0)
        ? texture(roadCurbTex, MetricUV / roadCurbScale).rgb
        : vec3(0.65, 0.62, 0.58);
#if MATCH_REFERENCE
    if (roadCurbValid != 0)
    {
        vec3 curbAvg = textureLod(roadCurbTex, MetricUV / roadCurbScale, 16.0).rgb;
        // Soften the contrast first: the scan has black soot stains and bright decals that read as
        // holes in the kerb.
        colCurb = matchMean(mix(curbAvg, colCurb, CURB_CONTRAST), curbAvg, REF_CURB);
    }
    else
        colCurb = REF_CURB;
#endif
    vec3 normCurb = (roadCurbNormalValid != 0)
        ? texture(roadCurbNormalTex, MetricUV / roadCurbScale).xyz * 2.0 - 1.0
        : vec3(0.0, 0.0, 1.0);

    // Herringbone brick sidewalk — the scan is a dark photogrammetry albedo, so a
    // strong linear lift is needed or the band collapses into near-black maroon under
    // the scene's dim ambient and reads as "no sidewalk". Lift toward warm terracotta.
#if MATCH_REFERENCE
    // Calibrated: the average becomes REF_BRICK, so the old fixed x2.45 lift is not needed.
    vec3 colSidewalk = REF_BRICK;
    if (roadSidewalkValid != 0) {
        vec2 swUV = sidewalkUV;
        colSidewalk = matchMean(texture(roadSidewalkTex, swUV).rgb,
                                textureLod(roadSidewalkTex, swUV, 16.0).rgb, REF_BRICK);
    }
#else
    vec3 colSidewalk = (roadSidewalkValid != 0)
        ? texture(roadSidewalkTex, sidewalkUV).rgb * 2.45
        : vec3(0.78, 0.50, 0.36); // warm terracotta fallback
#endif
    colSidewalk = clamp(colSidewalk, 0.0, 1.0);
    vec3 normSidewalk = (roadSidewalkNormalValid != 0)
        ? texture(roadSidewalkNormalTex, sidewalkUV).xyz * 2.0 - 1.0
        : vec3(0.0, 0.0, 1.0);

    vec3 colPaving = (roadPavingValid != 0)
        ? texture(roadPavingTex, pavingUV).rgb * roadSurfaceGain * roadSurfaceTint
        : vec3(0.62, 0.60, 0.56);
    vec3 colPavingAvg = (roadPavingValid != 0)
        ? textureLod(roadPavingTex, pavingUV, 16.0).rgb * roadSurfaceGain * roadSurfaceTint
        : colPaving;
#if MATCH_REFERENCE
    colCobble = matchMean(colCobble, colCobbleAvg, REF_COBBLE);   colCobbleAvg = REF_COBBLE;
    colPaving = matchMean(colPaving, colPavingAvg, REF_COBBLE);   colPavingAvg = REF_COBBLE;
#endif
    vec3 normPaving = (roadPavingNormalValid != 0)
        ? texture(roadPavingNormalTex, pavingUV).xyz * 2.0 - 1.0
        : vec3(0.0, 0.0, 1.0);

    vec3 colDirt = (roadDirtValid != 0)
        ? texture(roadDirtTex, MetricUV / roadDirtScale).rgb
        : vec3(0.50, 0.44, 0.33);
    vec3 normDirt = (roadDirtNormalValid != 0)
        ? texture(roadDirtNormalTex, MetricUV / roadDirtScale).xyz * 2.0 - 1.0
        : vec3(0.0, 0.0, 1.0);

    vec3 colSand = (roadSandValid != 0)
        ? texture(roadSandTex, MetricUV / roadSandScale).rgb
        : vec3(0.72, 0.66, 0.54);
    vec3 normSand = (roadSandNormalValid != 0)
        ? texture(roadSandNormalTex, MetricUV / roadSandScale).xyz * 2.0 - 1.0
        : vec3(0.0, 0.0, 1.0);

    // ---- procedural stone / brick (see the PROCEDURAL_* switches at the top) ----------
    float cobbleJoint    = -1.0;           // < 0 : joints come from texture luma
    float cobbleContrast = COBBLE_CONTRAST;
#if PROCEDURAL_COBBLE
    if (type == 0 || type == 1 || type == 4 || type == 5) {
        vec3 pn;
        colCobble     = proceduralCobble(MetricUV, cobbleJoint, pn);
        colCobbleAvg  = vec3(0.62, 0.57, 0.48);
        // procedural detail is finer than a texel: fade it out before it starts to alias
        float pf = smoothstep(4.0, 22.0, dist);
        colCobble     = mix(colCobble, colCobbleAvg * 0.92, pf);
        cobbleJoint   = mix(cobbleJoint, 0.30, pf);
        pn            = normalize(mix(pn, vec3(0.0, 0.0, 1.0), pf));
        normCobble    = pn;
        colPaving     = colCobble;  colPavingAvg = colCobbleAvg;  normPaving = pn;
        cobbleContrast = 1.0;
    }
#endif
#if PROCEDURAL_BRICK
    if (type == 0 || type == 1) {
        vec3 bn;
        float mortarM, brickId;
        vec3 pbCol = proceduralBrick(MetricUV, bn, mortarM, brickId);
        float bf = smoothstep(7.0, 22.0, dist);
#if PROCEDURAL_BRICK == 2
        // Hybrid: the texture supplies the brick colour (already calibrated to REF_BRICK), the shader
        // supplies the joints, per-brick tint and relief.
        vec3 hybCol = mix(colSidewalk * (0.86 + 0.28 * brickId), vec3(0.46, 0.41, 0.36), mortarM * 0.85);
        colSidewalk  = mix(hybCol, colSidewalk, bf);
#else
        colSidewalk  = mix(pbCol, vec3(0.66, 0.35, 0.23), bf);
#endif
        normSidewalk = normalize(mix(bn, vec3(0.0, 0.0, 1.0), bf));
    }
#endif

    vec3 albedo;
    vec3 finalNorm;
    float gloss = 0.08;
    float dustAmt = 0.0;      // how dusty / matte the final surface is (drives normals, gloss, wrap light)
    float sidewalkMask = 0.0; // set below; used to suppress shoulder sand-fade over brick

    bool isColonial = (type == 0);
    bool isUrban    = (type == 1);
    bool isCountryH = (type == 2);
    bool isDirt     = (type == 3);
    bool isPort     = (type == 4);
    bool isMedina   = (type == 5);

    if (isUrban || isColonial) {
        // ---------------- HISTORICAL PAVED CITY STREETS ---------------------
        // Saint Denis look: dust-veiled cobbles, dark chunky curb with a worn lighter
        // top edge, sand drifted against the curb foot, clean-ish red herringbone brick.

        // Matches RoadSpline::kSidewalkWidthU: the ribbon lifts the outer band to a
        // curb height over exactly this span, so the brick must land on the raised
        // geometry. The curb face sits at swWidth.
        float swWidth = 0.15;
        float swLeft  = 1.0 - smoothstep(swWidth - 0.002, swWidth + 0.002, u);
        float swRight = smoothstep(1.0 - swWidth - 0.002, 1.0 - swWidth + 0.002, u);
        float sidewalk = clamp(swLeft + swRight, 0.0, 1.0) * paint * (1.0 - pavementSuppression);

        // Signed distance (in u) from the nearest curb line: > 0 on the carriageway, < 0 on the sidewalk side
        float dStreet  = min(u - swWidth, (1.0 - swWidth) - u);
        float onStreet = step(0.0, dStreet);

        // Stone curb separating sidewalk from street (slightly chunkier than before)
        // Narrow kerb: top face just inside the sidewalk edge + the vertical face at the street.
        float curbLeft  = mark(u, swWidth - 0.006, 0.011, 0.003);
        float curbRight = mark(u, 1.0 - swWidth + 0.006, 0.011, 0.003);
        float curb = clamp(curbLeft + curbRight, 0.0, 1.0) * paint * (1.0 - pavementSuppression);
        curb = max(curb, joinCurb);

        // Wagon / cart wheel tracks (dust is pushed away here)
        float rut = (smoothstep(0.12, 0.02, abs(u - 0.35)) +
                     smoothstep(0.12, 0.02, abs(u - 0.65))) * paint;

        // ---- carriageway: dust-covered cobbles -------------------------------
        vec3 rawCar  = isColonial ? mix(colCobble,    colPaving,    0.55) : colCobble;
        vec3 avgCar  = isColonial ? mix(colCobbleAvg, colPavingAvg, 0.55) : colCobbleAvg;
        vec3 carriagewayNorm = isColonial ? mix(normCobble, normPaving, 0.55) : normCobble;

        float carDust;
        vec3 carriagewayCol = dustyStone(rawCar, avgCar, cobbleJoint, cobbleContrast, mMid, mMacro, mFine,
                                         clamp(rut, 0.0, 1.0), farFade, 1.0, carDust);

        // Sand / silt drifted against the curb foot (irregular, noise-modulated)
        float drift = (1.0 - smoothstep(0.0, 0.11, dStreet)) * onStreet;
        drift = clamp(drift * (0.35 + 1.0 * mMid), 0.0, 1.0) * paint;
        vec3 driftCol = mix(colSand, DUST_COL, 0.5) * 0.98;
        carriagewayCol  = mix(carriagewayCol, driftCol, drift * 0.75);
        carriagewayNorm = mix(carriagewayNorm, normSand, drift * 0.60);
        carDust = max(carDust, drift * 0.90);

        // Contact shadow where the street meets the curb face
        float contactAO = (1.0 - smoothstep(0.0, 0.05, dStreet)) * onStreet;
        carriagewayCol *= 1.0 - 0.38 * contactAO * paint;

        // Fine sand grain and a few scattered pebbles (fade out with distance)
        carriagewayCol *= 1.0 - (1.0 - farFade) * 0.05 * (1.0 - valueNoise(wxz * 90.0));
        vec4 peb = pebbles(wxz);
        carriagewayCol = mix(carriagewayCol, peb.rgb, peb.a * paint * (1.0 - smoothstep(8.0, 30.0, dist)));

        // Dust deadens the surface; flatten the stone relief under it
        carriagewayNorm = mix(carriagewayNorm, vec3(0.0, 0.0, 1.0), carDust * 0.92);

        albedo    = carriagewayCol;
        finalNorm = carriagewayNorm;
        dustAmt   = carDust;
        gloss     = mix(0.12, 0.18, rut * 0.6);

        // ---- curb: dark weathered stone ---------------------------------------
        float lip = clamp(mark(u, swWidth - 0.006, 0.004, 0.003) +
                          mark(u, 1.0 - swWidth + 0.006, 0.004, 0.003), 0.0, 1.0);
#if PROCEDURAL_CURB
        vec3 curbCol = proceduralCurb(wxz);
#else
        vec3 curbCol = colCurb * CURB_TINT;
#endif
        curbCol  = mix(curbCol, DUST_COL * 0.7, 0.05 + 0.08 * mMid);    // thin dust film
        curbCol *= 0.92 + 0.16 * mFine;
        curbCol += vec3(0.05, 0.05, 0.045) * lip;                        // worn, lighter top arris
        // foot of the curb face is dirtier / darker
        curbCol *= 1.0 - 0.25 * (1.0 - smoothstep(0.0, 0.010, dStreet)) * onStreet;

        albedo    = mix(albedo, curbCol, curb);
        finalNorm = mix(finalNorm, normCurb, curb);
        gloss     = mix(gloss, 0.10, curb);
        dustAmt   = mix(dustAmt, 0.15, curb);

        // ---- brick sidewalk (outside the curb) --------------------------------
        float activeSidewalk = sidewalk * (1.0 - curb);

        vec3 brick = colSidewalk;
        brick *= mix(0.90, 1.08, mMacro);                       // breaks up tiling, age mottling
        brick  = mix(brick, DUST_COL, 0.03 + 0.07 * mMid);      // thin dust film, brick stays red
        float swEdge = 1.0 - smoothstep(0.0, 0.07, edgeDist);   // dirt creeping in at the outer edge
        brick  = mix(brick, colSand * 0.92, swEdge * 0.30 * (0.5 + mFine));

        albedo    = mix(albedo, brick, activeSidewalk);
        // Blend normals at reduced strength so extreme brick relief doesn't face
        // away from the sun and darken the whole zone, but keep most of the herringbone
        // so the raised sidewalk reads as brick rather than a flat colour plane.
        vec3 normSwReduced = normalize(mix(vec3(0.0,0.0,1.0), normSidewalk, 0.70));
        finalNorm = mix(finalNorm, normSwReduced, activeSidewalk);
        gloss     = mix(gloss, 0.09, activeSidewalk); // slight brick-joint sheen
        dustAmt   = mix(dustAmt, 0.12, activeSidewalk);
        sidewalkMask = activeSidewalk; // protect from shoulder sand-fade below

    } else if (isPort) {
        // ---------------- HISTORICAL PORT / WHARFSIDE -----------------------
        // Heavy flagstones and commercial pier stone paving bordered by cut stone curbs
        float curb = clamp(mark(u, 0.04, 0.035, 0.008) + mark(u, 0.96, 0.035, 0.008), 0.0, 1.0) * paint;
        curb = max(curb, joinCurb);

        float pDust;
        albedo = dustyStone(colPaving, colPavingAvg, cobbleJoint, cobbleContrast, mMid, mMacro, mFine, 0.0, farFade, 0.85, pDust);
        finalNorm = mix(normPaving, vec3(0.0, 0.0, 1.0), pDust * 0.40);
        albedo = mix(albedo, colCurb * CURB_TINT, curb);
        finalNorm = mix(finalNorm, normCurb, curb);

        // Salt and sand deposits in joints
        float saltSand = clamp(dust * 0.45 + (1.0 - edgeAlpha) * 0.6, 0.0, 1.0);
        albedo = mix(albedo, colSand * 0.92, saltSand);
        finalNorm = mix(finalNorm, normSand, saltSand * 0.5);
        dustAmt = max(pDust * (1.0 - curb), saltSand * 0.6);
        gloss = 0.10;

    } else if (isMedina) {
        // ---------------- HISTORIC MEDINA ALLEYWAY --------------------------
        // Ancient flagstones and brick paving with dusty edges against walls
        vec3 rawMed = mix(colPaving, colCobble, 0.35);
        vec3 avgMed = mix(colPavingAvg, colCobbleAvg, 0.35);
        float mDust;
        albedo = dustyStone(rawMed, avgMed, cobbleJoint, cobbleContrast, mMid, mMacro, mFine, 0.0, farFade, 1.1, mDust);
        finalNorm = mix(mix(normPaving, normCobble, 0.35), vec3(0.0, 0.0, 1.0), mDust * 0.40);

        float edgeSand = clamp((1.0 - edgeAlpha) * 0.85 + (nFine - 0.5) * 0.20, 0.0, 1.0);
        albedo = mix(albedo, colSand, edgeSand);
        finalNorm = mix(finalNorm, normSand, edgeSand * 0.7);
        dustAmt = max(mDust, edgeSand * 0.7);
        gloss = 0.08;

    } else {
        // ---------------- COUNTRY HIGHWAY & CART DIRT TRACK -----------------
        // Unpaved rural roads: packed earth, muddy cart ruts, sun-baked sand crown
        float rutLeft = smoothstep(0.14, 0.03, abs(u - 0.30));
        float rutRight = smoothstep(0.14, 0.03, abs(u - 0.70));
        float rut = clamp(rutLeft + rutRight, 0.0, 1.0);

        albedo = colDirt;
        finalNorm = normDirt;

        // Compacted damp dirt in wheel depressions
        albedo = mix(albedo, albedo * 0.72, rut * 0.75);

        // Center crown & verges: dry sand and powdery earth
        float crownDust = smoothstep(0.35, 0.75, nMacro) * (1.0 - rut * 0.8) * 0.35 +
                          (1.0 - edgeAlpha) * 0.70;
        crownDust = clamp(crownDust, 0.0, 1.0);
        albedo = mix(albedo, colSand, crownDust);
        finalNorm = mix(finalNorm, normSand, crownDust * 0.75);

        dustAmt = 0.55;
        gloss = mix(0.04, 0.14, rut * 0.7);
    }

    // Outer shoulder fade: softly merge extreme road edges with natural sand.
    // Suppressed over the brick sidewalk so herringbone stays crisp and doesn't
    // blend into the sand shoulder colour that produces the "brown dirt" look.
    // The threshold is noise-warped so the road edge is ragged instead of a clean line.
    float edgeRagged = edgeDist + (nFine - 0.44) * 0.04;
    float shoulderFade = 1.0 - smoothstep(0.005, 0.045, edgeRagged);
    float fadeMix = shoulderFade * 0.65 * (1.0 - sidewalkMask * 0.95);
    albedo    = mix(albedo,    colSand, fadeMix);
    finalNorm = mix(finalNorm, normSand, fadeMix);

    // Dry grass tufts poking through at the road edge (as in the reference screenshot)
    float tuftGate = (isDirt || isCountryH) ? 1.0 : paint;
    float tuftN    = valueNoise(wxz * 11.0) * 0.6 + valueNoise(wxz * 27.0) * 0.4;
    float tuft     = smoothstep(0.58, 0.78, tuftN) * smoothstep(0.0, 0.5, shoulderFade)
                   * (1.0 - sidewalkMask) * tuftGate;
    vec3  grassCol = mix(vec3(0.34, 0.36, 0.17), vec3(0.58, 0.52, 0.28), valueNoise(wxz * 5.0));
    albedo = mix(albedo, grassCol, tuft * 0.55 * TUFT_AMOUNT);

    // ---- lighting & surface bump perturbation -----------------------------
    // Blend in the true face normal across the kerb band (urban / colonial streets only)
    float kerbDist = min(abs(u - 0.15), abs(u - 0.85));
    float kerbBand = (1.0 - smoothstep(0.0, 0.05, kerbDist)) * paint * ((type <= 1) ? 1.0 : 0.0);
    vec3 N0 = normalize(mix(normalize(Normal), Ngeo, kerbBand));   // geometric normal
    vec3 N  = N0;
    // Reference axis switches when N is nearly along X (the kerb face), where the old
    // normalize(vec3(1,0,0) - N * N.x) collapses to a zero vector -> NaN -> black.
    vec3 refAxis = (abs(N.x) < 0.9) ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 0.0, 1.0);
    vec3 T = normalize(refAxis - N * dot(refAxis, N));
    vec3 B = cross(N, T);
    // relief fades with distance (no shimmering, softer horizon like the screenshot)
    float nStrength = roadNormalStrength * (1.0 - 0.75 * farFade);
    vec3 perturbed = normalize(T * finalNorm.x * nStrength +
                               B * finalNorm.y * nStrength +
                               N * max(finalNorm.z, 0.05));
    N = normalize(mix(N, perturbed, 0.85));

    vec3 L = normalize(lightDir);
    vec3 V = normalize(viewPos - FragPos);
    vec3 H = normalize(L + V);
    float ndl = dot(N, L);

    // Dusty surfaces get a soft wrapped diffuse (powdery, low contrast); clean stone stays crisp
    float lambertHard = max(ndl, 0.0);
    float lambertWrap = clamp((ndl + WRAP_AMOUNT) / (1.0 + WRAP_AMOUNT), 0.0, 1.0);
    float lambert = mix(lambertHard, lambertWrap, dustAmt * 0.8);

    float spec = pow(max(dot(N, H), 0.0), 36.0);
    gloss *= 1.0 - 0.80 * dustAmt;                   // dust is matte

    vec3 finalColor = albedo * (AMBIENT_COL + SUN_TINT * lambert * 0.58);
    finalColor += vec3(0.24, 0.22, 0.20) * lambertHard * spec * gloss;

    // Forward-scattered dust glow at grazing angles (the pale sheen far down the street)
    float graze = pow(1.0 - clamp(dot(N0, V), 0.0, 1.0), 3.0);
    float sunSide = 0.5 + 0.5 * clamp(dot(N0, L), 0.0, 1.0);
    finalColor += DUST_COL * SUN_TINT * graze * sunSide * dustAmt * 0.10;

    // Optional atmospheric haze
    float haze = 1.0 - exp(-HAZE_DENSITY * dist);
    finalColor = mix(finalColor, HAZE_COL, clamp(haze, 0.0, 1.0));

    // UV inspection mode
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

    if (u_debugView == 1) { FragColor = vec4(albedo, 1.0);               return; }
    if (u_debugView == 2) { FragColor = vec4(N * 0.5 + 0.5, 1.0);        return; }
    if (u_debugView == 3) { FragColor = vec4(vec3(lambert), 1.0);        return; }

    if (any(isnan(finalColor)) || any(isinf(finalColor))) {
#if DEBUG_NAN_PINK
        finalColor = vec3(1.0, 0.0, 1.0);
#else
        finalColor = albedo * 0.5;
#endif
    }
    FragColor = vec4(finalColor, 1.0);
}
