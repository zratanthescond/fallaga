#pragma once
//
// Added mesh geometry for terrain sculpting.
//
// The terrain mesh runs at ~22 m per vertex, so a smooth brush on it can only move
// whole 22 m quads. There are no vertices between them, so no amount of weight blending
// produces a smoother surface -- the grid cannot express one. This class owns the extra
// resolution a brush adds when it needs it, and the smoothing that the extra resolution
// makes possible.
//
// Deliberately free of OpenGL and of TerrainManager: it is pure height-field bookkeeping
// over a grid the caller supplies, which is what makes it testable without a context.
//
// Two rules make it safe:
//   * Subdivision is grid-aligned. A refined vertex sits at an exact fraction of a
//     visual cell, so its global position is unambiguous and two neighbouring chunks
//     compute the same height for it. There is nothing to stitch and no crack.
//   * A new fine vertex is seeded on the bilinear surface of the base grid, which is
//     exactly the surface the uniform mesh already draws. So adding geometry cannot
//     change the silhouette on its own -- only sculpting the fine vertices does.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

class TerrainRefinement {
public:
    // Fine samples per visual cell on each axis. The finest a cell can be divided by
    // is this, so a level-2 cell is 4x4 fine cells (~5.6 m at a 22 m base).
    static constexpr int kScale = 4;
    static constexpr int kMaxLevel = 2;

    // One-time sizing. `cellW`/`cellH` are the number of visual-grid CELLS (grid
    // samples minus one).
    void init(int cellW, int cellH) {
        cellW_ = std::max(1, cellW);
        cellH_ = std::max(1, cellH);
        levels_.assign((std::size_t)cellW_ * (std::size_t)cellH_, 0);
        dim_ = cellW_ * kScale + 1;
        const std::size_t total = (std::size_t)dim_ * (std::size_t)dim_;
        heights_.assign(total, kUnset);
        present_.assign(total, 0);
    }

    bool empty() const { return levels_.empty(); }
    int dim() const { return dim_; }
    int cellW() const { return cellW_; }
    int cellH() const { return cellH_; }

    int levelAt(int cellX, int cellZ) const {
        if (cellX < 0 || cellZ < 0 || cellX >= cellW_ || cellZ >= cellH_) return 0;
        return (int)levels_[(std::size_t)cellZ * (std::size_t)cellW_ + (std::size_t)cellX];
    }

    bool any() const {
        for (unsigned char l : levels_)
            if (l > 0) return true;
        return false;
    }

    // Refined height, or `fallback` where the vertex is unset or outside the grid.
    float heightAt(int fgx, int fgz, float fallback) const {
        if (dim_ <= 0) return fallback;
        if (fgx < 0 || fgz < 0 || fgx >= dim_ || fgz >= dim_) return fallback;
        const std::size_t idx = (std::size_t)fgz * (std::size_t)dim_ + (std::size_t)fgx;
        if (idx >= present_.size() || !present_[idx]) return fallback;
        const float v = heights_[idx];
        return (v != v) ? fallback : v;
    }

    bool isSet(int fgx, int fgz) const {
        if (dim_ <= 0 || fgx < 0 || fgz < 0 || fgx >= dim_ || fgz >= dim_) return false;
        const std::size_t idx = (std::size_t)fgz * (std::size_t)dim_ + (std::size_t)fgx;
        return idx < present_.size() && present_[idx] != 0;
    }

    void set(int fgx, int fgz, float value) {
        if (dim_ <= 0 || fgx < 0 || fgz < 0 || fgx >= dim_ || fgz >= dim_) return;
        const std::size_t idx = (std::size_t)fgz * (std::size_t)dim_ + (std::size_t)fgx;
        if (idx >= heights_.size()) return;
        heights_[idx] = value;
        present_[idx] = 1;
    }

    // Reads the base height grid at an arbitrary grid position, bilinearly. `baseW`
    // and `baseH` are grid samples per axis; integer positions return the stored
    // sample exactly, so a cell corner always agrees with the coarse mesh.
    static float bilinearBase(const std::vector<float>& grid, int baseW, int baseHCount,
                              float gx, float gz) {
        if (baseW < 1 || baseHCount < 1 || grid.empty()) return 0.0f;
        const int x0 = std::clamp((int)std::floor(gx), 0, baseW - 1);
        const int z0 = std::clamp((int)std::floor(gz), 0, baseHCount - 1);
        const int x1 = std::min(x0 + 1, baseW - 1);
        const int z1 = std::min(z0 + 1, baseHCount - 1);
        const float tx = gx - (float)x0, tz = gz - (float)z0;
        const float a = grid[(std::size_t)z0 * baseW + x0] * (1.0f - tx) +
                        grid[(std::size_t)z0 * baseW + x1] * tx;
        const float b = grid[(std::size_t)z1 * baseW + x0] * (1.0f - tx) +
                        grid[(std::size_t)z1 * baseW + x1] * tx;
        return a * (1.0f - tz) + b * tz;
    }

