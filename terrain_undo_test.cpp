// Verifies the terrain sculpt undo/redo journal primitives, headless.
//
// TerrainUndo.h is deliberately free of OpenGL and TerrainManager, and
// TerrainRefinement carries the two hooks undo needs (setLevel/clearSample), so the
// parts of the journal that are easy to get quietly wrong can be checked without a
// context:
//   1. A record's apply swaps values and yields an inverse that restores the state.
//   2. Undo/redo round-trips exactly, including the fine-grid presence bit.
//   3. A new committed edit drops the redo branch; a no-op edit does not.
//   4. The byte budget evicts the oldest records, never the newest.
//   5. add()-style subdivision can be reverted with setLevel/clearSample.
//
// The TerrainManager glue (capture-before-write into a stroke, one record per
// stroke) is exercised by the editor itself.

#include "TerrainUndo.h"
#include "TerrainRefinement.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "[ok]  " : "[FAIL]", what);
    if (!ok) ++failures;
}

bool nearly(float a, float b) { return std::fabs(a - b) < 1e-5f; }

// A tiny stand-in for TerrainManager's base-only swap path: build a record whose
// stored values are the "before" snapshot, then apply it and keep the inverse.
TerrainEditRecord makeBaseRecord(const std::vector<float>& before,
                                 const std::vector<uint32_t>& idx) {
    TerrainEditRecord rec;
    rec.hasBase = true;
    rec.baseIdx = idx;
    rec.baseVal.reserve(idx.size());
    int mnx = 0, mxx = -1, mnz = 0, mxz = -1;
    for (std::size_t n = 0; n < idx.size(); ++n) {
        const uint32_t i = idx[n];
        rec.baseVal.push_back(before[i]);
        const int x = (int)(i % 8), z = (int)(i / 8);
        if (mxx < mnx) { mnx = mxx = x; mnz = mxz = z; }
        else {
            mnx = std::min(mnx, x); mxx = std::max(mxx, x);
            mnz = std::min(mnz, z); mxz = std::max(mxz, z);
        }
    }
    rec.minX = mnx; rec.maxX = mxx; rec.minZ = mnz; rec.maxZ = mxz;
    return rec;
}

} // namespace

