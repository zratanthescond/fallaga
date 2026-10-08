#version 330 core
out vec4 FragColor;
in vec3 WorldPos, Normal;
in vec2 UV;

uniform sampler2DArray terrainTex;
uniform sampler2DArray terrainNormal;
uniform sampler2D portMask;
uniform sampler2D roadMask;
uniform sampler2D splineRoadMask;
uniform sampler2D splatmap1;
uniform sampler2D splatmap2;
uniform sampler2D terrainSplatmap;

uniform vec3 viewPos, lightDir, lightColor;
uniform float sunTilt;
uniform float time;
uniform bool hasRoadMask;
uniform bool hasSplineRoadMask;
uniform float waterLevel;
uniform int activePaintLayerIdx;
// Distance beyond which one of the three base ground layers is dropped. It is a
// 0.90-1.15-scale variant of the lush grass, so the difference only reads at close
// range; dropping it saves 2 of the 6 base fetches over most of the screen.
// Overridable at runtime so the cost/quality point can be measured, not guessed.
uniform float u_farLodDistance;
// Brightness applied to the roadbed texture on the terrain, so the graded roadbed
// matches the road ribbon drawn over it instead of forming a dark outline round it.
// The gravel scan is authored dark (mean sRGB ~0.25), so this multiplies UP.
uniform float u_roadArmGain;

// --- UTILITIES ---
// Non-periodic Dave Hoskins hash (clean, no repeating lattice)
float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Band-limited smooth value noise
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1.0, 0.0)), f.x),
               mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), f.x), f.y);
}

// Sample a terrain layer with triplanar mapping (for steep cliff walls).
//
// Only the axes that actually contribute are sampled. On flat ground the blend
// weights collapse to the Y term, so the X and Z fetches are pure waste -- and the
// cliff and harbour branches call this four times per fragment (colour and normal for
// two layers), which is 12 array fetches even where a single projection would do.
// The 0.05 test is well below the smallest weight that changes a visible result.
vec3 sampleTriplanar(sampler2DArray tex, vec3 worldPos, float layer, vec3 blendW, float scale) {
    vec3 cY = texture(tex, vec3(worldPos.xz * scale, layer)).rgb;
    vec3 acc = cY * blendW.y;
    if (blendW.x > 0.05) acc += texture(tex, vec3(worldPos.yz * scale, layer)).rgb * blendW.x;
    if (blendW.z > 0.05) acc += texture(tex, vec3(worldPos.xy * scale, layer)).rgb * blendW.z;
    return acc;
}

