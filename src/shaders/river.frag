#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec2 FlowDirection;
in vec2 RiverUV;
in float FlowSpeed;
in float RiverDepth;

uniform float time;
uniform float seaLevel;
uniform vec3 cameraPos;
uniform vec3 lightDir;
uniform vec3 lightColor;
uniform sampler2D normalMap1;
uniform sampler2D normalMap2;
uniform sampler2D foamTexture;

void main() {
    vec2 flow = normalize(FlowDirection);
    vec2 uv = RiverUV * 0.035;
    float scroll = time * (0.025 + FlowSpeed * 0.055);
    vec3 normalA = texture(normalMap1, uv - flow * scroll).rgb * 2.0 - 1.0;
    vec3 normalB = texture(normalMap2, uv * 1.9 - flow * scroll * 1.35).rgb * 2.0 - 1.0;
    vec3 surfaceNormal = normalize(vec3((normalA.xy + normalB.xy * 0.55) * 0.24, 1.0));

    float depthTint = clamp(RiverDepth / 2.3, 0.0, 1.0);
    vec3 shallow = vec3(0.20, 0.43, 0.39);
    vec3 deep = vec3(0.035, 0.19, 0.20);
    vec3 waterColor = mix(shallow, deep, smoothstep(0.05, 1.0, depthTint));

    vec3 viewDir = normalize(cameraPos - FragPos);
    vec3 sunDir = normalize(lightDir);
    float diffuse = max(dot(surfaceNormal, sunDir), 0.0);
    float fresnel = pow(1.0 - max(dot(surfaceNormal, viewDir), 0.0), 4.0);
    float sparkle = pow(max(dot(reflect(-sunDir, surfaceNormal), viewDir), 0.0), 48.0);
    vec3 color = waterColor * (0.58 + diffuse * 0.36 * max(lightColor.r, 0.25));
    color += vec3(0.32, 0.48, 0.48) * (fresnel * 0.36 + sparkle * 0.5);

    float foamNoise = texture(foamTexture, uv * 2.0 - flow * scroll * 1.8).r;
    float foamAmount = smoothstep(0.72, 0.96, foamNoise) *
                       smoothstep(0.55, 2.6, FlowSpeed) *
                       (1.0 - smoothstep(1.2, 2.3, RiverDepth));
    color = mix(color, vec3(0.78, 0.84, 0.80), foamAmount * 0.42);

    // Fade the river ribbon into the ocean at sea mouths.
    float mouthFade = smoothstep(seaLevel - 0.25, seaLevel + 3.0, FragPos.y);
    float alpha = 0.88 * mouthFade;
    if (alpha < 0.015) discard;
    FragColor = vec4(color, alpha);
}
