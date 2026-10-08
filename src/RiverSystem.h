#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <GL/glew.h>
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

struct RiverVertex {
    glm::vec3 position{0.0f};
    glm::vec2 flowDirection{0.0f, 1.0f};
    glm::vec2 uv{0.0f}; // World XZ; shader scales this in metres.
    float flowSpeed = 0.0f;
    float depth = 0.0f;
};

class RiverMeshBuilder {
public:
    RiverMeshBuilder() = default;
    ~RiverMeshBuilder();
    RiverMeshBuilder(const RiverMeshBuilder&) = delete;
    RiverMeshBuilder& operator=(const RiverMeshBuilder&) = delete;

    // Option C: only channels with at least 12 km^2 catchment are perennial.
    // Smaller channels remain in RiverNetwork for the future wadi/flood system.
    void build(const RiverNetwork& network, float perennialCatchmentKm2 = 12.0f);
    void upload();
    void destroy();
    void render() const;

    size_t channelCount() const { return m_channelCount; }
    size_t vertexCount() const { return m_vertices.size(); }
    size_t triangleCount() const { return m_indices.size() / 3; }
    const std::vector<RiverVertex>& debugVertices() const { return m_vertices; }
    const std::vector<uint32_t>& debugIndices() const { return m_indices; }

private:
    std::vector<RiverVertex> m_vertices;
    std::vector<uint32_t> m_indices;
    size_t m_channelCount = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
};
