#version 460 core

// Blur of the half-res AO map. Four bilinear taps cover an exact 4x4 texel box (the same area as
// the 4x4 noise tile, so the rotation noise cancels out) for 4 fetches instead of the 25 the old
// 5x5 loop needed. Requires GL_LINEAR on the input texture.
// The sun contact shadow (.g) and the stored depth (.ba) are NOT blurred: the shadow is noise-free
// so it stays crisp, and the depth must stay exact for the composite's edge-aware upsample.
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D u_ao;
uniform vec2 u_res;

void main() {
    vec2 texel = 1.0 / u_res;
    float ao = texture(u_ao, vUV + vec2(-1.5, -1.5) * texel).r
             + texture(u_ao, vUV + vec2( 0.5, -1.5) * texel).r
             + texture(u_ao, vUV + vec2(-1.5,  0.5) * texel).r
             + texture(u_ao, vUV + vec2( 0.5,  0.5) * texel).r;
    vec4 c = texture(u_ao, vUV);          // exact centre texel (bilinear at a texel centre)
    FragColor = vec4(ao * 0.25, c.g, c.b, c.a);
}
