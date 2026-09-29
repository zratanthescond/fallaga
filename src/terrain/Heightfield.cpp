// Heightfield.cpp
#include "Heightfield.h"
#include <stb_image.h>
#include <algorithm>
#include <iostream>
#include <cmath>

Heightfield::Heightfield(const std::string &path, const TerrainBounds &b) {
    load(path, b);
}

bool Heightfield::load(const std::string &path, const TerrainBounds &b) {
    bounds = b;
    int w = 0, h = 0, ch = 0;
    stbi_set_flip_vertically_on_load(false);

    // Try 16-bit first (native precision for heightmaps)
    unsigned short *data16 = stbi_load_16(path.c_str(), &w, &h, &ch, 1);
    if (data16) {
        width = w;
        height = h;
        data.resize(w * h);
        float invDivisor = 1.0f / 65535.0f;
        float heightRange = bounds.heightRange();
        for (int i = 0; i < w * h; ++i) {
            data[i] = bounds.minHeight + (data16[i] * invDivisor) * heightRange;
        }
        stbi_image_free(data16);
        std::cout << "[Heightfield] Loaded 16-bit heightmap: " << path
                  << " (" << w << "x" << h << ") elevation ["
                  << bounds.minHeight << " .. " << bounds.maxHeight << "]m" << std::endl;
        return true;
    }

    // Fallback: 8-bit heightmap
    unsigned char *data8 = stbi_load(path.c_str(), &w, &h, &ch, 1);
    if (data8) {
        width = w;
        height = h;
        data.resize(w * h);
        float invDivisor = 1.0f / 255.0f;
        float heightRange = bounds.heightRange();
        for (int i = 0; i < w * h; ++i) {
            data[i] = bounds.minHeight + (data8[i] * invDivisor) * heightRange;
        }
        stbi_image_free(data8);
        std::cout << "[Heightfield] Loaded 8-bit heightmap: " << path
                  << " (" << w << "x" << h << ") elevation ["
                  << bounds.minHeight << " .. " << bounds.maxHeight << "]m" << std::endl;
        return true;
    }

    std::cerr << "[Heightfield] FAILED to load heightmap: " << path << std::endl;
    return false;
}

float Heightfield::sample(float x, float z) const {
    if (data.empty() || width <= 1 || height <= 1) return bounds.minHeight;

    glm::vec2 uv = bounds.worldToUV(x, z);
    return sampleUV(uv.x, uv.y);
}

float Heightfield::sampleUV(float u, float v) const {
    if (data.empty() || width <= 1 || height <= 1) return bounds.minHeight;

    float ux = std::clamp(u, 0.0f, 1.0f);
    float uz = std::clamp(v, 0.0f, 1.0f);

    float fx = ux * static_cast<float>(width - 1);
    float fz = uz * static_cast<float>(height - 1);

    int x0 = static_cast<int>(fx);
    int z0 = static_cast<int>(fz);
    int x1 = std::min(x0 + 1, width - 1);
    int z1 = std::min(z0 + 1, height - 1);

    float tx = fx - static_cast<float>(x0);
    float tz = fz - static_cast<float>(z0);

    float h00 = data[z0 * width + x0];
    float h10 = data[z0 * width + x1];
    float h01 = data[z1 * width + x0];
    float h11 = data[z1 * width + x1];

    return h00 * (1.0f - tx) * (1.0f - tz) +
           h10 * tx * (1.0f - tz) +
           h01 * (1.0f - tx) * tz +
           h11 * tx * tz;
}

glm::vec3 Heightfield::sampleNormal(float x, float z, float sampleDistance) const {
    float hL = sample(x - sampleDistance, z);
    float hR = sample(x + sampleDistance, z);
    float hS = sample(x, z - sampleDistance);
    float hN = sample(x, z + sampleDistance);
    return glm::normalize(glm::vec3(hL - hR, 2.0f * sampleDistance, hS - hN));
}
