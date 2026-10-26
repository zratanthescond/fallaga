#pragma once
//
// TerrainUndo.h -- journaled sculpt transactions for the terrain editor.
//
// The problem: a brush stroke is many frames, and every frame writes both the
// visual heightfield and (for "Add Geometry") the fine refinement grid. A plain
// "copy the whole heightfield before each edit" undo would need two full-grid
// snapshots per stroke -- ~50 MB each on the shipped 2508x2508 grid -- and would
// still not cover the fine grid. So instead a transaction records the original
// value of each sample *the first time it is about to be written*, deduplicated
// by a generation stamp, and at commit throws away every sample whose value came
// back unchanged (a no-op edit journales nothing).
//
// The record stores only the values to write. Applying it swaps them with the
// live arrays and captures the displaced values into an inverse record, so the
// same structure drives both undo and redo without ever holding two snapshots:
//
//   undo : record = undoStack.pop()  -> write record's values, inverse -> redoStack
//   redo : record = redoStack.pop()  -> write record's values, inverse -> undoStack
//
// Deliberately free of OpenGL and of TerrainManager for the parts that can be
// (the base heightfield and the road-offset layers). The fine-grid and
// subdivision-level application needs TerrainRefinement, so TerrainManager owns
// those two swaps; everything else is testable headlessly.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

// One reversible edit. Indices are flat positions into the array named by the
// field; `*Val` are the values to write when the record is applied.
//
//   baseIdx/baseVal        -> TerrainManager::visualHeights
//   layerIdx/roadOffVal    -> TerrainManager::appliedRoadOffsets
//             roadConVal   -> TerrainManager::appliedRoadConformOffsets
//   fineIdx/fineVal        -> TerrainRefinement fine grid (fgz*dim+fgx)
//             finePresent  -> whether that fine vertex is set (0/1)
//   cellIdx/levelVal       -> TerrainRefinement subdivision level (cz*cellW+cx)
struct TerrainEditRecord {
    bool hasBase = false;
    bool hasLayers = false;
    bool hasFine = false;
    bool hasCells = false;

    std::vector<uint32_t> baseIdx;
    std::vector<float> baseVal;

    std::vector<uint32_t> layerIdx;
    std::vector<float> roadOffVal;
    std::vector<float> roadConVal;

    std::vector<uint32_t> fineIdx;
    std::vector<float> fineVal;
    std::vector<uint8_t> finePresent;

    std::vector<uint32_t> cellIdx;
    std::vector<uint8_t> levelVal;

    // Visual-grid sample box touched by the base edits. `maxX < minX` means empty.
    int minX = 0, maxX = -1, minZ = 0, maxZ = -1;
    // Fine-grid sample box touched by the fine/level edits (for the chunk rebuild).
    int fMinX = 0, fMaxX = -1, fMinZ = 0, fMaxZ = -1;

    bool empty() const {
        return baseIdx.empty() && layerIdx.empty() && fineIdx.empty() && cellIdx.empty();
    }

    std::size_t bytes() const {
        return baseIdx.size() * sizeof(uint32_t) + baseVal.size() * sizeof(float) +
               layerIdx.size() * (sizeof(uint32_t) + 2 * sizeof(float)) +
               fineIdx.size() * (sizeof(uint32_t) + sizeof(float) + sizeof(uint8_t)) +
               cellIdx.size() * (sizeof(uint32_t) + sizeof(uint8_t));
    }

