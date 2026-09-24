// TerrainBounds.h
#pragma once
#ifndef TERRAIN_BOUNDS_DEFINED
#define TERRAIN_BOUNDS_DEFINED

#include <glm/glm.hpp>
#include <algorithm>

/**
 * @brief Defines the world coordinate bounds and elevation range for the Sousse terrain.
 *
 * Real Sousse world bounds match TerrainManager / heightmap_meta.txt:
 * X = [-13938, 13938]
 * Z = [-13938, 13938]
 * Height range = [-70.0, 87.8]
 */
struct TerrainBounds {
    float minX = -13938.0f;
    float maxX =  13938.0f;
    float minZ = -13938.0f;
    float maxZ =  13938.0f;
    float minHeight = -70.0f;
    float maxHeight =  87.8f;

    float worldExtentX() const { return maxX - minX; }
    float worldExtentZ() const { return maxZ - minZ; }
    float heightRange() const { return maxHeight - minHeight; }

    /**
     * @brief Convert a world-space (x, z) position to normalized UV coordinates [0,1].
     * (0,0) corresponds to (minX, minZ) and (1,1) corresponds to (maxX, maxZ).
     */
    glm::vec2 worldToUV(float x, float z) const {
        float u = (x - minX) / (maxX - minX);
        float v = (z - minZ) / (maxZ - minZ);
        return glm::vec2(u, v);
    }

    /**
     * @brief Overload taking glm::vec2 where .x is world X and .y is world Z.
     */
    glm::vec2 worldToUV(const glm::vec2 &worldPosXZ) const {
        return worldToUV(worldPosXZ.x, worldPosXZ.y);
    }

    /**
     * @brief Convert normalized UV coordinates [0,1] back to world-space (x, z).
     */
    glm::vec2 uvToWorld(float u, float v) const {
        float x = minX + u * (maxX - minX);
        float z = minZ + v * (maxZ - minZ);
        return glm::vec2(x, z);
    }

    glm::vec2 uvToWorld(const glm::vec2 &uv) const {
        return uvToWorld(uv.x, uv.y);
    }

    /**
     * @brief Checks whether a given world coordinate (x, z) lies inside the bounds.
     */
    bool isInside(float x, float z) const {
        return x >= minX && x <= maxX && z >= minZ && z <= maxZ;
    }

    bool isInside(const glm::vec2 &worldPosXZ) const {
        return isInside(worldPosXZ.x, worldPosXZ.y);
    }
};

#endif // TERRAIN_BOUNDS_DEFINED
