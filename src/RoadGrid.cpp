#include "RoadGrid.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace roadgrid {

RoadGrid::RoadGrid() {
    cells_.assign((std::size_t)kWidth * kHeight, kNone);
    dirtyMark_.assign((std::size_t)kChunksX * kChunksY, 0);
}

void RoadGrid::markDirty(int tx, int ty) {
    if (!inBounds(tx, ty)) return;
    // Mark this chunk, and the adjacent chunk when the cell sits on a border: both
    // auto-tiling (phase 2) and curbs (phase 3) read neighbours across the seam, so
    // a cell on a chunk edge changes the appearance of the chunk next door.
    const int cx = tx / kChunkSize, cy = ty / kChunkSize;
    const int lx = tx % kChunkSize, ly = ty % kChunkSize;
    const int loX = (lx == 0) ? cx - 1 : cx;
    const int hiX = (lx == kChunkSize - 1) ? cx + 1 : cx;
    const int loY = (ly == 0) ? cy - 1 : cy;
    const int hiY = (ly == kChunkSize - 1) ? cy + 1 : cy;
    for (int y = std::max(0, loY); y <= std::min(kChunksY - 1, hiY); ++y)
        for (int x = std::max(0, loX); x <= std::min(kChunksX - 1, hiX); ++x) {
            const std::uint16_t idx = (std::uint16_t)(y * kChunksX + x);
            if (idx < dirtyMark_.size() && !dirtyMark_[idx]) {
                dirtyMark_[idx] = 1;
                dirty_.push_back(idx);
            }
        }
}

bool RoadGrid::set(int tx, int ty, std::uint8_t type) {
    if (!inBounds(tx, ty)) return false;
    std::uint8_t& cell = cells_[(std::size_t)ty * kWidth + tx];
    if (cell == type) return false;
    const std::uint8_t before = cell;
    cell = type;
    if (before == kNone && type != kNone) ++roadCount_;
    else if (before != kNone && type == kNone) --roadCount_;
    if (inStroke_) stroke_.push_back({tx, ty, before, type});
    markDirty(tx, ty);
    // A neighbour's auto-tile piece depends on this cell, so its chunk is dirty too.
    markDirty(tx - 1, ty);
    markDirty(tx + 1, ty);
    markDirty(tx, ty - 1);
    markDirty(tx, ty + 1);
    return true;
}

void RoadGrid::clear() {
    std::fill(cells_.begin(), cells_.end(), (std::uint8_t)kNone);
    roadCount_ = 0;
    stroke_.clear();
    undo_.clear();
    redo_.clear();
    undoDepth_ = redoDepth_ = 0;
    markAllDirty();
}

void RoadGrid::beginStroke() {
    stroke_.clear();
    inStroke_ = true;
}

void RoadGrid::endStroke() {
    inStroke_ = false;
    // Drop no-op cells so a stroke that repainted existing road costs nothing.
    stroke_.erase(std::remove_if(stroke_.begin(), stroke_.end(),
                                 [](const CellChange& c) { return c.before == c.after; }),
                  stroke_.end());
    // A stroke that changed nothing must not become an undo entry. Pushing the empty
    // list made canUndo() true and undo() "succeed" while changing nothing, so the
    // next undo appeared to do nothing and the redo branch was discarded for no
    // reason. A no-op stroke is simply not an edit.
    if (stroke_.empty()) {
        stroke_.clear();
        return;
    }
    undo_.push_back(std::move(stroke_));
    stroke_.clear();
    if ((int)undo_.size() > kMaxUndo) undo_.erase(undo_.begin());
    undoDepth_ = (int)undo_.size();
    // A real edit invalidates any redo branch, as in every editor.
    redo_.clear();
    redoDepth_ = 0;
}

bool RoadGrid::undo() {
    if (undo_.empty()) return false;
    // Copy, then pop. Moving back() into a local and calling pop_back() afterwards
    // would pop the moved-from element and lose the stroke.
    std::vector<CellChange> stroke = undo_.back();
    undo_.pop_back();
    // Apply in reverse so repeated cells within one stroke settle correctly.
    for (auto it = stroke.rbegin(); it != stroke.rend(); ++it) {
        if (!inBounds(it->x, it->y)) continue;
        std::uint8_t& cell = cells_[(std::size_t)it->y * kWidth + it->x];
        if (cell == it->before) continue;
        const std::uint8_t was = cell;
        cell = it->before;
        if (was == kNone && it->before != kNone) ++roadCount_;
        else if (was != kNone && it->before == kNone) --roadCount_;
        markDirty(it->x, it->y);
        markDirty(it->x - 1, it->y); markDirty(it->x + 1, it->y);
        markDirty(it->x, it->y - 1); markDirty(it->x, it->y + 1);
    }
    redo_.push_back(std::move(stroke));
    undoDepth_ = (int)undo_.size();
    redoDepth_ = (int)redo_.size();
    return true;
}