    // Raises the subdivision level of every cell whose centre falls inside the
    // footprint, and seeds the new fine vertices onto `base` (the existing surface).
    // Cells already at or above `level` are skipped, so geometry is never removed and
    // already-sculpted vertices are never reset.
    //
    // Cell-box coordinates are visual-grid cells; returns cells that gained geometry
    // through minCellX/maxCellX/minCellZ/maxCellZ.
    int add(float wx, float wz, float radius,
            float worldMinX, float worldMaxX, float worldMinZ, float worldMaxZ,
            const std::vector<float>& base, int baseW, int baseH, int level,
            int* minCellX, int* maxCellX, int* minCellZ, int* maxCellZ) {
        if (empty() || radius <= 0.0f || baseW < 2 || baseH < 2 || level <= 0) return 0;
        level = std::clamp(level, 1, kMaxLevel);

        const float ux = std::clamp((wx - worldMinX) / (worldMaxX - worldMinX), 0.0f, 1.0f);
        const float uz = std::clamp((wz - worldMinZ) / (worldMaxZ - worldMinZ), 0.0f, 1.0f);
        const float cx = ux * (float)cellW_, cz = uz * (float)cellH_;
        const float rx = (radius / (worldMaxX - worldMinX)) * (float)cellW_;
        const float rz = (radius / (worldMaxZ - worldMinZ)) * (float)cellH_;
        const float r = std::max(rx, rz);

        const int loX = std::max(0, (int)std::floor(cx - r));
        const int hiX = std::min(cellW_ - 1, (int)std::ceil(cx + r));
        const int loZ = std::max(0, (int)std::floor(cz - r));
        const int hiZ = std::min(cellH_ - 1, (int)std::ceil(cz + r));

        int gained = 0;
        int gMinX = cellW_, gMaxX = -1, gMinZ = cellH_, gMaxZ = -1;
        for (int czi = loZ; czi <= hiZ; ++czi) {
            for (int cxi = loX; cxi <= hiX; ++cxi) {
                const float dx = ((float)cxi + 0.5f) - cx;
                const float dz = ((float)czi + 0.5f) - cz;
                if (std::sqrt(dx * dx + dz * dz) > r) continue;
                unsigned char& lvl = levels_[(std::size_t)czi * (std::size_t)cellW_ + (std::size_t)cxi];
                if ((int)lvl >= level) continue;
                // Only seed when this cell had no geometry at all; raising a level
                // later must not disturb vertices already sculpted at the old level.
                const bool seed = (lvl == 0);
                lvl = (unsigned char)level;
                ++gained;
                gMinX = std::min(gMinX, cxi); gMaxX = std::max(gMaxX, cxi);
                gMinZ = std::min(gMinZ, czi); gMaxZ = std::max(gMaxZ, czi);
                if (!seed) continue;

                for (int fgz = czi * kScale; fgz <= czi * kScale + kScale; ++fgz) {
                    for (int fgx = cxi * kScale; fgx <= cxi * kScale + kScale; ++fgx) {
                        if (fgx < 0 || fgz < 0 || fgx >= dim_ || fgz >= dim_) continue;
                        const std::size_t idx =
                            (std::size_t)fgz * (std::size_t)dim_ + (std::size_t)fgx;
                        if (present_[idx] && heights_[idx] == heights_[idx]) continue;
                        heights_[idx] = bilinearBase(base, baseW, baseH,
                                                     (float)fgx / (float)kScale,
                                                     (float)fgz / (float)kScale);
                        present_[idx] = 1;
                    }
                }
            }
        }

        if (gained > 0) {
            if (minCellX) *minCellX = gMinX;
            if (maxCellX) *maxCellX = gMaxX;
            if (minCellZ) *minCellZ = gMinZ;
            if (maxCellZ) *maxCellZ = gMaxZ;
        }
        return gained;
    }

