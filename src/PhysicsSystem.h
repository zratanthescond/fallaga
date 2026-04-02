#ifndef PHYSICS_SYSTEM_H
#define PHYSICS_SYSTEM_H

#include "Vec3.h"
#include "Terrain.h"

class PhysicsSystem {
public:
    static constexpr float GRAVITY = -98.1f; // Standard gravity scaled for the world
    
    /**
     * @brief Applies gravity and snaps an entity to the terrain.
     * 
     * @param position Current position (updated)
     * @param velocity Current velocity (updated Y component)
     * @param deltaTime Time since last frame
     * @param terrain Pointer to the terrain for height sampling
     * @param groundOffset Minimum height above terrain (e.g., 0.01f)
     */
    static void applyGravity(Vec3& position, Vec3& velocity, float deltaTime, const Terrain* terrain, float groundOffset = 0.0f) {
        if (!terrain) return;

        // 1. Apply Gravity to Y Velocity
        velocity.y += GRAVITY * deltaTime;

        // 2. Update Y Position
        position.y += velocity.y * deltaTime;

        // 3. Collision with Terrain
        float terrainHeight = terrain->getHeight(position.x, position.z);
        if (position.y < terrainHeight + groundOffset) {
            position.y = terrainHeight + groundOffset;
            velocity.y = 0.0f; // Stop falling
        }
    }

    /**
     * @brief Instantly snaps an entity to the terrain height. 
     * Useful for spawning buildings or objects that don't move vertically.
     * 
     * @param position Position to snap (updated)
     * @param terrain Pointer to the terrain
     * @param groundOffset Vertical offset from the surface
     */
    static void snapToGround(Vec3& position, const Terrain* terrain, float groundOffset = 0.0f) {
        if (!terrain) return;
        position.y = terrain->getHeight(position.x, position.z) + groundOffset;
    }
};

#endif // PHYSICS_SYSTEM_H
