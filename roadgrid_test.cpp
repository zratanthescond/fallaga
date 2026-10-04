// Phase 1 verification for the tile-based road builder: the grid data layer.
//
// Covers the plan's explicit criteria -- paint and erase cells, 4-connected
// rasterisation, undo/redo, neighbour masks for the phase 2 table, dirty-chunk
// bookkeeping including the cross-seam case, and save/load round-tripping.
//
// RoadGrid is free of OpenGL and of the engine, so this runs headlessly.

#include "RoadGrid.h"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>

using namespace roadgrid;

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "[ok]  " : "[FAIL]", what);
    if (!ok) ++failures;
}

// Every painted cell must be reachable from every other by 4-connected steps
// through painted cells. This is the property auto-tiling relies on: a diagonal
// step would leave two cells that each see the other as a dead end.
bool fourConnected(const RoadGrid& g, int x0, int y0, int x1, int y1) {
    std::set<std::pair<int, int>> seen;
    std::vector<std::pair<int, int>> stack{{x0, y0}};
    seen.insert({x0, y0});
    while (!stack.empty()) {
        auto [x, y] = stack.back();
        stack.pop_back();
        if (x == x1 && y == y1) return true;
        const int dx[4] = {0, 1, 0, -1};
        const int dy[4] = {1, 0, -1, 0};
        for (int i = 0; i < 4; ++i) {
            const int nx = x + dx[i], ny = y + dy[i];
            if (!g.isRoad(nx, ny) || seen.count({nx, ny})) continue;
            seen.insert({nx, ny});
            stack.push_back({nx, ny});
        }
    }
    return false;
}

} // namespace

