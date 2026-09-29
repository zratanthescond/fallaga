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
//    RoadTileField           : derived 4-direction auto-tiling bitmask field
//    RoadMeshBuilder         : runtime ribbon mesh generation + GPU upload
// ─────────────────────────────────────────────────────────────────────────────
#include <vector>
#include <string>
#include <cstdint>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include "Shader.h"
#include "RoadTileMask.h"

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

// Endpoints closer than this (metres) belong to the same junction. Single source of
// truth: RoadGraph::buildTopology clusters on it and RoadMeshBuilder::build clusters
// on the same value, so the topology the log reports is the topology the mesh draws.
// It has to be at least as generous as the road editor's own connect tolerance, or
// roads the editor considers joined are still rendered with a gap.
constexpr float kRoadJunctionTolerance = 6.0f;

// A road only forms a junction with a road it arrives at transversely: |cos| of the
// angle between the branch direction and the host tangent must be below this, i.e.
// the two must meet at roughly 30 degrees or more. Under that the roads are running
// side by side (a merge, handled as a mid-span crossing instead), and welding one
// onto the other would drag its end across the host's flank.
constexpr float kRoadJunctionApproachCos = 0.86f;

// The shader places the painted white edge stripe at u=0.085 / 0.915. Snap
// transverse branches to that same world-space offset so their edge stripes meet
// the host road's stripe instead of stopping short at a different shoulder offset.
constexpr float kRoadWhiteEdgeLineU = 0.085f;
inline float roadWhiteEdgeLineOffset(float roadWidth) {
    return roadWidth * (0.5f - kRoadWhiteEdgeLineU);
}

// ── Automatic gap splicing ──
// Two roads drawn in separate strokes can stop short of each other, leaving two end
// caps facing a hole. When their ends actually aim at one another, the network is
// meant to continue, so the ends are extended along their own axes to the point where
// their axes meet. This is deliberately not a distance rule: at 130 m apart, a
// "nearest road" test splices across whatever happens to be in between.
//
// Every condition below must hold, or the pair is left alone and reported:
//   - the two ends belong to different segments, and neither segment is a closed ring
//   - the gap is longer than kRoadJunctionTolerance (shorter ones the weld in
//     RoadMeshBuilder::build already handles) and at most kMaxRoadGapSplice metres
//   - each end aims at the other: both outward directions have a positive component
//     along the line joining them, so this is a continuation and not two roads that
//     merely happen to be nearby
//   - their extended axes meet at a well-defined point
//   - no third road's corridor crosses the gap, i.e. the splice does not drag a road
//     across part of the network to reach a neighbour on the far side
constexpr float kMaxRoadGapSplice      = 150.0f;
constexpr float kRoadGapSpliceFacing   = 0.35f;   // cos of the aim tolerance
constexpr float kRoadGapSpliceLock     = 6.0f;    // metres, axes must meet this closely

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

    // Per-road corner rounding, in metres. This is the tangent run of the fillet arc
    // laid into every interior corner, so the road reads as a smooth curve instead of
    // a chain of right angles. 0 (the default, and what existing road JSON leaves
    // unset) keeps the automatic radius of half the road width. Anything above 0 is
    // the author's explicit choice from the editor's CORNER RADIUS slider, still
    // clamped per-corner to half of each adjacent segment so two neighbouring fillets
    // can never grow into one another.
    float cornerRadius = 0.0f;

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
    // Cached centerline bounds for frequent walkable-surface height queries.
    // x=minX, y=minZ, z=maxX, w=maxZ.
    glm::vec4 surfaceBounds{0.0f};
    bool surfaceBoundsValid = false;
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
    void buildTopology(float snapTolerance = kRoadJunctionTolerance);

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
        float smoothSigma = 3.0f,
        bool closed = false
    );
}

// ──────────────────────── TERRAIN DEFORMATION ───────────────────────────────

class RoadDeformer {
public:
    RoadDeformer() = default;
    ~RoadDeformer() = default;

    // Build deformation field from all road segments
    void build(const std::vector<RoadSegment>& segments,
               const HeightSampler& sampler,
               const TerrainCoordinateMapper& mapper,
               int deformW, int deformH);

    // Query deformation offset at world position
    float getDeformation(float x, float z) const;

    // Query road presence: 0.0 = off-road, 1.0 = on road center
    float getRoadInfluence(float x, float z) const;

    const float* getDeformationData() const { return m_deformMap.empty() ? nullptr : m_deformMap.data(); }
    int getMapWidth() const { return m_mapW; }
    int getMapHeight() const { return m_mapH; }

