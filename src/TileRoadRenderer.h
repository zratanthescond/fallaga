#pragma once
//
// TileRoadRenderer -- puts the tile road grid on screen.
//
// This is the thin OpenGL half of the tile system, deliberately separate from
// RoadTileMesh (which is pure geometry and tested headlessly). All it does is:
//   * ask RoadTileMesh to build the cells that changed,
//   * upload the result, and
//   * draw it through the existing street shader.
//
// It reuses RoadMeshBuilder's vertex layout exactly -- same RoadVertex, same attribute
// locations -- so the already-working street shader renders it with no second shader
// to maintain. Tile-specific data is packed into fields the road shading path ignores:
//
//   uv        = (TileVertex::edge, 0)      the curb-to-centre gradient, and the
//                                          marking bands are suppressed for tiles
//   markMask  = 0                          tiles have no lane paint yet
//   metricUV  = (edge, ao)                 surface texture tiling is derived from the
//                                          world position in the shader for tiles
//   normal.y  < 0                          marks a CURB vertex (the road surface always
//                                          faces up, a curb lip never does), so phase 4
//                                          can blend curb material without a new
//                                          attribute or a new shader
//
// Chunked: the grid is divided into fixed blocks and only the blocks whose cells
// changed are rebuilt and re-uploaded. Phase 8 measures and tunes this.

#include <cstdint>
#include <vector>

#include <GL/glew.h>

#include "RoadGrid.h"
#include "RoadTileMesh.h"
#include "RoadSystem.h"

class Shader;

namespace roadgrid {

class TileRoadRenderer {
public:
    static constexpr int kBlockSize = 32; // grid cells per rebuild block

    ~TileRoadRenderer();

    // Rebuilds every block flagged dirty in the grid, then clears the flag. Cheap to
    // call every frame: with nothing dirty it does no work.
    void update(const RoadGrid& grid);
    // Rebuilds everything, for a load or a first frame.
    void rebuildAll(const RoadGrid& grid);

    void render(Shader& shader) const;

    int triangleCount() const { return indexCount_ / 3; }
    int vertexCount() const { return (int)vertices_.size(); }
    bool empty() const { return indexCount_ == 0; }
    int blocksBuilt() const { return blocksBuilt_; }
    float lastBuildMs() const { return lastBuildMs_; }

    // Reporting straight through from the geometry layer.
    int surfaceQuads() const { return tileMesh_.surfaceQuads(); }
    int curbQuads() const { return tileMesh_.curbQuads(); }

private:
    void upload();

    RoadTileMesh tileMesh_;
    std::vector<RoadVertex> vertices_;
    std::vector<std::uint32_t> indices_;

    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0;
    int indexCount_ = 0;
    std::vector<std::uint8_t> blockDirty_;
    int blocksBuilt_ = 0;
    float lastBuildMs_ = 0.0f;
};

} // namespace roadgrid
