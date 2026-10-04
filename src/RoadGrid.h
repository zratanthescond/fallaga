#pragma once
//
// RoadGrid -- the tile-based road network, Caesar IV style.
//
// Phase 0 decisions, locked:
//   * 4 m tiles, one world-space size shared by roads, buildings and plazas.
//   * A road is 1 tile wide (a street); avenues and plazas are just wider painted
//     areas of the same grid.
//   * Cell values are road TYPES: 0 = none, 1 = dirt, 2 = paved. Value 3 is left
//     free for a future type (stairs/bridge).
//   * This grid is the SINGLE SOURCE OF TRUTH. Meshes, curbs, auto-tile pieces,
//     terrain flattening and later the navigation graph are all derived from it
//     and never stored back into it.
//
// Connectivity is 4-connected on purpose: Caesar-style roads run on orthogonal
// axes and diagonals are drawn as staircase cells, so auto-tiling only has to
// consider N/E/S/W.
//
// Deliberately free of OpenGL, the rest of the engine and any global state: it is
// plain data plus arithmetic, which is what makes it testable headlessly.

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <string>
#include <vector>

namespace roadgrid {

// Cell values. 0 must stay "none" because arrays are zero-initialised.
enum RoadType : std::uint8_t {
    kNone   = 0,
    kDirt   = 1,
    kPaved  = 2,
    kStairs = 3,
    kBridge = 4,
};

// Tile size in metres (Phase 0). Every derived system must read this rather than
// hard-coding 4, so the one decision lives in one place.
constexpr float kTileSize = 4.0f;

// Grid dimensions and world placement. The origin is the world position of the
// CENTRE of tile (0,0), so tile indices can go negative in principle; the covered
// region is a fixed rectangle chosen to hold the whole existing road network
// (world x 2555..5261, z -2173..1653) with room to expand.
//
// 2048 * 2048 * 1 byte = 4 MB, which is small enough to keep resident and to
// snapshot for undo.
constexpr int kWidth  = 2048;
constexpr int kHeight = 2048;
constexpr float kOriginX = -1000.0f;
constexpr float kOriginZ = -4000.0f;

// Chunks are the rebuild unit for Phase 3. A cell edit marks its chunk dirty, and
// also the adjacent chunk when the cell is on a border, because auto-tiling and
// curbs read neighbours across the chunk seam.
constexpr int kChunkSize = 16;
constexpr int kChunksX = kWidth / kChunkSize;
constexpr int kChunksY = kHeight / kChunkSize;

// One changed cell, for undo/redo and for partial updates.
struct CellChange {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint8_t before = kNone;
    std::uint8_t after = kNone;
};

class RoadGrid {
public:
    RoadGrid();

    // ---- geometry helpers (world <-> tile) --------------------------------
    // Floor, not round: tile (i,j) spans [origin + i*T, origin + (i+1)*T), so the
    // whole plane is covered with no gap between tiles and no ambiguity at edges.
    static int worldToTileX(float wx) {
        return (int)std::floor((wx - kOriginX) / kTileSize);
    }
    static int worldToTileZ(float wz) {
        return (int)std::floor((wz - kOriginZ) / kTileSize);
    }
    // Centre of a tile, which is where the placement cursor should snap.
    static float tileCenterX(int tx) { return kOriginX + ((float)tx + 0.5f) * kTileSize; }
    static float tileCenterZ(int ty) { return kOriginZ + ((float)ty + 0.5f) * kTileSize; }

    static bool inBounds(int tx, int ty) {
        return tx >= 0 && ty >= 0 && tx < kWidth && ty < kHeight;
    }

    // ---- cells -------------------------------------------------------------
    std::uint8_t get(int tx, int ty) const {
        return inBounds(tx, ty) ? cells_[(std::size_t)ty * kWidth + tx] : kNone;
    }
    bool isRoad(int tx, int ty) const { return get(tx, ty) != kNone; }
    bool sameType(int tx, int ty, std::uint8_t type) const {
        return get(tx, ty) == type;
    }

    // Writes a cell and records the change. Returns true if the value changed.
    bool set(int tx, int ty, std::uint8_t type);

    bool empty() const { return roadCount_ == 0; }
    int roadCount() const { return roadCount_; }

    void clear();

