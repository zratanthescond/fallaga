// Standalone unit test for the 4-direction road bitmasking core.
//
//   g++ -std=c++17 -O1 roadtiles_test.cpp -o roadtiles_test.exe
//   roadtiles_test.exe
//
// No OpenGL / glm / project dependencies: RoadTileMask.h is header-only, so
// this exercises the exact code the engine ships (unlike roadfix_test.cpp,
// which duplicates the algorithm it tests).
#include "src/RoadTileMask.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace RoadTile;

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                      \
    } while (0)

static void checkShape(uint8_t mask, Shape expected) {
    ++g_checks;
    if (shapeFromMask(mask) != expected) {
        ++g_failures;
        std::printf("FAIL mask %d decoded as %s, expected %s\n", (int)mask,
                    shapeName(shapeFromMask(mask)), shapeName(expected));
    }
}

// ── 1. Bit contract ──────────────────────────────────────────────────────────
static void testBitContract() {
    CHECK(North == 1);
    CHECK(East  == 2);
    CHECK(South == 4);
    CHECK(West  == 8);
    CHECK(All   == 15);
    CHECK((North | East | South | West) == 15);
    CHECK(armCount(All) == 4);
    CHECK(armCount(0) == 0);
    CHECK(armCount((uint8_t)Shape::Straight_EW) == 2);

    // Held in variables so the checks are not constant expressions (MSVC C4127).
    const uint8_t flipN = opposite(North);
    const uint8_t flipE = opposite(East);
    const uint8_t flipNE = opposite(North | East);
    CHECK(flipN == South);
    CHECK(flipE == West);
    CHECK(flipNE == (South | West));
}

// ── 2. All 16 masks decode to the documented shape ────────────────────────────
static void testShapeDecoding() {
    checkShape(0,  Shape::Single);
    checkShape(1,  Shape::DeadEnd_N);
    checkShape(2,  Shape::DeadEnd_E);
    checkShape(3,  Shape::Corner_NE);
    checkShape(4,  Shape::DeadEnd_S);
    checkShape(5,  Shape::Straight_NS);
    checkShape(6,  Shape::Corner_SE);
    checkShape(7,  Shape::TJunction_NoW);
    checkShape(8,  Shape::DeadEnd_W);
    checkShape(9,  Shape::Corner_NW);
    checkShape(10, Shape::Straight_EW);
    checkShape(11, Shape::TJunction_NoS);
    checkShape(12, Shape::Corner_SW);
    checkShape(13, Shape::TJunction_NoE);
    checkShape(14, Shape::TJunction_NoN);
    checkShape(15, Shape::Crossroad);
    // Shape values are the masks, so the decode is a cast away.
    for (int m = 0; m <= 15; ++m)
        CHECK((int)shapeFromMask((uint8_t)m) == m);
}

// ── 3. Orientation: row 0 is south, so North is row + 1 ───────────────────────
static void testOrientation() {
    MaskGrid g;
    g.resize(8, 8);
    g.placeRoad(4, 4);
    g.placeRoad(4, 5);

    // (4,5) lies north of (4,4), so the southern cell carries a North arm and
    // the northern one a South arm. A flipped convention would swap both.
    CHECK(g.maskAt(4, 4) == North);
    CHECK(g.maskAt(4, 5) == South);
    CHECK(g.shapeAt(4, 4).value() == Shape::DeadEnd_N);
    CHECK(g.shapeAt(4, 5).value() == Shape::DeadEnd_S);

    g.placeRoad(5, 5);                                // arm to the east
    CHECK(g.maskAt(4, 5) == (South | East));
    CHECK(g.maskAt(5, 5) == West);

    g.placeRoad(1, 1);                                // isolated: mask 0
    CHECK(g.maskAt(1, 1) == 0);
    CHECK(g.shapeAt(1, 1).value() == Shape::Single);
}

