#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <functional>
#include <vector>

class TerrainChunk {
public:
    struct Vertex { glm::vec3 pos; glm::vec3 normal; glm::vec2 uv; };
    TerrainChunk(int x, int z, int size);
    ~TerrainChunk();
    // `refineLevel` reports how many times a global grid CELL should be subdivided
    // (0 = leave alone). It may be null, and when it is -- or when it reports 0 for
    // every cell in this chunk -- the original uniform-quad path is used unchanged,
    // so chunks without added geometry are byte-for-byte what they were.
    //
    // `sampleHeight` overrides the height used at a vertex, so refined vertices can
    // carry a finer height than the base grid while unrefined ones still copy it.
    using HeightFn = std::function<float(int gx, int gz, float fallback)>;
    void generateMesh(const std::vector<float>& globalHeights, int globalW, int globalH, 
                      float worldMinX, float worldMaxX, float worldMinZ, float worldMaxZ,
                      const std::vector<unsigned char>* roadMaskData = nullptr, int roadW = 0, int roadH = 0,
                      const std::function<int(int, int)>* refineLevel = nullptr,
                      const HeightFn& sampleHeight = nullptr);
    void render();
    int gridX, gridZ, size;
    glm::vec3 minBounds = glm::vec3(0.0f);
    glm::vec3 maxBounds = glm::vec3(0.0f);
private:
    GLuint VAO = 0, VBO = 0, EBO = 0;
    int indexCount = 0;
};