    // ---- strokes (undo / redo) ---------------------------------------------
    // A stroke is everything between beginStroke and endStroke. Cells that end up
    // unchanged are dropped, so a stroke over existing road costs nothing to undo.
    void beginStroke();
    void endStroke();
    bool canUndo() const { return undoDepth_ > 0; }
    bool canRedo() const { return redoDepth_ > 0; }
    // Diagnostics for tests.
    int undoDepthDiag() const { return undoDepth_; }
    int redoDepthDiag() const { return redoDepth_; }
    int strokeSizeDiag() const { return (int)stroke_.size(); }
    // Swap one stroke between the grid and the redo stack, in place.
    bool undo();
    bool redo();

    // ---- rasterisation -----------------------------------------------------
    // Paint a straight line of `radius` cells around a Bresenham run. Radius 0 is a
    // single-cell-wide line (a 1-tile street). 4-connected by construction.
    int paintLine(int x0, int y0, int x1, int y1, std::uint8_t type, int radius = 0);
    // Paint a filled rectangle, inclusive of both corners (the plaza tool).
    int paintRect(int x0, int y0, int x1, int y1, std::uint8_t type);
    // Paint a disc, used by the round brush.
    int paintDisc(int cx, int cy, int radius, std::uint8_t type);

    // ---- dirty chunks ------------------------------------------------------
    // Chunks touched since the last clearDirty. Phase 3 rebuilds only these.
    const std::vector<std::uint16_t>& dirtyChunks() const { return dirty_; }
    void clearDirty() { dirty_.clear(); dirtyMark_.assign(dirtyMark_.size(), 0); }
    void markAllDirty();
    static std::uint16_t chunkIndex(int tx, int ty) {
        return (std::uint16_t)((ty / kChunkSize) * kChunksX + (tx / kChunkSize));
    }

    // ---- persistence -------------------------------------------------------
    // Run-length encoded, which for road networks (long straight runs of one type)
    // is a large win over a raw 4 MB dump.
    bool save(const std::string& path) const;
    bool load(const std::string& path);

    // ---- phase 2 helper ----------------------------------------------------
    // 4-bit neighbour mask: N=1, E=2, S=4, W=8. `self` is the mask of cells sharing
    // the centre cell's type, so a dirt road next to a paved one is not treated as
    // connected for piece selection.
    int neighbourMask(int tx, int ty) const;

    // ---- phase 2: auto-tiling ----------------------------------------------
    // The cardinal mask (0..15) maps to exactly one piece:
    //   0        Single     1 piece
    //   1,2,4,8  DeadEnd    4 pieces
    //   5,10     Straight   2 pieces
    //   3,6,9,12 Corner     4 pieces
    //   7,11,13,14 TJunction 4 pieces
    //   15       Crossroad  1 piece
    // 1 + 4 + 2 + 4 + 4 + 1 = 16, the whole table.

    // 4-bit diagonal mask: NE=1, SE=2, SW=4, NW=8 for cells sharing the centre cell's type.
    int diagonalMask(int tx, int ty) const;

    enum class PieceType : std::uint8_t {
        Single    = 0, // Isolated cell (no road neighbours)
        DeadEnd   = 1, // 1 arm (terminator)
        Straight  = 2, // 2 opposite arms (NS or EW)
        Corner    = 3, // 2 adjacent arms (turns: NE, SE, SW, NW)
        TJunction = 4, // 3 arms (T-junctions)
        Crossroad = 5  // 4 arms (crossroad)
    };

    struct PieceInfo {
        PieceType type = PieceType::Single;
        int rotationDeg = 0;         // 0, 90, 180, 270 degrees
        std::uint8_t cardinalMask = 0; // N=1, E=2, S=4, W=8
        std::uint8_t diagonalMask = 0; // NE=1, SE=2, SW=4, NW=8
    };

    // Maps a 4-bit cardinal mask (0..15) and diagonal mask to its PieceType and rotation.
    static PieceInfo pieceFromMask(std::uint8_t cardinalMask, std::uint8_t diagMask = 0);
    PieceInfo pieceInfo(int tx, int ty) const;

private:
    void markDirty(int tx, int ty);

    std::vector<std::uint8_t> cells_;
    int roadCount_ = 0;

    std::vector<CellChange> stroke_;
    bool inStroke_ = false;
    std::vector<std::vector<CellChange>> undo_;
    std::vector<std::vector<CellChange>> redo_;
    int undoDepth_ = 0, redoDepth_ = 0;
    static constexpr int kMaxUndo = 64;

    mutable std::vector<std::uint16_t> dirty_;
    mutable std::vector<std::uint8_t> dirtyMark_;
};

} // namespace roadgrid