    // Swaps this record's base samples with `base`, writing the values it
    // displaced into `inverse` (so the inverse record restores the state this
    // apply overwrote). `inverse` is reset first. Regions are carried over.
    void applyBase(std::vector<float>& base, TerrainEditRecord& inverse) const {
        inverse = TerrainEditRecord();
        if (!hasBase || baseIdx.empty()) return;
        inverse.hasBase = true;
        inverse.minX = minX; inverse.maxX = maxX;
        inverse.minZ = minZ; inverse.maxZ = maxZ;
        const std::size_t n = baseIdx.size();
        inverse.baseIdx = baseIdx;
        inverse.baseVal.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            const uint32_t idx = baseIdx[i];
            if (idx >= base.size()) { inverse.baseVal[i] = 0.0f; continue; }
            inverse.baseVal[i] = base[idx];
            base[idx] = baseVal[i];
        }
    }

    // Same swap for the two road-shaping offset layers. Their indices share the
    // visual grid, so they travel together in one record.
    void applyLayers(std::vector<float>& roadOff, std::vector<float>& roadCon,
                     TerrainEditRecord& inverse) const {
        if (!hasLayers || layerIdx.empty()) return;
        inverse.hasLayers = true;
        inverse.layerIdx = layerIdx;
        const std::size_t n = layerIdx.size();
        inverse.roadOffVal.resize(n);
        inverse.roadConVal.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            const uint32_t idx = layerIdx[i];
            const float offCur = idx < roadOff.size() ? roadOff[idx] : 0.0f;
            const float conCur = idx < roadCon.size() ? roadCon[idx] : 0.0f;
            inverse.roadOffVal[i] = offCur;
            inverse.roadConVal[i] = conCur;
            if (idx < roadOff.size()) roadOff[idx] = roadOffVal[i];
            if (idx < roadCon.size()) roadCon[idx] = roadConVal[i];
        }
    }
};

// Bounded undo/redo stacks. A new committed edit clears the redo branch (the
// usual linear-history behaviour). The byte budget evicts the oldest undo
// records once the journal grows past it, so a session cannot grow without end.
class TerrainUndoStack {
public:
    static constexpr std::size_t kDefaultBudgetBytes = 96u * 1024u * 1024u;

    void setBudget(std::size_t bytes) { budget_ = bytes; evict(); }
    std::size_t budget() const { return budget_; }

    void clear() {
        undo_.clear();
        redo_.clear();
        undoBytes_ = redoBytes_ = 0;
    }

    // Commit a finished edit. This is the point at which the redo branch is
    // discarded, so it is only called for a record that actually changed
    // something (an empty/no-op edit must not throw away redo).
    void commit(TerrainEditRecord&& rec) {
        if (rec.empty()) return;
        redo_.clear();
        redoBytes_ = 0;
        undoBytes_ += rec.bytes();
        undo_.push_back(std::move(rec));
        evict();
    }

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    std::size_t undoCount() const { return undo_.size(); }
    std::size_t redoCount() const { return redo_.size(); }
    std::size_t undoBytes() const { return undoBytes_; }

    // Move the newest undo/redo record out for the caller to apply. Pair with
    // pushRedo()/pushUndo() handing back the inverse produced by the apply.
    TerrainEditRecord popUndo() {
        TerrainEditRecord rec = std::move(undo_.back());
        undoBytes_ -= std::min(undoBytes_, rec.bytes());
        undo_.pop_back();
        return rec;
    }
    TerrainEditRecord popRedo() {
        TerrainEditRecord rec = std::move(redo_.back());
        redoBytes_ -= std::min(redoBytes_, rec.bytes());
        redo_.pop_back();
        return rec;
    }
    void pushRedo(TerrainEditRecord&& rec) {
        if (rec.empty()) return;
        redoBytes_ += rec.bytes();
        redo_.push_back(std::move(rec));
    }
    void pushUndo(TerrainEditRecord&& rec) {
        if (rec.empty()) return;
        undoBytes_ += rec.bytes();
        undo_.push_back(std::move(rec));
        evict();
    }

private:
    void evict() {
        while (!undo_.empty() && undoBytes_ > budget_) {
            undoBytes_ -= std::min(undoBytes_, undo_.front().bytes());
            undo_.pop_front();
        }
    }

    std::deque<TerrainEditRecord> undo_;
    std::deque<TerrainEditRecord> redo_;
    std::size_t undoBytes_ = 0;
    std::size_t redoBytes_ = 0;
    std::size_t budget_ = kDefaultBudgetBytes;
};
