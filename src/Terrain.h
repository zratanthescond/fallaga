#ifndef TERRAIN_H
#define TERRAIN_H

#pragma once
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <glm/glm.hpp>
#include "ObjectModel.h"
#include "HeightmapTerrain.h"
#include "RoadSystem.h"
#include "Shader.h"
#include "TerrainManager.h"

struct Monument { char id; float x,z,w,h; const char* name; };
struct Street   { float x1,z1,x2,z2,width; };

// A named street polyline: chain of (x,z) points with a width
struct StreetPolyline {
    std::string name;
    float width;
    std::vector<glm::vec2> points; // (x, z) waypoints
};

class Terrain {
public:
    Shader* oceanShader   = nullptr;
    Shader* terrainShader = nullptr;
    Shader* modelShader   = nullptr;
    Shader* streetShader  = nullptr;

    static bool isFlatMode;

    Terrain();
    ~Terrain();

    // Legacy render overloads (kept for compatibility)
    void render(const glm::mat4& projection, const glm::mat4& view,
                const glm::vec3& camPos,     const glm::vec3& lightDir) const {
        // Stub — use renderOpaque/renderWater for the new pipeline
    }

    void render(Shader& /*ignored*/,
                const glm::mat4& projection, const glm::mat4& view,
                const glm::vec3& camPos,     const glm::vec3& lightDir) const {
        render(projection, view, camPos, lightDir);
    }

    // New multi-pass rendering
    void renderOpaque(const glm::mat4& proj, const glm::mat4& view,
                      const glm::vec3& cam,  const glm::vec3& light,
                      const glm::vec3& lightColor, int selectedIdx = -1,
                      bool editMode = false, int selectedAssetIdx = 0,
                      float previewAngle = 0.0f, float previewScale = 1.0f,
                      bool isReflectionPass = false) const;

    void renderWater(const glm::mat4& proj, const glm::mat4& view,
                     const glm::vec3& cam, const glm::vec3& light,
                     const glm::vec3& lightColor,
                     unsigned int sceneColorTex,
                     unsigned int sceneDepthTex) const;

    Shader& getShader()     const { return *oceanShader; }
    void    printDebugMap() const {}
    void    markBatchesDirty() const { batchesDirty = true; }
    void    update(const glm::vec3& camPos);
    void    saveTerrain();
    void    buildWaterFBOs(int width, int height);

    float     getHeight  (float x, float z) const;
    glm::vec3 getNormalAt(float x, float z) const;
    float     getRoadAt  (float x, float z) const;

    // Road Edit mode API (in-game road editor)
    RoadGraph*       getRoadGraph()       const { return roadGraph; }
    RoadMeshBuilder* getRoadMeshBuilder() const { return roadMeshBuilder; }
    HeightSampler&   getRoadHeightSampler()     { return roadHeightSampler; }
    void rebuildRoadMesh();
    void saveRoadsToJSON(const std::string& path) const;

    // Whole-road-network graph transforms (world space, pivot = center of network)
    void mirrorRoads(bool flipX);
    void rotateRoads(float angleDeg);
    void scaleRoads(float factor);
    void connectRoads(float tolerance);
    void translateRoads(float dx, float dz);

    // Real-time water level control (editor)
    void  setWaterLevel(float level);  // absolute world Y
    float getWaterLevel() const;
    void  updateWaterMapTexture(const float* data) const;
    mutable unsigned int waterMapTex = 0;

    // Water FBOs — accessible from Game.cpp for pre-pass renders
    unsigned int reflFBO = 0, reflColorTex = 0, reflDepthRBO = 0;
    unsigned int refrFBO = 0, refrColorTex = 0, refrDepthTex = 0;
    int          fboW = 0, fboH = 0;
    bool         fbosReady = false;

    TerrainManager* terrainManager = nullptr;
    mutable bool rayHitTerrain = false;
    mutable glm::vec3 lastRayHitPos = glm::vec3(0.0f);
    mutable int editorBrushMode = 0;
    mutable float brushRadius = 30.0f;

    enum class Zone { SEA, PORT, MEDINA, WADI, VILLE_NOUVELLE, PLAIN, BEACH, KASBAH };
    Zone getZoneAt(float x, float z) const;
    bool pointInPolygon(float x, float z, const std::vector<glm::vec2>& polygon) const;

    // City wall model matrix (for physics)
    mutable glm::mat4 cityWallModelMat = glm::mat4(1.0f);

private:
    HeightmapTerrain* heightmapTerrain = nullptr;

    // Geometry-based road system
    RoadGraph*       roadGraph       = nullptr;
    RoadDeformer*    roadDeformer    = nullptr;
    RoadMeshBuilder* roadMeshBuilder = nullptr;
    HeightSampler    roadHeightSampler;
    ObjModel* trainStationModel = nullptr;
    ObjModel* cityWallModel = nullptr;
    ObjModel* rockModel = nullptr;

    // Asset cache for instanced landmark rendering
    mutable std::map<std::string, ObjModel*> assetCache;
    struct CustomRenderTask {
        std::string modelPath;
        glm::mat4 modelMatrix;
        std::string texturePath;
        std::string texturePath2;
        float blendWeight;
    };
    mutable std::vector<CustomRenderTask> cachedCustomRenderTasks;
    mutable std::map<std::string, std::vector<glm::mat4>> cachedBatches;
    mutable bool batchesDirty = true;

    // GTA-style visibility caching: instanced object culling is expensive to redo
    // every frame across thousands of instances, and re-uploading GPU instance
    // buffers unconditionally each frame is even worse. Only recompute/reupload
    // when the camera has actually moved enough to matter, or periodically as a
    // fallback to catch rotation-only visibility changes.
    mutable std::map<std::string, std::vector<glm::mat4>> cachedCulledBatches;
    mutable glm::vec3 lastCullCamPos = glm::vec3(1e9f, 1e9f, 1e9f);
    mutable int cullFrameCounter = 0;

    // Water
    unsigned int waterVAO = 0, waterVBO = 0, waterEBO = 0;
    int          waterIndexCount = 0;
    unsigned int flatNormalTex   = 0;
    unsigned int normalMap1 = 0;
    unsigned int normalMap2 = 0;
    unsigned int foamTex    = 0;
    unsigned int mapTexture      = 0;  // 1899 Sousse map projected onto terrain


    // Wall geometry
    mutable unsigned int wallVAO = 0, wallVBO = 0, wallEBO = 0;
    mutable size_t wallIndexCount = 0;
    mutable std::vector<glm::mat4> cachedRockInstances;
    mutable float cachedGroundY = 0.0f;
    mutable bool firstWallBuild = true;

    // Street rendering
    unsigned int streetVAO = 0, streetVBO = 0, streetEBO = 0;
    int          streetVertexCount = 0;
    int          streetIndexCount = 0;

    std::vector<Monument>       monuments;
    std::vector<Street>         streets;
    std::vector<StreetPolyline> streetPolylines;

    void buildWaterMesh();
    void initStreets();
    void buildStreetMesh();
    void renderStreets(const glm::mat4& proj, const glm::mat4& view,
                       const glm::vec3& lightDir, const glm::vec3& viewPos) const;
    float distToSegment(float px, float pz,
                        float x1, float z1, float x2, float z2) const;
};

#endif