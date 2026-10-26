#version 460 core

// Composite: antialiases the scene during the upscale, then applies optional ambient occlusion
// (.r) and sun contact shadow (.g).
//
// The AO / shadow map is half resolution. Plain bilinear upsampling smears it across depth edges
// (kerbs, props, silhouettes), which is what made the shadows look blurry. Each of the four nearest
// half-res texels is weighted by how close its stored depth (.ba) is to the full-res pixel's depth,
// so edges stay sharp and flat areas stay smooth. Only ONE full-res depth read is needed.
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D u_scene;
uniform sampler2D u_ao;
uniform sampler2D u_depth;        // full-res scene depth
uniform int   u_aoEnabled;
uniform float u_aoStrength;
uniform float u_aoFloor;          // AO can never darken below this (0.45 -> never black)
uniform float u_shadowStrength;   // 0 = no sun contact shadows
uniform vec2  u_aoSize;           // size of the half-res map in texels
uniform vec2  u_projZ;            // projection[2][2], projection[3][2]

float viewDist(float d) { return u_projZ.y / (d * 2.0 - 1.0 + u_projZ.x); }   // positive metres
float storedDist(vec4 t) { return (t.b * 255.0 * 256.0 + t.a * 255.0) / 65535.0 * 256.0; }
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

// FXAA 3.11-style edge filter. It only blends along a detected high-contrast edge,
// preserving flat areas and avoiding the cost of multisampled scene/depth targets.
vec3 fxaaScene() {
    vec2 texel = 1.0 / vec2(textureSize(u_scene, 0));
    vec3 nw = texture(u_scene, vUV + vec2(-1.0, -1.0) * texel).rgb;
    vec3 ne = texture(u_scene, vUV + vec2( 1.0, -1.0) * texel).rgb;
    vec3 sw = texture(u_scene, vUV + vec2(-1.0,  1.0) * texel).rgb;
    vec3 se = texture(u_scene, vUV + vec2( 1.0,  1.0) * texel).rgb;
    vec3 center = texture(u_scene, vUV).rgb;

    float lNW = luma(nw), lNE = luma(ne), lSW = luma(sw), lSE = luma(se);
    float lCenter = luma(center);
    float lMin = min(lCenter, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lCenter, max(max(lNW, lNE), max(lSW, lSE)));
    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)),
                     (lNW + lSW) - (lNE + lSE));
    float reduce = max((lNW + lNE + lSW + lSE) * (0.25 * 0.125), 1.0 / 128.0);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcpDirMin, vec2(-8.0), vec2(8.0)) * texel;

    vec3 a = 0.5 * (texture(u_scene, vUV + dir * (1.0 / 3.0 - 0.5)).rgb +
                    texture(u_scene, vUV + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 b = a * 0.5 + 0.25 * (texture(u_scene, vUV + dir * -0.5).rgb +
                               texture(u_scene, vUV + dir *  0.5).rgb);
    float lB = luma(b);
    return (lB < lMin || lB > lMax) ? a : b;
}

void main() {
    vec3 color = fxaaScene();
    if (u_aoEnabled != 0) {
        float dFull = texelFetch(u_depth, ivec2(vUV * vec2(textureSize(u_scene, 0))), 0).r;

        vec2 m = vec2(1.0);                                 // sky stays untouched
        if (dFull < 0.9999) {
            float zf = viewDist(dFull);
            vec2 pos = vUV * u_aoSize - 0.5;                // continuous half-res texel coordinate
            ivec2 base = ivec2(floor(pos));
            vec2 fr = pos - vec2(base);
            ivec2 maxT = ivec2(u_aoSize) - 1;
            vec2 acc = vec2(0.0);
            float wsum = 0.0;
            float closest = 1e9;                            // best depth agreement of the four texels
            for (int j = 0; j < 2; ++j) {
                for (int i = 0; i < 2; ++i) {
                    vec4 t = texelFetch(u_ao, clamp(base + ivec2(i, j), ivec2(0), maxT), 0);
                    float bw = (i == 0 ? 1.0 - fr.x : fr.x) * (j == 0 ? 1.0 - fr.y : fr.y);
                    float rel = abs(zf - storedDist(t)) / max(zf, 0.1);
                    closest = min(closest, rel);
                    float w = bw / (0.003 + rel);
                    acc += t.rg * w;
                    wsum += w;
                }
            }
            m = acc / max(wsum, 1e-6);
            // The map was built from the depth buffer WITHOUT dynamic entities. A pixel whose depth
            // matches none of the four texels is an entity (character, horse, car): leave it alone,
            // it already has its own shading and shadow.
            if (closest > 0.04) m = vec2(1.0);
        }
        float ao = max(m.r, u_aoFloor);
        color *= mix(1.0, ao, u_aoStrength) * mix(1.0, m.g, u_shadowStrength);
    }
    FragColor = vec4(color, 1.0);
}
