#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

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