void main()
{
    vec3 N = normalize(Normal);
    vec3 V = normalize(viewPos - WorldPos);
    float dist = length(viewPos - WorldPos);

    vec4 portVals = texture(portMask, UV);
    float quayMask = portVals.r;       // Quays, Wharfs, Esplanade
    float bwMask   = portVals.g;       // Breakwater crest & moles
    float ripMask  = portVals.b;       // Breakwater boulder riprap
    bool isHarbor  = (quayMask > 0.05 || bwMask > 0.05 || ripMask > 0.05);

    // Exact slope in degrees: 0° = completely flat horizontal, 90° = vertical wall
    float angleDeg = acos(clamp(N.y, -1.0, 1.0)) * 57.2958;
    float alt = WorldPos.y;
    float specMask = 0.03; // Natural matte earth/grass default

    // Distance LOD fades to eliminate moire aliasing while keeping razor-sharp close-up detail
    float nearFade = clamp(1.0 - dist / 45.0, 0.0, 1.0);  // 0 to 45m: micro texture detail

    // Primary texture mapping coordinates
    float baseScale = 0.14; // ~7m repeat
    vec2 baseUV = WorldPos.xz * baseScale;
    vec2 microUV = WorldPos.xz * 0.55; // High-frequency close-up detail

    // Triplanar blend weights for steep slopes
    vec3 triW = abs(N);
    triW = max(triW - 0.25, 0.0);
    triW /= max(dot(triW, vec3(1.0)), 0.0001);

    // ── 1. BASE LANDSCAPE: AUTHENTIC MEDITERRANEAN / SAHEL SAVANNA ──
    // Layer 1: Clumpy Grass Tufts on Warm Earth (grp_dirt_grass_01)
    vec3 colClumpyGrass = texture(terrainTex, vec3(baseUV, 1.0)).rgb;
    vec3 nrmClumpyGrass = texture(terrainNormal, vec3(baseUV, 1.0)).rgb * 2.0 - 1.0;

    // Layer 2: Lush Prairie Grass (hea_grass_lush_je_02). Cheap distance LOD: beyond
    // u_farLodDistance this layer is a near-duplicate of layer 1 at a slightly
    // different scale, so it is not sampled at all and its weight folds into layer 1.
    bool nearEnoughForSecondLayer = dist < u_farLodDistance;
    vec3 colLushGrass = vec3(0.0), nrmLushGrass = vec3(0.0, 0.0, 1.0);
    if (nearEnoughForSecondLayer) {
        colLushGrass = texture(terrainTex, vec3(baseUV * 1.15, 2.0)).rgb;
        nrmLushGrass = texture(terrainNormal, vec3(baseUV * 1.15, 2.0)).rgb * 2.0 - 1.0;
    }

    // Layer 3: Warm Loam Soil (hen_mud_soil_01)
    vec3 colLoamSoil    = texture(terrainTex, vec3(baseUV * 0.90, 3.0)).rgb;
    vec3 nrmLoamSoil    = texture(terrainNormal, vec3(baseUV * 0.90, 3.0)).rgb * 2.0 - 1.0;

    // Smooth organic biome variation (low frequencies only - zero moire artifacts)
    float nMacro1 = noise(WorldPos.xz * 0.0035);
    float nMacro2 = noise(WorldPos.xz * 0.0100 + vec2(28.4, 61.2));
    float biomeNoise = nMacro1 * 0.65 + nMacro2 * 0.35;

    // Rich landscape blend:
    // Rolling hills are dominated by vibrant olive-green grass (Layer 2) with clumpy tufts (Layer 1)
    // and exposed warm loam soil (Layer 3) in depressions and drier knolls
    float lushWeight = smoothstep(0.30, 0.70, biomeNoise);
    if (!nearEnoughForSecondLayer) {
        // Fold the dropped layer's weight into layer 1 instead of mixing toward the
        // black that an unsampled fetch leaves behind -- otherwise distant terrain
        // would darken rather than just lose a near-duplicate detail layer.
        colLushGrass = colClumpyGrass;
        nrmLushGrass = nrmClumpyGrass;
    }
    vec3 colGrass = mix(colLushGrass, colClumpyGrass, (1.0 - lushWeight) * 0.40);
    vec3 nrmGrass = mix(nrmLushGrass, nrmClumpyGrass, (1.0 - lushWeight) * 0.40);

    float soilWeight = smoothstep(0.18, 0.42, 1.0 - biomeNoise) * 0.28;
    vec3 colGround = mix(colGrass, colLoamSoil, soilWeight);
    vec3 nrmGround = mix(nrmGrass, nrmLoamSoil, soilWeight);

    // Micro-detail texture overlay (blades of grass, soil pebbles) ONLY at close range (< 45m)
    if (nearFade > 0.01) {
        vec3 microTex  = texture(terrainTex, vec3(microUV, 2.0)).rgb;
        vec3 microNorm = texture(terrainNormal, vec3(microUV, 2.0)).rgb * 2.0 - 1.0;
        colGround *= mix(vec3(1.0), microTex * 1.35 + 0.30, nearFade * 0.35);
        nrmGround = normalize(mix(nrmGround, nrmGround + microNorm * 0.45, nearFade));
    }

    // Macro tone modulation (prevents visible tiling across distant vistas)
    float macroTone = noise(WorldPos.xz * 0.0012);
    colGround *= mix(vec3(0.96, 0.99, 0.94), vec3(1.05, 1.02, 0.96), macroTone);

    vec3 col = colGround;
    vec3 sampledNorm = nrmGround;

    // DEM-aligned material weights: R sand, G rock, B wadi gravel, A dry plain.
    // World-space tiling is about 4 m, with the masks aligned to the heightmap UVs.
    vec4 splatWeights = texture(terrainSplatmap, UV);
    float splatTotal = dot(splatWeights, vec4(1.0));
    splatWeights = splatTotal > 0.001
        ? splatWeights / splatTotal
        : vec4(0.0, 0.0, 0.0, 1.0);

    const float splatScale = 0.25;
    vec3 splatColor = vec3(0.0);
    vec3 splatNormal = vec3(0.0);
    if (splatWeights.r > 0.01) {
        splatColor += sampleTriplanar(terrainTex, WorldPos, 0.0, triW, splatScale) * splatWeights.r;
        splatNormal += (sampleTriplanar(terrainNormal, WorldPos, 0.0, triW, splatScale) * 2.0 - 1.0) * splatWeights.r;
    }
    if (splatWeights.g > 0.01) {
        vec3 screeColor = sampleTriplanar(terrainTex, WorldPos, 9.0, triW, splatScale);
        vec3 limeColor = sampleTriplanar(terrainTex, WorldPos, 4.0, triW, splatScale);
        vec3 screeNormal = sampleTriplanar(terrainNormal, WorldPos, 9.0, triW, splatScale) * 2.0 - 1.0;
        vec3 limeNormal = sampleTriplanar(terrainNormal, WorldPos, 4.0, triW, splatScale) * 2.0 - 1.0;
        float rockMix = smoothstep(8.0, 22.0, angleDeg);
        splatColor += mix(screeColor, limeColor, rockMix) * splatWeights.g;
        splatNormal += mix(screeNormal, limeNormal, rockMix) * splatWeights.g;
    }
    if (splatWeights.b > 0.01) {
        vec3 gravelColor = sampleTriplanar(terrainTex, WorldPos, 5.0, triW, splatScale);
        vec3 siltColor = sampleTriplanar(terrainTex, WorldPos, 6.0, triW, splatScale);
        vec3 gravelNormal = sampleTriplanar(terrainNormal, WorldPos, 5.0, triW, splatScale) * 2.0 - 1.0;
        vec3 siltNormal = sampleTriplanar(terrainNormal, WorldPos, 6.0, triW, splatScale) * 2.0 - 1.0;
        float wadiMix = noise(WorldPos.xz * 0.035 + vec2(17.0, 41.0));
        splatColor += mix(gravelColor, siltColor, wadiMix) * splatWeights.b;
        splatNormal += mix(gravelNormal, siltNormal, wadiMix) * splatWeights.b;
    }
    if (splatWeights.a > 0.01) {
        splatColor += sampleTriplanar(terrainTex, WorldPos, 3.0, triW, splatScale) * splatWeights.a;
        splatNormal += (sampleTriplanar(terrainNormal, WorldPos, 3.0, triW, splatScale) * 2.0 - 1.0) * splatWeights.a;
    }
    col = mix(colGround, splatColor, 0.72);
    sampledNorm = normalize(mix(nrmGround, normalize(splatNormal), 0.72));

    // ── 2. GEOLOGICAL SLOPE EROSION & ROCK CLIFF STRATA ──
    // A. Moderate slopes (12° to 24°): topsoil erodes, exposing stony scree talus
    float screeFactor = smoothstep(11.0, 20.0, angleDeg) * (1.0 - smoothstep(22.0, 32.0, angleDeg));
    if (screeFactor > 0.01) {
        vec3 colScree = texture(terrainTex, vec3(baseUV * 1.2, 9.0)).rgb; // Layer 9: Scree talus
        vec3 nrmScree = texture(terrainNormal, vec3(baseUV * 1.2, 9.0)).rgb * 2.0 - 1.0;
        col = mix(col, colScree, screeFactor * 0.85);
        sampledNorm = mix(sampledNorm, nrmScree, screeFactor * 0.85);
    }

    // B. Steep cliffs & bluffs (> 20°): sheer exposed sedimentary rock strata
    float cliffFactor = smoothstep(18.0, 30.0, angleDeg);
    if (cliffFactor > 0.01) {
        vec3 colCliffStrata = sampleTriplanar(terrainTex, WorldPos, 11.0, triW, baseScale * 0.65); // Layer 11: Cliff scan
        vec3 nrmCliffStrata = sampleTriplanar(terrainNormal, WorldPos, 11.0, triW, baseScale * 0.65) * 2.0 - 1.0;
        vec3 colLimestone   = sampleTriplanar(terrainTex, WorldPos, 4.0, triW, baseScale * 0.80);  // Layer 4: Limestone
        vec3 nrmLimestone   = sampleTriplanar(terrainNormal, WorldPos, 4.0, triW, baseScale * 0.80) * 2.0 - 1.0;

        // Horizontal sedimentary bedding planes
        float strata = sin(WorldPos.y * 0.70) * 0.35 + sin(WorldPos.y * 1.9) * 0.15 + 0.5;
        vec3 colRock = mix(colCliffStrata, colLimestone, clamp(strata, 0.0, 1.0));
        vec3 nrmRock = mix(nrmCliffStrata, nrmLimestone, clamp(strata, 0.0, 1.0));

        col = mix(col, colRock, cliffFactor);
        sampledNorm = mix(sampledNorm, nrmRock, cliffFactor);
    }

    // ── 3. COASTAL SAND TRANSITION (near sea level along the open eastern shoreline) ──
    float coastFactor = (1.0 - smoothstep(waterLevel + 0.2, waterLevel + 1.8, alt)) * smoothstep(3200.0, 3700.0, WorldPos.x);
    if (coastFactor > 0.01 && !isHarbor) {
        vec3 colCoastSand = texture(terrainTex, vec3(baseUV * 0.9, 0.0)).rgb; // Layer 0: Coastal Sand
        vec3 nrmCoastSand = texture(terrainNormal, vec3(baseUV * 0.9, 0.0)).rgb * 2.0 - 1.0;
        col = mix(col, colCoastSand, coastFactor);
        sampledNorm = mix(sampledNorm, nrmCoastSand, coastFactor);
    }

    // ── 4. HISTORIC PORT, QUAYS & BREAKWATER TEXTURING ──
    if (isHarbor) {
        // --- A. Textures and Scales ---
        // Layer 12: Port Cobblestone Paving (authentic street/wharf stone pavers)
        // Scale 0.22: ~4.5m repeat, authentic human scale for Mediterranean harbor cobblestones
        vec2 cobbleUV = WorldPos.xz * 0.22;
        vec3 colCobble = texture(terrainTex, vec3(cobbleUV, 12.0)).rgb;
        vec3 nrmCobble = texture(terrainNormal, vec3(cobbleUV, 12.0)).rgb * 2.0 - 1.0;

        // Subtle macro-variation for cobblestone to break up repeating patterns across long wharfs
        float cobbleNoise = noise(WorldPos.xz * 0.06);
        colCobble *= mix(vec3(0.92, 0.90, 0.88), vec3(1.04, 1.02, 1.00), cobbleNoise);

        // Layer 13: Ashlar Stone Masonry Blocks (heavy weathered cut granite/limestone blocks)
        vec2 ashlarUV = WorldPos.xz * 0.16; // ~6.25m repeat for cut stone blocks
        vec3 colAshlarFlat = texture(terrainTex, vec3(ashlarUV, 13.0)).rgb;
        vec3 nrmAshlarFlat = texture(terrainNormal, vec3(ashlarUV, 13.0)).rgb * 2.0 - 1.0;

        // Triplanar Ashlar for vertical retaining dock walls dropping sheer into the harbor basin
        vec3 colAshlarTri = sampleTriplanar(terrainTex, WorldPos, 13.0, triW, 0.16);
        vec3 nrmAshlarTri = sampleTriplanar(terrainNormal, WorldPos, 13.0, triW, 0.16) * 2.0 - 1.0;

        // Realistic maritime weathered patina: warm aged Mediterranean limestone/granite
        float ashlarNoise = noise(WorldPos.xz * 0.04);
        vec3 ashlarTone = mix(vec3(0.86, 0.84, 0.80), vec3(0.96, 0.94, 0.90), ashlarNoise);
        colAshlarFlat *= ashlarTone;
        colAshlarTri  *= ashlarTone;

        // Layer 14: Breakwater Stones & Coastal Armor Rock (Poly Haven rock_wall_06)
        vec3 colRiprapTri = sampleTriplanar(terrainTex, WorldPos, 14.0, triW, 0.18);
        vec3 nrmRiprapTri = sampleTriplanar(terrainNormal, WorldPos, 14.0, triW, 0.18) * 2.0 - 1.0;

        // Subtle maritime salt and mineral tone variation for breakwater stones
        float ripNoise = noise(WorldPos.xz * 0.05);
        colRiprapTri *= mix(vec3(0.95, 0.93, 0.90), vec3(1.03, 1.01, 0.98), ripNoise);

        // --- B. Steep Retaining Walls vs Flat Ground ---
        // Slopes > 20° represent vertical constructed quay walls
        float wallFactor = smoothstep(18.0, 28.0, angleDeg);

        // --- C. Zone 1: Port Quays, Wharfs & Esplanade (quayMask) ---
        // Flat surface gets cobblestones; vertical quay wall faces get ashlar masonry blocks
        vec3 quayCol = mix(colCobble, colAshlarTri, wallFactor);
        vec3 quayNrm = mix(nrmCobble, nrmAshlarTri, wallFactor);

        // --- D. Zone 2: Breakwater Moles & Pierheads (bwMask) ---
        // Full breakwater structure is constructed from heavy rock_wall_06 stone masonry
        vec3 bwCol = colRiprapTri;
        vec3 bwNrm = nrmRiprapTri;

        // --- E. Zone 3: Breakwater Riprap Apron & Seawalls (ripMask) ---
        vec3 ripCol = colRiprapTri;
        vec3 ripNrm = nrmRiprapTri;

        // --- F. Composite Harbor Shading ---
        float totalHarborWeight = clamp(quayMask + bwMask + ripMask, 0.0, 1.0);
        float bwWeight = clamp(bwMask / max(totalHarborWeight, 0.001), 0.0, 1.0);
        float ripWeight = clamp(ripMask / max(totalHarborWeight, 0.001), 0.0, 1.0);
        float quayWeight = clamp(quayMask / max(totalHarborWeight, 0.001), 0.0, 1.0);

        vec3 harborCol = quayCol * quayWeight + bwCol * bwWeight + ripCol * ripWeight;
        vec3 harborNrm = quayNrm * quayWeight + bwNrm * bwWeight + ripNrm * ripWeight;

        // --- G. Wet Stone & Tide Splash Darkening ---
        // Stone quays and rocks within 1.2m of sea level are dampened by waves/spray
        float tideLine = waterLevel + 0.1;
        float wetFactor = clamp(1.0 - (alt - tideLine) / 1.4, 0.0, 1.0);
        if (wetFactor > 0.01) {
            harborCol *= mix(vec3(1.0), vec3(0.68, 0.65, 0.62), wetFactor * 0.55);
            specMask = mix(specMask, 0.35, wetFactor * 0.70);
        }

        col = mix(col, harborCol, smoothstep(0.05, 0.60, totalHarborWeight));
        sampledNorm = mix(sampledNorm, harborNrm, smoothstep(0.05, 0.60, totalHarborWeight));
    }

    vec3 worldNormal = normalize(N + vec3(sampledNorm.x, 0.0, sampledNorm.y) * 0.65);

    // ── 5. ROADS, RAILWAYS, AND REALISTIC ASYMMETRIC WADIS ──
    if (hasRoadMask || hasSplineRoadMask) {
        // High-frequency sub-texel organic domain warp in world meters
        // Completely breaks up the 3.43m square texel grid alignment!
        vec2 roadWarp = (vec2(
            noise(WorldPos.xz * 0.28 + vec2(1.7, 3.1)),
            noise(WorldPos.zx * 0.28 + vec2(4.3, 7.9))
        ) - 0.5) * 0.00032;
        vec2 roadUV = clamp(UV + roadWarp, 0.0, 1.0);
        vec4 maskVals = vec4(0.0);
        if (hasRoadMask)
            maskVals = texture(roadMask, roadUV);
        float splineRoadRaw = hasSplineRoadMask ? texture(splineRoadMask, UV).r : 0.0;

        // A. Custom Painted Layer (Alpha) - only if active brush layer is selected
        if (maskVals.a > 0.05 && activePaintLayerIdx > 0) {
            vec3 paintedCol = texture(terrainTex, vec3(baseUV, float(activePaintLayerIdx))).rgb;
            col = mix(col, paintedCol, clamp(maskVals.a * 1.5, 0.0, 1.0));
        }

        // B. PHOTOREALISTIC MULTI-TIER ROAD SYSTEM
        // Hybrid rendering: terrain splatting provides seamless ground foundation
        float paintedRoadRaw = maskVals.r;
        float rRaw = max(paintedRoadRaw, splineRoadRaw);
        if (rRaw > 0.01 && (alt > waterLevel + 0.3 || isHarbor)) {
            // 1. Multi-scale organic edge breakup (fractal micro-noise for natural dirt-grass boundary)
            vec2 roadNoiseUV = WorldPos.xz * 0.38;
            float nEdge1 = noise(roadNoiseUV * 0.8);
            float nEdge2 = noise(roadNoiseUV * 2.5 + vec2(1.7, 3.2));
            float nEdge3 = noise(roadNoiseUV * 6.8 + vec2(4.1, 7.3));
            float edgeNoise = (nEdge1 * 0.50 + nEdge2 * 0.35 + nEdge3 * 0.15 - 0.5);

            // Warp the distance gradient subtly and organically (avoiding jagged sawtooth spikes)
            float paintedRoadWarped = clamp(
                paintedRoadRaw + edgeNoise * 0.025 * smoothstep(0.01, 0.40, paintedRoadRaw),
                0.0, 1.0);
            // Generated road material uses the unwarped spline raster, whose
            // distance profile is shared with terrain grading. It takes priority
            // inside its footprint; painted map roads keep their own edge warp
            // everywhere outside generated roads.
            float rWarped = splineRoadRaw > 0.0 ? splineRoadRaw : paintedRoadWarped;

            // Overall road presence: soft shoulder starts at 0.06, full roadbed inside 0.24
            float roadFactor = smoothstep(0.06, 0.24, rWarped);

            if (roadFactor > 0.005) {
                bool isUrban = (maskVals.g > 0.55 || isHarbor);

                // --- 2. Dual-Track Wheel Ruts (Worn cart & vehicle tracks) ---
                // Symmetrically located at rWarped ~0.46 to 0.78
                float rutProfile = smoothstep(0.44, 0.58, rWarped) * (1.0 - smoothstep(0.70, 0.86, rWarped));
                float rutWiggle = (noise(WorldPos.xz * 0.55 + vec2(2.1, 8.4)) - 0.5) * 0.20;
                float rutFactor = clamp(rutProfile + rutWiggle * rutProfile, 0.0, 1.0);

                // --- 3. Center Crown / Median (between the two wheel ruts) ---
                float centerFactor = smoothstep(0.82, 0.98, rWarped);

                // --- 4. Gravel Shoulders & Flanks ---
                float shoulderFactor = smoothstep(0.06, 0.16, rWarped) * (1.0 - smoothstep(0.24, 0.44, rWarped));

                vec3 finalRoadCol;
                vec3 finalRoadNorm;
                float roadSpec;

                if (isUrban) {
                    // ── TYPE A: URBAN TARRED MACADAM ──
                    // Same gravel scan the road ribbon uses (layer 10), at a comparable
                    // brightness. This used to be deliberately darkened --
                    // colTar * (0.72,0.70,0.68) for the bed and * 0.78 for the shoulder --
                    // which made terrain roadbed visibly BLACKER than the ribbon drawn
                    // over it, so an urban street showed the ribbon as a lighter strip
                    // inside a dark outline. The gravel scan is already dark (mean sRGB
                    // ~0.25), so it needs a gain here, not a cut.
                    vec2 roadTexUV = WorldPos.xz * 0.18;
                    vec3 colTar = texture(terrainTex, vec3(roadTexUV, 10.0)).rgb;
                    vec3 nrmTar = texture(terrainNormal, vec3(roadTexUV, 10.0)).rgb * 2.0 - 1.0;

                    // Fine Mediterranean dust on crown
                    vec3 colDust = texture(terrainTex, vec3(WorldPos.xz * 0.15, 6.0)).rgb;
                    colDust = mix(colDust, vec3(0.65, 0.58, 0.48), 0.40);

                    // Urban curbs/shoulders: crushed cobblestone aggregate (Layer 12)
                    vec3 colCobbleShoulder = texture(terrainTex, vec3(WorldPos.xz * 0.22, 12.0)).rgb;

                    vec3 colBitumen = colTar * u_roadArmGain;
                    // Polished, compacted wheel tracks: kept as a modest darkening from
                    // the bed, not a second hard cut to near-black.
                    vec3 colRut = colBitumen * vec3(0.86, 0.85, 0.83);
                    vec3 nrmRut = nrmTar * vec3(0.70, 0.70, 1.0);

                    vec3 colUrbanRoad = mix(colBitumen, colRut, rutFactor * 0.65);
                    colUrbanRoad = mix(colUrbanRoad, colDust * 0.85, centerFactor * 0.25);
                    colUrbanRoad = mix(colUrbanRoad, colCobbleShoulder * u_roadArmGain,
                                       shoulderFactor * 0.45);

                    finalRoadCol = colUrbanRoad;
                    finalRoadNorm = mix(nrmTar, nrmRut, rutFactor * 0.60);
                    roadSpec = 0.18;
                } else {
                    // ── TYPE B: RURAL COUNTRY DIRT ROAD & PACKED EARTH MACADAM ──
                    // Authentic Red Dead Redemption 2 country dirt road: Layer 8 (gua_dirt_road_01c)
                    vec2 dirtUV = WorldPos.xz * 0.18; // ~5.5m repeat for realistic ground scale
                    vec3 colDirtRoad = texture(terrainTex, vec3(dirtUV, 8.0)).rgb;
                    vec3 nrmDirtRoad = texture(terrainNormal, vec3(dirtUV, 8.0)).rgb * 2.0 - 1.0;

                    // Rich warm Mediterranean sun-baked dirt tones
                    colDirtRoad *= vec3(0.95, 0.90, 0.85);

                    // 1. Dual Wheel Tracks: compressed, packed smooth, distinctly darker earth
                    vec3 colRut = colDirtRoad * vec3(0.70, 0.65, 0.58);
                    vec3 nrmRut = nrmDirtRoad * vec3(0.65, 0.65, 1.0);

                    // 2. Center Crown: dry powdery dust + sparse grass tufts growing between tracks
                    vec3 colDustySilt = texture(terrainTex, vec3(WorldPos.xz * 0.20, 6.0)).rgb; // Silt
                    vec3 colCenter = mix(colDirtRoad * 1.15, colClumpyGrass, 0.35);
                    colCenter = mix(colCenter, colDustySilt * 1.05, 0.40);

                    // 3. Shoulders: rough pebbles fading into roadside grass
                    vec3 colShoulder = mix(colDirtRoad, colClumpyGrass, 0.55);

                    // Compose rural cross section:
                    vec3 colRuralRoad = colDirtRoad;
                    colRuralRoad = mix(colRuralRoad, colRut, rutFactor * 0.80);
                    colRuralRoad = mix(colRuralRoad, colCenter, centerFactor * 0.55);
                    colRuralRoad = mix(colRuralRoad, colShoulder, shoulderFactor * 0.65);

                    // Macro variation along road (dry sunlit stretches vs shaded hollows)
                    float roadWander = noise(WorldPos.xz * 0.020);
                    colRuralRoad *= mix(vec3(0.88, 0.85, 0.80), vec3(1.12, 1.08, 1.02), roadWander);

                    finalRoadCol = colRuralRoad;
                    finalRoadNorm = mix(nrmDirtRoad, nrmRut, rutFactor * 0.65);
                    roadSpec = 0.07 + rutFactor * 0.09;
                }

                // --- 5. 3D Road Crown / Camber (Normal Perturbation) ---
                // Subtle convex curvature shedding to shoulders
                float camberWiggle = (noise(WorldPos.xz * 0.30) - 0.5) * 0.20;
                finalRoadNorm = normalize(finalRoadNorm + vec3(camberWiggle, 0.0, camberWiggle) * (1.0 - rWarped));

                specMask = mix(specMask, roadSpec, roadFactor * 0.95);
                col = mix(col, finalRoadCol, roadFactor * 0.98);
                worldNormal = normalize(mix(worldNormal, normalize(N + vec3(finalRoadNorm.x, 0.0, finalRoadNorm.y) * 0.70), roadFactor * 0.88));
            }
        }

        // D. REALISTIC WADIS & FLUVIAL GEOMORPHOLOGY (Blue Channel)
        if (!isHarbor && maskVals.b > 0.01) {
            float rVal = maskVals.b;

            // Organic shoreline variation
            float wadiNoise = noise(WorldPos.xz * 0.030) * 0.6 + noise(WorldPos.xz * 0.075) * 0.4;
            float rWarped = clamp(rVal + (wadiNoise - 0.5) * 0.10, 0.0, 1.0);

            // Fluvial flatness: 1.0 on flat riverbed floor (< 11°), fades on steep banks
            float floorFlatness = clamp(1.0 - (angleDeg - 8.0) / 8.0, 0.0, 1.0);
            float cutbankSteepness = clamp((angleDeg - 13.0) / 12.0, 0.0, 1.0);

            // D.1 Riparian Lush Green Corridor (Outer borders: rWarped 0.03 to 0.38)
            // Vibrant greenery and shrubs flourish along the moisture fringe of the wadi
            float riparianFactor = smoothstep(0.03, 0.15, rWarped) * (1.0 - smoothstep(0.30, 0.50, rWarped));
            vec3 colRiparian = texture(terrainTex, vec3(WorldPos.xz * 0.14, 7.0)).rgb; // Layer 7: Riparian grass
            col = mix(col, colRiparian, riparianFactor * 0.90);

            // D.2 Alluvial Bank, Shoulders & Point Bar Silt (rWarped 0.12 to 0.58)
            float bankFactor = smoothstep(0.12, 0.45, rWarped);
            vec3 colSilt = texture(terrainTex, vec3(WorldPos.xz * 0.16, 6.0)).rgb; // Layer 6: Alluvial Silt
            vec3 nrmSilt = texture(terrainNormal, vec3(WorldPos.xz * 0.16, 6.0)).rgb * 2.0 - 1.0;
            vec3 colScreeBank = texture(terrainTex, vec3(WorldPos.xz * 0.18, 9.0)).rgb; // Layer 9: Scree
            vec3 wadiBankCol = mix(colLoamSoil, colSilt, 0.45);
            wadiBankCol = mix(wadiBankCol, colScreeBank, 0.25);
            vec3 wadiBankNrm = mix(nrmLoamSoil, nrmSilt, 0.50);

            // D.3 Riverbed Cobblestones (Flat channel floor: rWarped 0.52 to 0.95)
            // Fluvial physics: cobblestones strictly rest in the active riverbed channel!
            float cobblestoneFactor = smoothstep(0.50, 0.72, rWarped) * floorFlatness;
            vec3 colCobbles = texture(terrainTex, vec3(WorldPos.xz * 0.22, 5.0)).rgb; // Layer 5: Rocky Riverbed
            vec3 nrmCobbles = texture(terrainNormal, vec3(WorldPos.xz * 0.22, 5.0)).rgb * 2.0 - 1.0;

            vec3 bedCol = mix(wadiBankCol, colCobbles, cobblestoneFactor);
            vec3 bedNrm = mix(wadiBankNrm, nrmCobbles, cobblestoneFactor);

            // D.4 Moist Alluvial Mud in deepest channel depressions
            float moistMudFactor = smoothstep(0.65, 0.88, rWarped) * floorFlatness;
            vec3 colMud = texture(terrainTex, vec3(WorldPos.xz * 0.18, 8.0)).rgb; // Layer 8: Watery Mud
            bedCol = mix(bedCol, colMud, moistMudFactor * 0.65);

            // D.5 CUTBANK ROCK STRATA ON STEEP ERODED BANK WALLS (cutbankSteepness > 0.01)
            if (cutbankSteepness > 0.01) {
                vec2 wallUV = vec2(WorldPos.x * 0.12 - WorldPos.z * 0.12, WorldPos.y * 0.22);
                vec3 colCutbankStrata = texture(terrainTex, vec3(wallUV, 11.0)).rgb; // Layer 11: Strata cliff
                vec3 nrmCutbankStrata = texture(terrainNormal, vec3(wallUV, 11.0)).rgb * 2.0 - 1.0;
                vec3 colCutbankScree  = texture(terrainTex, vec3(baseUV, 9.0)).rgb;  // Layer 9: Scree
                vec3 colCutbankLime   = texture(terrainTex, vec3(wallUV * 1.1, 4.0)).rgb; // Layer 4: Limestone

                float strata = sin(WorldPos.y * 0.75) * 0.40 + sin(WorldPos.y * 2.2) * 0.15 + 0.5;
                vec3 colRockWall = mix(colCutbankStrata, colCutbankLime, clamp(strata, 0.0, 1.0));
                colRockWall = mix(colRockWall, colCutbankScree, 0.30); // Talus scree caught in crevices

                bedCol = mix(bedCol, colRockWall, cutbankSteepness * 0.92);
                bedNrm = mix(bedNrm, nrmCutbankStrata, cutbankSteepness * 0.92);
            }

            float wadiTotalBlend = smoothstep(0.08, 0.28, rWarped);
            col = mix(col, bedCol, wadiTotalBlend);
            worldNormal = normalize(mix(worldNormal, normalize(N + vec3(bedNrm.x, 0.0, bedNrm.y) * 0.75), wadiTotalBlend * 0.85));

            // D.6 Crystalline Freshwater Stream (Deepest center: rVal > 0.68)
            float streamFactor = smoothstep(0.68, 0.90, rVal) * (1.0 - cutbankSteepness);
            if (streamFactor > 0.01) {
                // Downstream ripples
                vec2 sUV1 = WorldPos.xz * 0.65 + vec2(time * 0.28, time * 0.14);
                vec2 sUV2 = WorldPos.xz * 1.30 - vec2(time * 0.20, time * 0.25);
                float r1 = noise(sUV1);
                float r2 = noise(sUV2);

                vec3 rippleNrm = normalize(worldNormal + vec3(r1 - 0.5, 0.0, r2 - 0.5) * 0.50);
                worldNormal = mix(worldNormal, rippleNrm, streamFactor * 0.80);

                // Optical water clarity: river stones remain clearly visible underwater!
                vec3 wetStones = col * vec3(0.72, 0.82, 0.78);
                vec3 streamColor = mix(vec3(0.12, 0.28, 0.26), vec3(0.05, 0.16, 0.18), smoothstep(0.72, 0.98, rVal));
                col = mix(wetStones, streamColor, streamFactor * 0.30);

                // Sparkling caustics in shallows
                float caustics = pow(max(0.0, 1.0 - abs(r1 - r2) * 2.2), 2.5);
                col += vec3(0.95, 0.92, 0.80) * caustics * 0.28 * streamFactor * clamp(sunTilt + 0.2, 0.0, 1.0);

                // Water specular highlight
                specMask = mix(specMask, 0.88, streamFactor);
            }

            // D.7 Subtle Moist Ground Darkening
            float wetWadi = smoothstep(0.35, 0.80, rWarped);
            col *= mix(vec3(1.0), vec3(0.82, 0.78, 0.72), wetWadi * 0.25);
            specMask = max(specMask, wetWadi * 0.10);
        }
    }

    // ── 6. BALANCED NATURAL LIGHTING (PBR-LITE) ──
    vec3 L = normalize(lightDir);
    float dayFactor = clamp(smoothstep(-0.15, 0.25, sunTilt), 0.35, 1.0);

    // Subsurface scatter wrap for sand, earth, and grass
    float wrap = 0.25;
    float wrapNdL = clamp((dot(worldNormal, L) + wrap) / (1.0 + wrap), 0.0, 1.0);

    // Rich hemisphere ambient (sky blue from above, warm earth from below)
    vec3 skyCol    = vec3(0.35, 0.55, 0.85) * dayFactor;
    vec3 groundCol = vec3(0.25, 0.20, 0.15) * dayFactor;
    vec3 ambient   = mix(groundCol, skyCol, worldNormal.y * 0.5 + 0.5) * col;

    // Direct warm sunlight
    vec3 diffuse = col * wrapNdL * lightColor * 1.25 * dayFactor;

    // Specular reflection (subtle gloss on wet mud, brilliant sheen on water stream)
    vec3 halfDir = normalize(L + V);
    float specPow = mix(16.0, 96.0, specMask);
    float spec = pow(max(dot(worldNormal, halfDir), 0.0), specPow) * specMask;
    vec3 specular = spec * lightColor * dayFactor;

    vec3 result = ambient + diffuse + specular;

    // ACES Filmic Tonemapping
    result = (result * (2.51 * result + 0.03)) / (result * (2.43 * result + 0.59) + 0.14);
    result = pow(max(result, vec3(0.0)), vec3(1.0 / 2.2));

    // ── 7. ATMOSPHERIC HORIZON FOG (Seamless sky blend) ──
    vec3 dayBottom    = vec3(0.5, 0.8, 1.0);
    vec3 sunsetBottom = vec3(1.0, 0.4, 0.2);
    vec3 nightBottom  = vec3(0.05, 0.1, 0.2);

    vec3 skyHorizon;
    if (sunTilt > 0.1) {
        float mixFactor = smoothstep(0.1, 0.5, sunTilt);
        skyHorizon = mix(sunsetBottom, dayBottom, mixFactor);
    } else if (sunTilt > -0.1) {
        float mixFactor = smoothstep(-0.1, 0.1, sunTilt);
        skyHorizon = mix(nightBottom, sunsetBottom, mixFactor);
    } else {
        skyHorizon = nightBottom;
    }

    // Atmospheric forward scatter halo
    float sunScattering = max(dot(V, -L), 0.0);
    vec3 sunHazeColor = mix(vec3(1.0, 0.65, 0.35), vec3(1.0, 0.95, 0.85), clamp(sunTilt * 2.0, 0.0, 1.0));
    skyHorizon = mix(skyHorizon, sunHazeColor, pow(sunScattering, 4.0) * 0.25 * smoothstep(-0.05, 0.2, sunTilt));

    // Fog curve: Crystal clear up to 2500m, soft atmospheric haze to 6500m towards the horizon
    float fogAmt = smoothstep(2500.0, 6500.0, dist) * pow(clamp(1.0 - abs(V.y), 0.0, 1.0), 1.5);
    fogAmt = pow(fogAmt, 1.4);

    FragColor = vec4(mix(result, skyHorizon, fogAmt), 1.0);
}
