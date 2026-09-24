#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  RoadSystem.h — Geometry-based road system for Sousse 1899
//
//  Provides the complete vector road pipeline:
//    TerrainCoordinateMapper : single authoritative coordinate transform
//    HeightSampler           : bilinear terrain height queries
//    RoadGraph               : road network data model (nodes + segments)
//    RoadSpline              : Catmull-Rom spline evaluation + smoothing
//    RoadDeformer            : non-destructive terrain deformation field
//    RoadMeshBuilder         : runtime ribbon mesh generation + GPU upload
// ─────────────────────────────────────────────────────────────────────────────
#include <vector>
#include <string>
#include <cstdint>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include "Shader.h"

// ──────────────────────── COORDINATE SYSTEM ─────────────────────────────────

#ifndef TERRAIN_BOUNDS_DEFINED
#define TERRAIN_BOUNDS_DEFINED
struct TerrainBounds {
    float minX, maxX;          // World-space X extent
    float minZ, maxZ;          // World-space Z extent
    float minHeight, maxHeight; // Elevation range
};
#endif

struct TerrainCoordinateMapper {
    TerrainBounds bounds;
    int heightmapW = 0, heightmapH = 0;
    bool flipHeightmapZ = false;

    // Pixel (col, row) → World (X, Z).
    glm::vec2 pixelToWorld(float px, float py) const {
        float u = px / float(heightmapW - 1);
        float v = py / float(heightmapH - 1);
        if (flipHeightmapZ)
            v = 1.0f - v;
        return glm::vec2(
            glm::mix(bounds.minX, bounds.maxX, u),
            glm::mix(bounds.minZ, bounds.maxZ, v)
        );
    }

    // World (X, Z) → Normalized UV [0,1]
    glm::vec2 worldToUV(float x, float z) const {
        float u = (x - bounds.minX) / (bounds.maxX - bounds.minX);
        float v = (z - bounds.minZ) / (bounds.maxZ - bounds.minZ);
        if (flipHeightmapZ)
            v = 1.0f - v;
        return glm::vec2(u, v);
    }

    // World (X, Z) → Heightmap pixel (col, row)
    glm::vec2 worldToPixel(float x, float z) const {
        glm::vec2 uv = worldToUV(x, z);
        return glm::vec2(
            uv.x * float(heightmapW - 1),
            uv.y * float(heightmapH - 1)
        );
    }
};

// ──────────────────────── HEIGHT SAMPLER ─────────────────────────────────────

class HeightSampler {
public:
    HeightSampler() = default;
    void init(const TerrainCoordinateMapper& mapper,
              const float* heights, int w, int h);

    // Bilinear height at world (x, z)
    float sample(float x, float z) const;

    bool isValid() const { return m_heights != nullptr; }

private:
    const float* m_heights = nullptr;
    int m_w = 0, m_h = 0;
    TerrainCoordinateMapper m_mapper;
};

// ──────────────────────── ROAD DATA MODEL ───────────────────────────────────

enum class RoadType : int {
    ColonialAvenue = 0,   // Paved macadam, ~10-13m wide
    UrbanStreet    = 1,   // Tarred gravel, ~8-9m
    CountryHighway = 2,   // Packed earth, ~8-10m
    CountryDirt    = 3,   // Cart track, ~6m
    Port           = 4,   // Wharf-side
    Medina         = 5    // Narrow alleyway
};

struct RoadPoint {
    glm::vec2 pos;        // World X, Z
};

struct RoadSegment {
    uint32_t id = 0;
    RoadType type = RoadType::CountryDirt;
    float width = 6.0f;
    float shoulderWidth = 1.5f;

    // Raw control points from JSON
    std::vector<RoadPoint> controlPoints;

    // True when the road forms a closed loop (round road / roundabout): rendered as
    // a continuous ring with no end caps and unbiased curvature at the seam.
    bool closed = false;

    // Evaluated spline (filled by RoadSpline::evaluate)
    std::vector<glm::vec3> splinePoints;   // x, y(elevation), z
    std::vector<glm::vec3> tangents;
    std::vector<glm::vec3> rightVecs;      // perpendicular right vector
    std::vector<float>     curvature;
    std::vector<float>     cumulativeDist; // distance along spline
};

struct RoadNode {
    uint32_t id = 0;
    glm::vec3 position = glm::vec3(0.0f);
    std::vector<uint32_t> connectedSegments;
};

class RoadGraph {
public:
    bool loadFromJSON(const std::string& path);
    bool saveToJSON(const std::string& path) const;

    // Detects junctions from shared endpoints (within tolerance)
    void buildTopology(float snapTolerance = 2.0f);

    std::vector<RoadSegment> segments;
    std::vector<RoadNode>    nodes;
};

// ──────────────────────── SPLINE SYSTEM ─────────────────────────────────────

namespace RoadSpline {
    // Catmull-Rom spline evaluation from 2D control points.
    // Samples heightmap for Y, then smooths elevation.
    // Straight stretches resample every (1/samplesPerMeter) meters; curved
    // sections (filleted corners, closed rings) tessellate by turn angle
    // (~5 degrees per sample) so tight corners never starve for geometry.
    void evaluate(
        RoadSegment& seg,
        const HeightSampler& sampler,
        float samplesPerMeter = 0.5f   // 1 sample every 2m on straights
    );

    // Smooth elevation profile to respect max road grade
    void smoothElevation(
        std::vector<glm::vec3>& positions,
        float maxGrade = 0.08f,
        float smoothSigma = 3.0f
    );
}

// ──────────────────────── TERRAIN DEFORMATION ───────────────────────────────

class RoadDeformer {
public:
    RoadDeformer() = default;
    ~RoadDeformer();

    // Build deformation field from all road segments
    void build(const std::vector<RoadSegment>& segments,
               const HeightSampler& sampler,
               const TerrainCoordinateMapper& mapper,
               int deformW, int deformH);

    // Query deformation offset at world position
    float getDeformation(float x, float z) const;

    // Query road presence: 0.0 = off-road, 1.0 = on road center
    float getRoadInfluence(float x, float z) const;

    // GPU deformation texture, sampled by the terrain vertex shader.
    GLuint getDeformationTexture() const { return m_deformTexture; }

    bool isBuilt() const { return !m_deformMap.empty(); }

private:
    std::vector<float> m_deformMap;     // Per-pixel elevation offset
    std::vector<float> m_influenceMap;  // Per-pixel road influence [0,1]
    int m_mapW = 0, m_mapH = 0;
    TerrainCoordinateMapper m_mapper;
    GLuint m_deformTexture = 0;
};

// ──────────────────────── ROAD MESH BUILDER ─────────────────────────────────

struct RoadVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;         // u = across road [0,1], v = distance along road
    float roadType;
};

class RoadMeshBuilder {
public:
    RoadMeshBuilder() = default;
    ~RoadMeshBuilder();

    // Generate ribbon mesh from evaluated road segments.
    // Segments are taken by value so branch endpoints can be retracted to the
    // host road's near edge before the ribbon is emitted.
    void build(std::vector<RoadSegment> segments,
               const HeightSampler& sampler,
               const RoadDeformer* deformer = nullptr);

    // Upload to GPU
    void upload();

    // Render with given shader
    void render(Shader& shader) const;

    // Cleanup
    void destroy();

    int getVertexCount() const { return (int)m_vertices.size(); }
    int getTriangleCount() const { return m_indexCount / 3; }

private:
    std::vector<RoadVertex>  m_vertices;
    std::vector<uint32_t>    m_indices;

    GLuint m_VAO = 0, m_VBO = 0, m_EBO = 0;
    int m_indexCount = 0;
};
