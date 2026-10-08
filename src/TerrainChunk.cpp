#include "TerrainChunk.h"
#include "WaterLevel.h"
#include <algorithm>
#include <iostream>

TerrainChunk::TerrainChunk(int x, int z, int size) : gridX(x), gridZ(z), size(size) {}
TerrainChunk::~TerrainChunk() {
    if (VAO) glDeleteVertexArrays(1, &VAO);
    if (VBO) glDeleteBuffers(1, &VBO);
    if (EBO) glDeleteBuffers(1, &EBO);
}

void TerrainChunk::generateMesh(const std::vector<float>& globalHeights, int globalW, int globalH,
                                float worldMinX, float worldMaxX, float worldMinZ, float worldMaxZ,
                                const std::vector<unsigned char>* roadMaskData, int roadW, int roadH,
                                const std::function<int(int, int)>* refineLevel,
                                const HeightFn& sampleHeight) {
    // Keep this in sync with Terrain.cpp's WATER_BASE. Subdivide only cells the
    // sea-level contour crosses; sample their existing bilinear height surface so
    // the shoreline gains geometric resolution without editing the heightmap.
    constexpr float kShorelineLevel = TerrainConfig::kBaseWaterLevel;
    constexpr int kShorelineRefinementLevel = 3; // 8x8 subcells, about 1.4 m per edge
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;

    int startCol = gridX * (size - 1);
    int startRow = gridZ * (size - 1);

    float worldWidth = worldMaxX - worldMinX;
    float worldHeight = worldMaxZ - worldMinZ;
    const float pixelSpacingX = worldWidth / std::max(1, globalW - 1);
    const float pixelSpacingZ = worldHeight / std::max(1, globalH - 1);

    glm::vec3 minB(1e9f), maxB(-1e9f);

    // Height at an arbitrary global grid position. Integer positions read the grid
    // directly; fractional ones interpolate bilinearly, which is exactly the surface
    // the uniform mesh describes. So a refined vertex starts ON the existing surface
    // and only departs from it once something sculpts it -- subdivision on its own
    // cannot change the silhouette.
    auto heightAt = [&](float gx, float gz) -> float {
        const int x0 = std::clamp((int)std::floor(gx), 0, globalW - 1);
        const int z0 = std::clamp((int)std::floor(gz), 0, globalH - 1);
        const int x1 = std::min(x0 + 1, globalW - 1);
        const int z1 = std::min(z0 + 1, globalH - 1);
        const float tx = gx - (float)x0, tz = gz - (float)z0;
        const float h00 = globalHeights[z0 * globalW + x0];
        const float h10 = globalHeights[z0 * globalW + x1];
        const float h01 = globalHeights[z1 * globalW + x0];
        const float h11 = globalHeights[z1 * globalW + x1];
        return (h00 * (1.0f - tx) + h10 * tx) * (1.0f - tz) +
               (h01 * (1.0f - tx) + h11 * tx) * tz;
    };
    auto vertexHeight = [&](float gx, float gz) -> float {
        const float fallback = heightAt(gx, gz);
        if (!sampleHeight) return fallback;
        return sampleHeight((int)std::lround(gx), (int)std::lround(gz), fallback);
    };
    // Heightfield normal, same central-difference form as the uniform path.
    auto normalAt = [&](float gx, float gz) -> glm::vec3 {
        const float hL = heightAt(gx - 1.0f, gz);
        const float hR = heightAt(gx + 1.0f, gz);
        const float hS = heightAt(gx, gz - 1.0f);
        const float hN = heightAt(gx, gz + 1.0f);
        return glm::normalize(glm::vec3((hL - hR) / (2.0f * pixelSpacingX), 1.0f,
                                        (hS - hN) / (2.0f * pixelSpacingZ)));
    };
    auto crossesShoreline = [&](int gx, int gz) -> bool {
        if (gx < 0 || gz < 0 || gx + 1 >= globalW || gz + 1 >= globalH) return false;
        const float h00 = globalHeights[gz * globalW + gx];
        const float h10 = globalHeights[gz * globalW + gx + 1];
        const float h01 = globalHeights[(gz + 1) * globalW + gx];
        const float h11 = globalHeights[(gz + 1) * globalW + gx + 1];
        const float lo = std::min(std::min(h00, h10), std::min(h01, h11));
        const float hi = std::max(std::max(h00, h10), std::max(h01, h11));
        return lo <= kShorelineLevel && hi >= kShorelineLevel;
    };

    // A cell intersecting sea level needs a few more vertices to represent the
    // continuous shoreline between source pixels. Other cells keep the original
    // path unless they were explicitly refined by the terrain editor.
    bool anyRefined = false;
    for (int row = 0; row + 1 < size && !anyRefined; ++row)
        for (int col = 0; col + 1 < size; ++col) {
            const int gx = std::min(startCol + col, globalW - 1);
            const int gz = std::min(startRow + row, globalH - 1);
            const bool editorRefined = refineLevel && (*refineLevel)(gx, gz) > 0;
            if (editorRefined || crossesShoreline(gx, gz)) {
                anyRefined = true;
                break;
            }
        }

    if (!anyRefined) {
        // ── Original uniform path, unchanged ────────────────────────────────
        for (int row = 0; row < size; ++row) {
            for (int col = 0; col < size; ++col) {
                int gCol = std::min(startCol + col, globalW - 1);
                int gRow = std::min(startRow + row, globalH - 1);

                // UV coordinates in heightmap space (0 to 1)
                float ux = (float)gCol / (globalW - 1);
                float uz = (float)gRow / (globalH - 1);

                // Map directly to world coordinates
                float wx = worldMinX + ux * worldWidth;
                float wz = worldMinZ + uz * worldHeight;

                // Get height directly from heightmap
                float wy = globalHeights[gRow * globalW + gCol];

                // Calculate normal using central differences from heightmap ONLY
                float hL = globalHeights[gRow * globalW + std::max(0, gCol - 1)];
                float hR = globalHeights[gRow * globalW + std::min(globalW - 1, gCol + 1)];
                float hN = globalHeights[std::min(globalH - 1, gRow + 1) * globalW + gCol];
                float hS = globalHeights[std::max(0, gRow - 1) * globalW + gCol];

                // Normal from gradient: (dZ/dX, 1, dZ/dZ)
                glm::vec3 normal = glm::normalize(glm::vec3(
                    (hL - hR) / (2.0f * pixelSpacingX),
                    1.0f,
                    (hS - hN) / (2.0f * pixelSpacingZ)
                ));

                vertices.push_back({ glm::vec3(wx, wy, wz), normal, glm::vec2(ux, uz) });

                minB.x = std::min(minB.x, wx);
                minB.y = std::min(minB.y, wy);
                minB.z = std::min(minB.z, wz);
                maxB.x = std::max(maxB.x, wx);
                maxB.y = std::max(maxB.y, wy);
                maxB.z = std::max(maxB.z, wz);
            }
        }

        // Generate triangle indices
        for (int r = 0; r < size - 1; ++r) {
            for (int c = 0; c < size - 1; ++c) {
                unsigned int tl = r * size + c, tr = tl + 1, bl = tl + size, br = bl + 1;
                indices.push_back(tl); indices.push_back(bl); indices.push_back(tr);
                indices.push_back(tr); indices.push_back(bl); indices.push_back(br);
            }
        }
    } else {
        // ── Refined path ────────────────────────────────────────────────────
        // Each cell is emitted as its own (n+1)x(n+1) sub-grid, where
        // n = 1 << level. Cell corners land on global grid positions and are sampled
        // through heightAt, so a refined cell and its unrefined neighbour share the
        // exact same corner heights -- the seam cannot crack.
        std::vector<unsigned int> cellStart;
        for (int row = 0; row + 1 < size; ++row) {
            for (int col = 0; col + 1 < size; ++col) {
                const int gCol = std::min(startCol + col, globalW - 1);
                const int gRow = std::min(startRow + row, globalH - 1);
                const int editorLevel = refineLevel ? (*refineLevel)(gCol, gRow) : 0;
                const int shorelineLevel = crossesShoreline(gCol, gRow)
                                               ? kShorelineRefinementLevel : 0;
                const int lvl = std::clamp(std::max(editorLevel, shorelineLevel), 0, 3);
                const int n = 1 << lvl;

                const unsigned int base = (unsigned int)vertices.size();
                for (int j = 0; j <= n; ++j) {
                    for (int i = 0; i <= n; ++i) {
                        const float fx = (float)gCol + (float)i / (float)n;
                        const float fz = (float)gRow + (float)j / (float)n;
                        const float ux = fx / (globalW - 1);
                        const float uz = fz / (globalH - 1);
                        const float wx = worldMinX + ux * worldWidth;
                        const float wz = worldMinZ + uz * worldHeight;
                        // Refined heights may sit above or below the base surface;
                        // that departure is the whole point of the extra geometry.
                        const float wy = vertexHeight(fx, fz);
                        vertices.push_back({ glm::vec3(wx, wy, wz),
                                             normalAt(fx, fz), glm::vec2(ux, uz) });
                        minB.x = std::min(minB.x, wx);
                        minB.y = std::min(minB.y, wy);
                        minB.z = std::min(minB.z, wz);
                        maxB.x = std::max(maxB.x, wx);
                        maxB.y = std::max(maxB.y, wy);
                        maxB.z = std::max(maxB.z, wz);
                    }
                }
                for (int j = 0; j < n; ++j) {
                    for (int i = 0; i < n; ++i) {
                        const unsigned int tl = base + (unsigned int)(j * (n + 1) + i);
                        const unsigned int tr = tl + 1;
                        const unsigned int bl = tl + (unsigned int)(n + 1);
                        const unsigned int br = bl + 1;
                        indices.push_back(tl); indices.push_back(bl); indices.push_back(tr);
                        indices.push_back(tr); indices.push_back(bl); indices.push_back(br);
                    }
                }
            }
        }
    }

    indexCount = (int)indices.size();

    // Setup VAO/VBO/EBO. The objects are created once and re-filled in place on
    // later rebuilds: generateMesh runs on every brush frame while sculpting, and
    // deleting/re-creating three GL objects per chunk per frame is pure driver
    // churn. glBufferData orphans and refills the existing store.
    if (VAO == 0) glGenVertexArrays(1, &VAO);
    if (VBO == 0) glGenBuffers(1, &VBO);
    if (EBO == 0) glGenBuffers(1, &EBO);
    if (VAO == 0 || VBO == 0 || EBO == 0) {
        std::cerr << "[TerrainChunk] Failed to allocate GPU buffers" << std::endl;
        return;
    }

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)sizeof(glm::vec3));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(2 * sizeof(glm::vec3)));

    glBindVertexArray(0);

    minBounds = minB;
    maxBounds = maxB;
}

void TerrainChunk::render() {
    if (VAO) {
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }
}
