#include "TerrainChunk.h"
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
                                const std::vector<unsigned char>* roadMaskData, int roadW, int roadH) {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;

    int startCol = gridX * (size - 1);
    int startRow = gridZ * (size - 1);

    float worldWidth = worldMaxX - worldMinX;
    float worldHeight = worldMaxZ - worldMinZ;

    glm::vec3 minB(1e9f), maxB(-1e9f);

    // Generate mesh directly from heightmap with NO procedural modifications
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

            // Pixel spacing in world units (a 1-column/row grid would divide by zero)
            float pixelSpacingX = worldWidth / std::max(1, globalW - 1);
            float pixelSpacingZ = worldHeight / std::max(1, globalH - 1);

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