// ── 4. Straights ─────────────────────────────────────────────────────────────
static void testStraights() {
    MaskGrid g;
    g.resize(12, 12);

    // Horizontal run along columns 1..5, row 3.
    for (int c = 1; c <= 5; ++c) g.placeRoad(c, 3);
    CHECK(g.maskAt(1, 3) == East);        // terminal cell, arm pointing east
    CHECK(g.maskAt(5, 3) == West);
    for (int c = 2; c <= 4; ++c) CHECK(g.maskAt(c, 3) == (uint8_t)Shape::Straight_EW);
    CHECK(g.roadCellCount() == 5);
    CHECK(g.countShape(Shape::Straight_EW) == 3);
    CHECK(g.countShape(Shape::DeadEnd_E) == 1);
    CHECK(g.countShape(Shape::DeadEnd_W) == 1);

    // Vertical run along rows 1..5, column 7 (North is row + 1).
    for (int r = 1; r <= 5; ++r) g.placeRoad(7, r);
    CHECK(g.maskAt(7, 1) == North);                  // southern terminal cell
    CHECK(g.maskAt(7, 5) == South);                  // northern terminal cell
    for (int r = 2; r <= 4; ++r) CHECK(g.maskAt(7, r) == (uint8_t)Shape::Straight_NS);

    // A straight must never read as a corner: perpendicular neighbours stay 0.
    CHECK(g.maskAt(2, 2) == 0);
    CHECK(g.maskAt(2, 4) == 0);
}

// ── 5. Corners ───────────────────────────────────────────────────────────────
static void testCorners() {
    MaskGrid g;
    g.resize(12, 12);
    g.placeRoad(3, 3);
    g.placeRoad(3, 4);   // arm to the north (row + 1)
    g.placeRoad(4, 3);   // arm to the east
    CHECK(g.maskAt(3, 3) == (North | East));
    CHECK(g.shapeAt(3, 3).value() == Shape::Corner_NE);
    CHECK(g.shapeAt(4, 3).value() == Shape::DeadEnd_W);
    CHECK(g.shapeAt(3, 4).value() == Shape::DeadEnd_S);
    CHECK(g.countShape(Shape::Corner_NE) == 1);
}

// ── 6. T-junction and crossroad ──────────────────────────────────────────────
static void testJunctions() {
    MaskGrid g;
    g.resize(12, 12);
    g.placeRoad(3, 3);   // centre
    g.placeRoad(3, 4);   // arm north
    g.placeRoad(2, 3);   // arm west
    g.placeRoad(4, 3);   // arm east
    CHECK(g.maskAt(3, 3) == (North | East | West));       // 11, missing south
    CHECK(g.shapeAt(3, 3).value() == Shape::TJunction_NoS);
    CHECK(armCount(g.maskAt(3, 3)) == 3);
    CHECK(g.countShape(Shape::TJunction_NoS) == 1);

    g.placeRoad(3, 2);                                    // closes the crossroad
    CHECK(g.maskAt(3, 3) == All);
    CHECK(g.shapeAt(3, 3).value() == Shape::Crossroad);
    CHECK(g.countShape(Shape::Crossroad) == 1);
    CHECK(g.countShape(Shape::TJunction_NoS) == 0);      // histogram stays exact
}

// ── 7. Incremental updates (place / remove) ──────────────────────────────────
static void testIncrementalUpdates() {
    MaskGrid g;
    g.resize(12, 12);
    g.placeRoad(5, 5);
    g.placeRoad(5, 6);
    CHECK(g.maskAt(5, 5) == North);

    g.removeRoad(5, 6);
    CHECK(!g.isRoad(5, 6));
    CHECK(g.maskAt(5, 5) == 0);                          // bit cleared, not stale
    CHECK(g.countShape(Shape::Single) == 1);
    CHECK(g.roadCellCount() == 1);

    // Removing a cell that is not a road is a no-op.
    CHECK(g.removeRoad(5, 6) == false);
    // Re-placing an identical cell is a no-op too (idempotence).
    CHECK(g.placeRoad(5, 5) == false);
    CHECK(g.maskAt(5, 5) == 0);
    CHECK(g.roadCellCount() == 1);

    // Neighbours far away are untouched.
    g.placeRoad(5, 7);
    CHECK(g.maskAt(5, 4) == 0);
    CHECK(g.maskAt(0, 0) == 0);
}

