#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include <GL/glew.h>
#include <glm/glm.hpp>

class TerrainManager;

struct RiverPoint {
    glm::vec2 position{0.0f}; // World XZ
    float bedY = 0.0f;
    float waterY = 0.0f;
};

struct RiverChannel {
    uint32_t id = 0;
    std::string channelClass;
    float drainageKm2 = 0.0f;
    float widthM = 0.0f;
    float depthM = 0.0f;
    float lengthM = 0.0f;
    float slope = 0.0f;
    bool coastalFlat = false;
    std::vector<RiverPoint> points;
};

// Source network used for wadi-bed carving and wet-ground placement. The
// coarse source water elevations are metadata only and must not be rendered as
// the final water surface.
class RiverNetwork {
public:
    // Loads the generated rivers.json format. On failure, the current network is
    // left untouched and `error` receives a line/column diagnostic when possible.
    bool loadFromJSON(const std::string& path, std::string* error = nullptr);

    const std::vector<RiverChannel>& channels() const { return m_channels; }
    float sourceSeaLevel() const { return m_sourceSeaLevel; }

private:
    std::vector<RiverChannel> m_channels;
    float m_sourceSeaLevel = 0.0f;
};

struct WetPool {
    glm::vec2 center{0.0f};
    float levelM = 0.0f;
    float bedMinM = 0.0f;
    float maxDepthM = 0.0f;
    float areaM2 = 0.0f;
    float volumeM3 = 0.0f;
    float radiusM = 0.0f;
};

struct WetTrickle {
    uint32_t riverId = 0;
    glm::vec2 spring{0.0f};
    float flowM3s = 0.0f;
    float wetLengthM = 0.0f;
    float widthM = 0.0f;
    float depthM = 0.0f;
    float meanIncisionM = 0.0f;
    std::vector<glm::vec2> points;
};

class WetWaterNetwork {
public:
    bool loadFromJSON(const std::string& path, std::string* error = nullptr);
    const std::vector<WetPool>& pools() const { return m_pools; }
    const std::vector<WetTrickle>& trickles() const { return m_trickles; }

private:
    std::vector<WetPool> m_pools;
    std::vector<WetTrickle> m_trickles;
};

struct WetWaterVertex {
    glm::vec3 position{0.0f}; // World XYZ; surface elevation follows final terrain for trickles.
    glm::vec2 flowDirection{0.0f};
    glm::vec2 worldXZ{0.0f};
    glm::vec2 patchCenter{0.0f};
    float patchRadius = 0.0f;
    float maxDepth = 0.0f;
    float flowSpeed = 0.0f;
    float kind = 0.0f; // 0 = pool/spring, 1 = flowing trickle.
    float flowFade = 1.0f;
};

class WetWaterMeshBuilder {
public:
    WetWaterMeshBuilder() = default;
    ~WetWaterMeshBuilder();
    WetWaterMeshBuilder(const WetWaterMeshBuilder&) = delete;
    WetWaterMeshBuilder& operator=(const WetWaterMeshBuilder&) = delete;

    // Builds CPU geometry from the currently rendered terrain heightfield. This
    // reads terrain only; it never carves or otherwise edits elevations.
    void build(const WetWaterNetwork& network, const TerrainManager& terrain);
    void upload();
    void destroy();
    void render() const;

    size_t poolCount() const { return m_poolCount; }
    size_t trickleCount() const { return m_trickleCount; }
    size_t vertexCount() const { return m_vertices.size(); }
    size_t triangleCount() const { return m_indices.size() / 3; }
    GLuint getTerrainHeightTexture() const { return m_terrainHeightTex; }

private:
    std::vector<WetWaterVertex> m_vertices;
    std::vector<uint32_t> m_indices;
    std::vector<float> m_terrainHeights;
    int m_terrainWidth = 0;
    int m_terrainHeight = 0;
    size_t m_poolCount = 0;
    size_t m_trickleCount = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    GLuint m_terrainHeightTex = 0;
};
