#version 460 core
// Depth-based shallow water: volume comes from the real depth (surface - bed), animation from a two-phase flow map.
in vec3  vWorld;
in float vBedY;
in vec2  vFlow;
in float vKind;
in float vLife;
out vec4 FragColor;

uniform float     time;                // MUST be set every frame (glfwGetTime) - without it nothing moves
uniform vec3      viewPos;
uniform vec3      lightDir;            // direction TOWARDS the sun, normalised
uniform vec3      lightColor;
uniform vec3      skyColor;
uniform sampler2D waterNormalMap;      // your existing tiling water normal map (wrap = GL_REPEAT)
uniform sampler2D sceneDepthTex;      // OPTIONAL: depth copied right after the terrain pass (blit it, never sample the buffer you are testing against)
uniform int       useSceneDepth;       // 1 = measure depth against the terrain that is really on screen (hides any CPU/GPU height mismatch)
uniform vec2      invViewport;         // 1.0 / framebuffer size
uniform vec2      nearFar;
uniform float     rain;                // 0..1, raindrop rings on pools while it rains
uniform vec3      fogColor;
uniform float     fogDensity;

vec3 flowNormal(vec2 uv, vec2 flow, float speedScale) {
    // two-phase flow map: two layers scroll along the flow and cross-fade, so it never "pulses" or stretches
    float t = time * 0.35;
    float p0 = fract(t), p1 = fract(t + 0.5);
    vec2 d = flow * speedScale;
    vec3 n0 = texture(waterNormalMap, uv - d * p0).xyz * 2.0 - 1.0;
    vec3 n1 = texture(waterNormalMap, uv - d * p1 + vec2(0.37, 0.61)).xyz * 2.0 - 1.0;
    float w0 = 1.0 - abs(1.0 - 2.0 * p0);
    return normalize(mix(n1, n0, w0));
}
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) { vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y); }

float linearDepth(float d) { float z = d * 2.0 - 1.0; return 2.0 * nearFar.x * nearFar.y / (nearFar.y + nearFar.x - z * (nearFar.y - nearFar.x)); }

void main() {
    vec3 Vv = normalize(vWorld - viewPos);
    float depth = vWorld.y - vBedY;              // water depth from the CPU height samples (metres)
    if (useSceneDepth == 1) {                    // better: thickness of water along the view ray, vertical part of it
        float sceneDist = linearDepth(texture(sceneDepthTex, gl_FragCoord.xy * invViewport).r);
        float fragDist  = linearDepth(gl_FragCoord.z);
        depth = max(sceneDist - fragDist, 0.0) * abs(Vv.y);
    }
    if (depth <= 0.002) discard;                 // ground is above the water: no water here, so it can never "fly"

    float speed = length(vFlow);
    bool trickle = vKind > 2.5;
    vec2 uv = vWorld.xz * 0.35;
    vec3 n;
    if (trickle) {
        vec2 dir = vFlow / max(speed, 1e-3);
        n = flowNormal(uv, dir * clamp(speed, 0.15, 1.2), 1.1);
        n.xy += 0.18 * (texture(waterNormalMap, uv * 2.3 - dir * time * speed * 0.9).xy * 2.0 - 1.0);   // fast fine ripples
    } else {
        vec3 a = flowNormal(uv * 0.8, vec2( 0.05,  0.02), 1.0);       // slow wind drift in two directions
        vec3 b = flowNormal(uv * 1.3 + 7.0, vec2(-0.03, 0.04), 1.0);
        n = normalize(a + b);
        if (rain > 0.01) {                                              // raindrop rings
            vec2 g = vWorld.xz * 1.6; vec2 id = floor(g); vec2 f = fract(g) - 0.5;
            float ph = fract(time * 1.3 + hash(id)); float r = length(f - (vec2(hash(id + 3.1), hash(id + 7.7)) - 0.5) * 0.5);
            float ring = smoothstep(0.04, 0.0, abs(r - ph * 0.5)) * (1.0 - ph) * step(hash(id + 11.3), rain);
            n.xy += ring * 0.6;
        }
    }
    n = normalize(vec3(n.x, 1.6, n.y));          // normal map -> world up-facing normal (tune 1.6 for ripple strength)

    // ---- volume: colour and opacity from depth
    float t = 1.0 - exp(-min(depth, 1.5) * 1.4);
    vec3 shallow = vec3(0.46, 0.50, 0.40), deep = vec3(0.14, 0.27, 0.27);   // turbid desert rain water, never black
    vec3 body = mix(shallow, deep, t);
    float alpha = mix(0.35, 0.82, t) * smoothstep(0.0, 0.08, depth) * smoothstep(0.0, 0.25, vLife);

    // ---- light: fresnel sky reflection + sun glint
    vec3 V = normalize(viewPos - vWorld);
    float fres = 0.02 + 0.98 * pow(1.0 - max(dot(n, V), 0.0), 5.0);
    vec3 H = normalize(lightDir + V);
    float spec = pow(max(dot(n, H), 0.0), 220.0) * 1.6;
    vec3 col = mix(body, skyColor, clamp(fres * 0.65, 0.0, 0.85)) + lightColor * spec;
    col *= 0.55 + 0.45 * max(dot(vec3(0, 1, 0), lightDir), 0.0);

    // ---- shore foam and a bit of foam where the current is fast
    // Trickles are only 5-50 cm deep, so a wide foam rim swallowed them whole. Keep it
    // to a thin 3.5 cm edge and only foam fast flow.
    // GLSL leaves smoothstep undefined when edge0 >= edge1. The old reversed
    // range could mark large parts of a pool as foam, producing solid white
    // patches in-game. Keep foam confined to the shallow shoreline band.
    float shore = 1.0 - smoothstep(0.0, 0.035, depth);
    float fn = noise(vWorld.xz * 2.2 + (trickle ? vFlow * -time * 0.8 : vec2(0.0)) + time * 0.1);
    float foam = shore * smoothstep(0.35, 0.8, fn) + (trickle ? smoothstep(1.0, 1.8, speed) * smoothstep(0.55, 0.9, fn) * 0.25 : 0.0);
    col = mix(col, vec3(0.92, 0.94, 0.95), clamp(foam, 0.0, 1.0) * 0.8);
    alpha = max(alpha, foam * 0.7 * smoothstep(0.0, 0.02, depth));

    float dist = length(viewPos - vWorld);
    col = mix(col, fogColor, 1.0 - exp(-fogDensity * dist));
    FragColor = vec4(col, clamp(alpha, 0.0, 1.0));
}
