// Phase 3 verification for the tile road builder: mesh generation.
//
// The plan's criteria are that straights, turns, T's and crossings look clean with no
// overlapping geometry and no gaps at curb corners, so the checks are about the
// structural rules that make that true:
//   * one surface quad per cell, always;
//   * a curb on every side whose neighbour is not road, and NEVER across a junction
//     opening -- that is the rule that removes the "dirty junction" look;
//   * a mitre at every corner where two curb runs meet, so there is no gap;
//   * surface inset only where a curb actually sits, so roads meeting flush have no
//     seam and no gap where a curb would have been;
//   * world-space UV0 (no per-tile 0..1, which would make stones repeat), the edge
//     gradient in uv1.x running 1 at the centre to 0 at the curb, and heights in the
//     plan's stated ranges.
//
// RoadTileMesh and RoadGrid are free of OpenGL, so this runs headlessly.

#include "RoadTileMesh.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace roadgrid;

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "[ok]  " : "[FAIL]", what);
    if (!ok) ++failures;
}

// Counts how many vertices are road surface (kind 0) and how many are curb (kind 1).
void countKinds(const RoadTileMesh& m, int& road, int& curb) {
    road = curb = 0;
    for (const TileVertex& v : m.vertices())
        (v.kind < 0.5f ? road : curb)++;
}

// Every index must be in range and no triangle may be degenerate (zero area), which is
// the cheap structural proof that nothing is overlapping or collapsed.
bool indicesValidAndNonDegenerate(const RoadTileMesh& m, int& degenerate,
                                  float& minArea, float& maxArea) {
    degenerate = 0;
    minArea = 1e30f;
    maxArea = -1e30f;
    const auto& V = m.vertices();
    const auto& I = m.indices();
    for (std::size_t i = 0; i + 2 < I.size(); i += 3) {
        if (I[i] >= V.size() || I[i + 1] >= V.size() || I[i + 2] >= V.size())
            return false;
        const TileVertex& a = V[I[i]];
        const TileVertex& b = V[I[i + 1]];
        const TileVertex& c = V[I[i + 2]];
        const float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
        const float vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
        const float cx = uy * vz - uz * vy;
        const float cy = uz * vx - ux * vz;
        const float cz = ux * vy - uy * vx;
        const float area = 0.5f * std::sqrt(cx * cx + cy * cy + cz * cz);
        if (area <= 1e-6f) ++degenerate;
        minArea = std::min(minArea, area);
        maxArea = std::max(maxArea, area);
    }
    return true;
}

} // namespace