bool RoadGrid::redo() {
    if (redo_.empty()) return false;
    std::vector<CellChange> stroke = redo_.back();
    redo_.pop_back();
    for (const CellChange& c : stroke) {
        if (!inBounds(c.x, c.y)) continue;
        std::uint8_t& cell = cells_[(std::size_t)c.y * kWidth + c.x];
        if (cell == c.after) continue;
        const std::uint8_t was = cell;
        cell = c.after;
        if (was == kNone && c.after != kNone) ++roadCount_;
        else if (was != kNone && c.after == kNone) --roadCount_;
        markDirty(c.x, c.y);
        markDirty(c.x - 1, c.y); markDirty(c.x + 1, c.y);
        markDirty(c.x, c.y - 1); markDirty(c.x, c.y + 1);
    }
    undo_.push_back(std::move(stroke));
    undoDepth_ = (int)undo_.size();
    redoDepth_ = (int)redo_.size();
    return true;
}

int RoadGrid::paintLine(int x0, int y0, int x1, int y1, std::uint8_t type, int radius) {
    // Bresenham, but stepping exactly ONE axis per iteration. The textbook version
    // steps both when both error tests pass, which produces diagonal jumps; auto-tiling
    // is 4-connected, so a diagonal step would leave two cells that each see the other
    // as a dead end and the road would show a notch at every step. Preferring the
    // horizontal step makes a 45-degree line come out as a staircase of single cells,
    // which is exactly how Caesar-style roads are drawn.
    int painted = 0;
    int x = x0, y = y0;
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (radius <= 0) {
            if (set(x, y, type)) ++painted;
        } else {
            for (int oy = -radius; oy <= radius; ++oy)
                for (int ox = -radius; ox <= radius; ++ox) {
                    if (ox * ox + oy * oy > radius * radius + radius) continue;
                    if (set(x + ox, y + oy, type)) ++painted;
                }
        }
        if (x == x1 && y == y1) break;
        const int e2 = 2 * err;
        // Only one of these can fire per iteration.
        if (e2 >= dy && x != x1) {
            err += dy;
            x += sx;
        } else if (e2 <= dx && y != y1) {
            err += dx;
            y += sy;
        } else if (x != x1) {
            err += dy;
            x += sx;
        } else {
            err += dx;
            y += sy;
        }
    }
    return painted;
}

int RoadGrid::paintRect(int x0, int y0, int x1, int y1, std::uint8_t type) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    int painted = 0;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (set(x, y, type)) ++painted;
    return painted;
}

int RoadGrid::paintDisc(int cx, int cy, int radius, std::uint8_t type) {
    int painted = 0;
    for (int oy = -radius; oy <= radius; ++oy)
        for (int ox = -radius; ox <= radius; ++ox) {
            if (ox * ox + oy * oy > radius * radius) continue;
            if (set(cx + ox, cy + oy, type)) ++painted;
        }
    return painted;
}

void RoadGrid::markAllDirty() {
    dirty_.clear();
    dirtyMark_.assign((std::size_t)kChunksX * kChunksY, 0);
    for (int i = 0; i < kChunksX * kChunksY; ++i) {
        dirtyMark_[i] = 1;
        dirty_.push_back((std::uint16_t)i);
    }
}

int RoadGrid::diagonalMask(int tx, int ty) const {
    // Diagonals are only used to pick outer-corner decorations, never connectivity:
    // roads are 4-connected, so a diagonal step is not a road junction. Matching the
    // centre cell's type keeps a dirt road from borrowing a paved neighbour's corner.
    const std::uint8_t t = get(tx, ty);
    if (t == kNone) return 0;
    int mask = 0;
    if (get(tx + 1, ty + 1) == t) mask |= 1; // NE
    if (get(tx + 1, ty - 1) == t) mask |= 2; // SE
    if (get(tx - 1, ty - 1) == t) mask |= 4; // SW
    if (get(tx - 1, ty + 1) == t) mask |= 8; // NW
    return mask;
}

int RoadGrid::neighbourMask(int tx, int ty) const {
    // Match on the CENTRE CELL's type: a dirt road touching a paved one is not
    // "connected" for piece selection, so the two get their own curb line at the
    // boundary instead of merging into one surface.
    const std::uint8_t t = get(tx, ty);
    if (t == kNone) return 0;
    int mask = 0;
    if (get(tx, ty + 1) == t) mask |= 1; // N
    if (get(tx + 1, ty) == t) mask |= 2; // E
    if (get(tx, ty - 1) == t) mask |= 4; // S
    if (get(tx - 1, ty) == t) mask |= 8; // W
    return mask;
}