// ── 8. No diagonal leakage ───────────────────────────────────────────────────
static void testNoDiagonalLeak() {
    MaskGrid g;
    g.resize(8, 8);
    g.placeRoad(2, 2);
    g.placeRoad(3, 3);   // diagonal neighbour only
    CHECK(g.maskAt(2, 2) == 0);
    CHECK(g.maskAt(3, 3) == 0);
    CHECK(g.shapeAt(2, 2).value() == Shape::Single);

    g.placeRoad(3, 2);   // bridge the diagonal: orthogonal links only
    CHECK(g.maskAt(2, 2) == East);
    CHECK(g.maskAt(3, 3) == South);
    CHECK(g.maskAt(3, 2) == (North | West));
}

// ── 9. Bounds ────────────────────────────────────────────────────────────────
static void testBounds() {
    MaskGrid g;
    g.resize(4, 4);
    CHECK(g.placeRoad(-1, 0) == false);
    CHECK(g.placeRoad(0, -1) == false);
    CHECK(g.placeRoad(4, 0) == false);
    CHECK(g.placeRoad(0, 4) == false);
    CHECK(g.roadCellCount() == 0);
    CHECK(g.maskAt(-1, -1) == 0);
    CHECK(g.maskAt(4, 4) == 0);
    CHECK(!g.isRoad(-1, 0));
    CHECK(!g.shapeAt(9, 9).has_value());
    CHECK(g.roadTypeAt(0, 0) == MaskGrid::kNoRoad);

    // The four corners must stay independent (no wrap-around).
    g.placeRoad(0, 0);
    g.placeRoad(3, 0);
    g.placeRoad(0, 3);
    g.placeRoad(3, 3);
    CHECK(g.maskAt(0, 0) == 0);
    CHECK(g.maskAt(3, 0) == 0);
    CHECK(g.roadCellCount() == 4);
}

// ── 10. Derived stamping equals incremental authoring ────────────────────────
static void testDerivedMatchesAuthored() {
    const int W = 16, H = 16;
    std::vector<std::pair<int, int>> cells;
    for (int r = 2; r <= 12; ++r)
        for (int c = 3; c <= 11; ++c)
            if ((r + c) % 7 != 0) cells.push_back({ c, r });   // holes everywhere

    MaskGrid authored;
    authored.resize(W, H);
    for (const auto& c : cells) authored.placeRoad(c.first, c.second);

    // Same layout stamped in reverse order, masks recomputed in one pass.
    MaskGrid derived;
    derived.resize(W, H);
    for (auto it = cells.rbegin(); it != cells.rend(); ++it)
        derived.stampOccupancy(it->first, it->second, 1);
    derived.recomputeAllMasks();

    bool identical = authored.roadCellCount() == derived.roadCellCount();
    for (int r = 0; r < H && identical; ++r)
        for (int c = 0; c < W; ++c)
            if (authored.maskAt(c, r) != derived.maskAt(c, r) ||
                authored.isRoad(c, r) != derived.isRoad(c, r))
                identical = false;
    CHECK(identical);

    // Rebuilding twice from scratch is deterministic.
    MaskGrid again;
    again.resize(W, H);
    for (const auto& c : cells) again.stampOccupancy(c.first, c.second, 1);
    again.recomputeAllMasks();
    bool stable = true;
    for (int r = 0; r < H; ++r)
        for (int c = 0; c < W; ++c)
            if (again.maskAt(c, r) != authored.maskAt(c, r)) stable = false;
    CHECK(stable);

    // A paved avenue outranks a dirt track in a shared cell.
    MaskGrid types;
    types.resize(4, 4);
    types.stampOccupancy(1, 1, 4);   // CountryDirt + 1
    types.stampOccupancy(1, 1, 1);   // ColonialAvenue + 1 wins
    types.stampOccupancy(1, 1, 2);   // ignored
    CHECK(types.roadTypeAt(1, 1) == 1);
    CHECK(types.roadTypeAt(2, 2) == MaskGrid::kNoRoad);
}

