// Deterministic test for the road surface texture coordinate.
//
// street.frag samples the carriageway as texture(roadSurfaceTex, metricUV / scale)
// (and its normal map the same way), so metricUV decides how the cobble/asphalt is
// laid down. A world-XZ projection stays locked to the world axes, so on a road that
// changes direction the pattern shears diagonally across the carriageway. The ribbon
// must instead carry a road-following coordinate: metres laterally across the road
// and metres travelled along it.
//
// This builds ribbon meshes with an invalid (plan-view) sampler -- no GL context and
// no Terrain instance needed -- and checks the coordinate directly on the vertices.
#include "src/RoadSystem.cpp"

#include <cmath>
#include <cstdio>
#include <vector>

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

static RoadSegment makeSeg(uint32_t id, RoadType type, float width,
                           glm::vec2 a, glm::vec2 b) {
    RoadSegment s;
    s.id = id;
    s.type = type;
    s.width = width;
    s.shoulderWidth = 1.5f;
    s.controlPoints.push_back({ a });
    s.controlPoints.push_back({ b });
    return s;
}

static std::vector<RoadVertex> buildAll(std::vector<RoadSegment> segs) {
    HeightSampler sampler;                 // invalid: plan-view only
    for (auto& s : segs) RoadSpline::evaluate(s, sampler);
    RoadMeshBuilder b;
    b.build(segs, sampler, nullptr, 2.0f);
    return b.debugVertices();
}

// ── 1. On a 45 degree road v must equal the along-the-road distance, not z ─────
// The distance travelled from the start is (x + z)/sqrt2, which a world-XZ v (= z)
// is not, so the two are distinguishable.
static void testRibbonMetricUVFollowsRoad() {
    const float W = 8.0f;
    const std::vector<RoadVertex> v =
        buildAll({ makeSeg(1, RoadType::UrbanStreet, W,
                           glm::vec2(0, 0), glm::vec2(100, 100)) });
    const float inv = 1.0f / std::sqrt(2.0f);
    int checked = 0, worldXZ = 0;
    for (const auto& x : v) {
        const float along = (x.position.x + x.position.z) * inv;
        if (along < 12.0f || along > 88.0f) continue;   // skip the end caps
        ++checked;
        // v advances one metre per metre travelled along the road...
        CHECK(std::fabs(x.metricUV.y - along) < 0.6f);
        // ...and u spans the carriageway in metres.
        CHECK(x.metricUV.x > -0.01f && x.metricUV.x < W + 0.01f);
        // A world-XZ stamp would satisfy metricUV == (x, z); the fix must not.
        if (std::fabs(x.metricUV.x - x.position.x) < 0.05f &&
            std::fabs(x.metricUV.y - x.position.z) < 0.05f)
            ++worldXZ;
    }
    CHECK(checked > 0);
    CHECK(worldXZ == 0);
}

// ── 2. The along axis is the road direction for any heading ───────────────────
// Straight roads run along +X, +Z, -X and both diagonals from the same origin; for
// each, v must equal the projection of the vertex onto the road direction. A world
// projection would give v = z for every heading, so the rotating cases fail.
static void testAlongAxisTracksHeading() {
    const glm::vec2 dirs[] = { {1, 0}, {0, 1}, {-1, 0}, {1, 1}, {1, -1}, {-2, 1} };
    for (const glm::vec2& d : dirs) {
        const glm::vec2 a(50.0f, 50.0f);
        const glm::vec2 n = glm::normalize(d);
        const glm::vec2 b = a + n * 100.0f;
        const std::vector<RoadVertex> v =
            buildAll({ makeSeg(1, RoadType::UrbanStreet, 8.0f, a, b) });
        int checked = 0;
        for (const auto& x : v) {
            const glm::vec2 p(x.position.x, x.position.z);
            const float along = glm::dot(p - a, n);
            if (along < 12.0f || along > 88.0f) continue;   // skip the end caps
            ++checked;
            CHECK(std::fabs(x.metricUV.y - along) < 0.6f);
            CHECK(x.metricUV.x > -0.01f && x.metricUV.x < 8.01f);
        }
        CHECK(checked > 0);
    }
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    testRibbonMetricUVFollowsRoad();
    testAlongAxisTracksHeading();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