RoadGrid::PieceInfo RoadGrid::pieceFromMask(std::uint8_t cardinalMask, std::uint8_t diagMask) {
    // Explicit table rather than derived arithmetic: the 16 entries are the design, and
    // writing them out is what makes the piece/rotation assignment reviewable and
    // directly testable against the plan's shape list.
    static const struct { PieceType type; int rot; } kTable[16] = {
        { PieceType::Single,    0 },   // 0: Isolated
        { PieceType::DeadEnd,   0 },   // 1: N
        { PieceType::DeadEnd,  90 },   // 2: E
        { PieceType::Corner,    0 },   // 3: NE
        { PieceType::DeadEnd, 180 },   // 4: S
        { PieceType::Straight,  0 },   // 5: NS
        { PieceType::Corner,   90 },   // 6: SE
        { PieceType::TJunction, 0 },   // 7: T no W
        { PieceType::DeadEnd, 270 },   // 8: W
        { PieceType::Corner,  270 },   // 9: NW
        { PieceType::Straight, 90 },   // 10: EW
        { PieceType::TJunction,90 },   // 11: T no S
        { PieceType::Corner,  180 },   // 12: SW
        { PieceType::TJunction,180 },  // 13: T no E
        { PieceType::TJunction,270 },  // 14: T no N
        { PieceType::Crossroad, 0 },   // 15: Cross
    };
    PieceInfo info;
    const std::uint8_t m = cardinalMask & 0x0F;
    info.type = kTable[m].type;
    info.rotationDeg = kTable[m].rot;
    info.cardinalMask = m;
    info.diagonalMask = diagMask & 0x0F;
    return info;
}

RoadGrid::PieceInfo RoadGrid::pieceInfo(int tx, int ty) const {
    if (!isRoad(tx, ty)) return {};
    return pieceFromMask((std::uint8_t)neighbourMask(tx, ty), (std::uint8_t)diagonalMask(tx, ty));
}

// ---- persistence -----------------------------------------------------------

namespace {
constexpr char kMagic[8] = {'F', 'R', 'D', 'G', 'R', 'D', '0', '1'};

void putU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back((std::uint8_t)(v & 0xFF));
    out.push_back((std::uint8_t)((v >> 8) & 0xFF));
    out.push_back((std::uint8_t)((v >> 16) & 0xFF));
    out.push_back((std::uint8_t)((v >> 24) & 0xFF));
}
std::uint32_t getU32(const std::vector<std::uint8_t>& in, std::size_t& at) {
    if (at + 4 > in.size()) { at = in.size(); return 0; }
    std::uint32_t v = (std::uint32_t)in[at] | ((std::uint32_t)in[at + 1] << 8) |
                      ((std::uint32_t)in[at + 2] << 16) | ((std::uint32_t)in[at + 3] << 24);
    at += 4;
    return v;
}
} // namespace

bool RoadGrid::save(const std::string& path) const {
    // Run-length encoded: (runLength, value) pairs over the row-major cell array.
    // Road networks are long straight runs of one type, so this is far smaller than
    // the 4 MB raw dump and still trivially correct. Runs longer than 2^32 are
    // impossible here (the grid is 4 M cells), so the split loop is belt and braces.
    std::vector<std::uint8_t> out;
    out.reserve(1 << 16);
    out.insert(out.end(), kMagic, kMagic + sizeof(kMagic));
    putU32(out, (std::uint32_t)kWidth);
    putU32(out, (std::uint32_t)kHeight);

    const std::size_t n = cells_.size();
    std::size_t i = 0;
    while (i < n) {
        const std::uint8_t v = cells_[i];
        std::size_t run = 1;
        while (i + run < n && cells_[i + run] == v) ++run;
        std::size_t left = run;
        while (left > 0) {
            const std::uint32_t chunk =
                (std::uint32_t)std::min<std::size_t>(left, 0xFFFFFFFFu);
            putU32(out, chunk);
            out.push_back(v);
            left -= chunk;
        }
        i += run;
    }

    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const std::size_t written = std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);
    return written == out.size();
}

bool RoadGrid::load(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long len = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (len < (long)(sizeof(kMagic) + 8)) { std::fclose(f); return false; }
    std::vector<std::uint8_t> in((std::size_t)len);
    const std::size_t got = std::fread(in.data(), 1, in.size(), f);
    std::fclose(f);
    if (got != in.size()) return false;
    if (std::memcmp(in.data(), kMagic, sizeof(kMagic)) != 0) return false;

    std::size_t at = sizeof(kMagic);
    const std::uint32_t w = getU32(in, at), h = getU32(in, at);
    if ((int)w != kWidth || (int)h != kHeight) return false;

    // Parse into a temporary first, so a truncated file cannot leave a half-loaded
    // grid behind (the same commit-after-parse rule the road JSON loader uses).
    std::vector<std::uint8_t> fresh((std::size_t)kWidth * kHeight, kNone);
    std::size_t written = 0;
    while (written < fresh.size()) {
        if (at + 5 > in.size()) return false;
        const std::uint32_t run = getU32(in, at);
        const std::uint8_t v = in[at++];
        if (run == 0 || written + run > fresh.size()) return false;
        std::fill(fresh.begin() + (std::ptrdiff_t)written,
                  fresh.begin() + (std::ptrdiff_t)(written + run), v);
        written += run;
    }

    cells_.swap(fresh);
    roadCount_ = 0;
    for (std::uint8_t v : cells_)
        if (v != kNone) ++roadCount_;
    undo_.clear(); redo_.clear(); undoDepth_ = redoDepth_ = 0;
    stroke_.clear();
    markAllDirty();
    return true;
}

} // namespace roadgrid