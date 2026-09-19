#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>

class TerrainChunk {
public:
    struct Vertex { glm::vec3 pos; glm::vec3 normal; glm::vec2 uv; };
    TerrainChunk(int x, int z, int size);
    ~TerrainChunk();
    void generateMesh(const std::vector<float>& globalHeights, int globalW, int globalH, 
                      float worldMinX, float worldMaxX, float worldMinZ, float worldMaxZ,
                      const std::vector<unsigned char>* roadMaskData = nullptr, int roadW = 0, int roadH = 0);
    void render();
    int gridX, gridZ, size;
    glm::vec3 minBounds = glm::vec3(0.0f);
    glm::vec3 maxBounds = glm::vec3(0.0f);
private:
    GLuint VAO = 0, VBO = 0, EBO = 0;
    int indexCount = 0;
};