// ── 11. World → cell mapping (the flip/axis trap) ─────────────────────────────
static void testWorldMapping() {
    WorldToCell m;
    m.minX = -100.0f; m.maxX = 100.0f;
    m.minZ = -50.0f;  m.maxZ = 50.0f;
    const int W = 11, H = 11;

    // Row 0 is the south edge (minZ), column 0 the west edge (minX).
    float x, z;
    m.worldFor(0, 0, W, H, x, z);
    CHECK(x == -100.0f && z == -50.0f);
    m.worldFor(10, 10, W, H, x, z);
    CHECK(x == 100.0f && z == 50.0f);
    CHECK(m.colFor(-100.0f, W) == 0);
    CHECK(m.rowFor(-50.0f, H) == 0);
    CHECK(m.colFor(100.0f, W) == 10);
    CHECK(m.rowFor(50.0f, H) == 10);

    // North (+Z) is row + 1, East (+X) is col + 1.
    CHECK(m.northRow() == 1);
    CHECK(m.rowFor(-40.0f, H) == m.rowFor(-50.0f, H) + 1);
    CHECK(m.rowFor(40.0f, H) == m.rowFor(50.0f, H) - 1);
    CHECK(m.colFor(0.0f, W) == 5);
    CHECK(m.rowFor(60.0f, H) == 10);                    // clamped at the edge
    CHECK(m.colFor(110.0f, W) == 10);                   // clamped, no overflow
    CHECK(m.colFor(-110.0f, W) == 0);

    // Round trip: every cell centre maps back to itself.
    bool roundTrip = true;
    for (int c = 0; c < W; ++c)
        for (int r = 0; r < H; ++r) {
            m.worldFor(c, r, W, H, x, z);
            if (m.colFor(x, W) != c || m.rowFor(z, H) != r) roundTrip = false;
        }
    CHECK(roundTrip);

    // A Z-flipped heightmap inverts the row order and the north direction.
    WorldToCell f = m;
    f.flipZ = true;
    CHECK(f.northRow() == -1);
    f.worldFor(0, 0, W, H, x, z);
    CHECK(z == 50.0f);
    f.worldFor(10, 10, W, H, x, z);
    CHECK(z == -50.0f);
    bool flipRoundTrip = true;
    for (int c = 0; c < W; ++c)
        for (int r = 0; r < H; ++r) {
            f.worldFor(c, r, W, H, x, z);
            if (f.colFor(x, W) != c || f.rowFor(z, H) != r) flipRoundTrip = false;
        }
    CHECK(flipRoundTrip);
}

