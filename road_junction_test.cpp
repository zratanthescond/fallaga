// Invariant test for road junctions: the arms meeting at a junction must arrive at the
// same height.
//
// The plan called this out as a risk -- junction centres, gap-splice joints and caps each
// compute their height independently, so arms can meet at slightly different heights and
// show a step or a patch at the join.
//
// This measures the spread directly rather than assuming it is a problem: it finds the
// endpoint clusters (the same proximity rule the mesh builder uses, kRoadJunctionTolerance)
// and reports the difference between the highest and lowest ribbon vertex in each
// cluster's end rings. A small spread means there is nothing to fix.
//
// Includes RoadSystem.cpp directly, like road_surface_test, so no GL context is needed.

#include "src/RoadSystem.cpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>

#include <algorithm>
#include <functional>
#include <cmath>
#include <cstdio>
#include <vector>

int main() {
    // The shipped DEM, loaded the way TerrainManager does.
    int iw = 0, ih = 0, ch = 0;
    unsigned short* raw = stbi_load_16("assets/terrain/heightmap.png", &iw, &ih, &ch, 1);
    if (!raw || iw < 8 || ih < 8) {
        std::printf("could not load assets/terrain/heightmap.png (run from the repo root)\n");
        return 1;
    }
    std::vector<float> heights((size_t)iw * ih);
    for (size_t i = 0; i < heights.size(); ++i)
        heights[i] = -70.0f + ((float)raw[i] / 65535.0f) * (87.8f + 70.0f);
    stbi_image_free(raw);

    TerrainCoordinateMapper mapper;
    mapper.bounds = { -13938.0f, 13938.0f, -13938.0f, 13938.0f, -70.0f, 87.8f };
    mapper.heightmapW = iw;
    mapper.heightmapH = ih;

    HeightSampler sampler;
    sampler.init(mapper, heights.data(), iw, ih);

    RoadGraph graph;
    if (!graph.loadFromJSON("assets/roads/roads_manual.json")) {
        std::printf("could not load assets/roads/roads_manual.json\n");
        return 1;
    }
    for (auto& seg : graph.segments)
        for (auto& p : seg.controlPoints) {
            p.pos.x += 3600.0f;
            p.pos.y += -701.0f;
        }
    graph.buildTopology();

    std::vector<RoadSegment> roads = graph.segments;
    for (auto& s : roads)
        RoadSpline::evaluate(s, sampler, 0.5f);

    // Measure the spread BEFORE unifying, so the test reports what it fixed rather than
    // only that it passes.
    auto measureSpread = [&](const std::vector<RoadSegment>& rs) {
        struct End { int seg; bool back; glm::vec2 p; float y; };
        std::vector<End> ends;
        for (int s = 0; s < (int)rs.size(); ++s) {
            if (rs[s].splinePoints.size() < 2) continue;
            ends.push_back({s, false, {rs[s].splinePoints.front().x, rs[s].splinePoints.front().z},
                            rs[s].splinePoints.front().y});
            ends.push_back({s, true, {rs[s].splinePoints.back().x, rs[s].splinePoints.back().z},
                            rs[s].splinePoints.back().y});
        }
        std::vector<int> parent(ends.size());
        for (size_t i = 0; i < parent.size(); ++i) parent[i] = (int)i;
        std::function<int(int)> find = [&](int a) {
            while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; }
            return a;
        };
        for (size_t i = 0; i < ends.size(); ++i)
            for (size_t j = i + 1; j < ends.size(); ++j)
                if (glm::length(ends[i].p - ends[j].p) <= kRoadJunctionTolerance)
                    parent[find((int)i)] = find((int)j);

        float worst = 0.0f;
        glm::vec2 worstAt(0.0f);
        int junctions = 0, multiArm = 0;
        for (size_t i = 0; i < ends.size(); ++i) {
            if (find((int)i) != (int)i) continue;
            float lo = 1e9f, hi = -1e9f;
            glm::vec2 at(0.0f);
            int members = 0;
            for (size_t j = 0; j < ends.size(); ++j) {
                if (find((int)j) != (int)i) continue;
                ++members;
                lo = std::min(lo, ends[j].y);
                hi = std::max(hi, ends[j].y);
                at = ends[j].p;
            }
            if (members < 2) continue;
            ++junctions;
            if (members >= 3) ++multiArm;
            if (hi - lo > worst) { worst = hi - lo; worstAt = at; }
        }
        std::printf("   junctions=%d (>=3 arms: %d) worst spread=%.4f m at (%.1f, %.1f)\n",
                    junctions, multiArm, worst, worstAt.x, worstAt.y);
        return worst;
    };

    std::printf("junction arm height spread:\n  before unify:\n");
    const float spreadBefore = measureSpread(roads);

    // Apply the same unification the mesh builder applies. build() takes its segments by
    // value, so its own copy cannot be inspected from here; calling the production
    // function on this copy is what makes the result observable, and it keeps the test on
    // the production path rather than a reimplementation of it.
    unifyJunctionHeights(roads, kRoadJunctionTolerance, kJunctionHeightBlend);
    std::printf("  after unify:\n");
    const float worstSpread = measureSpread(roads);

    // The ribbon applies Z_BIAS 0.035 plus the crown. A spread inside a few centimetres
    // is within that bias and cannot read as a step.
    constexpr float kMaxAcceptableSpread = 0.05f;
    if (spreadBefore <= kMaxAcceptableSpread)
        std::printf("NOTE: the un-unified spread was already %.4f m, so nothing needed "
                    "fixing; the assertions below still hold for the future.\n", spreadBefore);
    if (worstSpread > kMaxAcceptableSpread) {
        std::printf("FAIL: arms still meet at heights differing by %.4f m (limit %.3f m)\n",
                    worstSpread, kMaxAcceptableSpread);
        return 1;
    }
    std::printf("PASS: every junction reconciles inside %.3f m (worst %.4f m, was %.4f m)\n",
                kMaxAcceptableSpread, worstSpread, spreadBefore);
    return 0;
}