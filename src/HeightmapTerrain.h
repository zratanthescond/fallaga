#pragma once
#include <vector>
#include <string>
#include <GL/glew.h>
#include <glm/glm.hpp>

// ─────────────────────────────────────────────────────────────────────────────
//  HeightmapTerrain
//
//  Loads a grayscale PNG heightmap and builds a dense triangle-strip terrain
//  mesh. Exposes the same getHeightAt() / getNormalAt() interface that
//  ObjModel previously provided, so callers need no changes.
//
//  World coordinate system (matching the rest of the engine):
//    X:  worldMinX .. worldMaxX  (West → East, coast to the East)
//    Y:  minHeight .. maxHeight  (game-unit elevation)
//    Z:  worldMinZ .. worldMaxZ  (South → North)
// ─────────────────────────────────────────────────────────────────────────────
class HeightmapTerrain {
public:
    // heightmapPath : path to 8-bit grayscale PNG
    // worldMinX/MaxX, worldMinZ/MaxZ : game-world extent covered by the map
    // minHeight, maxHeight : real elevation mapped to PNG [0,255]
    HeightmapTerrain(const char* heightmapPath,
                     float worldMinX, float worldMaxX,
                     float worldMinZ, float worldMaxZ,
                     float minHeight, float maxHeight);

    ~HeightmapTerrain();

    // Render the terrain mesh (VAO-based)
    void render() const;

    // Sample terrain height at (x, z) — same interface as ObjModel::getHeightAt
    float     getHeightAt  (float x, float z) const;
    glm::vec3 getNormalAt  (float x, float z) const;

    // World-space extents (read-only)
    float worldMinX, worldMaxX, worldMinZ, worldMaxZ;
    float minHeight, maxHeight;

    GLuint getTextureID() const { return heightmapTex; }

private:
    // Raw height grid [row * gridW + col], row 0 = south (Z = worldMinZ)
    std::vector<float> heights;
    int gridW = 0;   // number of columns (X axis)
    int gridH = 0;   // number of rows    (Z axis)

    // OpenGL
    GLuint VAO = 0, VBO = 0, EBO = 0;
    GLuint heightmapTex = 0;
    int indexCount = 0;

    // Helpers
    bool loadPNG(const char* path);
    void buildMesh();

    // Bilinear sample of heights[] at normalised coords (ux, uz) in [0,1]
    float sampleNorm(float ux, float uz) const;
};
