// Heightfield.h
#pragma once

#include "TerrainBounds.h"
#include <string>
#include <vector>
#include <glm/glm.hpp>

/**
 * @brief Pure data heightfield representation loaded from an image file.
 *
 * Provides thread-safe, CPU-only elevation and normal sampling
 * without requiring any OpenGL contexts or GPU buffers.
 */
class Heightfield {
public:
    Heightfield() = default;
    Heightfield(const std::string &path, const TerrainBounds &bounds);

    /**
     * @brief Load height data from an 8-bit or 16-bit PNG.
     * @param path Path to the heightmap image.
     * @param bounds World bounds and elevation range.
     * @return True if loaded successfully, false otherwise.
     */
    bool load(const std::string &path, const TerrainBounds &bounds);

    /**
     * @brief Bilinear elevation query in world coordinates.
     * @param x World X coordinate.
     * @param z World Z coordinate.
     * @return Interpolated height Y in world units.
     */
    float sample(float x, float z) const;

    /**
     * @brief Bilinear elevation query with normalized UV coordinates [0, 1].
     */
    float sampleUV(float u, float v) const;

    /**
     * @brief Calculate surface normal vector using central differences.
     * @param x World X coordinate.
     * @param z World Z coordinate.
     * @param sampleDistance Distance in world units for gradient computation.
     * @return Normalized normal vector pointing upward (+Y).
     */
    glm::vec3 sampleNormal(float x, float z, float sampleDistance = 1.0f) const;

    bool isInside(float x, float z) const { return bounds.isInside(x, z); }
    const TerrainBounds& getBounds() const { return bounds; }
    void setBounds(const TerrainBounds &b) { bounds = b; }

    int getWidth() const { return width; }
    int getHeight() const { return height; }
    bool isLoaded() const { return !data.empty() && width > 0 && height > 0; }
    const std::vector<float>& getData() const { return data; }

private:
    std::vector<float> data; // Elevation values in world units (meters)
    int width = 0;
    int height = 0;
    TerrainBounds bounds;
};
