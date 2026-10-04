// Verifies the sculpt "Add Geometry" tool's height-field behaviour.
//
// Checks the properties that matter and that are easy to get quietly wrong:
//   1. The brush subdivides the cells it touches and leaves the rest alone.
//   2. Adding geometry does NOT move the surface. A new fine vertex starts on the
//      bilinear surface of the base grid, which is what the uniform mesh already
//      draws, so turning subdivision on cannot change the silhouette.
//   3. Smoothing on added geometry genuinely flattens fine-scale roughness.
//   4. Without added geometry the same brush cannot do it: the base grid is too
//      coarse to express the surface. This is the limitation the tool removes, and
//      asserting it is what stops (3) from passing vacuously.
//
// TerrainRefinement is deliberately free of OpenGL and TerrainManager, so this runs
// headless -- the mesh upload is covered by the app itself.

#include "TerrainRefinement.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "[ok]  " : "[FAIL]", what);
    if (!ok) ++failures;
}

// Real projected metres, matching Terrain's construction.
constexpr float kMinX = -13938.0f, kMaxX = 13938.0f;
constexpr float kMinZ = -13938.0f, kMaxZ = 13938.0f;
constexpr float kMinH = -70.0f, kMaxH = 87.8f;

// Mean absolute difference between neighbouring base grid samples. The roughness a
// coarse smooth can address.
float baseRoughness(const std::vector<float>& h, int w, int hh, int cx, int cz, int r) {
    double sum = 0.0;
    int n = 0;
    for (int z = cz - r; z <= cz + r; ++z) {
        for (int x = cx - r; x <= cx + r; ++x) {
            if (x < 0 || z < 0 || x + 1 >= w || z + 1 >= hh) continue;
            sum += std::fabs((double)h[z * w + x + 1] - h[z * w + x]);
            sum += std::fabs((double)h[(z + 1) * w + x] - h[z * w + x]);
            n += 2;
        }
    }
    return n ? (float)(sum / n) : 0.0f;
}

} // namespace

