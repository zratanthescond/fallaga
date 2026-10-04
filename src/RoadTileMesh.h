#pragma once
//
// RoadTileMesh -- Phase 3 of the tile road builder: turns grid cells into geometry.
//
// For each road cell this emits:
//   * a road surface quad, inset where a curb will sit on that side;
//   * a curb strip on every side whose neighbour is NOT road, so the curb runs only
//     along the outside of the road and opens at junctions. This is what removes the
//     "dirty junction" look, because no curb quad ever crosses a connecting road;
//   * a mitred corner block where two curb runs meet, so the curb is continuous
//     round a turn instead of leaving a notch;
//   * a quarter-round end cap at a dead end.
//
// Attributes per vertex (see TileVertex):
//   uv0   world-space tiling position, so stones neither stretch nor visibly repeat
//         from tile to tile. Deliberately NOT per-tile 0..1.
//   uv1.x distance from the road edge: 0 at the curb, 1 at the centre. street.frag
//         blends curb material to road material across this.
//   ao    baked ambient occlusion, darker at the curb and in corners.
//   height  metres above the terrain plane: surface first, curb a little higher.
//
// Pure geometry generation: no OpenGL, no engine types, no globals. It writes plain
// vertex/index arrays, which is what makes it verifiable without a context.

#include <cstdint>
#include <vector>

#include "RoadGrid.h"

namespace roadgrid {

struct TileVertex {
    float x = 0.0f, y = 0.0f, z = 0.0f; // world position
    float u = 0.0f, v = 0.0f;           // uv0: world tiling
    float edge = 0.0f;                  // uv1.x: 0 curb .. 1 centre
    float ao = 1.0f;                    // baked ambient occlusion
    float kind = 0.0f;                  // 0 road, 1 curb (for the shader's own blend)
    std::uint8_t type = kPaved;         // road type, for dirt vs paved
};

// Cross-section constants, in metres. These are the plan's phase 3 step 6 numbers.
struct TileMeshParams {
    float roadLift = 0.04f;   // surface sits 2-5 cm above the terrain
    float curbLift = 0.05f;   // curb a further 3-6 cm
    float inset = 0.28f;      // how far the road surface is pulled back from the
                              // cell edge where a curb is present
    float curbWidth = 0.30f;  // width of the curb top
    float tile = kTileSize;
};

class RoadTileMesh {
public:
    // Builds geometry for every cell in [minX,maxX]x[minY,maxY]. Reads neighbouring
    // cells OUTSIDE that range too, which is what keeps curbs correct across a chunk
    // boundary; only the cells inside the range emit.
    void build(const RoadGrid& grid, int minX, int minY, int maxX, int maxY,
               const TileMeshParams& params = {});

    const std::vector<TileVertex>& vertices() const { return vertices_; }
    const std::vector<std::uint32_t>& indices() const { return indices_; }
    void clear() { vertices_.clear(); indices_.clear(); }

    // Reporting, so a test can assert what was emitted without reading geometry.
    int surfaceQuads() const { return surfaceQuads_; }
    int curbQuads() const { return curbQuads_; }
    int cornerQuads() const { return cornerQuads_; }
    // Curbs that were NOT emitted because the neighbour is road. This is the count the
    // "no curb across a junction opening" rule is judged by.
    int openSides() const { return openSides_; }

    // Sum of the outward-facing border edges of the built region: every one of them
    // must carry a curb. Used with openSides to prove the rule.
    int borderSides() const { return borderSides_; }

private:
    void addQuad(const TileVertex& a, const TileVertex& b, const TileVertex& c,
                 const TileVertex& d);
    void addTri(const TileVertex& a, const TileVertex& b, const TileVertex& c);

    std::vector<TileVertex> vertices_;
    std::vector<std::uint32_t> indices_;
    int surfaceQuads_ = 0, curbQuads_ = 0, cornerQuads_ = 0;
    int openSides_ = 0, borderSides_ = 0;
};

} // namespace roadgrid
