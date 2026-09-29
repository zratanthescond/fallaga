#pragma once

#include "Vec3.h"
#include <glm/glm.hpp>

// Forward declarations
class Terrain;
class Shader;

// ============================================================================
// GameplayUtils.h
// Centralized utility functions for character/vehicle gameplay logic
// Eliminates redundancy in state transitions, terrain interactions, and shaders
// ============================================================================

namespace GameplayUtils {

// ==== Terrain Interaction ====

// Snaps a position to the terrain surface with an optional vertical offset
inline void snapToTerrain(Vec3& position, Terrain* terrain, float verticalOffset = 0.01f) {
    if (terrain) {
        float terrainHeight = terrain->getGroundSurfaceHeight(position.x, position.z);
        position.y = terrainHeight + verticalOffset;
    }
}

// Gets terrain height at a given XZ position
inline float getTerrainHeightAt(Terrain* terrain, float x, float z) {
    if (terrain) {
        return terrain->getGroundSurfaceHeight(x, z);
    }
    return 0.0f;
}

// Checks if a position is above terrain (accounting for a ground clearance threshold)
inline bool isAboveTerrain(const Vec3& position, Terrain* terrain, float clearanceThreshold = 0.1f) {
    if (!terrain) return true;
    float terrainHeight = terrain->getGroundSurfaceHeight(position.x, position.z);
    return position.y > terrainHeight + clearanceThreshold;
}

// ==== Shader Setup ====

// Common setup pattern for entity shaders (character, horse, car)
// Reduces ~5 lines of repeated code per entity type
inline void setupEntityShader(Shader& shader,
                              const glm::vec3& viewPos,
                              const glm::vec3& lightDir,
                              const glm::vec3& lightColor) {
    shader.use();
    shader.setVec3("viewPos", viewPos);
    shader.setVec3("lightDir", lightDir);
    shader.setVec3("lightColor", lightColor);
}

// ==== Animation State Helpers ====

// Checks if character can perform general (non-special) actions
// Used to guard against simultaneous mount/drive states
inline bool canPerformGeneralAction(bool isRiding, bool isDriving,
                                    bool isEnteringCar, bool isExitingCar) {
    return !isRiding && !isDriving && !isEnteringCar && !isExitingCar;
}

// Checks if character is in any vehicular state
inline bool isInVehicularState(bool isRiding, bool isDriving,
                               bool isEnteringCar, bool isExitingCar) {
    return isRiding || isDriving || isEnteringCar || isExitingCar;
}

// Checks if character is transitioning into/out of a vehicle
inline bool isTransitioningVehicle(bool isEnteringCar, bool isExitingCar) {
    return isEnteringCar || isExitingCar;
}

} // namespace GameplayUtils
