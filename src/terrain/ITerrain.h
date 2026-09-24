// ITerrain.h
#pragma once

#include <glm/glm.hpp>

/**
 * @brief Abstract interface for terrain queries.
 *
 * Existing gameplay code (Character, Car, etc.) currently calls
 * `Terrain::getHeight` and `Terrain::getNormal`. By introducing this
 * interface, the legacy system can be wrapped and the new system can be
 * swapped in without touching the callers.
 */
class ITerrain {
public:
    virtual ~ITerrain() = default;

    /**
     * @brief Returns the height (Y) at the given world XZ coordinate.
     * @return Height value in world units.
     */
    virtual float getHeight(float x, float z) const = 0;

    /**
     * @brief Returns the normal vector at the given world XZ coordinate.
     * @return Normalized glm::vec3 where Y points up.
     */
    virtual glm::vec3 getNormal(float x, float z) const = 0;

    /**
     * @brief Checks whether the coordinate lies inside the terrain.
     */
    virtual bool isInside(float x, float z) const = 0;

    /**
     * @brief Update any dynamic terrain state (e.g., LOD) based on camera.
     */
    virtual void update(const glm::vec3 &cameraPos) = 0;

    /**
     * @brief Render the terrain using the provided camera.
     */
    virtual void render(const class Camera &camera) = 0;
};
