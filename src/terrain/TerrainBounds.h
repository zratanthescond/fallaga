// TerrainBounds.h
#pragma once

#include <glm/glm.hpp>

/**
 * @brief Defines the world coordinate bounds for the Sousse terrain.
 *
 * The Sousse world spans X = [-1250, 1250] and Z = [-2500, 2500].
 * Y (height) bounds are optional and can be set after loading the
 * heightfield.
 */
struct TerrainBounds {
    float minX = -1250.0f;
    float maxX =  1250.0f;
    float minZ = -2500.0f;
    float maxZ =  2500.0f;
    float minY =  0.0f;   // will be overridden after loading height data
    float maxY =  0.0f;

    /**
     * @brief Convert a world-space (x, z) position to normalized UV coordinates [0,1].
     *
     * @param worldPos XZ world position (y is ignored).
     * @return glm::vec2 UV coordinates where (0,0) corresponds to (minX, minZ)
     *         and (1,1) corresponds to (maxX, maxZ).
     */
    glm::vec2 worldToUV(const glm::vec2 &worldPos) const {
        float u = (worldPos.x - minX) / (maxX - minX);
        float v = (worldPos.y - minZ) / (maxZ - minZ);
        return glm::vec2(u, v);
    }

    /**
     * @brief Convert normalized UV coordinates back to world-space XZ.
     *
     * @param uv UV coordinates in [0,1].
     * @return glm::vec2 world XZ position.
     */
    glm::vec2 uvToWorld(const glm::vec2 &uv) const {
        float x = minX + uv.x * (maxX - minX);
        float z = minZ + uv.y * (maxZ - minZ);
        return glm::vec2(x, z);
    }

    /**
     * @brief Checks whether a given world XZ coordinate lies inside the bounds.
     */
    bool isInside(const glm::vec2 &worldPos) const {
        return worldPos.x >= minX && worldPos.x <= maxX &&
               worldPos.y >= minZ && worldPos.y <= maxZ;
    }
};
