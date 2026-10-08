#version 330 core

out vec4 FragColor;

in vec3 vWorldPosition;
in vec2 vWorldXZ;
in vec2 vFlowDirection;
in vec2 vPatchCenter;
in float vPatchRadius;
in float vMaxDepth;
in float vFlowSpeed;
in float vKind;
in float vFlowFade;

uniform sampler2D terrainHeightTex;
uniform sampler2D paintedRoadMask;
uniform sampler2D vectorRoadMask;
uniform sampler2D normalMap1;
uniform sampler2D normalMap2;
uniform sampler2D foamTexture;
uniform sampler2D sceneColorTex;

uniform int usePaintedRoadMask;
uniform int useVectorRoadMask;
uniform int useSceneColor;
uniform float worldMinX;
uniform float worldMaxX;
uniform float worldMinZ;
uniform float worldMaxZ;
uniform float time;
uniform vec3 cameraPos;
uniform vec3 lightDir;

void main() {
    vec2 terrainUV = vec2(
        (vWorldXZ.x - worldMinX) / (worldMaxX - worldMinX),
        (vWorldXZ.y - worldMinZ) / (worldMaxZ - worldMinZ));
    if (any(lessThan(terrainUV, vec2(0.0))) || any(greaterThan(terrainUV, vec2(1.0))))
        discard;

    if (vKind < 0.5) {
        float radialDistance = length(vWorldXZ - vPatchCenter);
        if (radialDistance > vPatchRadius)
            discard;
    }

    // This texture contains the final physics/render heightfield in world units,
    // including the road-conform pass, rather than the source DEM texture.
    float terrainHeight = texture(terrainHeightTex, terrainUV).r;
    float waterDepth = vWorldPosition.y - terrainHeight;
    if (waterDepth <= 0.015 || waterDepth > vMaxDepth + 0.20)
        discard;

    // River and painted-road masks keep the added water surface from covering asphalt.
    if (usePaintedRoadMask != 0 && texture(paintedRoadMask, terrainUV).r > 0.35)
        discard;
    if (useVectorRoadMask != 0 && texture(vectorRoadMask, terrainUV).r > 0.30)
        discard;

    vec2 flow = vFlowDirection;
    if (dot(flow, flow) < 1e-6)
        flow = vec2(1.0, 0.0);
    else
        flow = normalize(flow);

    float flowRate = max(vFlowSpeed, 0.04);
    vec2 worldUV = vWorldXZ * 0.035;
    vec2 flowOffset = flow * time * flowRate * 0.045;
    if (vKind < 0.5)
        flowOffset = vec2(time * 0.002, -time * 0.0013);

    vec3 rippleA = texture(normalMap1, worldUV + flowOffset).rgb * 2.0 - 1.0;
    vec3 rippleB = texture(normalMap2, worldUV * 1.83 - flowOffset * 0.72).rgb * 2.0 - 1.0;
    vec3 ripple = normalize(vec3((rippleA.xy + rippleB.xy) * 0.18, 1.0));

    float shallow = 1.0 - smoothstep(0.08, max(vMaxDepth, 0.12), waterDepth);
    vec3 shallowColor = (vKind < 0.5)
        ? vec3(0.30, 0.48, 0.40)
        : vec3(0.36, 0.48, 0.36);
    vec3 deepColor = (vKind < 0.5)
        ? vec3(0.025, 0.22, 0.24)
        : vec3(0.08, 0.27, 0.23);
    vec3 waterColor = mix(deepColor, shallowColor, shallow);

    vec3 normal = normalize(vec3(ripple.x, max(ripple.z, 0.4), ripple.y));
    vec3 light = normalize(lightDir);
    float diffuse = 0.72 + 0.28 * max(dot(normal, light), 0.0);
    vec3 viewDirection = normalize(cameraPos - vWorldPosition);
    float fresnel = 0.04 + 0.22 * pow(1.0 - max(dot(normal, viewDirection), 0.0), 5.0);
    waterColor = waterColor * diffuse + vec3(0.10, 0.20, 0.22) * fresnel;

    float foamNoise = texture(foamTexture, worldUV * 1.7 + flowOffset * 0.35).r;
    float edgeFoam = (1.0 - smoothstep(0.04, 0.24, waterDepth)) *
                     smoothstep(0.25, 0.85, foamNoise);
    waterColor = mix(waterColor, vec3(0.72, 0.79, 0.72), edgeFoam * 0.22);

    if (vKind < 0.5 && vPatchRadius > 0.0) {
        float edge = 1.0 - smoothstep(vPatchRadius * 0.88, vPatchRadius,
                                      length(vWorldXZ - vPatchCenter));
        if (edge < 0.02)
            discard;
    }

    if (useSceneColor != 0) {
        ivec2 sceneSize = textureSize(sceneColorTex, 0);
        vec2 screenUV = gl_FragCoord.xy / max(vec2(sceneSize), vec2(1.0));
        vec3 refracted = texture(sceneColorTex, screenUV + ripple.xy * 0.0015).rgb;
        waterColor = mix(waterColor, refracted, 0.12 + 0.08 * (1.0 - shallow));
    }

    float alpha = (vKind < 0.5) ? 0.76 : 0.68;
    alpha *= mix(0.72, 1.0, 1.0 - shallow * 0.25);
    alpha *= clamp(vFlowFade, 0.0, 1.0);
    if (vKind < 0.5 && vPatchRadius > 0.0) {
        float edgeAlpha = 1.0 - smoothstep(vPatchRadius * 0.94, vPatchRadius,
                                           length(vWorldXZ - vPatchCenter));
        alpha *= edgeAlpha;
    }
    FragColor = vec4(waterColor, alpha);
}
