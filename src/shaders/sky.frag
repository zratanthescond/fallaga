#version 460 core
out vec4 FragColor;
in vec3 viewDir;

uniform vec3 lightDir; // Sun/Moon direction
uniform float time;
uniform sampler2D cloudNoise;

void main() {
    vec3 dir = normalize(viewDir);
    float sunTilt = lightDir.y; // Positive for day, negative for night

    // 1. DYNAMIC SKY COLORS (Cinematic RDR2 Big Valley Atmosphere)
    vec3 dayTop    = vec3(0.04, 0.28, 0.72); // Deep rich zenith blue
    vec3 dayBottom = vec3(0.48, 0.72, 0.92); // Horizon sky
    vec3 sunsetTop = vec3(0.18, 0.12, 0.38);
    vec3 sunsetBottom = vec3(0.98, 0.42, 0.18);
    vec3 nightTop  = vec3(0.015, 0.03, 0.08);
    vec3 nightBottom = vec3(0.04, 0.08, 0.16);

    vec3 skyTop, skyBottom;
    
    if (sunTilt > 0.1) {
        // Day
        float mixFactor = smoothstep(0.1, 0.5, sunTilt);
        skyTop = mix(sunsetTop, dayTop, mixFactor);
        skyBottom = mix(sunsetBottom, dayBottom, mixFactor);
    } else if (sunTilt > -0.1) {
        // Sunrise/Sunset
        float mixFactor = smoothstep(-0.1, 0.1, sunTilt);
        skyTop = mix(nightTop, sunsetTop, mixFactor);
        skyBottom = mix(nightBottom, sunsetBottom, mixFactor);
    } else {
        // Night
        skyTop = nightTop;
        skyBottom = nightBottom;
    }

    float vertical = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 skyColor = mix(skyBottom, skyTop, pow(vertical, 0.85));

    // 2. PROCEDURAL CLOUDS (Sculpted cumulus with silver tops and shaded bases)
    float cloudDensity = 0.0;
    float cloudShading = 1.0;
    if (dir.y > 0.02) {
        vec2 cloudUV = dir.xz / (dir.y + 0.04);
        float cloudSpeed = time * 0.005;
        float rawNoise = texture(cloudNoise, cloudUV * 0.40 + cloudSpeed).r;
        float fineNoise = texture(cloudNoise, cloudUV * 1.10 - cloudSpeed * 1.5).r;
        float combined = rawNoise * 0.75 + fineNoise * 0.25;
        cloudDensity = smoothstep(0.40, 0.76, combined);
        cloudShading = smoothstep(0.35, 0.85, rawNoise);
    }

    // Only show clouds above horizon with realistic volume lighting
    float cloudMask = smoothstep(0.02, 0.25, dir.y);
    vec3 cloudLit   = vec3(1.0, 1.0, 0.98);
    vec3 cloudShade = vec3(0.48, 0.55, 0.68);
    vec3 cloudColor = mix(cloudShade, cloudLit, cloudShading);
    if (sunTilt < 0.0) cloudColor *= 0.25; // Darker at night
    else if (sunTilt < 0.2) cloudColor = mix(vec3(0.95, 0.55, 0.28), cloudColor, smoothstep(0.0, 0.2, sunTilt));

    skyColor = mix(skyColor, cloudColor, cloudDensity * cloudMask * 0.88);

    // 3. SUN AND MOON
    float sunTheta = max(dot(dir, normalize(lightDir)), 0.0);
    float moonTheta = max(dot(dir, normalize(-lightDir)), 0.0);
    
    // Sun
    float sunDisc = smoothstep(0.997, 0.999, sunTheta);
    float sunGlow = pow(sunTheta, 64.0) * 0.8;
    vec3 sunFinal = vec3(1.0, 0.95, 0.8) * (sunDisc * 10.0 + sunGlow) * smoothstep(-0.1, 0.1, sunTilt);
    
    // Moon
    float moonDisc = smoothstep(0.998, 0.999, moonTheta);
    float moonGlow = pow(moonTheta, 32.0) * 0.2;
    vec3 moonFinal = vec3(0.8, 0.9, 1.0) * (moonDisc * 2.0 + moonGlow) * smoothstep(0.1, -0.1, sunTilt);

    vec3 finalColor = skyColor + sunFinal + moonFinal;
    
    // Horizon fade
    float horizonMask = smoothstep(-0.1, 0.1, dir.y);
    finalColor = mix(skyBottom, finalColor, horizonMask);

    FragColor = vec4(finalColor, 1.0);
}
