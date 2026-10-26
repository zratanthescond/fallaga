#version 460 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoords;

out vec2  TexCoords;
out vec3  FragPos;
out vec3  Normal;

uniform mat4  model;
uniform mat4  view;
uniform mat4  projection;
uniform float time;
uniform vec3  viewPos;

// Heightmap uniforms for shoreline wave fading
uniform sampler2D heightmapTex;
uniform int useHeightmap;
uniform float hmMinElev;
uniform float hmMaxElev;
uniform float hmWorldMinX;
uniform float hmWorldMaxX;
uniform float hmWorldMinZ;
uniform float hmWorldMaxZ;
uniform float waterLevel;
uniform sampler2D waterMapTex;
uniform sampler2D roadMask;

void addGerstnerWave(vec3 basePos, vec2 d, float a, float s, float l, float q, 
                     inout vec3 pos, inout vec3 tangent, inout vec3 binormal) {
    float k = 2.0 * 3.14159265 / l;
    float f = k * (dot(d, basePos.xz) - s * time);
    float cosF = cos(f);
    float sinF = sin(f);
    
    pos.x += d.x * (q * a * cosF);
    pos.y += a * sinF;
    pos.z += d.y * (q * a * cosF);
    
    tangent.x -= q * d.x * d.x * k * a * sinF;
    tangent.y += d.x * k * a * cosF;
    tangent.z -= q * d.x * d.y * k * a * sinF;
    
    binormal.x -= q * d.x * d.y * k * a * sinF;
    binormal.y += d.y * k * a * cosF;
    binormal.z -= q * d.y * d.y * k * a * sinF;
}

void main() {
    vec3 pos = aPos;
    vec4 simState = texture(waterMapTex, aTexCoords);
    float surfaceLevel = simState.g > 0.001 ? simState.r : waterLevel;
    // The original shared surface follows simulated local stages. The simulation
    // map and this grid use the same resolution; no per-stream mesh is generated.
    pos.y = surfaceLevel;
    vec3 tangent = vec3(1.0, 0.0, 0.0);
    vec3 binormal = vec3(0.0, 0.0, 1.0);
    
    float dist = length(viewPos - aPos);
    float fade3 = 1.0 - smoothstep(100.0, 300.0, dist);
    float fade4 = 1.0 - smoothstep(60.0, 200.0, dist);
    float fade5 = 1.0 - smoothstep(30.0, 100.0, dist);
    float fade6 = 1.0 - smoothstep(15.0, 50.0, dist);

    // Depth-based wave scaling to fade waves at the shoreline
    float waveScale = 1.0;
    float isRiver = 0.0;
    if (useHeightmap == 1) {
        float u = (aPos.x - hmWorldMinX) / (hmWorldMaxX - hmWorldMinX);
        float v = (aPos.z - hmWorldMinZ) / (hmWorldMaxZ - hmWorldMinZ);
        if (u >= 0.0 && u <= 1.0 && v >= 0.0 && v <= 1.0) {
            float normElev = texture(heightmapTex, vec2(u, v)).r;
            float terrainH = hmMinElev + normElev * (hmMaxElev - hmMinElev);
            isRiver = max(texture(roadMask, vec2(u, v)).b,
                          smoothstep(1.5, 2.5, simState.b));
            
            float depth = max(0.0, surfaceLevel - terrainH);
            waveScale = clamp(depth / 3.5, 0.0, 1.0);
        }
    }
    
    // Long-period ocean swells only (wavelengths >= 160m to prevent grid aliasing on the 54m mesh)
    // Micro and medium waves are handled with photorealistic normal maps in the fragment shader
    float oceanSwellScale = waveScale * (1.0 - smoothstep(0.1, 0.6, isRiver) * 0.90);
    addGerstnerWave(aPos, normalize(vec2(-0.95,  0.08)), 0.35 * oceanSwellScale, 1.10, 260.0, 0.35, pos, tangent, binormal);
    addGerstnerWave(aPos, normalize(vec2(-0.85,  0.35)), 0.20 * oceanSwellScale, 1.30, 160.0, 0.30, pos, tangent, binormal);
    
    FragPos     = vec3(model * vec4(pos, 1.0));
    TexCoords   = aTexCoords * 1115.04;
    
    vec3 rawNormal = normalize(cross(binormal, tangent));
    Normal = normalize(mat3(transpose(inverse(model))) * rawNormal);
    
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
