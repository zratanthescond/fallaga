#version 460 core

// Screen-space ambient occlusion + screen-space sun contact shadows, in ONE half-res pass.
//
//   .r  = ambient occlusion      (1 = open, lower = occluded)
//   .g  = sun contact shadow     (1 = lit,  0 = in the shadow of something visible on screen)
//   .ba = this pixel's view distance, 16-bit over 0..256 m. The composite uses it to upsample
//         the half-res map without smearing it across depth edges (no extra full-res depth reads).
//
// The "ray tracing" here is a short ray march through the depth buffer towards the sun
// (screen-space ray tracing): cheap, and it gives the directional contact shadows that plain
// SSAO cannot (kerb, props, people, building bases).
//
// Cost control (this pass used to be the lag):
//   * 8 hemisphere samples instead of 32 (the 4x4 rotation tile + blur hide the noise).
//   * No mat4 * vec4 per sample: view-space z comes from the depth buffer with two scalar
//     operations, and xy from the projection terms.
//   * Everything beyond u_fadeEnd metres exits immediately. In an open world that is most of
//     the screen (terrain, sea, sky) and it costs one depth fetch.
//   * The shadow march is skipped for surfaces already facing away from the sun.
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D u_depth;
uniform sampler2D u_noise;
uniform mat4  u_proj;
uniform vec2  u_res;
uniform vec2  u_noiseScale;
uniform float u_radius;
uniform float u_bias;
uniform float u_power;

uniform float u_fadeStart;        // AO / shadow start fading at this view distance (m)
uniform float u_fadeEnd;          // ... and are skipped beyond it

uniform int   u_shadowEnabled;
uniform vec3  u_sunView;          // unit vector pointing TOWARDS the sun, in view space
uniform float u_shadowDist;       // how far (m) the ray travels towards the sun
uniform float u_shadowThickness;  // occluders thicker than this behind the ray are ignored (m)

#define KERNEL_SIZE 8
uniform vec3 u_kernel[KERNEL_SIZE];

#define SHADOW_STEPS 8

// Depth-buffer value -> view-space z (negative, camera looks down -z).
float viewZ(float d) {
    return -u_proj[3][2] / (d * 2.0 - 1.0 + u_proj[2][2]);
}

// View-space position of a screen uv at view depth z (perspective projection).
vec3 viewPos(vec2 uv, float z) {
    vec2 ndc = uv * 2.0 - 1.0;
    return vec3((-z * ndc.x - u_proj[2][0] * z) / u_proj[0][0],
                (-z * ndc.y - u_proj[2][1] * z) / u_proj[1][1],
                z);
}

// View-space position -> screen uv.
vec2 projectUV(vec3 v) {
    float invW = 1.0 / -v.z;
    return vec2((u_proj[0][0] * v.x + u_proj[2][0] * v.z) * invW,
                (u_proj[1][1] * v.y + u_proj[2][1] * v.z) * invW) * 0.5 + 0.5;
}

vec4 packResult(float ao, float shadow, float dist) {
    float q = floor(clamp(dist / 256.0, 0.0, 1.0) * 65535.0 + 0.5);
    float hi = floor(q / 256.0);
    return vec4(ao, shadow, hi / 255.0, (q - hi * 256.0) / 255.0);
}

