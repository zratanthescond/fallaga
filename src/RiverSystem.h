#pragma once

#include <cstdint>
#include <string>
#include <vector>

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