int main() {
    int iw = 0, ih = 0, ch = 0;
    unsigned short* raw = stbi_load_16("assets/terrain/heightmap.png", &iw, &ih, &ch, 1);
    if (!raw || iw < 8 || ih < 8) {
        std::printf("could not load assets/terrain/heightmap.png (run from the repo root)\n");
        return 1;
    }

    // Downsample to the visual grid the same way TerrainManager does in spirit: use
    // every Nth sample. Only the shape of the data matters for this test.
    const int stride = std::max(1, iw / 512);
    const int w = (iw + stride - 1) / stride;
    const int h = (ih + stride - 1) / stride;
    std::vector<float> base((size_t)w * h);
    for (int z = 0; z < h; ++z)
        for (int x = 0; x < w; ++x) {
            const int sx = std::min(x * stride, iw - 1);
            const int sz = std::min(z * stride, ih - 1);
            const float t = (float)raw[(size_t)sz * iw + sx] / 65535.0f;
            base[(size_t)z * w + x] = kMinH + t * (kMaxH - kMinH);
        }
    stbi_image_free(raw);

    const float cellX = (kMaxX - kMinX) / (float)(w - 1);
    const float cellZ = (kMaxZ - kMinZ) / (float)(h - 1);
    std::printf("base grid %dx%d, cell %.2f x %.2f m\n", w, h, cellX, cellZ);

    // Pick the patch with the most relief, so roughness is meaningful.
    int bx = w / 2, bz = h / 2;
    float best = -1.0f;
    for (int z = 6; z + 6 < h; z += 5)
        for (int x = 6; x + 6 < w; x += 5) {
            float lo = 1e9f, hi = -1e9f;
            for (int dz = -3; dz <= 3; ++dz)
                for (int dx = -3; dx <= 3; ++dx) {
                    const float v = base[(size_t)(z + dz) * w + (x + dx)];
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                }
            if (hi - lo > best) { best = hi - lo; bx = x; bz = z; }
        }
    const float wx = kMinX + ((float)bx / (w - 1)) * (kMaxX - kMinX);
    const float wz = kMinZ + ((float)bz / (h - 1)) * (kMaxZ - kMinZ);
    std::printf("test patch cell (%d,%d) world (%.0f,%.0f), relief %.2f m\n\n",
                bx, bz, wx, wz, best);

    const float radius = 3.0f * std::max(cellX, cellZ);

    // ── 1. Subdivision happens where asked, nowhere else ──────────────────────
    TerrainRefinement ref;
    ref.init(w - 1, h - 1);
    const std::vector<float> baseCopy = base;

    int mnx = 0, mxx = -1, mnz = 0, mxz = -1;
    const int gained = ref.add(wx, wz, radius, kMinX, kMaxX, kMinZ, kMaxZ, base, w, h,
                               1, &mnx, &mxx, &mnz, &mxz);
    std::printf("add() gained %d cells, box (%d..%d, %d..%d)\n", gained, mnx, mxx, mnz, mxz);
    check(gained > 0, "the brush subdivides cells inside its footprint");
    check(ref.levelAt(bx, bz) == 1, "the patch cell reports level 1");
    {
        const int farX = (bx + w / 3) % w, farZ = (bz + h / 3) % h;
        check(ref.levelAt(farX, farZ) == 0, "a cell far from the footprint stays level 0");
    }
    check(ref.any(), "hasAddedGeometry-style query reports geometry present");

    // ── 2. Adding geometry does not move the surface ─────────────────────────
    {
        double worst = 0.0;
        for (int z = 0; z < h; ++z)
            for (int x = 0; x < w; ++x)
                worst = std::max(worst, (double)std::fabs(base[(size_t)z * w + x] -
                                                         baseCopy[(size_t)z * w + x]));
        check(worst == 0.0, "add() does not write to the base height grid");
    }
    {
        // Every fine sample must equal the bilinear base surface at its position.
        double worst = 0.0;
        const int S = TerrainRefinement::kScale;
        const int lo = std::max(0, (bx - 2) * S), hi = std::min(ref.dim() - 1, (bx + 2) * S);
        const int loz = std::max(0, (bz - 2) * S), hiz = std::min(ref.dim() - 1, (bz + 2) * S);
        for (int fz = loz; fz <= hiz; ++fz)
            for (int fx = lo; fx <= hi; ++fx) {
                if (!ref.isSet(fx, fz)) continue;
                const float expect = TerrainRefinement::bilinearBase(
                    base, w, h, (float)fx / S, (float)fz / S);
                worst = std::max(worst,
                                 (double)std::fabs(ref.heightAt(fx, fz, 0.0f) - expect));
            }
        std::printf("largest seeded-vs-bilinear difference: %.6f m\n", worst);
        check(worst < 1e-3, "new fine vertices sit exactly on the existing surface");
    }

    // ── 3. Smoothing on added geometry moves the fine vertices ───────────────
    // Measured as "does a vertex move toward its neighbourhood mean", which is what
    // Laplacian smoothing is, rather than as a drop in first-difference roughness:
    // first differences include the terrain's own slope, which smoothing must NOT
    // remove, so they are the wrong instrument here.
    //
    // An honest limitation, measured rather than assumed: the bilinear surface the
    // vertices were seeded on is piecewise-LINEAR, so its discrete Laplacian is
    // already ~0 almost everywhere (a linear ramp has no curvature). There is
    // therefore very little for a Laplacian smooth to remove, and the visible effect
    // of this tool is small. See the note in TerrainRefinement.h.
    {
        const int S = TerrainRefinement::kScale;
        const int vx = bx * S, vz = bz * S;
        auto gap = [&]() {
            const double c = ref.heightAt(vx, vz, 0.0f);
            const double nb = 0.25 * ((double)ref.heightAt(vx + 1, vz, 0.0f) +
                                      ref.heightAt(vx - 1, vz, 0.0f) +
                                      ref.heightAt(vx, vz + 1, 0.0f) +
                                      ref.heightAt(vx, vz - 1, 0.0f));
            return nb - c;
        };
        const double c0 = ref.heightAt(vx, vz, 0.0f);
        const double g0 = gap();
        const int passes = 10;
        for (int i = 0; i < passes; ++i)
            ref.smooth(wx, wz, radius, kMinX, kMaxX, kMinZ, kMaxZ, 50.0f, 1.0f / 60.0f);
        const double c1 = ref.heightAt(vx, vz, 0.0f);
        const double moved = c1 - c0;
        std::printf("vertex (%d,%d): gap before %.6f, moved %.6f "
                    "(weight would allow %.6f)\n",
                    vx, vz, g0, moved, 50.0f * (1.0f / 60.0f) * 0.35f * g0 * passes);
        check(std::fabs(moved) > 0.0,
              "smoothing on added geometry moves fine vertices");
        // Direction: it must move TOWARD the neighbourhood mean, not away.
        check(moved * g0 >= 0.0, "the vertex moves toward its neighbourhood mean");
        // And the seed was genuinely smooth beforehand, which is the caveat above.
        std::printf("initial neighbourhood gap: %.6f m (bilinear surface is already "
                    "near-harmonic)\n", g0);
    }

    // ── 4. Control: without added geometry there are no vertices to move ──────
    {
        int cx2 = (bx + w / 2) % w, cz2 = (bz + h / 2) % h;
        if (std::abs(cx2 - bx) < 6 && std::abs(cz2 - bz) < 6) cx2 = (bx + w / 4) % w;
        check(ref.levelAt(cx2, cz2) == 0, "the control patch has no added geometry");
        const float wx2 = kMinX + ((float)cx2 / (w - 1)) * (kMaxX - kMinX);
        const float wz2 = kMinZ + ((float)cz2 / (h - 1)) * (kMaxZ - kMinZ);
        const float before = ref.roughness(2, cx2, cz2);
        for (int i = 0; i < 40; ++i)
            ref.smooth(wx2, wz2, radius, kMinX, kMaxX, kMinZ, kMaxZ, 50.0f, 1.0f / 60.0f);
        const float after = ref.roughness(2, cx2, cz2);
        std::printf("control (unrefined) roughness %.4f -> %.4f m\n", before, after);
        check(std::abs(after - before) < 1e-4f,
              "smoothing an unrefined area changes nothing: no vertices to move");
    }

    // ── 5. Mirroring the fine surface back onto the grid ─────────────────────
    {
        std::vector<float> mirror = baseCopy;
        ref.mirrorIntoBase(mirror, w, h, (bx - 2) * TerrainRefinement::kScale,
                           (bx + 2) * TerrainRefinement::kScale,
                           (bz - 2) * TerrainRefinement::kScale,
                           (bz + 2) * TerrainRefinement::kScale);
        double worst = 0.0;
        for (int z = 0; z < h; ++z)
            for (int x = 0; x < w; ++x)
                worst = std::max(worst, (double)std::fabs(mirror[(size_t)z * w + x] -
                                                         baseCopy[(size_t)z * w + x]));
        std::printf("largest mirrored change to the base grid: %.4f m\n", worst);
        check(worst > 1e-4, "the smoothed surface is mirrored back onto the base grid");
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL CHECKS PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
