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
    //
    // `lod` (0..) selects a geometric level of detail: the top surface is meshed from
    // every 1<<lod-th grid sample, so level 1 is a quarter of the triangles, level 3 a
    // sixteenth of the vertices per edge. Level 0 is the original mesh.
    //
    // `addSkirt` drops a vertical apron `skirtDepth` metres around the chunk edge. Where
    // two chunks meet at different LODs the finer surface does not follow the coarser
    // chord, leaving a slit; the skirt (drawn from the higher side of each seam) closes
    // it. Skirts are only requested for chunks that actually border a different level.
    void generateMesh(const std::vector<float>& globalHeights, int globalW, int globalH, 
                      float worldMinX, float worldMaxX, float worldMinZ, float worldMaxZ,
                      const std::vector<unsigned char>* roadMaskData = nullptr, int roadW = 0, int roadH = 0,
                      const std::function<int(int, int)>* refineLevel = nullptr,
                      const HeightFn& sampleHeight = nullptr,
                      int lod = 0, bool addSkirt = false, float skirtDepth = 0.0f);
    void render();
    int gridX, gridZ, size;
    // Geometric level this chunk is currently meshed at, and whether that mesh carries
    // skirts. Owned by TerrainManager so it can spot when a rebuild is needed.
    int lodLevel = 0;
    bool lodSkirt = false;
    glm::vec3 minBounds = glm::vec3(0.0f);
    glm::vec3 maxBounds = glm::vec3(0.0f);
private:
    GLuint VAO = 0, VBO = 0, EBO = 0;
    int indexCount = 0;
};
