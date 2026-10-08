#version 330 core
out vec4 FragColor;

in vec2  TexCoords;
in vec3  FragPos;
in vec3  Normal;

uniform float time;
uniform vec3  lightDir;
uniform vec3  viewPos;
uniform vec3  lightColor;
uniform float sunTilt;

uniform sampler2D heightmapTex;
uniform int       useHeightmap;
uniform float     hmMinElev;
uniform float     hmMaxElev;
uniform float     hmWorldMinX;
uniform float     hmWorldMaxX;
uniform float     hmWorldMinZ;
uniform float     hmWorldMaxZ;
uniform float     waterLevel;
uniform sampler2D waterMapTex;

uniform sampler2D normalMap1;
uniform sampler2D normalMap2;
uniform sampler2D foamTexture;
uniform sampler2D roadMask;

// Fast procedural noise for organic wave fronts and foam variation
float hash21(vec2 p) {
    p = fract(p * vec2(234.34, 435.345));
    p += dot(p, p + 34.23);
    return fract(p.x * p.y);
}

float noise2D(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

void main() {
    float sunElev = lightDir.y;
    float sunVisibility = smoothstep(-0.15, 0.25, sunElev);
    float dist = length(viewPos - FragPos);

    // ── 1. TERRAIN & WATER DEPTH QUERY ────────────────────────────────────────
    float terrainH   = -100.0;
    float waterDepth = 15.0;
    float isRiver    = 0.0;
    vec2  shoreNormal = vec2(-1.0, 0.0); // Points toward shore (uphill)
    float shoreEdge  = 1.0;
    float simulatedCoverage = 1.0;
    float simulatedKind = 0.0;
    float simulatedFlowSpeed = 0.0;
    float surfaceLevel = waterLevel;

    if (useHeightmap == 1) {
        float u = (FragPos.x - hmWorldMinX) / (hmWorldMaxX - hmWorldMinX);
        float v = (FragPos.z - hmWorldMinZ) / (hmWorldMaxZ - hmWorldMinZ);

        if (u >= 0.0 && u <= 1.0 && v >= 0.0 && v <= 1.0) {
            vec4 waterState = texture(waterMapTex, vec2(u, v));
            simulatedCoverage = clamp(waterState.g, 0.0, 1.0);
            simulatedKind = waterState.b;
            simulatedFlowSpeed = waterState.a;
            if (simulatedCoverage <= 0.001) discard;
            surfaceLevel = waterState.r;

            float normElev = texture(heightmapTex, vec2(u, v)).r;
            terrainH = hmMinElev + normElev * (hmMaxElev - hmMinElev);

            // Use the same interpolated heightfield as the terrain. A hard discard
            // at exactly zero depth aliases the coast at screen resolution, so keep
            // a one-pixel coverage ramp around the true waterline instead.
            float rawDepth = surfaceLevel - terrainH;
            float shorelineAa = max(fwidth(rawDepth), 0.02);
            if (rawDepth < -shorelineAa) discard;

            waterDepth = max(0.0, rawDepth);
            shoreEdge = smoothstep(-shorelineAa, shorelineAa, rawDepth);

            // Sample river mask (Blue channel)
            isRiver = max(texture(roadMask, vec2(u, v)).b,
                          smoothstep(1.5, 2.5, simulatedKind));

            // Heightmap gradient: points uphill towards beach / dry ground
            float eps = 0.0025;
            float hL = texture(heightmapTex, vec2(u - eps, v)).r;
            float hR = texture(heightmapTex, vec2(u + eps, v)).r;
            float hD = texture(heightmapTex, vec2(u, v - eps)).r;
            float hU = texture(heightmapTex, vec2(u, v + eps)).r;
            vec2 grad = vec2(hR - hL, hU - hD);
            if (length(grad) > 0.00005) {
                shoreNormal = normalize(grad);
            }
        }
    }

    // ── 2. FLOW DYNAMICS & PHASE-SHIFTED ADVECTION ────────────────────────────
    // Ocean swells refract and turn perpendicular to the shoreline as they enter shallow water
    vec2 oceanDeepFlow = normalize(vec2(-0.95, 0.08));
    float shallowRefract = clamp(1.0 - waterDepth / 8.0, 0.0, 1.0);
    vec2 oceanFlow = normalize(mix(oceanDeepFlow, shoreNormal, shallowRefract * 0.75));

    // Rivers rush downhill (-shoreNormal)
    vec2 riverFlow = -shoreNormal;
    vec2 flowDir   = mix(oceanFlow, riverFlow, isRiver);

    float flowRate = mix(0.07, 0.35, isRiver) *
                     mix(1.0, clamp(simulatedFlowSpeed, 0.25, 2.0), isRiver);
    float t1 = fract(time * flowRate);
    float t2 = fract(time * flowRate + 0.5);
    float flowBlend = abs(t1 - 0.5) / 0.5;

    // ── 3. MULTI-LAYER NORMAL MAP WITH DISTANCE LOD ───────────────────────────
    // Medium swell
    vec3 n1_a = texture(normalMap1, TexCoords * 1.80 + flowDir * t1 * 0.4).rgb * 2.0 - 1.0;
    vec3 n1_b = texture(normalMap1, TexCoords * 1.80 + flowDir * t2 * 0.4).rgb * 2.0 - 1.0;
    vec3 n1   = mix(n1_a, n1_b, flowBlend);

    // Choppy cross-ripples
    vec2 crossDir = vec2(-flowDir.y, flowDir.x) * 0.5 + flowDir * 0.5;
    vec3 n2_a = texture(normalMap2, TexCoords * 3.70 + crossDir * t1 * 0.35).rgb * 2.0 - 1.0;
    vec3 n2_b = texture(normalMap2, TexCoords * 3.70 + crossDir * t2 * 0.35).rgb * 2.0 - 1.0;
    vec3 n2   = mix(n2_a, n2_b, flowBlend);

    // Micro capillary ripples (close range only < 35m)
    float closeFade = 1.0 - smoothstep(8.0, 45.0, dist);
    vec3 nMicro_a = texture(normalMap2, TexCoords * 10.0 + flowDir * t1 * 0.7).rgb * 2.0 - 1.0;
    vec3 nMicro_b = texture(normalMap2, TexCoords * 10.0 + flowDir * t2 * 0.7).rgb * 2.0 - 1.0;
    vec3 nMicro   = mix(nMicro_a, nMicro_b, flowBlend);

    // Far swell normals to avoid tiling patterns on the distant sea
    float farFade = smoothstep(60.0, 350.0, dist);
    vec3 nFar_a = texture(normalMap1, TexCoords * 0.22 + flowDir * t1 * 0.12).rgb * 2.0 - 1.0;
    vec3 nFar_b = texture(normalMap1, TexCoords * 0.22 + flowDir * t2 * 0.12).rgb * 2.0 - 1.0;
    vec3 nFar   = mix(nFar_a, nFar_b, flowBlend);

    // Combine normals
    vec3 blendedNormal = n1 * 0.22 + n2 * 0.18 + nMicro * 0.14 * closeFade;
    blendedNormal = mix(blendedNormal, nFar * 0.18, farFade);
    vec3 waveSurfaceNormal = normalize(Normal + vec3(blendedNormal.x, blendedNormal.z * 0.5, blendedNormal.y));

    // ── 4. PHOTOREALISTIC WATER COLOR PALETTE (Beer-Lambert Absorption) ───────
    // Very shallow (< 0.5m) water is clear and takes on the warm tone of the seafloor
    // Mid depth (0.5m - 4.0m) displays that breathtaking crystal Mediterranean turquoise-azure
    // Deep ocean (> 4m) is deep rich cobalt sapphire navy
    vec3 sandTint   = vec3(0.24, 0.48, 0.45);   // Sunlit clear water over sand
    vec3 seaShallow = vec3(0.08, 0.38, 0.42);   // Crystalline coastal turquoise
    vec3 seaMid     = vec3(0.025, 0.22, 0.35);  // Mediterranean vibrant azure
    vec3 seaDeep    = vec3(0.008, 0.055, 0.17); // Oceanic deep cobalt navy
    vec3 seaAbyss   = vec3(0.002, 0.018, 0.08); // Open sea horizon sapphire

    float dNorm = clamp(waterDepth / 9.0, 0.0, 1.0);
    vec3 oceanColor = mix(sandTint, seaShallow, smoothstep(0.0, 0.10, dNorm));
    oceanColor = mix(oceanColor, seaMid, smoothstep(0.10, 0.35, dNorm));
    oceanColor = mix(oceanColor, seaDeep, smoothstep(0.35, 0.70, dNorm));
    oceanColor = mix(oceanColor, seaAbyss, smoothstep(0.70, 1.0, dNorm));

    // Freshwater stream / wadi (silt, mineral, and riverbed tones)
    vec3 riverShallow = vec3(0.24, 0.44, 0.38);
    vec3 riverMid     = vec3(0.12, 0.32, 0.28);
    vec3 riverDeep    = vec3(0.04, 0.18, 0.15);
    vec3 riverColor   = mix(riverShallow, riverMid, smoothstep(0.0, 0.35, dNorm));
    riverColor        = mix(riverColor, riverDeep, smoothstep(0.35, 1.0, dNorm));

    vec3 waterBodyColor = mix(oceanColor, riverColor, smoothstep(0.1, 0.6, isRiver));

    // ── 5. LIGHTING VECTORS & FRESNEL ─────────────────────────────────────────
    vec3 V = normalize(viewPos - FragPos);
    vec3 L = normalize(lightDir);
    vec3 H = normalize(L + V);

    float NdotV = max(dot(waveSurfaceNormal, V), 0.0);
    float NdotL = max(dot(waveSurfaceNormal, L), 0.0);

    // Exact Schlick Fresnel for water (dielectric IOR = 1.333, F0 = 0.020)
    float F0 = 0.022;
    float fresnel = F0 + (1.0 - F0) * pow(clamp(1.0 - NdotV, 0.0, 1.0), 5.0);

    // ── 6. ATMOSPHERIC SKY & HORIZON REFLECTION ───────────────────────────────
    vec3 R = reflect(-V, waveSurfaceNormal);
    float reflElev = clamp(R.y, 0.0, 1.0);

    // Sky palette across day / sunset / night
    vec3 dayZenith   = vec3(0.12, 0.38, 0.72);
    vec3 dayHorizon  = vec3(0.55, 0.75, 0.90);
    vec3 sunZenith   = vec3(0.20, 0.10, 0.35);
    vec3 sunHorizon  = vec3(0.95, 0.45, 0.18);
    vec3 nightZenith = vec3(0.005, 0.012, 0.035);
    vec3 nightHorizon= vec3(0.02, 0.04, 0.08);

    vec3 skyZenith, skyHorizon;
    if (sunElev > 0.15) {
        float f = smoothstep(0.15, 0.50, sunElev);
        skyZenith  = mix(sunZenith, dayZenith, f);
        skyHorizon = mix(sunHorizon, dayHorizon, f);
    } else if (sunElev > -0.15) {
        float f = smoothstep(-0.15, 0.15, sunElev);
        skyZenith  = mix(nightZenith, sunZenith, f);
        skyHorizon = mix(nightHorizon, sunHorizon, f);
    } else {
        skyZenith  = nightZenith;
        skyHorizon = nightHorizon;
    }

    vec3 skyReflection = mix(skyHorizon, skyZenith, pow(reflElev, 0.65));

    // Reflected sun disk & atmospheric sun glow
    float RdotL = max(dot(R, L), 0.0);
    vec3 sunGlowColor = mix(vec3(1.0, 0.55, 0.20), vec3(1.0, 0.96, 0.88), smoothstep(0.05, 0.40, sunElev));
    float sunGlow = pow(RdotL, 32.0) * 0.85 + pow(RdotL, 256.0) * 2.5;
    skyReflection += sunGlowColor * sunGlow * sunVisibility;

    // ── 7. SUN SPECULAR GLITTER TRAIL (Multi-Lobe Ocean Path) ──────────────────
    float NdotH = max(dot(waveSurfaceNormal, H), 0.0);

    // Broad glittering trail (warm sunset glitter spreading over wave slopes)
    float broadTrail = pow(NdotH, 28.0) * 0.45;
    // Crisp solar wave crest glints
    float sharpGlint = pow(NdotH, 380.0) * 4.0;
    // Anti-sparkle distance damping
    float glitterFade = 1.0 - smoothstep(120.0, 800.0, dist);
    vec3 sunGlitter = sunGlowColor * (broadTrail + sharpGlint) * sunVisibility * (0.35 + 0.65 * glitterFade);

    // ── 8. SUBSURFACE SCATTERING (Light entering wave crests) ─────────────────
    float sssLight = pow(max(dot(V, -L), 0.0), 4.0) * pow(1.0 - waveSurfaceNormal.y, 1.8);
    vec3 sssTint = mix(vec3(0.08, 0.55, 0.50), vec3(0.15, 0.45, 0.25), isRiver);
    vec3 subsurfaceColor = sssTint * sssLight * sunVisibility * 0.70;

    // ── 9. CAUSTICS IN SHALLOWS (Depth < 2.5m, visible near shore) ────────────
    vec3 caustics = vec3(0.0);
    if (waterDepth < 3.0 && sunVisibility > 0.05) {
        float cDistFade = 1.0 - smoothstep(3.0, 30.0, dist);
        if (cDistFade > 0.01) {
            vec2 cUV1 = TexCoords * 12.0 + flowDir * time * 0.35;
            vec2 cUV2 = TexCoords * 12.0 - flowDir * time * 0.28 + vec2(0.4, 0.6);
            float c1 = texture(normalMap1, cUV1).r;
            float c2 = texture(normalMap2, cUV2).r;
            float causticPattern = pow(max(0.0, 1.0 - abs(c1 - c2) * 2.8), 3.0);
            float depthDamp = smoothstep(3.0, 0.2, waterDepth);
            caustics = vec3(1.0, 0.95, 0.85) * causticPattern * depthDamp * cDistFade * sunVisibility * 0.40;
        }
    }

    // ── 10. COMPOSITE WATER BODY & SKY REFLECTION ─────────────────────────────
    // Diffuse ambient + sun lighting on the water body
    float waterDiffuse = NdotL * 0.35 + 0.65;
    vec3 shadedWater = waterBodyColor * (waterDiffuse * mix(0.3, 1.0, sunVisibility)) + subsurfaceColor + caustics;

    // Fresnel blend: steep view looks into water, grazing view reflects the gorgeous sky
    vec3 waterLit = mix(shadedWater, skyReflection, fresnel);
    waterLit += sunGlitter * fresnel;

    // ── 11. ORGANIC SHORELINE SURF WASH & DYNAMIC FOAM ────────────────────────
    float foamTotal = 0.0;

    if (useHeightmap == 1) {
        // Organic shoreline wave runup: waves advance towards the beach (waterDepth -> 0)
        // Two out-of-phase wash pulses modulated by noise to follow natural coastlines
        vec2 worldCoord = FragPos.xz * 0.05;
        float coastNoise = noise2D(worldCoord) * 0.8 + noise2D(worldCoord * 2.5) * 0.3;

        // Wave surge 1: primary incoming breaker
        float washPhase1 = time * 1.35 - waterDepth * 2.6 + coastNoise;
        float washWave1  = sin(washPhase1) * 0.5 + 0.5;
        // Narrow the wave crest into a sharp surf line
        float surfLine1  = smoothstep(0.68, 0.96, washWave1) * smoothstep(4.0, 0.1, waterDepth);

        // Wave surge 2: secondary slower swell
        float washPhase2 = time * 0.85 - waterDepth * 1.8 + coastNoise * 1.4 + 2.2;
        float washWave2  = sin(washPhase2) * 0.5 + 0.5;
        float surfLine2  = smoothstep(0.72, 0.98, washWave2) * smoothstep(3.0, 0.1, waterDepth);

        // Shoreline crash foam: where water touches the sandy shore (depth < 0.55m)
        float crashBand = smoothstep(0.55, 0.02, waterDepth);

        // High wave crest / wind chop foam (on steep wave faces in deeper water)
        float waveSteepness = 1.0 - clamp(waveSurfaceNormal.y, 0.0, 1.0);
        float crestFoam = smoothstep(0.26, 0.50, waveSteepness) * smoothstep(12.0, 1.5, waterDepth);

        // Sample foam texture for organic bubble and lace structure
        vec2 foamUV = TexCoords * 6.5 + flowDir * time * 0.08;
        float fSample1 = texture(foamTexture, foamUV).r;
        float fSample2 = texture(foamTexture, foamUV * 1.8 - flowDir * time * 0.04).r;
        float foamTexturePattern = (fSample1 * 0.65 + fSample2 * 0.35);

        // Combine foam components
        float rawFoam = crashBand * 0.85 
                      + surfLine1 * 0.75 
                      + surfLine2 * 0.55 
                      + crestFoam * 0.40;

        // In inland streams/wadis, replace ocean surf breakers with delicate water riffles
        float riverRiffle = smoothstep(0.30, 0.05, waterDepth) * 0.40;
        rawFoam = mix(rawFoam, riverRiffle, smoothstep(0.2, 0.7, isRiver));

        rawFoam *= (0.45 + 0.55 * foamTexturePattern);

        // Distance fade for foam
        float foamDistFade = 1.0 - smoothstep(100.0, 350.0, dist);
        foamTotal = clamp(rawFoam * foamDistFade, 0.0, 1.0);
    }

    // Lit foam color (aerated bubbles reflect sky and direct sun)
    vec3 foamColor = mix(skyHorizon * 0.95 + vec3(0.05), vec3(1.0, 1.0, 1.0), sunVisibility);
    foamColor += sunGlitter * 1.5; // Foam sparkles under direct sun

    // Blend foam into water
    vec3 finalColor = mix(waterLit, foamColor, foamTotal * 0.85);

    // ── 12. SHORELINE SMOOTH OPACITY (Eliminates harsh stepped cutoff) ─────────
    float distOpacity = smoothstep(15.0, 200.0, dist);
    // Near shore: clear water! Depth < 0.4m is transparent so golden sand shows through clearly
    float depthOpacity = smoothstep(0.0, 2.0, waterDepth) * 0.88 + 0.08;
    float finalAlpha = mix(depthOpacity, 0.98, distOpacity);

    // Boost opacity where foam is thick
    finalAlpha = clamp(finalAlpha + foamTotal * 0.75, 0.0, 1.0);

    // Smooth alpha ramp down to the exact waterline
    finalAlpha *= shoreEdge;
    finalAlpha *= smoothstep(0.0, 0.6, simulatedCoverage);

    // ── 13. ATMOSPHERIC HORIZON FOG (Seamlessly blends water into sky) ────────
    float oceanFogAmt = smoothstep(1200.0, 5500.0, dist);
    oceanFogAmt = pow(oceanFogAmt, 1.35);
    finalColor = mix(finalColor, skyHorizon, oceanFogAmt);
    finalAlpha = mix(finalAlpha, 1.0, oceanFogAmt);

    FragColor = vec4(finalColor, finalAlpha);
}