    // Laplacian smooth on the fine grid, applied only where the cell has added
    // geometry so the footprint blends into its unrefined edges. Writes are gathered
    // before being applied, so the result does not depend on iteration order.
    void smooth(float wx, float wz, float radius,
                float worldMinX, float worldMaxX, float worldMinZ, float worldMaxZ,
                float strength, float dt) {
        if (empty() || radius <= 0.0f) return;
        const float ux = std::clamp((wx - worldMinX) / (worldMaxX - worldMinX), 0.0f, 1.0f);
        const float uz = std::clamp((wz - worldMinZ) / (worldMaxZ - worldMinZ), 0.0f, 1.0f);
        const float cx = ux * (float)cellW_ * (float)kScale;
        const float cz = uz * (float)cellH_ * (float)kScale;
        const float rx = (radius / (worldMaxX - worldMinX)) * (float)cellW_ * (float)kScale;
        const float rz = (radius / (worldMaxZ - worldMinZ)) * (float)cellH_ * (float)kScale;
        const float r = std::max(rx, rz);
        if (r <= 0.0f) return;

        const int loX = std::max(0, (int)std::floor(cx - r));
        const int hiX = std::min(dim_ - 1, (int)std::ceil(cx + r));
        const int loZ = std::max(0, (int)std::floor(cz - r));
        const int hiZ = std::min(dim_ - 1, (int)std::ceil(cz + r));

        const float k = std::clamp(strength * dt * 0.35f, 0.0f, 1.0f);
        scratch_.clear();

        for (int fgz = loZ; fgz <= hiZ; ++fgz) {
            for (int fgx = loX; fgx <= hiX; ++fgx) {
                const float dx = (float)fgx - cx, dz = (float)fgz - cz;
                const float d = std::sqrt(dx * dx + dz * dz);
                if (d > r) continue;
                const float falloff = 1.0f - (d / r);
                const float w = falloff * falloff * k;
                if (w <= 0.0f) continue;

                const int cellX = std::min(fgx / kScale, cellW_ - 1);
                const int cellZ = std::min(fgz / kScale, cellH_ - 1);
                if (levelAt(cellX, cellZ) <= 0) continue;

                const std::size_t idx =
                    (std::size_t)fgz * (std::size_t)dim_ + (std::size_t)fgx;
                if (idx >= present_.size() || !present_[idx]) continue;
                const float cur = heights_[idx];
                if (cur != cur) continue;

                float sum = 0.0f;
                int count = 0;
                const int nx[4] = {fgx - 1, fgx + 1, fgx, fgx};
                const int nz[4] = {fgz, fgz, fgz - 1, fgz + 1};
                for (int t = 0; t < 4; ++t) {
                    if (nx[t] < 0 || nz[t] < 0 || nx[t] >= dim_ || nz[t] >= dim_) continue;
                    const std::size_t nidx =
                        (std::size_t)nz[t] * (std::size_t)dim_ + (std::size_t)nx[t];
                    if (nidx >= heights_.size()) continue;
                    const float h = heights_[nidx];
                    if (h != h) continue;
                    sum += h;
                    ++count;
                }
                if (count < 2) continue;
                const float mean = sum / (float)count;
                scratch_.push_back({idx, cur + (mean - cur) * w});
            }
        }
        for (const auto& wr : scratch_) heights_[wr.first] = wr.second;
    }

    // Copies fine samples that land exactly on grid positions back into the base
    // grid, so height queries, physics and the road conformance see the ground the
    // mesh draws. In-between samples have no base slot; the fine grid is
    // authoritative for those.
    void mirrorIntoBase(std::vector<float>& base, int baseW, int baseH,
                        int fMinX, int fMaxX, int fMinZ, int fMaxZ) const {
        if (empty() || baseW < 1 || baseH < 1) return;
        const int loX = std::max(0, fMinX), hiX = std::min(dim_ - 1, fMaxX);
        const int loZ = std::max(0, fMinZ), hiZ = std::min(dim_ - 1, fMaxZ);
        for (int fgz = loZ; fgz <= hiZ; ++fgz) {
            if (fgz % kScale != 0) continue;
            for (int fgx = loX; fgx <= hiX; ++fgx) {
                if (fgx % kScale != 0) continue;
                const std::size_t idx =
                    (std::size_t)fgz * (std::size_t)dim_ + (std::size_t)fgx;
                if (idx >= heights_.size() || !present_[idx]) continue;
                const float h = heights_[idx];
                if (h != h) continue;
                const int gx = fgx / kScale, gz = fgz / kScale;
                if (gx >= 0 && gz >= 0 && gx < baseW && gz < baseH)
                    base[(std::size_t)gz * (std::size_t)baseW + (std::size_t)gx] = h;
            }
        }
    }

    // Mean absolute difference between neighbouring FINE samples over a cell box.
    // Test/telemetry only: this is the quantity the smooth brush exists to reduce.
    float roughness(int cellRadius, int cellX, int cellZ) const {
        if (empty()) return 0.0f;
        const int loX = std::max(0, (cellX - cellRadius) * kScale);
        const int hiX = std::min(dim_ - 2, (cellX + cellRadius) * kScale + kScale);
        const int loZ = std::max(0, (cellZ - cellRadius) * kScale);
        const int hiZ = std::min(dim_ - 2, (cellZ + cellRadius) * kScale + kScale);
        double sum = 0.0;
        int n = 0;
        for (int z = loZ; z <= hiZ; ++z) {
            for (int x = loX; x <= hiX; ++x) {
                const float c = heightAt(x, z, 0.0f);
                sum += std::fabs(heightAt(x + 1, z, 0.0f) - c);
                sum += std::fabs(heightAt(x, z + 1, 0.0f) - c);
                n += 2;
            }
        }
        return n ? (float)(sum / n) : 0.0f;
    }

private:
    static constexpr float kUnset = std::numeric_limits<float>::quiet_NaN();
    int cellW_ = 0, cellH_ = 0, dim_ = 0;
    std::vector<unsigned char> levels_;
    std::vector<float> heights_;
    std::vector<unsigned char> present_;
    std::vector<std::pair<std::size_t, float>> scratch_;
};
