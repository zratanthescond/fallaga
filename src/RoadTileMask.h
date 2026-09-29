#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  RoadTileMask.h — 4-direction road auto-tiling primitives
//
//  Every road cell inspects its 4 direct neighbours and ORs the bits of the
//  ones that are also road; the resulting 0..15 value identifies one shape and
//  one rotation (the classic Caesar-style bitmasking trick).
//
//      North (up,    +Z) = 1   (0001)
//      East  (right, +X) = 2   (0010)
//      South (down,   -Z) = 4   (0100)
//      West  (left,  -X) = 8   (1000)
//
//  This header is intentionally free of OpenGL / glm / project dependencies so
//  the bit logic and the world→cell orientation can be unit-tested standalone
//  (see roadtiles_test.cpp).
// ─────────────────────────────────────────────────────────────────────────────
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace RoadTile {

constexpr uint8_t North = 1;
constexpr uint8_t East  = 2;
constexpr uint8_t South = 4;
constexpr uint8_t West  = 8;
constexpr uint8_t All   = North | East | South | West;   // 15

// Shape decoded from a 4-bit mask (the "RoadSprite" of the reference design).
// Each enumerator equals the mask it decodes, so Shape <-> mask is a cast away.
// T-junctions are named after the arm that is MISSING — that is the only thing
// telling the four of them apart.
enum class Shape : uint8_t {
    Single         = 0,    // isolated patch
    DeadEnd_N      = 1,
    DeadEnd_E      = 2,
    Corner_NE      = 3,
    DeadEnd_S      = 4,
    Straight_NS    = 5,    // vertical   (N|S)
    Corner_SE      = 6,
    TJunction_NoW  = 7,    // arms N|E|S
    DeadEnd_W      = 8,
    Corner_NW      = 9,
    Straight_EW    = 10,   // horizontal (E|W)
    TJunction_NoS  = 11,   // arms N|E|W
    Corner_SW      = 12,
    TJunction_NoE  = 13,   // arms N|S|W
    TJunction_NoN  = 14,   // arms E|S|W
    Crossroad      = 15
};

constexpr Shape shapeFromMask(uint8_t mask) {
    return static_cast<Shape>(mask & All);
}

inline const char* shapeName(Shape s) {
    switch (s) {
        case Shape::Single:        return "Single";
        case Shape::DeadEnd_N:     return "DeadEnd_N";
        case Shape::DeadEnd_E:     return "DeadEnd_E";
        case Shape::Corner_NE:     return "Corner_NE";
        case Shape::DeadEnd_S:     return "DeadEnd_S";
        case Shape::Straight_NS:   return "Straight_NS";
        case Shape::Corner_SE:     return "Corner_SE";
        case Shape::TJunction_NoW: return "T_NoW";
        case Shape::DeadEnd_W:     return "DeadEnd_W";
        case Shape::Corner_NW:     return "Corner_NW";
        case Shape::Straight_EW:   return "Straight_EW";
        case Shape::TJunction_NoS: return "T_NoS";
        case Shape::Corner_SW:     return "Corner_SW";
        case Shape::TJunction_NoE: return "T_NoE";
        case Shape::TJunction_NoN: return "T_NoN";
        case Shape::Crossroad:     return "Crossroad";
    }
    return "Unknown";
}

// One ASCII glyph per shape, for the console dump (' ' = no road).
inline char shapeGlyph(Shape s) {
    switch (s) {
        case Shape::Single:        return '.';
        case Shape::DeadEnd_N:     return '|';
        case Shape::DeadEnd_E:     return '-';
        case Shape::Corner_NE:     return 'L';
        case Shape::DeadEnd_S:     return '|';
        case Shape::Straight_NS:   return '|';
        case Shape::Corner_SE:     return 'J';
        case Shape::TJunction_NoW: return 'H';
        case Shape::DeadEnd_W:     return '-';
        case Shape::Corner_NW:     return '7';
        case Shape::Straight_EW:   return '-';
        case Shape::TJunction_NoS: return 'T';
        case Shape::Corner_SW:     return 'r';
        case Shape::TJunction_NoE: return 'I';
        case Shape::TJunction_NoN: return 'T';
        case Shape::Crossroad:     return '+';
    }
    return '?';
}

constexpr int armCount(uint8_t mask) {
    int n = 0;
    for (uint8_t bit : { North, East, South, West })
        if (mask & bit) ++n;
    return n;
}

constexpr uint8_t opposite(uint8_t direction) {
    return static_cast<uint8_t>(
        ((direction & North) ? South : 0) |
        ((direction & South) ? North : 0) |
        ((direction & East)  ? West  : 0) |
        ((direction & West)  ? East  : 0));
}

// ──────────────────────── WORLD → CELL MAPPING ──────────────────────────────
//
//  Row convention: row 0 is the SOUTH edge (minZ) and column 0 the WEST edge
//  (minX), matching TerrainCoordinateMapper::worldToUV and RoadDeformer, so
//  North is row + 1 and needs no flip. When the heightmap is Z-flipped the row
//  order is inverted and northRow() returns -1.

struct WorldToCell {
    float minX = 0.0f, maxX = 0.0f;
    float minZ = 0.0f, maxZ = 0.0f;
    bool  flipZ = false;

    int northRow() const { return flipZ ? -1 : 1; }

    // Continuous cell coordinates (may fall outside [0, count-1]).
    void cellF(float x, float z, int w, int h, float& col, float& row) const {
        float u = (maxX > minX) ? (x - minX) / (maxX - minX) : 0.0f;
        float v = (maxZ > minZ) ? (z - minZ) / (maxZ - minZ) : 0.0f;
        if (flipZ) v = 1.0f - v;
        col = u * (w - 1);
        row = v * (h - 1);
    }

    int colFor(float x, int w) const {
        float col, row;
        cellF(x, 0.0f, w, 1, col, row);
        return std::clamp(static_cast<int>(std::lround(col)), 0, w - 1);
    }

    int rowFor(float z, int h) const {
        float col, row;
        cellF(0.0f, z, 1, h, col, row);
        return std::clamp(static_cast<int>(std::lround(row)), 0, h - 1);
    }

    void worldFor(int col, int row, int w, int h, float& x, float& z) const {
        float u = static_cast<float>(col) / static_cast<float>(w - 1);
        float v = static_cast<float>(row) / static_cast<float>(h - 1);
        if (flipZ) v = 1.0f - v;
        x = minX + u * (maxX - minX);
        z = minZ + v * (maxZ - minZ);
    }
};

//  Visits every cell the segment (x0,z0)→(x1,z1) passes through, in order, one
//  cell per road cross-section. A step that clips a cell corner visits both
//  cells, so consecutive visits are always 4-adjacent: the ribbon never breaks
//  into diagonal-only fragments the way a naive "stamp the cell containing each
//  sample" loop does. Out-of-bounds cells are visited too — MaskGrid ignores
//  them — so the traversal never has to clip.
template <typename Visitor>
inline void walkRibbon(const WorldToCell& map, int w, int h,
                       float x0, float z0, float x1, float z1, Visitor&& visit) {
    float c0, r0, c1, r1;
    map.cellF(x0, z0, w, h, c0, r0);
    map.cellF(x1, z1, w, h, c1, r1);
    if (!std::isfinite(c0) || !std::isfinite(r0) ||
        !std::isfinite(c1) || !std::isfinite(r1)) return;   // degenerate input

    int col = static_cast<int>(std::floor(c0));
    int row = static_cast<int>(std::floor(r0));
    const int colEnd = static_cast<int>(std::floor(c1));
    const int rowEnd = static_cast<int>(std::floor(r1));
    visit(col, row);

    const int stepC = (c1 > c0) ? 1 : (c1 < c0 ? -1 : 0);
    const int stepR = (r1 > r0) ? 1 : (r1 < r0 ? -1 : 0);
    const float dC = std::fabs(c1 - c0);
    const float dR = std::fabs(r1 - r0);
    const float kFar = 1e30f;
    const bool walkC = stepC != 0 && dC > 1e-6f;
    const bool walkR = stepR != 0 && dR > 1e-6f;

    // Distance (as a fraction of the segment) to the first cell border.
    float tC = walkC ? ((stepC > 0 ? (col + 1 - c0) : (c0 - col)) / dC) : kFar;
    float tR = walkR ? ((stepR > 0 ? (row + 1 - r0) : (r0 - row)) / dR) : kFar;
    const float dtC = walkC ? 1.0f / dC : kFar;
    const float dtR = walkR ? 1.0f / dR : kFar;

    // A straight line crosses at most |dc| + |dr| cell borders; the bound only
    // exists so degenerate input cannot spin forever.
    int stepsLeft = static_cast<int>(dC + dR) + 2;
    while ((col != colEnd || row != rowEnd) && stepsLeft-- > 0) {
        if (tC < tR) {
            col += stepC; tC += dtC;
        } else {
            row += stepR; tR += dtR;   // ties step the row first, then the column
        }
        visit(col, row);
    }
};

// ──────────────────────── OCCUPANCY + MASK GRID ──────────────────────────────

//  Two ways in:
//    - authoring API : placeRoad() / removeRoad() update the cell and its 4
//                      direct neighbours, like the reference design. The shape
//                      histogram is maintained on the spot, so it is always
//                      consistent with the stored masks.
//    - derived API   : beginBulkStamp(), then stampOccupancy() for every cell
//                      covered by road geometry, then recomputeMasksInRect() per
//                      stamped span and rebuildHistogram() at the end. This
//                      never walks the whole grid, so a road edit costs time
//                      proportional to the road, not to the map.
//
//  The set of stamped road cells is tracked in a side list, so clearing,
//  counting and finding the road bounding box are all O(road cells) even
//  though the grid itself holds one byte per cell for occupancy and one for the
//  mask (23 MB at 3485 x 3485).
//
//  Road type is stored per cell as (type + 1) so that 0 means "no road" while
//  lower values keep winning on overlap — a paved avenue (type 0) outranks a
//  dirt track (type 3) in a shared cell, mirroring the host-selection rule used
//  by RoadMeshBuilder.
class MaskGrid {
public:
    static constexpr uint8_t kNoRoad = 0;

    void resize(int w, int h) {
        m_w = std::max(0, w);
        m_h = std::max(0, h);
        m_road.assign(static_cast<size_t>(m_w) * m_h, kNoRoad);
        m_masks.assign(static_cast<size_t>(m_w) * m_h, uint8_t(0));
        m_roadCells.clear();
        m_histogram.fill(0);
    }

    void clear() {
        std::fill(m_road.begin(), m_road.end(), kNoRoad);
        std::fill(m_masks.begin(), m_masks.end(), uint8_t(0));
        m_roadCells.clear();
        m_histogram.fill(0);
    }

    // Prepares the grid for a fresh derived rebuild. The backing arrays are
    // only reallocated when the resolution actually changes, so rebuilding at a
    // fixed cell size costs nothing but clearing the previous stamp set.
    void beginBulkStamp(int w, int h) {
        m_w = std::max(0, w);
        m_h = std::max(0, h);
        const size_t cells = static_cast<size_t>(m_w) * m_h;
        if (m_road.size() != cells) {
            m_road.assign(cells, kNoRoad);
            m_masks.assign(cells, uint8_t(0));
        }
        clearRoad();
    }

    // Drops every stamped road cell but keeps the allocation. O(road cells).
    void clearRoad() {
        for (uint32_t i : m_roadCells) {
            m_road[i] = kNoRoad;
            m_masks[i] = 0;
        }
        m_roadCells.clear();
        m_histogram.fill(0);
    }

    int  width()  const { return m_w; }
    int  height() const { return m_h; }
    bool empty()  const { return m_w <= 0 || m_h <= 0; }
    size_t cellCount() const { return m_road.size(); }

    bool inBounds(int col, int row) const {
        return col >= 0 && col < m_w && row >= 0 && row < m_h;
    }

    bool isRoad(int col, int row) const {
        return inBounds(col, row) && m_road[index(col, row)] != kNoRoad;
    }

    uint8_t maskAt(int col, int row) const {
        return inBounds(col, row) ? m_masks[index(col, row)] : 0;
    }

    std::optional<Shape> shapeAt(int col, int row) const {
        if (!isRoad(col, row)) return std::nullopt;
        return shapeFromMask(maskAt(col, row));
    }

    // Stored value is (RoadType + 1); kNoRoad means the cell is empty.
    uint8_t roadTypeAt(int col, int row) const {
        return inBounds(col, row) ? m_road[index(col, row)] : kNoRoad;
    }

    bool placeRoad(int col, int row)         { return setOccupancy(col, row, 1); }
    bool removeRoad(int col, int row)        { return setOccupancy(col, row, kNoRoad); }

    // Occupancy only: masks are refreshed by recomputeMasksInRect() and the
    // histogram by rebuildHistogram() once every segment has been stamped.
    void stampOccupancy(int col, int row, uint8_t typePlusOne) {
        if (!inBounds(col, row) || typePlusOne == kNoRoad) return;
        size_t i = index(col, row);
        uint8_t& cell = m_road[i];
        if (cell == kNoRoad) {
            m_roadCells.push_back(static_cast<uint32_t>(i));
            cell = typePlusOne;
        } else if (typePlusOne < cell) {
            cell = typePlusOne;
        }
    }

    // Recomputes the mask of every road cell in the rect. Idempotent and
    // order-independent: it only writes masks, so it is safe to call on
    // overlapping or already-updated rects. Callers pass a rect already
    // dilated by one cell, because a mask depends on its 4 neighbours.
    void recomputeMasksInRect(int col0, int row0, int col1, int row1) {
        col0 = std::max(0, col0);
        row0 = std::max(0, row0);
        col1 = std::min(m_w - 1, col1);
        row1 = std::min(m_h - 1, row1);
        for (int row = row0; row <= row1; ++row) {
            for (int col = col0; col <= col1; ++col) {
                size_t i = index(col, row);
                if (m_road[i] != kNoRoad) m_masks[i] = computeMask(col, row);
            }
        }
    }

    void recomputeAllMasks() {
        for (int row = 0; row < m_h; ++row) {
            for (int col = 0; col < m_w; ++col) {
                size_t i = index(col, row);
                m_masks[i] = m_road[i] != kNoRoad ? computeMask(col, row) : uint8_t(0);
            }
        }
        rebuildHistogram();
    }

    // Recounts the shape histogram from the road cells that actually exist, so
    // the bulk path stays self-healing whatever the stamp order was.
    void rebuildHistogram() {
        m_histogram.fill(0);
        for (uint32_t i : m_roadCells) ++m_histogram[m_masks[i]];
    }

    int  roadCellCount() const { return static_cast<int>(m_roadCells.size()); }
    int  countShape(Shape s) const { return countMask(static_cast<uint8_t>(s)); }
    int  countMask(uint8_t mask) const { return m_histogram[mask & All]; }

    // Clamps a [lo,hi] index range to at most `limit` entries, anchored on the
    // first index so the window always starts on a road cell. Returns true when
    // it had to crop. Public so the console window logic is unit-testable.
    static bool cropRange(int& lo, int& hi, int limit, int count) {
        if (hi - lo + 1 <= limit) return false;
        hi = std::min(count - 1, lo + limit - 1);
        return true;
    }

    // ASCII dump of the road cells, cropped to their bounding box. The window
    // is capped so a full-map field cannot flood the console.
    void debugPrint(int maxWindow = 48) const {
        int c0 = m_w, c1 = -1, r0 = m_h, r1 = -1;
        for (uint32_t i : m_roadCells) {
            int col = static_cast<int>(i % static_cast<uint32_t>(m_w));
            int row = static_cast<int>(i / static_cast<uint32_t>(m_w));
            c0 = std::min(c0, col); c1 = std::max(c1, col);
            r0 = std::min(r0, row); r1 = std::max(r1, row);
        }
        if (c1 < 0) {
            std::string out = "[RoadTileMask] no road cells (" + std::to_string(m_w) +
                              "x" + std::to_string(m_h) + ")\n";
            fputs(out.c_str(), stdout);
            return;
        }
        bool croppedCols = cropRange(c0, c1, maxWindow, m_w);
        bool croppedRows = cropRange(r0, r1, maxWindow, m_h);
        bool cropped = croppedCols || croppedRows;
        std::string out = "[RoadTileMask] " + std::to_string(m_w) + "x" +
                          std::to_string(m_h) + " cells, " +
                          std::to_string(roadCellCount()) + " road cells" +
                          (cropped ? " (cropped)\n" : "\n");
        // North is row + 1, so print from the northernmost row down to read
        // like a map.
        for (int row = r1; row >= r0; --row) {
            out += "  ";
            for (int col = c0; col <= c1; ++col) {
                out += isRoad(col, row) ? shapeGlyph(shapeFromMask(maskAt(col, row))) : ' ';
            }
            out += '\n';
        }
        fputs(out.c_str(), stdout);
    }

private:
    size_t index(int col, int row) const {
        return static_cast<size_t>(row) * m_w + col;
    }

    // Row 0 is south, so North is row + 1 and West is col - 1.
    uint8_t computeMask(int col, int row) const {
        uint8_t mask = 0;
        if (isRoad(col, row + 1)) mask |= North;
        if (isRoad(col + 1, row)) mask |= East;
        if (isRoad(col - 1, row)) mask |= West;
        if (isRoad(col, row - 1)) mask |= South;
        return mask;
    }

    bool setOccupancy(int col, int row, uint8_t typePlusOne) {
        if (!inBounds(col, row)) return false;
        size_t i = index(col, row);
        uint8_t previous = m_road[i];
        if (previous == typePlusOne) return false;
        m_road[i] = typePlusOne;
        if (typePlusOne == kNoRoad) {
            m_roadCells.erase(std::find(m_roadCells.begin(), m_roadCells.end(),
                                        static_cast<uint32_t>(i)));
        } else if (previous == kNoRoad) {
            m_roadCells.push_back(static_cast<uint32_t>(i));
        }

        // Empty cells carry no shape, so only road cells are counted. Drop the
        // previous contribution before recomputing, then add the new one.
        if (previous != kNoRoad) --m_histogram[m_masks[i]];
        m_masks[i] = typePlusOne != kNoRoad ? computeMask(col, row) : uint8_t(0);
        if (typePlusOne != kNoRoad) ++m_histogram[m_masks[i]];

        // A neighbour's mask only changes if this cell is one of its four.
        refreshNeighbour(col, row + 1);
        refreshNeighbour(col + 1, row);
        refreshNeighbour(col - 1, row);
        refreshNeighbour(col, row - 1);
        return true;
    }

    // Recompute the mask of a cell whose occupancy is unchanged.
    void refreshNeighbour(int col, int row) {
        if (!inBounds(col, row)) return;
        size_t i = index(col, row);
        if (m_road[i] == kNoRoad) return;
        --m_histogram[m_masks[i]];
        m_masks[i] = computeMask(col, row);
        ++m_histogram[m_masks[i]];
    }

    int m_w = 0, m_h = 0;
    std::vector<uint8_t> m_road;   // kNoRoad, else (RoadType + 1)
    std::vector<uint8_t> m_masks;  // 4-direction bitmask per cell
    std::vector<uint32_t> m_roadCells;  // indices of the road cells, for O(roads) work
    std::array<int, 16> m_histogram{};
};

}  // namespace RoadTile
