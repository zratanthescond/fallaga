#version 330 core
out vec4 FragColor;
in vec3 viewDir;

uniform vec3 lightDir; // Sun/Moon direction
uniform float time;

// Simple hash for noise
float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

// 2D Noise
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

// Fractal Brownian Motion for clouds
float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    vec2 shift = vec2(100.0);
    for (int i = 0; i < 5; ++i) {
        v += a * noise(p);
        p = p * 2.0 + shift;
        a *= 0.5;
    }
    return v;
}

void main() {
    vec3 dir = normalize(viewDir);
    float sunTilt = lightDir.y; // Positive for day, negative for night
    
    // 1. DYNAMIC SKY COLORS
    vec3 dayTop    = vec3(0.1, 0.5, 0.9);
    vec3 dayBottom = vec3(0.5, 0.8, 1.0);
    vec3 sunsetTop = vec3(0.2, 0.1, 0.4);
    vec3 sunsetBottom = vec3(1.0, 0.4, 0.2);
    vec3 nightTop  = vec3(0.02, 0.05, 0.1);
    vec3 nightBottom = vec3(0.05, 0.1, 0.2);

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
    vec3 skyColor = mix(skyBottom, skyTop, pow(vertical, 0.8));

    // 2. PROCEDURAL CLOUDS
    vec2 cloudUV = dir.xz / (dir.y + 0.01);
    float cloudSpeed = time * 0.01;
    float cloudDensity = fbm(cloudUV * 0.5 + cloudSpeed);
    
    // Only show clouds above horizon
    float cloudMask = smoothstep(0.0, 0.2, dir.y);
    vec3 cloudColor = vec3(1.0, 1.0, 1.0);
    if (sunTilt < 0.0) cloudColor *= 0.3; // Darker at night
    else if (sunTilt < 0.2) cloudColor = mix(vec3(1.0, 0.6, 0.3), cloudColor, smoothstep(0.0, 0.2, sunTilt));

    skyColor = mix(skyColor, cloudColor, cloudDensity * cloudMask * 0.4);

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
