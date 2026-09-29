// TerrainSystem.cpp
#include "TerrainSystem.h"
#include "../Camera.h"

TerrainSystem::TerrainSystem(const std::string &heightmapPath,
                             const TerrainBounds &b,
                             float minHeight,
                             float maxHeight)
    : bounds(b),
      heightfield(heightmapPath, b) {
    (void)minHeight;
    (void)maxHeight;
}

float TerrainSystem::getHeight(float x, float z) const {
    if (!isInside(x, z)) return 0.0f;
    return heightfield.sample(x, z);
}

glm::vec3 TerrainSystem::getNormal(float x, float z) const {
    if (!isInside(x, z)) return glm::vec3(0.0f, 1.0f, 0.0f);
    return heightfield.sampleNormal(x, z);
}

bool TerrainSystem::isInside(float x, float z) const {
    return bounds.isInside(x, z);
}

void TerrainSystem::update(const glm::vec3 &cameraPos) {
    // Placeholder: future LOD updates could be driven here.
    (void)cameraPos; // suppress unused warning
}

void TerrainSystem::render(const Camera &camera) {
    // In a full system we would render terrain chunks
    (void)camera; // suppress unused warning for now
}
