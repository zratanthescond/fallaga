#version 330 core
out vec4 FragColor;

in vec2  TexCoords;
in vec3  FragPos;
in float WaveHeight;
in float Compression;

uniform sampler2D normalMap;
uniform float time;
uniform vec3 lightDir;
uniform vec3 viewPos;
uniform float waveBase;

uniform sampler2D heightmapTex;
uniform int useHeightmap;
uniform float minElev;
uniform float maxElev;
uniform float worldMinX;
uniform float worldMaxX;
uniform float worldMinZ;
uniform float worldMaxZ;

// --- NOISE FUNCTIONS ---
float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
float noise(vec2 p) {
    vec2 i = floor(p); vec2 f = fract(p);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}
float fbm(vec2 p) {
    float v = 0.0; float a = 0.5;
    vec2 shift = vec2(100.0);
    for (int i = 0; i < 5; ++i) {
        v += a * noise(p);
        p = p * 2.0 + shift;
        a *= 0.5;
    }
    return v;
}

void main()
{
    float sunTilt = lightDir.y;
    float sunVisibility = smoothstep(-0.2, 0.2, sunTilt);

    // Normalise wave height: 0 = trough, 1 = crest
    float normH = clamp((WaveHeight - (waveBase - 2.5)) / 5.0, 0.0, 1.0);

    /* 1. NORMAL MAP */
    vec2 scroll1 = vec2(time * 0.015, time * 0.01);
    vec2 scroll2 = vec2(time * -0.01, time * 0.02);
    vec3 n1 = texture(normalMap, TexCoords * 2.17 + scroll1).rgb * 2.0 - 1.0;
    vec3 n2 = texture(normalMap, TexCoords * 4.61 + scroll2).rgb * 2.0 - 1.0;
    
    // Soften normals drastically for smoother, highly-reflective glassy water
    vec3 normal = normalize(n1 * 0.3 + n2 * 0.3 + vec3(0.0, 0.0, 1.5));

    // ── SHORELINE / DEPTH-BASED INTERSECTION ──
    float terrainH = -100.0;
    float shoreFoam = 0.0;
    float edgeAlpha = 1.0;
    float depthFactor = 1.0 - normH;

    if (useHeightmap == 1) {
        float u = (FragPos.x - worldMinX) / (worldMaxX - worldMinX);
        float v = (worldMaxZ - FragPos.z) / (worldMaxZ - worldMinZ);
        
        if (u >= 0.0 && u <= 1.0 && v >= 0.0 && v <= 1.0) {
            float normElev = texture(heightmapTex, vec2(u, v)).r;
            terrainH = minElev + normElev * (maxElev - minElev);
            float waterDepth = FragPos.y - terrainH;
            
            // Discard fragments strictly underneath the terrain geometry
            if (waterDepth <= -0.05) discard;
            
            // Fade alpha out softly where water barely grazes the sand
            edgeAlpha = smoothstep(0.0, 0.3, waterDepth);
            
            // Color mapping based on actual water depth instead of just wave height
            // Smooth depth out over 5 meters for clear visible sand
            depthFactor = clamp(waterDepth / 5.0, 0.0, 1.0);
            
            // Generate rushing sea-foam exactly along the collision boundary
            float shoreNoise = fbm(TexCoords * 40.0 - time * 0.15);
            float waveSurge = fbm(vec2(time * 0.2, TexCoords.y * 5.0)); // push/pull effect
            // Less overwhelming foam, highly localized to the beach edge
            shoreFoam = smoothstep(0.4 + waveSurge*0.2, 0.0, waterDepth) * smoothstep(0.4, 0.9, shoreNoise);
            shoreFoam += smoothstep(0.05, 0.0, waterDepth); // tight white line
        }
    }

    /* 2. COLORS (Mediterranean) */
    vec3 shoreColor = vec3(0.12, 0.75, 0.70); // Crystal cyan
    vec3 midColor   = vec3(0.04, 0.40, 0.58);
    vec3 deepColor  = vec3(0.00, 0.12, 0.26);
    vec3 waterColor = mix(shoreColor, midColor, smoothstep(0.0, 0.4, depthFactor));
    waterColor = mix(waterColor, deepColor, smoothstep(0.4, 1.0, depthFactor));

    /* 3. VIEW & LIGHT */
    vec3 V = normalize(viewPos - FragPos);
    vec3 L = normalize(lightDir);
    vec3 H = normalize(L + V);

    /* 4. SUBSURFACE SCATTERING */
    float sssFactor = pow(max(dot(V, -L), 0.0), 6.0) * smoothstep(0.35, 0.80, normH);
    vec3 sssColor = vec3(0.05, 0.85, 0.65) * sssFactor * sunVisibility * 0.9;

    /* 5. LIGHTING */
    float diffuse = max(dot(normal, L), 0.0) * 0.3 + 0.7;
    float specBase = max(dot(normal, H), 0.0);
    float spec = pow(specBase, 128.0) * 2.5;
    float sparkles = pow(specBase, 1024.0) * 8.0;
    vec3 specularColor = vec3(1.0, 1.0, 0.98) * (spec + sparkles);

    /* 6. FRESNEL */
    float fresnel = pow(1.0 - max(dot(normal, V), 0.0), 4.5);
    fresnel = clamp(fresnel, 0.15, 0.98);

    float foamJacobian = smoothstep(0.65, 1.30, Compression);
    float foamCrest    = smoothstep(0.70, 0.95, normH);
    float foamNoise    = fbm(TexCoords * 12.0 + time * 0.05);
    float foamMask = clamp((foamJacobian + foamCrest * 0.4) * smoothstep(0.3, 0.7, foamNoise) + shoreFoam, 0.0, 1.0);
    vec3 foamColor = vec3(0.95, 0.98, 1.0);

    /* 8. REFLECTIONS */
    vec3 reflectionDir = reflect(-V, normal);
    float verticalRefl = clamp(reflectionDir.y * 0.5 + 0.5, 0.0, 1.0);

    vec3 dayTop = vec3(0.1, 0.5, 0.9); vec3 dayBottom = vec3(0.5, 0.8, 1.0);
    vec3 sunsetTop = vec3(0.2, 0.1, 0.4); vec3 sunsetBottom = vec3(1.0, 0.4, 0.2);
    vec3 nightTop = vec3(0.01, 0.03, 0.07); vec3 nightBottom = vec3(0.05, 0.1, 0.2);

    vec3 skyT, skyB;
    if (sunTilt > 0.1) {
        float mixF = smoothstep(0.1, 0.5, sunTilt);
        skyT = mix(sunsetTop, dayTop, mixF); skyB = mix(sunsetBottom, dayBottom, mixF);
    } else if (sunTilt > -0.1) {
        float mixF = smoothstep(-0.1, 0.1, sunTilt);
        skyT = mix(nightTop, sunsetTop, mixF); skyB = mix(nightBottom, sunsetBottom, mixF);
    } else {
        skyT = nightTop; skyB = nightBottom;
    }
    vec3 reflectionColor = mix(skyB, skyT, pow(verticalRefl, 0.7)) * 1.5;

    vec2 cloudUV = reflectionDir.xz / (max(abs(reflectionDir.y), 0.01));
    float cloudDensity = fbm(cloudUV * 0.23 + time * 0.01);
    reflectionColor = mix(reflectionColor, vec3(1.0), cloudDensity * smoothstep(0.05, 0.4, reflectionDir.y) * 0.6);

    vec3 finalColor = waterColor * diffuse + sssColor;
    finalColor = mix(finalColor, reflectionColor, fresnel * mix(0.5, 0.9, sunVisibility));
    finalColor *= mix(0.2, 1.0, sunVisibility);
    finalColor += specularColor * fresnel * sunVisibility;
    finalColor = mix(finalColor, foamColor * mix(0.5, 1.0, sunVisibility), foamMask);

    /* 9. ALPHA / REFRACTION */
    // Shallow water should be highly transparent so you can see the sand!
    // At deep water it becomes primarily colored surface reflection
    float depthOpacity = 0.90;
    if (useHeightmap == 1) {
        float waterDepth = FragPos.y - terrainH;
        depthOpacity = clamp(waterDepth / 3.0, 0.25, 0.90);
    }
    
    float alpha = mix(depthOpacity, 1.0, foamMask) * edgeAlpha;

    FragColor = vec4(finalColor, alpha);
}