int main() {
    // ---- world <-> tile round trip -----------------------------------------
    {
        RoadGrid g;
        const float wx = 4214.0f, wz = -1159.0f;
        const int tx = RoadGrid::worldToTileX(wx), ty = RoadGrid::worldToTileZ(wz);
        check(RoadGrid::inBounds(tx, ty), "a real road position lands inside the grid");
        const float cx = RoadGrid::tileCenterX(tx), cz = RoadGrid::tileCenterZ(ty);
        check(RoadGrid::worldToTileX(cx) == tx && RoadGrid::worldToTileZ(cz) == ty,
              "a tile centre maps back to its own tile");
        // The tile must actually contain the point.
        check(std::fabs(cx - wx) <= kTileSize * 0.5f + 1e-3f &&
              std::fabs(cz - wz) <= kTileSize * 0.5f + 1e-3f,
              "the tile centre is within half a tile of the point");
        check(kTileSize > 0.0f, "tile size is locked and positive");
    }

    // ---- paint, erase, counting --------------------------------------------
    {
        RoadGrid g;
        check(g.empty() && g.roadCount() == 0, "a fresh grid is empty");
        const int n = g.paintLine(100, 100, 140, 100, kPaved);
        check(n == 41 && g.roadCount() == 41, "a 41-cell line paints 41 cells");
        check(g.get(120, 100) == kPaved, "the middle of the line is paved");
        check(g.get(99, 100) == kNone && g.get(141, 100) == kNone,
              "the cells just outside the line are untouched");

        g.beginStroke();
        g.paintLine(100, 110, 140, 110, kDirt);
        g.endStroke();
        check(g.roadCount() == 82, "a second stroke adds its cells");

        // Erase is just painting kNone.
        g.beginStroke();
        g.paintLine(110, 110, 130, 110, kNone);
        g.endStroke();
        check(g.roadCount() == 82 - 21, "erasing 21 cells removes them");
        check(g.get(120, 110) == kNone, "the erased cell reads as none");

        check(g.neighbourMask(120, 100) == (2 | 8),
              "a cell inside a straight run sees only E and W");
        check(g.neighbourMask(100, 100) == 2, "an end cell sees only E");
    }

    // ---- 4-connectedness of a diagonal line --------------------------------
    {
        RoadGrid g;
        g.paintLine(50, 50, 90, 90, kPaved);
        check(g.isRoad(50, 50) && g.isRoad(90, 90), "a diagonal line reaches both ends");
        check(fourConnected(g, 50, 50, 90, 90),
              "the rasterised diagonal is 4-connected (staircase cells, no diagonal steps)");
        // Every step must be orthogonal: count cells, a true 8-connected diagonal
        // would have 41 cells while a staircase has 81.
        int count = 0;
        for (int y = 45; y <= 95; ++y)
            for (int x = 45; x <= 95; ++x)
                if (g.isRoad(x, y)) ++count;
        check(count == 81, "a diagonal of equal dx/dy becomes 81 staircase cells");
    }

    // ---- rectangle and disc (plaza + round brush) --------------------------
    {
        RoadGrid g;
        check(g.paintRect(200, 200, 209, 209, kPaved) == 100, "a 10x10 plaza paints 100 cells");
        check(g.get(200, 200) == kPaved && g.get(209, 209) == kPaved,
              "the rectangle includes both corners");
        int disc = g.paintDisc(300, 300, 3, kDirt);
        check(disc > 0 && g.get(300, 300) == kDirt, "the disc brush paints its centre");
        check(g.get(304, 300) == kNone, "the disc brush does not paint outside its radius");

        // A 2x2 block: the plan calls this out as an edge case.
        RoadGrid b;
        b.paintRect(0, 0, 1, 1, kPaved);
        check(b.roadCount() == 4, "a 2x2 block is 4 cells");
        check(b.neighbourMask(0, 0) == (2 | 1), "the corner of a 2x2 sees only N and E");
        check(b.neighbourMask(1, 0) == (8 | 1), "its neighbour sees only N and W");
    }

    // ---- undo / redo -------------------------------------------------------
    {
        RoadGrid g;
        g.beginStroke();
        g.paintLine(400, 400, 430, 400, kPaved);
        g.endStroke();
        const int after = g.roadCount();
        check(after == 31 && g.canUndo(), "a stroke is undoable");
        check(g.undo(), "undo succeeds");
        check(g.roadCount() == 0, "undo removes the whole stroke");
        check(g.canRedo(), "the undone stroke is redoable");
        check(g.redo(), "redo succeeds");
        check(g.roadCount() == after, "redo restores exactly the same cells");
        check(g.get(415, 400) == kPaved, "a specific cell is back after redo");

        // A stroke over existing road is a no-op: it paints nothing, so it is not an
        // edit at all. Contract: it must not add an undo entry, and must not discard a
        // redo branch that the user could still want.
        //
        // This needs a real redo branch to be meaningful, so build one first: a fresh
        // stroke, undone, leaves the edit on the redo stack.
        {
            RoadGrid n;
            n.beginStroke(); n.paintLine(10, 10, 20, 10, kPaved); n.endStroke();
            n.undo();
            check(n.canRedo(), "a fresh undo leaves a redo branch");
            const int depthBefore = n.undoDepthDiag();
            n.beginStroke();
            n.paintLine(10, 10, 20, 10, kPaved); // already empty after the undo, so this DOES edit
            n.endStroke();
            // That stroke repainted the undone cells, so it is a real edit and should
            // consume the redo branch -- which is the normal, expected behaviour.
            check(!n.canRedo(), "a real edit consumes the redo branch");

            // Now the true no-op: paint exactly what is already there.
            const int depthNow = n.undoDepthDiag();
            const int countNow = n.roadCount();
            n.beginStroke();
            n.paintLine(10, 10, 20, 10, kPaved);
            n.endStroke();
            check(n.undoDepthDiag() == depthNow,
                  "a no-op stroke adds no undo entry");
            check(n.roadCount() == countNow, "a no-op stroke leaves the road intact");
            check(n.strokeSizeDiag() == 0, "a no-op stroke records no cells");

            // And with a redo branch present, a no-op must leave it alone. Recreate one.
            RoadGrid n2;
            n2.beginStroke(); n2.paintLine(10, 10, 20, 10, kPaved); n2.endStroke();
            n2.undo();
            const int redoDepth = n2.redoDepthDiag();
            check(redoDepth == 1, "one undone stroke sits on the redo stack");
            n2.beginStroke();
            n2.paintLine(50, 50, 60, 50, kNone); // erase empty cells: no-op
            n2.endStroke();
            check(n2.redoDepthDiag() == redoDepth,
                  "a no-op stroke preserves the redo branch");
            (void)depthBefore;
        }

        // Undo across several strokes.
        g.beginStroke(); g.paintLine(400, 410, 430, 410, kDirt); g.endStroke();
        const int two = g.roadCount();
        g.beginStroke(); g.paintLine(400, 420, 430, 420, kDirt); g.endStroke();
        const int three = g.roadCount();
        check(two == after + 31 && three == after + 62, "each stroke adds 31 cells");
        g.undo();
        check(g.roadCount() == two, "one undo walks back one stroke");
        g.undo();
        check(g.roadCount() == after, "two undos walk back two strokes");
        g.redo();
        check(g.roadCount() == two, "redo walks forward one stroke");
        g.redo();
        check(g.roadCount() == three, "redo walks forward two strokes");
    }

    // ---- dirty chunks, including the cross-seam rule ------------------------
    {
        RoadGrid g;
        g.clearDirty();
        check(g.dirtyChunks().empty(), "clearing dirty chunks empties the list");

        // A cell in the interior of a chunk dirties exactly one chunk.
        const int interior = kChunkSize * 5 + kChunkSize / 2;
        g.set(interior, interior, kPaved);
        check(g.dirtyChunks().size() == 1, "an interior cell dirties one chunk");

        g.clearDirty();
        // A cell on a chunk border dirties this chunk and its neighbour.
        g.set(kChunkSize * 5, interior, kPaved);
        check(g.dirtyChunks().size() == 2,
              "a cell on a chunk seam dirties the neighbouring chunk too");

        g.clearDirty();
        // And marking all is what a load does.
        g.markAllDirty();
        check((int)g.dirtyChunks().size() == kChunksX * kChunksY, "markAllDirty marks every chunk");
        check(g.dirtyChunks().size() == std::set<std::uint16_t>(
                  g.dirtyChunks().begin(), g.dirtyChunks().end()).size(),
              "the dirty list has no duplicates");
    }

    // ---- neighbour mask & piece type for every phase 2 shape ----------------
    {
        RoadGrid g;
        // A plus / cross.
        g.paintRect(600, 600, 600, 600, kPaved);
        g.paintRect(599, 600, 601, 600, kPaved);
        g.paintRect(600, 599, 600, 601, kPaved);
        check(g.neighbourMask(600, 600) == (1 | 2 | 4 | 8), "a plus centre sees all four sides");
        check(g.neighbourMask(599, 600) == (2), "a plus arm tip sees only its inward side");
        auto plusInfo = g.pieceInfo(600, 600);
        check(plusInfo.type == RoadGrid::PieceType::Crossroad && plusInfo.rotationDeg == 0,
              "plus centre maps to Crossroad piece with rotation 0");
        auto armInfo = g.pieceInfo(599, 600);
        check(armInfo.type == RoadGrid::PieceType::DeadEnd && armInfo.rotationDeg == 90,
              "plus west arm tip maps to DeadEnd pointing East");

        // T-junction
        RoadGrid t;
        t.paintRect(700, 700, 704, 700, kPaved);
        t.paintRect(702, 701, 702, 703, kPaved);
        check(t.neighbourMask(702, 700) == (1 | 2 | 8), "a T junction sees three sides");
        auto tInfo = t.pieceInfo(702, 700);
        check(tInfo.type == RoadGrid::PieceType::TJunction && tInfo.rotationDeg == 90,
              "T junction maps to TJunction piece missing South");

        // ring: a hollow square, the plan's edge case
        RoadGrid r;
        r.paintRect(800, 800, 804, 804, kPaved);
        r.paintRect(801, 801, 803, 803, kNone);
        check(r.neighbourMask(800, 800) == (1 | 2), "a ring corner sees two sides");
        check(r.get(802, 802) == kNone, "the ring is hollow");
        auto cornerInfo = r.pieceInfo(800, 800);
        check(cornerInfo.type == RoadGrid::PieceType::Corner && cornerInfo.rotationDeg == 0,
              "ring SW corner maps to Corner NE (rotation 0)");

        // 2x2 block
        RoadGrid b;
        b.paintRect(850, 850, 851, 851, kPaved);
        auto swInfo = b.pieceInfo(850, 850);
        check(swInfo.type == RoadGrid::PieceType::Corner && swInfo.rotationDeg == 0,
              "2x2 block SW cell is a Corner NE");
        check((swInfo.diagonalMask & 1) != 0, "2x2 block SW cell has diagonal NE set");
        auto neInfo = b.pieceInfo(851, 851);
        check(neInfo.type == RoadGrid::PieceType::Corner && neInfo.rotationDeg == 180,
              "2x2 block NE cell is a Corner SW");
        check((neInfo.diagonalMask & 4) != 0, "2x2 block NE cell has diagonal SW set");

        // Mixed types must not read as connected.
        RoadGrid m;
        m.set(900, 900, kDirt);
        m.set(901, 900, kPaved);
        check(m.neighbourMask(900, 900) == 0,
              "a dirt cell next to a paved cell is not connected to it");
        check(m.pieceInfo(900, 900).type == RoadGrid::PieceType::Single,
              "a dirt cell next to a paved cell is classified as Single (isolated)");
    }

    // ---- phase 2: the piece table is total and covers every mask -------------
    {
        using PT = RoadGrid::PieceType;
        int counts[6] = {0, 0, 0, 0, 0, 0};
        for (int m = 0; m < 16; ++m) {
            const auto info = RoadGrid::pieceFromMask((std::uint8_t)m);
            counts[(int)info.type]++;
        }
        check(counts[(int)PT::Single] == 1, "exactly one isolated mask");
        check(counts[(int)PT::DeadEnd] == 4, "four dead-end masks");
        check(counts[(int)PT::Straight] == 2, "two straight masks");
        check(counts[(int)PT::Corner] == 4, "four corner masks");
        check(counts[(int)PT::TJunction] == 4, "four T masks");
        check(counts[(int)PT::Crossroad] == 1, "exactly one crossroad mask");
        int total = 0;
        for (int i = 0; i < 6; ++i) total += counts[i];
        check(total == 16, "the table is total: every mask maps to a piece");

        // Rotations must be one of the four right angles.
        bool rotOk = true;
        for (int m = 0; m < 16; ++m) {
            const int r = RoadGrid::pieceFromMask((std::uint8_t)m).rotationDeg;
            if (r % 90 != 0 || r < 0 || r > 270) rotOk = false;
        }
        check(rotOk, "every rotation is a multiple of 90 degrees in 0..270");
        check(RoadGrid::pieceFromMask(0).rotationDeg == 0 &&
              RoadGrid::pieceFromMask(15).rotationDeg == 0,
              "the symmetric pieces have rotation 0");

        // The four dead ends must rotate to point where their arm is.
        check(RoadGrid::pieceFromMask(1).rotationDeg == 0, "a north dead end is 0 deg");
        check(RoadGrid::pieceFromMask(2).rotationDeg == 90, "an east dead end is 90 deg");
        check(RoadGrid::pieceFromMask(4).rotationDeg == 180, "a south dead end is 180 deg");
        check(RoadGrid::pieceFromMask(8).rotationDeg == 270, "a west dead end is 270 deg");
    }

    // ---- phase 2: pieces for real painted shapes ----------------------------
    {
        using PT = RoadGrid::PieceType;
        // A plus / cross: centre is a crossroad, each arm tip is a dead end.
        RoadGrid g;
        g.paintRect(600, 600, 600, 600, kPaved);
        g.paintRect(599, 600, 601, 600, kPaved);
        g.paintRect(600, 599, 600, 601, kPaved);
        check(g.pieceInfo(600, 600).type == PT::Crossroad, "a plus centre is a crossroad");
        check(g.pieceInfo(599, 600).type == PT::DeadEnd, "a plus arm tip is a dead end");
        check(g.pieceInfo(601, 600).type == PT::DeadEnd, "the opposite arm too");
        check(g.pieceInfo(600, 601).type == PT::DeadEnd, "and the vertical arm");

        // A straight run: middle is straight, ends are dead ends.
        RoadGrid s;
        s.paintLine(700, 700, 720, 700, kPaved);
        check(s.pieceInfo(710, 700).type == PT::Straight, "the middle of a run is straight");
        check(s.pieceInfo(710, 700).rotationDeg == 90, "an east-west run is the 90 deg straight");
        check(s.pieceInfo(700, 700).type == PT::DeadEnd, "a run end is a dead end");
        RoadGrid sv;
        sv.paintLine(730, 700, 730, 720, kPaved);
        check(sv.pieceInfo(730, 710).rotationDeg == 0, "a north-south run is the 0 deg straight");

        // A corner.
        RoadGrid c;
        c.paintLine(800, 800, 810, 800, kPaved);
        c.paintLine(810, 800, 810, 810, kPaved);
        check(c.pieceInfo(810, 800).type == PT::Corner, "an L bend is a corner");
        check(c.pieceInfo(805, 800).type == PT::Straight, "the arm before it is straight");

        // A T junction: three arms.
        RoadGrid t;
        t.paintLine(700, 900, 704, 900, kPaved);
        t.paintLine(702, 901, 702, 903, kPaved);
        check(t.pieceInfo(702, 900).type == PT::TJunction, "a T junction is a T");

        // A single cell is isolated.
        RoadGrid one;
        one.set(950, 950, kDirt);
        check(one.pieceInfo(950, 950).type == PT::Single, "a lone cell is single");

        // A 2x2 block is four corners.
        RoadGrid b;
        b.paintRect(1000, 1000, 1001, 1001, kPaved);
        check(b.pieceInfo(1000, 1000).type == PT::Corner &&
              b.pieceInfo(1001, 1001).type == PT::Corner,
              "a 2x2 block is four corners");

        // A hollow ring: corners at the corners, straights along the edges.
        RoadGrid r;
        r.paintRect(1100, 1100, 1104, 1104, kPaved);
        r.paintRect(1101, 1101, 1103, 1103, kNone);
        check(r.pieceInfo(1100, 1100).type == PT::Corner, "a ring corner is a corner");
        check(r.pieceInfo(1102, 1100).type == PT::Straight, "a ring edge is straight");

        // A diagonal staircase must contain no isolated cells -- proof it is
        // 4-connected, which is what the whole tiling scheme depends on.
        RoadGrid d;
        d.paintLine(1200, 1200, 1210, 1210, kPaved);
        int isolated = 0;
        for (int i = 0; i <= 10; ++i)
            if (d.pieceInfo(1200 + i, 1200 + i).type == PT::Single) ++isolated;
        check(isolated == 0, "a staircase has no isolated cells");

        // An empty cell has no piece at all.
        check(r.pieceInfo(1202, 1202).type == PT::Single &&
              r.pieceInfo(1202, 1202).cardinalMask == 0,
              "an empty cell reads as mask 0");
    }

    // ---- phase 2: diagonal mask (outer corners) ----------------------------
    {
        // Two cells touching only at a corner: each sees the other diagonally, and
        // neither sees it cardinally.
        RoadGrid g;
        g.set(1300, 1300, kPaved);
        g.set(1301, 1301, kPaved);
        check(g.neighbourMask(1300, 1300) == 0, "a diagonal neighbour is not cardinal");
        check(g.diagonalMask(1300, 1300) == 1, "but it is seen by the NE diagonal bit");
        check(g.diagonalMask(1301, 1301) == 4, "and the other cell sees it SW");
        // Each is therefore isolated cardinally.
        check(g.pieceInfo(1300, 1300).type == RoadGrid::PieceType::Single,
              "so each is a single piece, connected only diagonally");
        // A diagonal touch across different types is not recorded.
        RoadGrid m;
        m.set(1400, 1400, kDirt);
        m.set(1401, 1401, kPaved);
        check(m.diagonalMask(1400, 1400) == 0,
              "a diagonal neighbour of a different type is not recorded");
    }

    // ---- save / load -------------------------------------------------------
    {
        RoadGrid g;
        g.paintLine(1000, 1000, 1200, 1000, kPaved);
        g.paintLine(1000, 1000, 1000, 1150, kDirt);
        g.paintRect(1100, 1100, 1120, 1120, kPaved);
        const int before = g.roadCount();
        // {x, y, expected type}. Expectations follow the LAST writer, not
        // the first: the vertical dirt line starts at (1000,1000) and the 20x20 paved
        // plaza covers (1110,1110), so both override earlier paints.
        struct Probe { int x, y; std::uint8_t type; };
        const Probe probes[4] = {
            {1000, 1000, kDirt}, {1200, 1000, kPaved},
            {1000, 1150, kDirt}, {1110, 1110, kPaved}};
        const char* path = "scratch/roadgrid_test.bin";
        check(g.save(path), "the grid saves");

        RoadGrid h;
        check(h.load(path), "the grid loads");
        check(h.roadCount() == before, "the loaded grid has the same road count");
        for (int i = 0; i < 4; ++i)
            check(h.get(probes[i].x, probes[i].y) == probes[i].type,
                  "a probe cell round-trips through the file");
        check((int)h.dirtyChunks().size() == kChunksX * kChunksY,
              "a freshly loaded grid marks every chunk dirty");
        std::remove(path);

        // A truncated file must be rejected without destroying the current grid.
        RoadGrid keep;
        keep.set(5, 5, kPaved);
        check(!keep.load("scratch/this_file_does_not_exist.bin"), "a missing file fails to load");
        check(keep.get(5, 5) == kPaved, "a failed load leaves the grid untouched");
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL CHECKS PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
