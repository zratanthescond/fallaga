#ifndef WATER_UTILS_H
#define WATER_UTILS_H

#include <glm/glm.hpp>
#include <vector>
#include <cmath>
#include "WaterLevel.h"

/**
 * @brief Shared Gerstner Wave logic for both Physics (CPU) and Rendering (GPU).
 * This ensures that objects bob exactly with the visible wave crests.
 */
namespace WaterUtils {
    struct WaveLayer {
        glm::vec2 direction;
        float amplitude;
        float speed;
        float length;
        float steepness;
    };

    // Optimized 4-Layer Gerstner - Synchronized with new user ocean.vert (LOD 0)
    static const std::vector<WaveLayer> waveLayers = {
        { glm::normalize(glm::vec2(0.91f, 0.11f)),  1.00f, 1.40f, 48.0f, 0.90f },
        { glm::normalize(glm::vec2(-0.67f, 0.29f)), 0.60f, 1.90f, 29.0f, 0.80f },
        { glm::normalize(glm::vec2(0.43f, 0.87f)),  0.35f, 2.50f, 17.0f, 0.70f },
        { glm::normalize(glm::vec2(-0.13f,-0.97f)), 0.25f, 3.80f, 10.0f, 0.60f }
    };

    /**
     * @brief Calculates the vertical displacement for a given (x, z) at a specific time.
     * Matches the logic in ocean.vert but optimized for CPU performance.
     */
    inline float getWaveHeight(float x, float z, float time) {
        float height = TerrainConfig::kBaseWaterLevel;
        
        for (const auto& w : waveLayers) {
            float k = 2.0f * 3.14159265f / w.length;
            float f = k * (glm::dot(w.direction, glm::vec2(x, z)) - w.speed * time);
            height += w.amplitude * std::sin(f);
        }
        
        return height;
    }
}

#endif // WATER_UTILS_H
