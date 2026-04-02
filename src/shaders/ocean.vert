#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 2) in vec2 aTexCoords;

out vec2  TexCoords;
out vec3  FragPos;
out float WaveHeight;
out float Compression;

uniform mat4  model;
uniform mat4  view;
uniform mat4  projection;
uniform float time;

vec3 GerstnerWave(vec3 p, vec2 d, float a, float s, float l, float q, inout float comp) {
    float k = 2.0 * 3.14159 / l;
    float f = k * (dot(d, p.xz) - s * time);
    float q_norm = q / (k * a * 6.0);
    comp += q * k * a * cos(f);
    return vec3(
        d.x * (q_norm * a * cos(f)),
        a * sin(f),
        d.y * (q_norm * a * cos(f))
    );
}

void main() {
    vec3 pos = aPos;
    float comp = 0.0;

    // 6-Layer Gerstner
    pos += GerstnerWave(aPos, normalize(vec2(0.91, 0.11)),  0.60, 1.15, 53.0, 0.8, comp);
    pos += GerstnerWave(aPos, normalize(vec2(-0.67, 0.29)), 0.30, 1.45, 31.0, 0.6, comp);
    pos += GerstnerWave(aPos, normalize(vec2(0.43, 0.87)),  0.18, 1.85, 19.0, 0.5, comp);
    pos += GerstnerWave(aPos, normalize(vec2(-0.13,-0.97)), 0.12, 2.55, 11.0, 0.4, comp);
    pos += GerstnerWave(aPos, normalize(vec2(0.79,-0.41)),  0.07, 3.25, 6.7,  0.3, comp);
    pos += GerstnerWave(aPos, normalize(vec2(-0.37, 0.53)), 0.04, 4.15, 3.7,  0.2, comp);

    WaveHeight  = pos.y;
    Compression = comp;
    FragPos     = vec3(model * vec4(pos, 1.0));
    TexCoords   = aTexCoords;
    gl_Position = projection * view * vec4(FragPos, 1.0);
}