int main() {
    // ── 1. applyBase swaps and yields a restoring inverse ─────────────────────
    {
        std::vector<float> base = {10, 20, 30, 40, 50, 60, 70, 80};
        const std::vector<float> original = base;
        const std::vector<uint32_t> idx = {1, 3, 5};
        TerrainEditRecord rec = makeBaseRecord(original, idx);

        // Simulate the edit: the brush changes the same samples after capture.
        base[1] = 21; base[3] = 43; base[5] = 55;

        TerrainEditRecord inverse;
        rec.applyBase(base, inverse);
        check(base[1] == 20 && base[3] == 40 && base[5] == 60,
              "applyBase restores the captured (pre-edit) values");
        check(inverse.hasBase && inverse.baseVal.size() == 3,
              "applyBase fills an inverse record");
        check(inverse.baseVal[0] == 21 && inverse.baseVal[1] == 43 && inverse.baseVal[2] == 55,
              "the inverse holds the values the apply displaced");

        TerrainEditRecord back;
        inverse.applyBase(base, back);
        check(base[1] == 21 && base[3] == 43 && base[5] == 55,
              "re-applying the inverse returns to the post-edit values");
    }

    // ── 2. Undo/redo round-trip through the stack ─────────────────────────────
    {
        TerrainUndoStack stack;
        std::vector<float> base = {0, 0, 0, 0};
        const std::vector<uint32_t> idx = {0, 1, 2, 3};

        std::vector<float> before = base;
        base = {1, 2, 3, 4};
        TerrainEditRecord rec = makeBaseRecord(before, idx);
        // Drop unchanged samples, as commit() does, before pushing.
        {
            TerrainEditRecord filtered;
            filtered.hasBase = true;
            filtered.minX = rec.minX; filtered.maxX = rec.maxX;
            filtered.minZ = rec.minZ; filtered.maxZ = rec.maxZ;
            for (std::size_t i = 0; i < rec.baseIdx.size(); ++i)
                if (base[rec.baseIdx[i]] != rec.baseVal[i]) {
                    filtered.baseIdx.push_back(rec.baseIdx[i]);
                    filtered.baseVal.push_back(rec.baseVal[i]);
                }
            stack.commit(std::move(filtered));
        }
        check(stack.canUndo() && stack.undoCount() == 1, "one committed edit is undoable");
        check(!stack.canRedo(), "a fresh edit has no redo");

        {
            TerrainEditRecord r = stack.popUndo();
            TerrainEditRecord inv;
            r.applyBase(base, inv);
            stack.pushRedo(std::move(inv));
        }
        check(base[0] == 0 && base[3] == 0, "undo restores the original values");
        check(stack.canRedo() && stack.redoCount() == 1, "undo produces a redo record");

        {
            TerrainEditRecord r = stack.popRedo();
            TerrainEditRecord inv;
            r.applyBase(base, inv);
            stack.pushUndo(std::move(inv));
        }
        check(base[0] == 1 && base[3] == 4, "redo reapplies the edit");
        check(stack.canUndo() && !stack.canRedo(), "redo moves the record back to undo");
    }

    // ── 3. New edit clears redo; no-op edit does not ──────────────────────────
    {
        TerrainUndoStack stack;
        TerrainEditRecord a; a.hasBase = true; a.baseIdx = {0}; a.baseVal = {1.0f};
        TerrainEditRecord b; b.hasBase = true; b.baseIdx = {0}; b.baseVal = {2.0f};
        stack.commit(std::move(a));
        TerrainEditRecord inv; inv.hasBase = true; inv.baseIdx = {0}; inv.baseVal = {5.0f};
        stack.pushRedo(std::move(inv));
        check(stack.canRedo(), "precondition: a redo record exists");

        TerrainEditRecord empty; // no fields set -> empty()
        stack.commit(std::move(empty));
        check(stack.canRedo(), "a no-op edit leaves the redo branch intact");

        stack.commit(std::move(b));
        check(!stack.canRedo(), "a real new edit discards the redo branch");
    }

    // ── 4. Byte budget evicts the oldest, keeps the newest ────────────────────
    {
        TerrainUndoStack stack;
        const std::size_t oneRec = 200;
        stack.setBudget(oneRec); // room for roughly one record
        for (int k = 0; k < 5; ++k) {
            TerrainEditRecord r;
            r.hasBase = true;
            r.baseIdx.resize(10, (uint32_t)k);
            r.baseVal.resize(10, (float)k);
            stack.commit(std::move(r));
        }
        check(stack.undoCount() >= 1, "the budget keeps at least the newest record");
        TerrainEditRecord top = stack.popUndo();
        check(top.hasBase && top.baseVal[0] == 4.0f, "the newest edit survives eviction");
    }

    // ── 5. Subdivision can be reverted: setLevel + clearSample ────────────────
    {
        const int W = 6, H = 5;
        TerrainRefinement ref;
        ref.init(W - 1, H - 1);
        std::vector<float> grid((std::size_t)W * H, 5.0f);
        for (int z = 0; z < H; ++z)
            for (int x = 0; x < W; ++x) grid[(std::size_t)z * W + x] = 5.0f + x + z;

        int mnx = 0, mxx = -1, mnz = 0, mxz = -1;
        const float wx = 1.0f, wz = 1.0f;
        const int gained = ref.add(wx, wz, 2.0f, 0.0f, (float)(W - 1), 0.0f, (float)(H - 1),
                                   grid, W, H, 1, &mnx, &mxx, &mnz, &mxz);
        check(gained > 0, "add() subdivides at least one cell");
        check(ref.levelAt(1, 1) == 1 && ref.isSet(1 * TerrainRefinement::kScale,
                                                   1 * TerrainRefinement::kScale),
              "the touched cell reports level 1 with seeded fine vertices");

        // Undo exactly what add() did: clear the seeded fine vertices and reset levels.
        const int S = TerrainRefinement::kScale;
        for (int cz = mnz; cz <= mxz; ++cz)
            for (int cx = mnx; cx <= mxx; ++cx)
                ref.setLevel(cx, cz, 0);
        for (int fz = mnz * S; fz <= mxz * S + S; ++fz)
            for (int fx = mnx * S; fx <= mxx * S + S; ++fx)
                ref.clearSample(fx, fz);

        check(ref.levelAt(1, 1) == 0, "setLevel reverts the subdivision level");
        check(!ref.isSet(1 * S, 1 * S), "clearSample removes a seeded fine vertex");
        check(!ref.any(), "after the revert no cell reports added geometry");
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL CHECKS PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
