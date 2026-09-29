// TerrainSystem.h
#pragma once

#include "ITerrain.h"
#include "Heightfield.h"
#include "TerrainBounds.h"

/**
 * @brief Concrete implementation of ITerrain that wraps Heightfield.
 */
class TerrainSystem : public ITerrain {
public:
    TerrainSystem(const std::string &heightmapPath,
                  const TerrainBounds &bounds,
                  float minHeight,
                  float maxHeight);
    ~TerrainSystem() override = default;

    float getHeight(float x, float z) const override;
    glm::vec3 getNormal(float x, float z) const override;
    bool isInside(float x, float z) const override;
    void update(const glm::vec3 &cameraPos) override;
    void render(const class Camera &camera) override;

private:
    Heightfield heightfield;
    TerrainBounds bounds;
};
