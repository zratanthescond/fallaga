// Verifies the flat-surface sculpt brushes: Flatten (id 9) and Level (id 10).
//
// Both are pure, GL-free algorithms in SculptTools.cpp, so this runs headless. It
// pins the properties that are easy to get quietly wrong:
//   1. Flatten pulls the whole footprint to the requested target height and leaves
//      everything outside the radius untouched.
//   2. Flatten honours the falloff shape: the centre reaches the target before the
//      rim, and the rim itself is not moved.
//   3. Flatten never overshoots the target and clamps to the field's height limits.
//   4. Level flattens toward the falloff-weighted mean of the footprint, so the
//      patch's overall elevation is preserved rather than dragged to min/max.
//   5. Flatten and Level are reachable through apply() (ids 9/10) and unknown ids
//      still return nothing.

#include "SculptTools.cpp" // GL-free; compiled directly so no CMake source list is needed

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

constexpr int   kW = 41, kH = 41;   // samples
constexpr float kCell = 1.0f;       // world metres per sample
constexpr float kMinH = -70.0f, kMaxH = 87.8f;

// World mapping for a kW x kH grid with 1 m cells starting at the origin.
SculptTools::HeightField makeField(std::vector<float>& heights) {
    return SculptTools::HeightField{heights, kW, kH,
                                    0.0f, (float)(kW - 1) * kCell,
                                    0.0f, (float)(kH - 1) * kCell,
                                    kMinH, kMaxH};
}

float worldX(int x) { return (float)x * kCell; }
float worldZ(int z) { return (float)z * kCell; }

// A deterministic, bumpy but bounded starting surface.
std::vector<float> bumpyGrid() {
    std::vector<float> h((size_t)kW * kH);
    for (int z = 0; z < kH; ++z)
        for (int x = 0; x < kW; ++x)
            h[(size_t)z * kW + x] =
                10.0f + 3.0f * std::sin(x * 0.7f) + 2.0f * std::cos(z * 0.9f);
    return h;
}

// Plain mean of the samples inside the (constant-falloff) disc; with Constant
// falloff the weighted mean is exactly this.
double discMean(const std::vector<float>& h, float cx, float cz, float r) {
    double sum = 0.0; int n = 0;
    for (int z = 0; z < kH; ++z)
        for (int x = 0; x < kW; ++x) {
            const float dx = worldX(x) - cx, dz = worldZ(z) - cz;
            if (dx * dx + dz * dz <= r * r) { sum += h[(size_t)z * kW + x]; ++n; }
        }
    return n ? sum / n : 0.0;
}

} // namespace