// ── 12. Ribbon walk (supercover: consecutive cells always 4-adjacent) ────────
static void testWalkRibbon() {
    WorldToCell m;
    m.minX = 0.0f; m.maxX = 100.0f;
    m.minZ = 0.0f; m.maxZ = 100.0f;
    const int W = 11, H = 11;   // 10 m cells

    auto collect = [&](float x0, float z0, float x1, float z1) {
        std::vector<std::pair<int, int>> cells;
        walkRibbon(m, W, H, x0, z0, x1, z1,
                   [&](int c, int r) { cells.push_back({ c, r }); });
        return cells;
    };

    // Axis-aligned runs touch every cell on the way, and only those.
    auto h = collect(0.0f, 50.0f, 100.0f, 50.0f);
    CHECK(h.size() == 11);
    CHECK(h.front() == std::make_pair(0, 5) && h.back() == std::make_pair(10, 5));

    auto v = collect(50.0f, 0.0f, 50.0f, 100.0f);
    CHECK(v.size() == 11);
    CHECK(v.front() == std::make_pair(5, 0) && v.back() == std::make_pair(5, 10));

    // A 45° run visits both cells at every corner, so it stays 4-connected.
    auto d = collect(0.0f, 0.0f, 100.0f, 100.0f);
    CHECK(d.size() == 21);                       // 11 cells + 10 corner bridges
    CHECK(d.front() == std::make_pair(0, 0) && d.back() == std::make_pair(10, 10));
    bool diagonalConnected = true;
    for (size_t i = 1; i < d.size(); ++i) {
        int step = std::abs(d[i].first - d[i - 1].first) +
                   std::abs(d[i].second - d[i - 1].second);
        if (step != 1) diagonalConnected = false;   // never a diagonal-only jump
    }
    CHECK(diagonalConnected);

    // Degenerate input must terminate and stay on the start cell.
    auto zero = collect(30.0f, 30.0f, 30.0f, 30.0f);
    CHECK(zero.size() == 1);
    CHECK(zero.front() == std::make_pair(3, 3));

    // Out-of-range endpoints are visited, not clamped: the grid drops them.
    // -500 m and +500 m map to cell -50 and +50 on a 10 m grid.
    auto outside = collect(-500.0f, 50.0f, 500.0f, 50.0f);
    CHECK(outside.size() == 101);
    CHECK(outside.front().first == -50);
    CHECK(outside.back().first == 50);

    // Every walked cell is 4-adjacent to the next, for arbitrary directions.
    bool allConnected = true;
    for (int k = 0; k < 32; ++k) {
        float a = -20.0f + k * 4.3f, b = 70.0f - k * 1.7f;
        auto cells = collect(a, b, 100.0f - b, a);
        for (size_t i = 1; i < cells.size(); ++i)
            if (std::abs(cells[i].first - cells[i - 1].first) +
                    std::abs(cells[i].second - cells[i - 1].second) != 1)
                allConnected = false;
    }
    CHECK(allConnected);
}

// ── 13. Console window cropping ──────────────────────────────────────────────
static void testCropRange() {
    int lo = 0, hi = 0;

    lo = 10; hi = 20;
    CHECK(MaskGrid::cropRange(lo, hi, 48, 1000) == false);   // small range: untouched
    CHECK(lo == 10 && hi == 20);

    lo = 100; hi = 200;
    CHECK(MaskGrid::cropRange(lo, hi, 48, 1000) == true);
    CHECK(lo == 100 && hi == 147);                         // anchored on the first cell
    CHECK(hi - lo + 1 == 48);

    lo = 0; hi = 100;                                       // at the low edge
    CHECK(MaskGrid::cropRange(lo, hi, 48, 1000) == true);
    CHECK(lo == 0 && hi - lo + 1 == 48);

    lo = 940; hi = 1000;                                    // wide range past the last cell
    CHECK(MaskGrid::cropRange(lo, hi, 48, 1000) == true);
    CHECK(lo == 940 && hi == 987);                          // shifted inside the grid
    CHECK(hi - lo + 1 == 48);
}

// ── 13. Smoke: debug dump of a small field ───────────────────────────────────
static void testDebugPrint() {
    MaskGrid g;
    g.resize(9, 9);
    for (int c = 2; c <= 6; ++c) g.placeRoad(c, 4);
    g.placeRoad(4, 5);
    g.placeRoad(4, 6);
    g.placeRoad(7, 4);
    g.debugPrint();

    // A field whose roads span far more than the print window must still print
    // the window instead of an empty block.
    MaskGrid wide;
    wide.resize(200, 200);
    wide.placeRoad(20, 20);
    wide.placeRoad(180, 180);
    wide.debugPrint(8);
}