void main() {
    float d0 = texture(u_depth, vUV).r;
    // Sky / far plane: nothing to occlude.
    if (d0 >= 0.9999) { FragColor = packResult(1.0, 1.0, 256.0); return; }

    float z0 = viewZ(d0);
    // Far pixels are cheap: one fetch and out.
    if (-z0 > u_fadeEnd) { FragColor = packResult(1.0, 1.0, -z0); return; }
    float fade = 1.0 - smoothstep(u_fadeStart, u_fadeEnd, -z0);

    vec2 texel = 1.0 / u_res;
    vec3 p = viewPos(vUV, z0);

    // Normal from the four neighbours. Per axis, use the neighbour that is closer in depth
    // so a silhouette edge does not produce an edge-aligned garbage normal.
    float zr = viewZ(texture(u_depth, vUV + vec2(texel.x, 0.0)).r);
    float zl = viewZ(texture(u_depth, vUV - vec2(texel.x, 0.0)).r);
    float zu = viewZ(texture(u_depth, vUV + vec2(0.0, texel.y)).r);
    float zd = viewZ(texture(u_depth, vUV - vec2(0.0, texel.y)).r);
    vec3 ddx = (abs(zr - z0) < abs(zl - z0)) ? viewPos(vUV + vec2(texel.x, 0.0), zr) - p
                                             : p - viewPos(vUV - vec2(texel.x, 0.0), zl);
    vec3 ddy = (abs(zu - z0) < abs(zd - z0)) ? viewPos(vUV + vec2(0.0, texel.y), zu) - p
                                             : p - viewPos(vUV - vec2(0.0, texel.y), zd);
    vec3 n = cross(ddx, ddy);
    float nl2 = dot(n, n);
    n = (nl2 > 1e-20) ? n * inversesqrt(nl2) : vec3(0.0, 0.0, 1.0);   // never NaN
    if (dot(n, -p) < 0.0) n = -n;                                      // face the camera

    // ---------------------------------------------------------------- ambient occlusion
    vec3 randomVec = texture(u_noise, vUV * u_noiseScale).xyz;
    vec3 tangent = randomVec - n * dot(randomVec, n);
    float tl2 = dot(tangent, tangent);
    tangent = (tl2 > 1e-8) ? tangent * inversesqrt(tl2)
                           : normalize(cross(n, abs(n.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
    mat3 TBN = mat3(tangent, cross(n, tangent), n);

    // Depth precision and grazing angles get worse with distance, which shows up as bands of
    // false occlusion (self-shadowing) on flat ground / long walls. Scale the bias with the
    // view distance and start the hemisphere slightly above the surface.
    float bias = u_bias + 0.0025 * -z0;
    vec3  pAO  = p + n * bias;

    float occ = 0.0;
    for (int i = 0; i < KERNEL_SIZE; ++i) {
        vec3 sp = pAO + (TBN * u_kernel[i]) * u_radius;
        if (sp.z > -0.05) continue;                       // behind / at the camera plane
        float sz = viewZ(textureLod(u_depth, projectUV(sp), 0.0).r);
        float rangeCheck = smoothstep(0.0, 1.0, u_radius / max(abs(z0 - sz), 1e-4));
        occ += (sz >= sp.z + bias ? 1.0 : 0.0) * rangeCheck;
    }
    float ao = pow(clamp(1.0 - occ / float(KERNEL_SIZE), 0.0, 1.0), u_power);
    ao = mix(1.0, ao, fade);

    // ------------------------------------------------- screen-space sun contact shadow
    // Contact shadows only matter up close: half the AO range, so the march costs nothing
    // on the distant terrain.
    float shadow = 1.0;
    float shadowFade = 1.0 - smoothstep(u_fadeStart * 0.5, u_fadeEnd * 0.5, -z0);
    if (u_shadowEnabled != 0 && shadowFade > 0.0 && dot(n, u_sunView) > 0.02) {   // facing away from the sun = already dark
        // Steps are spaced quadratically: tiny steps right at the contact point (crisp, accurate
        // shadow edge, no gaps under thin props) and long steps further out. No jitter, so the
        // shadow needs no blur and stays sharp.
        vec3 origin = p + n * bias;               // lift off the surface: no self-shadowing
        float lit = 1.0;
        for (int i = 0; i < SHADOW_STEPS; ++i) {
            float f = (float(i) + 1.0) / float(SHADOW_STEPS);
            float t = u_shadowDist * f * f;
            vec3 rp = origin + u_sunView * t;
            if (rp.z > -0.05) break;
            vec2 uv = projectUV(rp);
            if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;   // ray left the screen
            float sz = viewZ(textureLod(u_depth, uv, 0.0).r);
            float gap = sz - rp.z;                // > 0: the visible surface is in front of the ray
            if (gap > 0.03 && gap < u_shadowThickness) {
                lit = 0.0;                         // hard, crisp contact shadow
                break;
            }
        }
        shadow = mix(1.0, lit, shadowFade);
    }

    FragColor = packResult(ao, shadow, -z0);
}
