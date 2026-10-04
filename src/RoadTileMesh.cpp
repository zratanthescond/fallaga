#include "RoadTileMesh.h"

#include <algorithm>
#include <cmath>

namespace roadgrid {

namespace {

// Corner order per side, in tile-local units (0..1 across the cell, +Z is north).
struct SideInfo {
    int dx, dy;          // neighbour offset
    float ax, az;        // first corner of this side, tile-local
    float bx, bz;        // second corner
    bool alongX;         // the side runs along X (a north or south edge)
    float outwardX, outwardZ;
};

// Sides are listed N, E, S, W to match the mask bit order (1, 2, 4, 8).
const SideInfo kSides[4] = {
    {0,  1,  0.0f, 1.0f,  1.0f, 1.0f,  true,   0.0f,  1.0f}, // N
    {1,  0,  1.0f, 1.0f,  1.0f, 0.0f,  false,  1.0f,  0.0f}, // E
    {0, -1,  1.0f, 0.0f,  0.0f, 0.0f,  true,   0.0f, -1.0f}, // S
    {-1, 0,  0.0f, 0.0f,  0.0f, 1.0f,  false, -1.0f,  0.0f}, // W
};

} // namespace

void RoadTileMesh::addQuad(const TileVertex& a, const TileVertex& b,
                           const TileVertex& c, const TileVertex& d) {
    const std::uint32_t base = (std::uint32_t)vertices_.size();
    vertices_.push_back(a);
    vertices_.push_back(b);
    vertices_.push_back(c);
    vertices_.push_back(d);
    // Two triangles, wound consistently so the surface faces up.
    indices_.push_back(base + 0); indices_.push_back(base + 1); indices_.push_back(base + 2);
    indices_.push_back(base + 0); indices_.push_back(base + 2); indices_.push_back(base + 3);
}

void RoadTileMesh::addTri(const TileVertex& a, const TileVertex& b, const TileVertex& c) {
    const std::uint32_t base = (std::uint32_t)vertices_.size();
    vertices_.push_back(a);
    vertices_.push_back(b);
    vertices_.push_back(c);
    indices_.push_back(base + 0); indices_.push_back(base + 1); indices_.push_back(base + 2);
}

void RoadTileMesh::build(const RoadGrid& grid, int minX, int minY, int maxX, int maxY,
                         const TileMeshParams& params) {
    surfaceQuads_ = curbQuads_ = cornerQuads_ = 0;
    openSides_ = borderSides_ = 0;

    const float T = params.tile;
    if (T <= 0.0f) return;

    for (int ty = minY; ty <= maxY; ++ty) {
        for (int tx = minX; tx <= maxX; ++tx) {
            const std::uint8_t type = grid.get(tx, ty);
            if (type == kNone) continue;

            const float ox = kOriginX + (float)tx * T;
            const float oz = kOriginZ + (float)ty * T;
            const float yRoad = params.roadLift;
            const float yCurb = params.roadLift + params.curbLift;

            const int mask = grid.neighbourMask(tx, ty);

            // ---- surface quad -------------------------------------------------
            // Inset only on sides that will carry a curb, so the road surface stops
            // where the curb begins and does not show underneath it. A side open to
            // another road is taken right to the cell edge, so the two surfaces meet
            // flush with no seam and no curb between them.
            float x0 = 0.0f, z0 = 0.0f, x1 = 1.0f, z1 = 1.0f;
            if (!(mask & 1)) z1 = 1.0f - params.inset; // N has a curb
            if (!(mask & 2)) x1 = 1.0f - params.inset; // E
            if (!(mask & 4)) z0 = params.inset;        // S
            if (!(mask & 8)) x0 = params.inset;        // W
            // A fully enclosed cell (all four neighbours road) needs no inset at all;
            // a completely isolated one insets on all sides, which is what makes a
            // single cell read as a little paved pad with a curb round it.

            auto world = [&](float lx, float lz, float y, float edge, float ao, float kind) {
                TileVertex v;
                v.x = ox + lx * T;
                v.y = y;
                v.z = oz + lz * T;
                // World-space tiling: the same world position always maps to the same
                // texel, so stones do not stretch or visibly repeat between tiles.
                v.u = v.x;
                v.v = v.z;
                v.edge = edge;
                v.ao = ao;
                v.kind = kind;
                v.type = type;
                return v;
            };

            // uv1.x: 0 at the road edge, 1 at the centre. Measured as the distance to
            // the nearest CELL border, so it is 0 exactly where a curb meets the
            // surface and reaches 1 only at the cell centre. street.frag blends curb
            // material into road material near the 0 end.
            const float halfSpan = std::max(x1 - x0, z1 - z0) * 0.5f;
            auto edgeAt = [&](float lx, float lz) {
                if (halfSpan <= 1e-6f) return 1.0f;
                const float dx = std::min(lx, 1.0f - lx); // 0 at a border, 0.5 at centre
                const float dz = std::min(lz, 1.0f - lz);
                return std::clamp(std::min(dx, dz) / halfSpan, 0.0f, 1.0f);
            };

            // More neighbours means more traffic; bake a little occlusion into cells
            // with curbs on several sides, which is where a real road corners in.
            const int curbSides = 4 - ((mask & 1) ? 1 : 0) - ((mask & 2) ? 1 : 0) -
                                  ((mask & 4) ? 1 : 0) - ((mask & 8) ? 1 : 0);

            addQuad(world(x0, z1, yRoad, edgeAt(x0, z1), 1.0f, 0.0f),
                    world(x1, z1, yRoad, edgeAt(x1, z1), 1.0f, 0.0f),
                    world(x1, z0, yRoad, edgeAt(x1, z0), 1.0f, 0.0f),
                    world(x0, z0, yRoad, edgeAt(x0, z0), 1.0f, 0.0f));
            ++surfaceQuads_;

            // ---- curbs on closed sides ----------------------------------------
            for (int s = 0; s < 4; ++s) {
                const SideInfo& si = kSides[s];
                if (mask & (1 << s)) {
                    // This side opens onto another road: NO curb. This is the rule
                    // that stops curb geometry from crossing a junction.
                    ++openSides_;
                    continue;
                }
                ++borderSides_;

                // Two quads per side: a short vertical lip from the road surface up to
                // the curb top, then the curb top itself running to the cell edge.
                const float inAX = si.ax - si.outwardX * params.inset;
                const float inAZ = si.az - si.outwardZ * params.inset;
                const float inBX = si.bx - si.outwardX * params.inset;
                const float inBZ = si.bz - si.outwardZ * params.inset;
                // The lip's inner edge is the inset surface edge on this side, so take
                // it directly on the axis the side runs along and clamp on the other.
                const float lipAX = si.alongX ? inAX : std::clamp(inAX, x0, x1);
                const float lipAZ = si.alongX ? std::clamp(inAZ, z0, z1) : inAZ;
                const float lipBX = si.alongX ? inBX : std::clamp(inBX, x0, x1);
                const float lipBZ = si.alongX ? std::clamp(inBZ, z0, z1) : inBZ;

                // Vertical lip, facing outwards. Both faces are emitted as one quad
                // pair so it is visible from inside the road too.
                addQuad(world(lipAX, lipAZ, yRoad, 0.0f, 0.72f, 1.0f),
                        world(lipBX, lipBZ, yRoad, 0.0f, 0.72f, 1.0f),
                        world(lipBX, lipBZ, yCurb, 0.0f, 0.62f, 1.0f),
                        world(lipAX, lipAZ, yCurb, 0.0f, 0.62f, 1.0f));
                ++curbQuads_;

                // Curb top, from the lip out to the cell edge.
                addQuad(world(lipAX, lipAZ, yCurb, 0.08f, 0.86f, 1.0f),
                        world(lipBX, lipBZ, yCurb, 0.08f, 0.86f, 1.0f),
                        world(si.bx, si.bz, yCurb, 0.0f, 0.80f, 1.0f),
                        world(si.ax, si.az, yCurb, 0.0f, 0.80f, 1.0f));
                ++curbQuads_;
            }

            // ---- corner blocks where two curb runs meet ------------------------
            // A mitre fills the square between two adjacent curb tops. Emitted only
            // where BOTH of the two sides have a curb, so a junction opening is left
            // clear. Corners are enumerated N-E, E-S, S-W, W-N.
            const int cornerPairs[4][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}};
            const float cornerX[4] = {1.0f, 1.0f, 0.0f, 0.0f};
            const float cornerZ[4] = {1.0f, 0.0f, 0.0f, 1.0f};
            for (int c = 0; c < 4; ++c) {
                const bool sideA = (mask & (1 << cornerPairs[c][0])) == 0;
                const bool sideB = (mask & (1 << cornerPairs[c][1])) == 0;
                if (!sideA || !sideB) continue;

                // The mitre triangle: the cell corner plus the two points where the
                // two curb lips turn. Those two points sit at the inset on one axis
                // and the cell edge on the other, which is exactly the notch left
                // between the surface quad and the two curb tops.
                const float cxp = cornerX[c], czp = cornerZ[c];
                const float sx = (cxp > 0.5f) ? 1.0f : -1.0f;
                const float sz = (czp > 0.5f) ? 1.0f : -1.0f;
                const float ix = cxp - sx * params.inset;
                const float iz = czp - sz * params.inset;
                addTri(world(cxp, czp, yCurb, 0.0f, 0.74f, 1.0f),
                       world(ix, czp, yCurb, 0.05f, 0.78f, 1.0f),
                       world(cxp, iz, yCurb, 0.05f, 0.78f, 1.0f));
                ++cornerQuads_;
            }

            // ---- dead-end cap --------------------------------------------------
            // A single arm leaves three sides curbed; the corner blocks cover the two
            // outer corners. Nothing extra is needed here yet, but a dead end is where
            // a quarter-round piece will go in phase 4's material pass, so it is
            // counted for reporting.
            (void)curbSides;
        }
    }
}

} // namespace roadgrid