// ── 15. Incremental bulk rebuild (the per-edit path) ──────────────────────────
static void testIncrementalBulkRebuild() {
    const int W = 24, H = 24;
    std::vector<std::pair<int, int>> cells;
    for (int r = 3; r <= 18; ++r)
        for (int c = 4; c <= 20; ++c)
            if ((r * c) % 5 != 0) cells.push_back({ c, r });

    auto stampAndRefresh = [](MaskGrid& g, const std::vector<std::pair<int, int>>& cs) {
        for (const auto& c : cs) g.stampOccupancy(c.first, c.second, 1);
        // Per-span rects, dilated by one, exactly like RoadTileField::build.
        for (const auto& c : cs)
            g.recomputeMasksInRect(c.first - 1, c.second - 1, c.first + 1, c.second + 1);
        g.rebuildHistogram();
    };

    // Reference: full-grid pass over the same layout.
    MaskGrid reference;
    reference.resize(W, H);
    for (const auto& c : cells) reference.stampOccupancy(c.first, c.second, 1);
    reference.recomputeAllMasks();

    // The incremental path must agree with the full pass, cell for cell.
    MaskGrid g;
    g.beginBulkStamp(W, H);
    stampAndRefresh(g, cells);
    bool same = g.roadCellCount() == reference.roadCellCount();
    for (int r = 0; r < H && same; ++r)
        for (int c = 0; c < W; ++c)
            if (g.maskAt(c, r) != reference.maskAt(c, r)) same = false;
    CHECK(same);
    for (int m = 0; m < 16; ++m) CHECK(g.countMask((uint8_t)m) == reference.countMask((uint8_t)m));

    // Rebuilding on top of a previous stamp (a second road edit) must not leave
    // any stale mask behind, and must drop the cells the new layout dropped.
    std::vector<std::pair<int, int>> fewer(cells.begin(), cells.begin() + cells.size() / 2);
    g.beginBulkStamp(W, H);
    stampAndRefresh(g, fewer);
    MaskGrid ref2;
    ref2.resize(W, H);
    for (const auto& c : fewer) ref2.stampOccupancy(c.first, c.second, 1);
    ref2.recomputeAllMasks();
    same = g.roadCellCount() == ref2.roadCellCount();
    for (int r = 0; r < H && same; ++r)
        for (int c = 0; c < W; ++c)
            if (g.maskAt(c, r) != ref2.maskAt(c, r) || g.isRoad(c, r) != ref2.isRoad(c, r))
                same = false;
    CHECK(same);
    for (int m = 0; m < 16; ++m) CHECK(g.countMask((uint8_t)m) == ref2.countMask((uint8_t)m));

    // clearRoad() empties the stamp set without touching the allocation.
    g.clearRoad();
    CHECK(g.roadCellCount() == 0);
    CHECK(!g.isRoad(4, 3));
    CHECK(g.maskAt(4, 3) == 0);
    int total = 0;
    for (int m = 0; m < 16; ++m) total += g.countMask((uint8_t)m);
    CHECK(total == 0);

    // A shrink+rebuild (new resolution) must not resurrect the old cells.
    g.beginBulkStamp(8, 8);
    stampAndRefresh(g, { { 1, 2 }, { 2, 2 }, { 3, 2 } });
    CHECK(g.roadCellCount() == 3);
    CHECK(g.countShape(Shape::Straight_EW) == 1);      // only the middle cell
    CHECK(g.countShape(Shape::DeadEnd_E) == 1);
    CHECK(g.countShape(Shape::DeadEnd_W) == 1);
    CHECK(!g.isRoad(4, 3));

    // beginBulkStamp at an unchanged size keeps a usable grid (no full clear).
    g.beginBulkStamp(8, 8);
    stampAndRefresh(g, { { 1, 1 }, { 2, 1 } });
    CHECK(g.roadCellCount() == 2);
    CHECK(g.maskAt(1, 1) == East);                    // two cells = two stubs
    CHECK(g.maskAt(2, 1) == West);
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    testBitContract();
    testShapeDecoding();
    testOrientation();
    testStraights();
    testCorners();
    testJunctions();
    testIncrementalUpdates();
    testNoDiagonalLeak();
    testBounds();
    testDerivedMatchesAuthored();
    testWorldMapping();
    testWalkRibbon();
    testCropRange();
    testDebugPrint();
    testIncrementalBulkRebuild();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
