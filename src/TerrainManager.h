#pragma once
#include <vector>
#include <string>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include "TerrainChunk.h"
#include "Shader.h"
#include "RoadSystem.h"

class TerrainManager {
public:
    TerrainManager(const std::string& hmPath, const std::string& pmPath, float minX, float maxX, float minZ, float maxZ, float minH, float maxH);
    ~TerrainManager();
    void update(const glm::vec3& camPos);
    void render(const glm::mat4& proj, const glm::mat4& view, const glm::vec3& camPos, const glm::vec3& lightDir, const glm::vec3& lightColor, float waterLevel, const glm::vec4& clipPlane = glm::vec4(0.0f, -1.0f, 0.0f, 100000.0f), float maxDrawDistance = 6000.0f);
    float getHeightAt(float x, float z) const;
    void setVisualSmoothingAffectsPhysics(bool v) { visualSmoothingAffectsPhysics = v; }
    bool getVisualSmoothingAffectsPhysics() const { return visualSmoothingAffectsPhysics; }
    void exportToOBJ(const std::string& filename, int downsample = 4) const;
    glm::vec3 getNormalAt(float x, float z) const;
    float getRoadAt(float x, float z) const;
    float getRiverAt(float x, float z) const;
    GLuint getHeightmapTexture() const { return heightmapTexID; }
    GLuint getRoadMask() const { return roadMask; }
    // Apply/remove road shaping in the visual and physics heightfields. Passing
    // null offsets removes tracked road-bed and mesh-conformance changes.
    void applyRoadBedHeightfield(const float* offsets, int offsetW, int offsetH);
    void setRoadGeometryMask(const unsigned char* mask, int maskW, int maskH);
    void sculpt(float wx, float wz, float radius, float speed, int mode, float dt,
                float directionDegrees = 0.0f, float gradeDegrees = 20.0f);
    void conformTerrainToRoadMesh(float wx, float wz, float radius, float speed, float dt,
                                  const std::vector<RoadVertex>& roadVertices,
                                  const std::vector<uint32_t>& roadIndices,
                                  bool wholeNetwork = false,
                                  float bermWidth = 30.0f);
    void paint(float wx, float wz, float radius, float speed, int layerIdx, float dt);
    void saveTerrain();
    void erodeGlobal();
    void updateGPUHeightmapTexture();
    // FALLAGA_ROADGAP: coarse signature of the rendered heightfield. Used to trace which
    // stage of a tool stroke raises terrain back, since the brush, the roadbed deform and
    // the conform all write the same array.
    void logHeightfieldSignature(const char* stage) const;

    // Accessors for road system integration
    const float* getHeightsData() const { return heights.data(); }
    int getGridW() const { return gridW; }
    int getGridH() const { return gridH; }
    int getVisualGridW() const { return visualW; }
    int getVisualGridH() const { return visualH; }
    float getWorldMinX() const { return worldMinX; }
    float getWorldMaxX() const { return worldMaxX; }
    float getWorldMinZ() const { return worldMinZ; }
    float getWorldMaxZ() const { return worldMaxZ; }
    float getMinHeight() const { return minHeight; }
    float getMaxHeight() const { return maxHeight; }
    // Rendered-terrain heightfield. Roads are sampled from getHeightsData(), so
    // anything comparing the two (road/terrain conformance checks) must read this
    // instead to see the surface the GPU actually draws.
    const std::vector<float>& getVisualHeightsData() const { return visualHeights; }

private:
    void loadHeightmap(const std::string& path);
    void setupTextures(const std::string& pmPath);
    void erode(int numDroplets, float brushX, float brushZ, float radius, bool isLocal);
    std::vector<float> heights;
    std::vector<float> visualHeights;
    std::vector<float> physicsVisualHeights; // authoritative for height queries (visual grid res)
    bool visualSmoothingAffectsPhysics = false;
    std::vector<float> appliedRoadOffsets;
    std::vector<float> appliedRoadConformOffsets;
    std::vector<unsigned char> roadMaskData;
    bool hasRoadGeometryMask = false;
    int gridW=0, gridH=0;
    int visualW=0, visualH=0;
    int roadW=0, roadH=0;
    float worldMinX, worldMaxX, worldMinZ, worldMaxZ, minHeight, maxHeight;
    std::vector<TerrainChunk*> chunks;
    GLuint terrainLayerArray=0, terrainNormalArray=0, terrainDispArray=0, terrainRoughArray=0, portMask=0, roadMask=0, roadGeometryMask=0, heightmapTexID=0, albedoTex=0, splatmap2Tex=0;
    bool hasRoadMask = false;
    int activePaintLayerIdx = 2;
    Shader* terrainShader = nullptr;
    std::string hmFilePath;
    time_t lastHmModTime = 0;
    const int CHUNK_SIZE = 129;
};