int main() {
    const TileMeshParams P; // defaults from the plan

    // ---- one isolated cell: 1 surface, 4 curbs, 4 corners -------------------
    {
        RoadGrid g;
        g.set(100, 100, kPaved);
        RoadTileMesh m;
        m.build(g, 100, 100, 100, 100, P);
        check(m.surfaceQuads() == 1, "an isolated cell emits one surface quad");
        check(m.borderSides() == 4, "it has four border sides");
        check(m.curbQuads() == 8, "each border side emits a lip and a top (4 x 2)");
        check(m.openSides() == 0, "no side is open to another road");
        check(m.cornerQuads() == 4, "all four corners are mitred");
        int road = 0, curb = 0;
        countKinds(m, road, curb);
        check(road == 4, "the surface quad has four road vertices");
        check(curb > 0, "and the curbs contribute curb vertices");
        int degen = 0; float mn = 0, mx = 0;
        check(indicesValidAndNonDegenerate(m, degen, mn, mx), "all indices are in range");
        check(degen == 0, "no degenerate triangles");
    }

    // ---- two cells in a row: the shared side has NO curb --------------------
    {
        RoadGrid g;
        g.paintLine(200, 200, 201, 200, kPaved);
        RoadTileMesh m;
        m.build(g, 200, 200, 201, 200, P);
        check(m.surfaceQuads() == 2, "two cells emit two surface quads");
        // Two cells side by side: each has 3 border sides, so 6 border sides total.
        check(m.borderSides() == 6, "two cells in a row have six border sides");
        check(m.openSides() == 2, "the shared side is open at both cells");
        check(m.curbQuads() == 12, "curbs only on the six border sides");
        // Corners: each cell has two mitred corners on the shared side's ends.
        check(m.cornerQuads() > 0, "the run has mitred corners");
    }

    // ---- the junction rule: a cross has NO curb across its openings ----------
    {
        RoadGrid g;
        g.paintRect(300, 300, 302, 300, kPaved);
        g.paintRect(301, 299, 301, 301, kPaved);
        // The centre cell has four road neighbours, so all four sides open.
        check(g.neighbourMask(301, 300) == 15, "the plus centre sees four neighbours");
        RoadTileMesh m;
        m.build(g, 301, 300, 301, 300, P);
        check(m.surfaceQuads() == 1, "the centre is one surface quad");
        check(m.curbQuads() == 0, "a fully open cell emits NO curb: nothing crosses a junction");
        check(m.cornerQuads() == 0, "and no corner mitres, since no curbs meet");
        check(m.openSides() == 4 && m.borderSides() == 0, "all four sides are open");

        // The whole plus: the four arm tips are dead ends, so they DO have curbs.
        RoadTileMesh all;
        all.build(g, 299, 299, 303, 301, P);
        check(all.surfaceQuads() == 5, "the plus has five road cells");
        check(all.openSides() == 8, "the plus has eight open sides in total");
        check(all.curbQuads() > 0, "and the arm tips carry curbs");
    }

    // ---- surface inset only where a curb is present -------------------------
    {
        // Between two joined cells the surface must reach the shared cell edge so the
        // two meet flush. Find the extreme x of the road-surface vertices.
        RoadGrid g;
        g.paintLine(400, 400, 401, 400, kPaved);
        RoadTileMesh m;
        m.build(g, 400, 400, 400, 400, P); // only the west cell
        float maxRoadX = -1e30f;
        for (const TileVertex& v : m.vertices())
            if (v.kind < 0.5f) maxRoadX = std::max(maxRoadX, v.x);
        const float cellEast = kOriginX + 401.0f * kTileSize;
        check(std::fabs(maxRoadX - cellEast) < 1e-3f,
              "the surface reaches the shared cell edge, so joined cells are flush");

        // On the far (west) side there is no neighbour, so the surface is inset and the
        // curb occupies the gap.
        float minRoadX = 1e30f;
        for (const TileVertex& v : m.vertices())
            if (v.kind < 0.5f) minRoadX = std::min(minRoadX, v.x);
        const float cellWest = kOriginX + 400.0f * kTileSize;
        check(minRoadX > cellWest + 1e-3f,
              "on a curbed side the surface stops short of the cell edge");
        // `inset` is a fraction of the tile, not metres: the gap is inset * tile.
        check(std::fabs((minRoadX - cellWest) - P.inset * kTileSize) < 1e-3f,
              "and it stops exactly at the inset distance");
    }

    // ---- heights are in the plan's stated ranges ----------------------------
    {
        RoadGrid g;
        g.paintLine(500, 500, 503, 500, kPaved);
        RoadTileMesh m;
        m.build(g, 500, 500, 503, 500, P);
        float minRoadY = 1e30f, maxRoadY = -1e30f, maxCurbY = -1e30f, minCurbY = 1e30f;
        for (const TileVertex& v : m.vertices()) {
            if (v.kind < 0.5f) {
                minRoadY = std::min(minRoadY, v.y);
                maxRoadY = std::max(maxRoadY, v.y);
            } else {
                minCurbY = std::min(minCurbY, v.y);
                maxCurbY = std::max(maxCurbY, v.y);
            }
        }
        check(minRoadY >= 0.02f && maxRoadY <= 0.05f + 1e-4f,
              "the road surface sits 2-5 cm above the terrain");
        check(maxCurbY > maxRoadY, "the curb stands proud of the road");
        check(maxCurbY - maxRoadY >= 0.03f && maxCurbY - maxRoadY <= 0.06f + 1e-4f,
              "the curb is a further 3-6 cm above the road");
    }

    // ---- UV0 is world space, not per-tile ----------------------------------
    {
        RoadGrid g;
        g.paintLine(600, 600, 601, 600, kPaved);
        RoadTileMesh a, b;
        a.build(g, 600, 600, 600, 600, P);
        b.build(g, 601, 600, 601, 600, P);
        // The two cells must not share the same uv range: per-tile UVs would both run
        // 0..1 and the stones would restart at every tile.
        float aMinU = 1e30f, aMaxU = -1e30f, bMinU = 1e30f;
        for (const TileVertex& v : a.vertices()) {
            aMinU = std::min(aMinU, v.u);
            aMaxU = std::max(aMaxU, v.u);
        }
        for (const TileVertex& v : b.vertices()) bMinU = std::min(bMinU, v.u);
        check(aMaxU <= bMinU + 1e-3f && aMaxU > aMinU,
              "uv0 is world space: the second tile continues where the first stopped");
        // And uv0 must equal the world position, which is what makes it continuous.
        bool uvIsWorld = true;
        for (const TileVertex& v : a.vertices())
            if (std::fabs(v.u - v.x) > 1e-4f || std::fabs(v.v - v.z) > 1e-4f)
                uvIsWorld = false;
        check(uvIsWorld, "uv0 equals the world XZ position exactly");
    }

    // ---- uv1.x edge gradient, and AO ---------------------------------------
    {
        RoadGrid g;
        g.set(700, 700, kPaved);
        RoadTileMesh m;
        m.build(g, 700, 700, 700, 700, P);
        float roadMaxEdge = -1e30f, roadMinEdge = 1e30f;
        float curbMaxEdge = -1e30f;
        float minAo = 1e30f, maxAo = -1e30f;
        for (const TileVertex& v : m.vertices()) {
            if (v.kind < 0.5f) {
                roadMaxEdge = std::max(roadMaxEdge, v.edge);
                roadMinEdge = std::min(roadMinEdge, v.edge);
            } else {
                curbMaxEdge = std::max(curbMaxEdge, v.edge);
            }
            minAo = std::min(minAo, v.ao);
            maxAo = std::max(maxAo, v.ao);
        }
        check(roadMaxEdge > 0.5f,
              "uv1.x reaches the centre value on the road surface");
        check(curbMaxEdge <= 0.10f + 1e-3f,
              "uv1.x is at the curb end on every curb vertex");
        check(minAo < maxAo, "ambient occlusion actually varies");
        check(minAo > 0.0f && maxAo <= 1.0f, "AO stays in 0..1");
        check(minAo < 0.90f, "the curbs are visibly occluded relative to the road");
    }

    // ---- vertex count scales as expected, and types are carried ------------
    {
        RoadGrid g;
        g.paintRect(800, 800, 809, 809, kDirt); // a 10x10 dirt plaza
        RoadTileMesh m;
        m.build(g, 800, 800, 809, 809, P);
        check(m.surfaceQuads() == 100, "a 10x10 plaza emits 100 surface quads");
        // Interior cells are fully enclosed, so they emit no curbs at all.
        // Perimeter cells: 40 sides border the outside, and the four corners have 2
        // each while the edge cells have 1, so 4*9*... -- assert the structural facts
        // rather than a magic total.
        check(m.borderSides() == 40,
              "only the perimeter emits curbs: 40 border sides for a 10x10 block");
        bool allDirt = true;
        for (const TileVertex& v : m.vertices())
            if (v.type != kDirt) allDirt = false;
        check(allDirt, "every vertex carries the cell's road type");
        int degen = 0; float mn = 0, mx = 0;
        check(indicesValidAndNonDegenerate(m, degen, mn, mx), "the plaza indices are valid");
        check(degen == 0, "the plaza has no degenerate triangles");
        check(mx < (P.tile * P.tile),
              "no triangle is larger than a tile, so nothing spans cells");

        // Paved and dirt produce the same structure, differing only in type.
        RoadGrid g2;
        g2.paintRect(800, 800, 809, 809, kPaved);
        RoadTileMesh m2;
        m2.build(g2, 800, 800, 809, 809, P);
        check(m2.surfaceQuads() == m.surfaceQuads() &&
              m2.curbQuads() == m.curbQuads() &&
              m2.cornerQuads() == m.cornerQuads(),
              "paved and dirt have identical structure");
    }

    // ---- empty grid emits nothing ------------------------------------------
    {
        RoadGrid g;
        RoadTileMesh m;
        m.build(g, 0, 0, 63, 63, P);
        check(m.vertices().empty() && m.indices().empty(),
              "an empty grid emits no geometry");
        check(m.surfaceQuads() == 0 && m.curbQuads() == 0, "and reports nothing built");
    }

    // ---- chunk borders read neighbours outside the range -------------------
    {
        // The curb rule must consult cells outside the built range, or a chunk seam
        // would grow a wall down the middle of a road.
        RoadGrid g;
        g.paintLine(900, 900, 920, 900, kPaved);
        RoadTileMesh m;
        // Build only the first half of the line.
        m.build(g, 900, 900, 909, 900, P);
        // Derive the expectation from the mask logic rather than hard-coding it: an
        // interior cell of a run has BOTH ends open (west to the previous cell, east to
        // the next), so a 10-cell half of a 21-cell line has 1 + 9*2 = 19 open sides and
        // the remaining 4*10 - 19 = 21 border sides.
        int expectOpen = 0, expectBorder = 0;
        for (int i = 900; i <= 909; ++i) {
            const int msk = g.neighbourMask(i, 900);
            for (int s = 0; s < 4; ++s) {
                if (msk & (1 << s)) ++expectOpen;
                else ++expectBorder;
            }
        }
        check(m.openSides() == expectOpen, "open sides match the mask bits");
        check(m.borderSides() == expectBorder, "border sides match the mask bits");
        check(m.openSides() + m.borderSides() == 40, "every side is either open or curbed");
        check(m.curbQuads() == m.borderSides() * 2, "each border side emits exactly two quads");
        check(m.surfaceQuads() == 10, "all ten cells emit a surface quad");
        check(expectOpen == 19, "an interior cell of a run has two open sides");
        // The east-most built cell must NOT have a curb on its east side even though
        // its neighbour is outside the built range.
        float maxRoadX = -1e30f, maxCurbX = -1e30f;
        for (const TileVertex& v : m.vertices()) {
            if (v.kind < 0.5f) maxRoadX = std::max(maxRoadX, v.x);
            else maxCurbX = std::max(maxCurbX, v.x);
        }
        const float seam = kOriginX + 910.0f * kTileSize;
        check(std::fabs(maxRoadX - seam) < 1e-3f,
              "the surface still reaches the chunk seam, so no wall appears there");
        check(maxCurbX < seam + 1e-3f, "and no curb is emitted across the seam");
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL CHECKS PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
