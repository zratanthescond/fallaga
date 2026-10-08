#ifndef TERRAIN_H
#define TERRAIN_H

#pragma once
#include <vector>
#include <string>
#include <map>
#include <cstdint>
#include <unordered_map>
#include <glm/glm.hpp>
#include "ObjectModel.h"
#include "HeightmapTerrain.h"
#include "RoadSystem.h"
#include "Shader.h"
#include "TerrainManager.h"
#include "RiverSystem.h"

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
    void    markBatchesDirty() const {
        batchesDirty = true;
        carCollisionGridDirty = true;
    }
    void    update(const glm::vec3& camPos);
    void    saveTerrain();
    void    buildWaterFBOs(int width, int height);

    float     getHeight  (float x, float z) const;
    // Height of the walkable/drivable surface: road mesh where present, terrain elsewhere.
    float     getGroundSurfaceHeight(float x, float z) const;
    std::uint64_t getCollisionSurfaceRevision() const { return collisionSurfaceRevision; }
    glm::vec3 getNormalAt(float x, float z) const;
    float     getRoadAt  (float x, float z) const;
    bool resolveCarCollision(glm::vec3& position, glm::vec3& velocity,
                             float carRadius, float restitution = 0.12f) const;

    // Road Edit mode API (in-game road editor)
    RoadGraph*       getRoadGraph()       const { return roadGraph; }
    RoadMeshBuilder* getRoadMeshBuilder() const { return roadMeshBuilder; }
    RoadTileField*   getRoadTileField()   const { return roadTileField; }
    const RoadNavigationGraph& getRoadNavigationGraph() const { return roadNavigationGraph; }
    const WetWaterNetwork& getWetWaterNetwork() const { return wetWaterNetwork; }
    HeightSampler&   getRoadHeightSampler()     { return roadHeightSampler; }
    void rebuildRoadMesh();
    void rebuildRoadNavigation();
    // Re-seats the road ribbon on the heightfield the GPU actually draws.
    // The roads are sampled from the native DEM grid while the terrain mesh is built
    // from the visual grid, and conforming terrain to the road only moves the coarse
    // heightfield. Without this the asphalt keeps its pre-deform height and hovers
    // above the rendered surface (measured up to 2.6 m on a 22 m cell grid).
    void snapRoadsToRenderedTerrain();
    // Re-inits roadHeightSampler (and any other cache holding a pointer into the
    // heightfield) when TerrainManager reports that the heights buffer moved.
    void refreshHeightDependentCaches();
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
    mutable float editorSculptDirectionDegrees = 0.0f;
    mutable float editorSculptGradeDegrees = 20.0f;

    enum class Zone { SEA, PORT, MEDINA, WADI, VILLE_NOUVELLE, PLAIN, BEACH, KASBAH };
    Zone getZoneAt(float x, float z) const;
    bool pointInPolygon(float x, float z, const std::vector<glm::vec2>& polygon) const;

    // City wall model matrix (for physics)
    mutable glm::mat4 cityWallModelMat = glm::mat4(1.0f);

private:
    HeightmapTerrain* heightmapTerrain = nullptr;
    // Low-resolution CPU copy of the terrain wadi mask, used for surface physics.
    std::vector<unsigned char> wadiSurfaceMask;
    int wadiSurfaceMaskW = 0;
    int wadiSurfaceMaskH = 0;
    std::vector<unsigned char> beachSplatMask;
    int beachSplatMaskW = 0;
    int beachSplatMaskH = 0;

    // Geometry-based road system
    RoadGraph*       roadGraph       = nullptr;
    RoadDeformer*    roadDeformer    = nullptr;
    RoadMeshBuilder* roadMeshBuilder = nullptr;
    RoadTileField*   roadTileField   = nullptr;
    RoadNavigationGraph roadNavigationGraph;
    HeightSampler    roadHeightSampler;
    RiverNetwork     riverNetwork;
    WetWaterNetwork  wetWaterNetwork;
    TerrainCoordinateMapper roadMapper;   // plan→world transform shared by the road pipeline
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

    struct CarCollisionInstance {
        const std::string* modelPath = nullptr;
        glm::mat4 modelMatrix{1.0f};
    };
    mutable std::unordered_map<std::uint64_t,
        std::vector<CarCollisionInstance>> carCollisionGrid;
    mutable bool carCollisionGridDirty = true;

    struct RoadSurfaceEdge {
        uint32_t segmentIndex = 0;
        uint32_t pointIndex = 0;
    };
    std::unordered_map<std::uint64_t, std::vector<RoadSurfaceEdge>> roadSurfaceEdgeGrid;
    bool roadSurfaceIndexReady = false;
    std::uint64_t collisionSurfaceRevision = 0;
    // TerrainManager::getHeightRevision() value the height samplers were built for.
    unsigned int cachedHeightRevision = 0;
    unsigned int cachedHeightmapRevision = 0;

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
    // Tarred gravel surface for the road ribbon, bound for the road pass only. The
    // street shader samples it with the metric UV so the aggregate keeps a constant
    // physical size. 0 falls back to the procedural asphalt in street.frag.
    unsigned int roadSurfaceTex  = 0;
    // Matching normal map, OpenGL convention, linear (normals are data not colour).
    unsigned int roadNormalTex   = 0;


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
    void rebuildCarCollisionGrid() const;
    void rebuildRoadSurfaceIndex();
    float distToSegment(float px, float pz,
                        float x1, float z1, float x2, float z2) const;
};

#endif