int main() {
    const float cx = worldX(kW / 2), cz = worldZ(kH / 2);
    const float radius = 8.0f;
    const float target = 5.0f;

    // Saturated blend: speed*2*dt = 50*2*(1/60) > 1, so one apply moves a sample
    // all the way to its target where the falloff is 1.
    SculptTools::BrushParams base;
    base.wx = cx; base.wz = cz;
    base.radius = radius;
    base.speed = 50.0f;
    base.dt = 1.0f / 60.0f;
    base.falloff = SculptTools::FalloffMode::Constant;
    base.hardness = 0.0f;
    base.targetHeight = target;

    // ── 1. Flatten pins the footprint to the target, outside is untouched ──────
    {
        std::vector<float> h = bumpyGrid();
        const std::vector<float> before = h;
        SculptTools::HeightField f = makeField(h);
        const SculptTools::IntBox b = SculptTools::apply(f, 9, base);
        check(!b.empty(), "flatten reports a non-empty footprint");

        double worstInside = 0.0, worstOutside = 0.0;
        int inside = 0;
        for (int z = 0; z < kH; ++z)
            for (int x = 0; x < kW; ++x) {
                const float dx = worldX(x) - cx, dz = worldZ(z) - cz;
                const bool in = dx * dx + dz * dz <= radius * radius;
                const double v = h[(size_t)z * kW + x];
                if (in) { worstInside = std::max(worstInside, std::fabs(v - target)); ++inside; }
                else    { worstOutside = std::max(worstOutside,
                                                  (double)std::fabs(v - before[(size_t)z * kW + x])); }
            }
        std::printf("flatten: worst |h-target| inside = %.4f m over %d samples; "
                    "worst change outside = %.4f m\n", worstInside, inside, worstOutside);
        check(inside > 0, "the footprint contains samples");
        check(worstInside < 1e-3, "every in-radius sample sits exactly on the target");
        check(worstOutside == 0.0, "samples outside the radius are untouched");
    }

    // ── 2. Falloff: centre leads, rim is left alone ────────────────────────────
    {
        std::vector<float> h = bumpyGrid();
        SculptTools::BrushParams p = base;
        p.falloff = SculptTools::FalloffMode::Smooth;
        SculptTools::HeightField f = makeField(h);
        SculptTools::apply(f, 9, p);

        const int c = kW / 2;
        const double centerGap = std::fabs((double)h[(size_t)c * kW + c] - target);
        // A sample a few cells in from the rim: closer to the target than before,
        // but not fully there.
        const int near = c + (int)radius - 2;
        const double nearGap = std::fabs((double)h[(size_t)c * kW + near] - target);
        // A sample exactly on the rim must not move (falloff is 0 at distance==radius).
        const int rim = c + (int)radius;
        const double rimBefore = bumpyGrid()[(size_t)c * kW + rim];
        const double rimAfter = h[(size_t)c * kW + rim];

        std::printf("smooth falloff: centre gap %.4f, near-rim gap %.4f, rim moved %.6f\n",
                    centerGap, nearGap, std::fabs(rimAfter - rimBefore));
        check(centerGap < 1e-3, "the centre reaches the target under smooth falloff");
        check(nearGap > centerGap, "the rim lags the centre: falloff is respected");
        check(std::fabs(rimAfter - rimBefore) < 1e-4, "the exact rim sample is unchanged");
    }

    // ── 3. No overshoot, and the grid clamps to its height limits ─────────────
    {
        std::vector<float> h = bumpyGrid();
        SculptTools::BrushParams p = base;
        // Target far above the grid maximum: the result must clamp, not blow past it.
        p.targetHeight = kMaxH + 500.0f;
        SculptTools::HeightField f = makeField(h);
        SculptTools::apply(f, 9, p);
        double hi = -1e9;
        for (float v : h) hi = std::max(hi, (double)v);
        std::printf("clamped flatten: max height = %.2f (limit %.2f)\n", hi, kMaxH);
        check(hi <= kMaxH + 1e-3, "flatten never exceeds the field maximum");
        check(std::fabs(hi - kMaxH) < 1e-3, "an out-of-range target clamps to the maximum");
    }

    // ── 4. Level flattens to the local mean and preserves elevation ────────────
    {
        std::vector<float> h = bumpyGrid();
        const std::vector<float> before = h;
        const double mean = discMean(before, cx, cz, radius);
        SculptTools::BrushParams p = base; // Constant falloff -> weighted mean == plain mean
        SculptTools::HeightField f = makeField(h);
        SculptTools::apply(f, 10, p);

        double worstInside = 0.0, worstOutside = 0.0;
        for (int z = 0; z < kH; ++z)
            for (int x = 0; x < kW; ++x) {
                const float dx = worldX(x) - cx, dz = worldZ(z) - cz;
                if (dx * dx + dz * dz <= radius * radius)
                    worstInside = std::max(worstInside,
                                           std::fabs((double)h[(size_t)z * kW + x] - mean));
                else
                    worstOutside = std::max(worstOutside,
                                            (double)std::fabs(h[(size_t)z * kW + x] -
                                                              before[(size_t)z * kW + x]));
            }
        std::printf("level: disc mean %.4f, worst |h-mean| inside %.4f, outside %.4f\n",
                    mean, worstInside, worstOutside);
        check(worstInside < 1e-3, "every in-radius sample lands on the local mean");
        check(worstOutside == 0.0, "level leaves the rest of the map untouched");
    }

    // ── 5. apply() dispatch wires ids 9 and 10, and unknown ids do nothing ─────
    {
        std::vector<float> a = bumpyGrid(), b = bumpyGrid();
        SculptTools::HeightField fa = makeField(a), fb = makeField(b);
        SculptTools::flatten(fa, base);
        SculptTools::apply(fb, 9, base);
        bool same = true;
        for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) { same = false; break; }
        check(same, "apply(9) is exactly flatten()");

        std::vector<float> c = bumpyGrid(), d = bumpyGrid();
        SculptTools::HeightField fc = makeField(c), fd = makeField(d);
        SculptTools::level(fc, base);
        SculptTools::apply(fd, 10, base);
        same = true;
        for (size_t i = 0; i < c.size(); ++i) if (c[i] != d[i]) { same = false; break; }
        check(same, "apply(10) is exactly level()");

        std::vector<float> e = bumpyGrid();
        const std::vector<float> eBefore = e;
        SculptTools::HeightField fe = makeField(e);
        const SculptTools::IntBox box = SculptTools::apply(fe, 11, base);
        bool unchanged = true;
        for (size_t i = 0; i < e.size(); ++i) if (e[i] != eBefore[i]) { unchanged = false; break; }
        check(box.empty() && unchanged, "an unknown brush id changes nothing");
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL CHECKS PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