    bool isBuilt() const { return !m_deformMap.empty(); }

private:
    std::vector<float> m_deformMap;     // Per-pixel elevation offset
    std::vector<float> m_influenceMap;  // Per-pixel road influence [0,1]
    int m_mapW = 0, m_mapH = 0;
    TerrainCoordinateMapper m_mapper;
};

// ──────────────────────── ROAD DIRECTION FIELD ──────────────────────────────

//  4-direction auto-tiling field derived from the evaluated road splines.
//  Authoring stays spline-based (RoadGraph); this field is the read-only
//  consequence used for connectivity queries, shape classification and future
//  junction/corner auto-tiling. One cell per road cross-section, one byte per
//  cell: 0 = no road, otherwise the bitmask of the neighbouring road cells
//  (RoadTile::North/East/South/West). Road width plays no part — use
//  Terrain::getRoadAt for continuous road presence.
//
//  The masks are exact for a grid-aligned network, which the historic plan
//  mostly is. A 45° segment reads as a staircase of corners rather than one
//  long straight, so the reliable signal is connectivity (which of the 4
//  neighbours hold road); the shape name is an approximation of it.
class RoadTileField {
public:
    //  Rasterizes every segment into a cellSize grid over the mapper bounds.
    //  `cellSize` is rounded so the whole world fits in `maxResolution` cells
    //  per axis. Must run AFTER RoadSpline::evaluate (it reads splinePoints).
    void build(const std::vector<RoadSegment>& segments,
               const TerrainCoordinateMapper& mapper,
               float cellSize = 8.0f,
               int maxResolution = 4096);

    int   width()        const { return m_grid.width(); }
    int   height()       const { return m_grid.height(); }
    float cellSize()     const { return m_cellSize; }
    bool  isBuilt()      const { return !m_grid.empty(); }
    int   roadCellCount() const { return m_grid.roadCellCount(); }

    //  World-space queries. Outside the field nothing is a road and no shape.
    bool                 isRoadAt(float x, float z) const;
    uint8_t              maskAt(float x, float z) const;
    std::optional<RoadTile::Shape> shapeAt(float x, float z) const;

    //  Number of connected arms (0..4) — cheap connectivity test for gameplay
    //  and for the road editor.
    int   armCountAt(float x, float z) const;

    //  True when a road cell at (x, z) has another road cell in `direction`
    //  (pass one of RoadTile::North / East / South / West).
    bool  isConnectedTowards(float x, float z, uint8_t direction) const;

    int  countShape(RoadTile::Shape s) const { return m_grid.countShape(s); }

    //  Compact "straights 8120, corners 2011, ..." line for the rebuild log.
    std::string shapeSummary() const;

    void debugPrint() const { m_grid.debugPrint(); }

private:
    RoadTile::MaskGrid   m_grid;
    RoadTile::WorldToCell m_map;
    float m_cellSize = 8.0f;
};

// ──────────────────────── ROAD MESH BUILDER ─────────────────────────────────

struct RoadVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;         // u = across road [0,1], v = distance along road
    float roadType;
    float surfacePriority; // wider roads own overlap pixels; type breaks width ties
    // 1.0 = paint this fragment's markings, 0.0 = bare asphalt (inside a junction
    // pad or merged into a more major road). Interpolated across a triangle, so
    // markings fade out over one ring instead of snapping, and never smear sideways
    // the way collapsing the whole cross-section to a single u did.
    float markMask;
};

struct RoadJoinShaderStroke {
    glm::vec2 p0, p1, p2, p3;
    float halfWidth = 0.0f;
    bool curb = false;
};

struct RoadJoinShaderContact {
    glm::vec2 hostCenter;
    glm::vec2 hostTangent;
    float hostSide = 1.0f;
    float hostWidth = 0.0f;
    float whiteSpan = 0.0f;
    float pavementSpan = 0.0f;
    float radius = 0.0f;
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
               const RoadDeformer* deformer = nullptr,
               float snapMargin = 1.5f);

    // Upload to GPU
    void upload();

    // Render with given shader
    void render(Shader& shader) const;

    // Cleanup
    void destroy();

    int getVertexCount() const { return (int)m_vertices.size(); }
    int getTriangleCount() const { return m_indexCount / 3; }

    // Read-only access to the generated geometry (tests / debug tooling).
    const std::vector<RoadVertex>& debugVertices() const { return m_vertices; }
    const std::vector<uint32_t>& debugIndices() const { return m_indices; }

private:
    std::vector<RoadVertex>  m_vertices;
    std::vector<uint32_t>    m_indices;
    std::vector<RoadJoinShaderStroke> m_joinShaderStrokes;
    std::vector<RoadJoinShaderContact> m_joinShaderContacts;

    GLuint m_VAO = 0, m_VBO = 0, m_EBO = 0;
    int m_indexCount = 0;
};
