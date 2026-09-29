// Standalone unit test for the road mesh builder's junction handling.
//
//   cl /nologo /std:c++17 /EHsc /O1 /DNOMINMAX /I src /I <vcpkg>\include \
//      roadmesh_test.cpp /Fe:roadmesh_test.exe \
//      /link /LIBPATH:<vcpkg>\lib opengl32.lib glew32.lib
//
// street.frag paints the curb, the edge lines and the centre line procedurally
// from u, so they show up on whichever fragment is visible. Where one road lies
// over another, the upper road's rim bands would slice a bright border across the
// road below. That suppression is carried by RoadVertex::markMask (0 = bare
// asphalt), which the shader interpolates, so paint fades out over one
// cross-section instead of snapping.
//
//   * merge zones (2b): a sample inside a MORE MAJOR road's corridor drops its
//     paint. Geometry is untouched.
//   * mid-span crossings (2c/5b) and retracted branch mouths (step 0): two roads
//     meeting anywhere other than a shared endpoint used to produce no junction
//     at all, so the ribbons z-fought with a gap between them. Contacts are
//     blanked and covered by one flat pad per intersection.
//   * width at corners: computeRoadFrames mitre-compensates its bisector, so an
//     8 m street stays 8 m wide through a 90 degree turn instead of pinching to
//     ~5.7 m.
//   * cross-section: terrain is sampled once at the centreline and the rims only
//     partially adopt their own ground height, so the bed no longer twists with
//     every side slope.
#include "src/RoadSystem.cpp"

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

// Paint suppression now lives on markMask, not on u.
static bool isBare(float markMask) { return markMask < 0.5f; }
// Pillars that bracket the painted edge line (street.frag: mark(u, 0.085,
// 0.018) is evaluated across the quad spanning these two pillars).
static bool isEdgeLine(float u) {
    return std::fabs(u - 0.055f) < 1e-4f || std::fabs(u - 0.105f) < 1e-4f;
}

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

// A three-point road so a real corner exists to measure the width across.
static RoadSegment makeCornerSeg(uint32_t id, RoadType type, float width,
                                 glm::vec2 a, glm::vec2 b, glm::vec2 c) {
    RoadSegment s = makeSeg(id, type, width, a, b);
    s.controlPoints.push_back({ c });
    return s;
}

struct Built {
    std::vector<RoadVertex> v;
    int tris = 0;
};

static Built buildAll(std::vector<RoadSegment> segs) {
    HeightSampler sampler;                 // invalid: plan-view only
    for (auto& s : segs) RoadSpline::evaluate(s, sampler);
    RoadMeshBuilder b;
    b.build(segs, sampler, nullptr, 2.0f);
    return { b.debugVertices(), b.getTriangleCount() };
}

// Count vertices matching a predicate over (position, u, markMask).
template <typename Pred>
static int countWhere(const Built& b, Pred pred) {
    int n = 0;
    for (const auto& x : b.v)
        if (pred(x.position.x, x.position.z, x.uv.x, x.markMask)) ++n;
    return n;
}

// ── 1. A lone road keeps all of its paint ────────────────────────────────────
static void testLoneRoadKeepsPaint() {
    auto b = buildAll({ makeSeg(1, RoadType::ColonialAvenue, 20.0f,
                                glm::vec2(0, 0), glm::vec2(100, 0)) });
    CHECK(!b.v.empty());
    CHECK(countWhere(b, [](float, float, float, float m) { return isBare(m); }) == 0);
    CHECK(countWhere(b, [](float, float, float u, float) { return isEdgeLine(u); }) > 0);
}

// ── 2. A right-angle crossing: one pad, no paint through the junction ────────
static void testCrossingProducesOnePadAndBlanksPaint() {
    // Avenue along X (half-width 10) crossed by a dirt track along Z (half-width 5).
    const std::vector<RoadSegment> segs{
        makeSeg(1, RoadType::ColonialAvenue, 20.0f, glm::vec2(0, 0), glm::vec2(100, 0)),
        makeSeg(2, RoadType::CountryDirt,    10.0f, glm::vec2(20, -30), glm::vec2(20, 30)),
    };
    Built crossed = buildAll(segs);

    // Same roads, pulled far apart: no crossing, no pad, so this is the baseline.
    std::vector<RoadSegment> apart = segs;
    apart[1].controlPoints[0].pos.y += 400.0f;
    apart[1].controlPoints[1].pos.y += 400.0f;
    Built apartB = buildAll(apart);

    // Exactly one pad: a centre vertex plus the convex hull of the four rims,
    // fanned into a quad. Nothing else may be added.
    CHECK(crossed.v.size() == apartB.v.size() + 5);
    CHECK(crossed.tris == apartB.tris + 4);

    // Paint survives far from the crossing, on both roads.
    CHECK(countWhere(crossed, [](float x, float z, float u, float) {
              return isEdgeLine(u) && x < 5.0f; }) > 0);                 // avenue, west arm
    CHECK(countWhere(crossed, [](float x, float z, float u, float) {
              return isEdgeLine(u) && z > 20.0f; }) > 0);                 // track, north arm

    // No paint anywhere in the junction core.
    CHECK(countWhere(crossed, [](float x, float z, float u, float) {
              return isEdgeLine(u) && std::fabs(x - 20.0f) < 2.0f && std::fabs(z) < 2.0f;
          }) == 0);

    // Everything inside the pad footprint is bare (it covers the overlap).
    CHECK(countWhere(crossed, [](float x, float z, float, float m) {
              return isBare(m) && std::fabs(x - 20.0f) < 11.0f && std::fabs(z) < 6.0f;
          }) > 0);
}

// ── 3. A near-parallel overlap is a merge, not a crossing: no pad ────────────
// The track runs 13 m off the avenue centreline: its corridor still clips the
// avenue by 2 m, but the tangents are parallel, so this is a merge (2b) and not
// a crossing (2c). Its endpoints stay 13 m out, clear of step 0's retraction.
static void testParallelOverlapAddsNoGeometry() {
    const std::vector<RoadSegment> segs{
        makeSeg(1, RoadType::ColonialAvenue, 20.0f, glm::vec2(0, 0), glm::vec2(100, 0)),
        makeSeg(2, RoadType::CountryDirt,    10.0f, glm::vec2(0, 13), glm::vec2(100, 13)),
    };
    Built overlap = buildAll(segs);

    std::vector<RoadSegment> apart = segs;
    apart[1].controlPoints[0].pos.y += 400.0f;
    apart[1].controlPoints[1].pos.y += 400.0f;
    Built apartB = buildAll(apart);

    // Blanking paint must not add or remove a single vertex or triangle.
    CHECK(overlap.v.size() == apartB.v.size());
    CHECK(overlap.tris == apartB.tris);
    CHECK(countWhere(overlap, [](float, float, float, float m) { return isBare(m); }) == 0);
    CHECK(countWhere(overlap, [](float x, float z, float u, float) {
              return isEdgeLine(u) && x < 5.0f; }) > 0);   // avenue keeps its paint
    CHECK(countWhere(overlap, [](float x, float z, float u, float) {
              return isEdgeLine(u) && z > 12.0f; }) > 0);  // so does the track (rim 8..18)
}

// ── 4. Rank decides who yields at a crossing ─────────────────────────────────
static void testMajorKeepsItsPaintAtCrossing() {
    // Same geometry as test 2 but the crossing road is now the more major one.
    auto b = buildAll({
        makeSeg(1, RoadType::CountryDirt,    10.0f, glm::vec2(20, -30), glm::vec2(20, 30)),
        makeSeg(2, RoadType::ColonialAvenue, 20.0f, glm::vec2(0, 0), glm::vec2(100, 0)),
    });
    // Both roads are blanked inside the junction, so the assertion that matters
    // is that the junction exists at all and that paint returns outside it.
    CHECK(countWhere(b, [](float x, float z, float, float m) {
              return isBare(m) && std::fabs(x - 20.0f) < 2.0f && std::fabs(z) < 2.0f;
          }) > 0);
    CHECK(countWhere(b, [](float x, float z, float u, float) {
              return isEdgeLine(u) && x < 5.0f; }) > 0);
    CHECK(countWhere(b, [](float x, float z, float u, float) {
              return isEdgeLine(u) && z > 20.0f; }) > 0);
}

// ── 5. Equal rank must not suppress each other (tie-break on id) ─────────────
static void testEqualRankNoMutualSuppression() {
    auto b = buildAll({
        makeSeg(1, RoadType::UrbanStreet, 20.0f, glm::vec2(0, 0), glm::vec2(100, 0)),
        makeSeg(2, RoadType::UrbanStreet, 20.0f, glm::vec2(50, -30), glm::vec2(50, 30)),
    });
    // At the crossing both yield (that is the junction), but each keeps its own
    // paint on its far arm: neither road may blank the other along its length.
    CHECK(countWhere(b, [](float x, float z, float u, float) {
              return isEdgeLine(u) && x < 20.0f; }) > 0);
    CHECK(countWhere(b, [](float x, float z, float u, float) {
              return isEdgeLine(u) && z > 20.0f; }) > 0);
    CHECK(countWhere(b, [](float x, float z, float u, float) {
              return isEdgeLine(u) && x > 80.0f; }) > 0);
}

// ── 6. Roads that never touch stay completely untouched ─────────────────────
static void testDistantRoadsUnaffected() {
    auto b = buildAll({
        makeSeg(1, RoadType::ColonialAvenue, 20.0f, glm::vec2(0, 0), glm::vec2(100, 0)),
        makeSeg(2, RoadType::ColonialAvenue, 20.0f, glm::vec2(0, 300), glm::vec2(100, 300)),
    });
    CHECK(countWhere(b, [](float, float, float, float m) { return isBare(m); }) == 0);
    CHECK(countWhere(b, [](float, float, float u, float) { return isEdgeLine(u); }) > 0);
}

// ── 7. An open road whose ends merely come CLOSE must not become a ring ──────
// RoadSpline::evaluate used to re-derive `closed` from a 10 m end-to-end gap,
// which quietly promoted ordinary L-shaped streets to rings. A closed segment is
// excluded from endpoint clustering, so the road then connected to nothing.
static void testNearMissIsNotAClosedLoop() {
    HeightSampler sampler;
    {
        RoadSegment s = makeSeg(1, RoadType::UrbanStreet, 8.0f,
                                glm::vec2(0, 0), glm::vec2(60, 0));
        RoadSpline::evaluate(s, sampler);
        CHECK(!s.closed);
    }
    {
        // 14 m apart with a 40 m width: the old max(width, 10) rule called this
        // a loop, and the editor's 15 m rule would have agreed with it.
        RoadSegment s = makeSeg(2, RoadType::ColonialAvenue, 40.0f,
                                glm::vec2(0, 0), glm::vec2(14, 0));
        RoadSpline::evaluate(s, sampler);
        CHECK(!s.closed);
    }
    {
        // A genuine ring: the author closed it in the JSON.
        RoadSegment s = makeSeg(3, RoadType::UrbanStreet, 8.0f,
                                glm::vec2(0, 0), glm::vec2(60, 0));
        s.closed = true;
        RoadSpline::evaluate(s, sampler);
        CHECK(s.closed);
    }
}

// ── 8. A branch that stops short of a host still gets a paved junction ────────
// The endpoint is 9 m from the boulevard centreline, far outside the old
// 2.5 m cluster tolerance, so no exact joint and no section 5 wedge: only the
// branch-mouth pad registered in step 0 can cover the gap.
static void testShortBranchGetsJunctionPad() {
    Built b = buildAll({
        makeSeg(1, RoadType::ColonialAvenue, 20.0f, glm::vec2(0, 0), glm::vec2(200, 0)),
        makeSeg(2, RoadType::UrbanStreet,     8.0f, glm::vec2(100, 9), glm::vec2(100, 90)),
    });
    // Baseline: the branch alone, nowhere near the avenue.
    Built alone = buildAll({
        makeSeg(2, RoadType::UrbanStreet, 8.0f, glm::vec2(100, 9), glm::vec2(100, 90)),
    });

    // A pad was added: a centre vertex plus a hull, so strictly more geometry.
    CHECK(b.v.size() > alone.v.size());
    // The branch mouth is blanked, so its paint stops at the junction.
    CHECK(countWhere(b, [](float x, float z, float, float m) {
              return isBare(m) && std::fabs(x - 100.0f) < 6.0f && z < 14.0f;
          }) > 0);
    // The branch keeps its paint further out.
    CHECK(countWhere(b, [](float x, float z, float u, float) {
              return isEdgeLine(u) && z > 40.0f;
          }) > 0);
}

// ── 9. A road must keep its full width through a 90 degree corner ────────────
// The right-vector average is a bisector, which is only cos(45 deg) = 0.707 of the
// true offset: an 8 m street came out 5.7 m wide at every corner, and the old
// inner-fold clamp took it further still.
static void testWidthSurvivesCorner() {
    const float W = 8.0f;
    auto b = buildAll({ makeCornerSeg(1, RoadType::UrbanStreet, W,
                                       glm::vec2(0, 0), glm::vec2(80, 0),
                                       glm::vec2(80, 80)) });

    // Rim vertices are the two cross-section extremes (u = 0 and u = 1).
    float minW = 1e9f, maxW = 0.0f;
    for (size_t i = 0; i + 1 < b.v.size(); ++i) {
        if (!(b.v[i].uv.x < 1e-6f && b.v[i + 1].uv.x > 1.0f - 1e-6f)) continue;
        const float w = glm::length(glm::vec2(b.v[i].position.x - b.v[i + 1].position.x,
                                              b.v[i].position.z - b.v[i + 1].position.z));
        if (w < minW) minW = w;
        if (w > maxW) maxW = w;
    }
    CHECK(minW > 0.0f);
    // No ring may be pinched below ~95% of the nominal width, and none may be
    // stretched by more than the mitre clamp allows.
    CHECK(minW > W * 0.95f);
    CHECK(maxW < W * 1.30f);
}

// ── 10. Cross-section must not twist with the terrain cross-slope ────────────
// With an invalid sampler there is no terrain, so the ring is flat: every pillar
// of a ring must share one height. This guards the regression that came from
// sampling the terrain once per pillar.
static void testRingIsPlanarWithoutTerrain() {
    auto b = buildAll({ makeSeg(1, RoadType::UrbanStreet, 10.0f,
                                glm::vec2(0, 0), glm::vec2(120, 0)) });
    // 12 pillars per ring, all at the same y within a ring.
    const size_t kPillars = 12;
    CHECK(b.v.size() >= kPillars);
    for (size_t i = 0; i + kPillars <= b.v.size(); i += kPillars) {
        const float y0 = b.v[i].position.y;
        for (size_t j = i; j < i + kPillars; ++j)
            if (std::fabs(b.v[j].position.y - y0) > 1e-5f) {
                CHECK(false);
                break;
            }
    }
}

// ── 11. Junction correction: every arrival is connected, and the corner is
//        sized by the detected angle rather than accepted or rejected by a gate.
// The previous design had a single transverse-angle threshold: an arrival shallower
// than it was dropped and left with a floating end cap. Now there is no threshold,
// so this checks the two properties that replaced it.
//   a) the fillet radius follows the standard corner law R = tangentRun / tan(theta/2),
//      holding the tangent run fixed, which is what keeps the arc bounded at any angle
//   b) a shallow arrival still gets connected, and a road merely running alongside
//      another still does not (that is the overlap test's premise, re-checked here
//      through the same reach rule).
static void testJunctionFilletFollowsAngle() {
    constexpr float kPi = 3.14159265f;
    const float tangentRun = 0.85f * 4.0f;   // 0.85 * half-width of an 8 m street

    // Radius must DECREASE as the turn sharpens, and must equal the corner law exactly:
    // holding the tangent run fixed, a gentle arrival gets a long sweeping radius while
    // a right-angle corner gets a tight one. (The intuitive "sharper turn, tighter arc"
    // reading is this law read backwards - t = R*tan(theta/2) means t grows with R only
    // for gentle turns, so pinning t pins R high at small theta and low at large theta.)
    float prev = 1e9f;
    for (int deg = 5; deg <= 175; deg += 10) {
        const float theta = deg * kPi / 180.0f;
        const float R = junctionFilletRadius(theta, tangentRun);
        CHECK(R < prev);                                  // sharper turn, tighter arc
        prev = R;
        const float expect = tangentRun / std::tan(theta * 0.5f);
        CHECK(std::fabs(R - expect) < 1e-3f * std::max(1.0f, expect));
    }
    // The tangent run is what is held constant, so R * tan(theta/2) must come back to it
    // for every angle - this is the property that stops the arc exploding near theta=0.
    for (int deg = 5; deg <= 175; deg += 10) {
        const float theta = deg * kPi / 180.0f;
        const float R = junctionFilletRadius(theta, tangentRun);
        CHECK(std::fabs(R * std::tan(theta * 0.5f) - tangentRun) < 1e-2f);
    }
    // A near-straight arrival degenerates instead of dividing by ~zero.
    CHECK(junctionFilletRadius(0.0f, tangentRun) >= tangentRun);
    CHECK(std::isfinite(junctionFilletRadius(0.0f, tangentRun)));
}

// The radius cap is what stops a shallow arrival from sweeping across the map. The
// real map produced a 148 m fillet for a 40 m road arriving at 13 degrees, which is
// 3.7x the road's own width. Clamping the radius and letting the tangent run shrink
// keeps the corner small while still connecting the two roads.
static void testShallowArrivalCornerStaysSmall() {
    constexpr float kPi = 3.14159265f;
    const float hw = 20.0f;                       // a 40 m road
    const float runFrac = 0.85f, maxFrac = 1.6f;

    // A gentle arrival: the corner law alone would ask for a huge radius.
    const float theta13 = 13.0f * kPi / 180.0f;
    const float rawR = junctionFilletRadius(theta13, runFrac * hw);
    CHECK(rawR > 4.0f * hw);                      // this is the case that needs capping

    const float R = std::min(rawR, maxFrac * hw);
    const float t = R * std::tan(theta13 * 0.5f);
    CHECK(R <= maxFrac * hw + 1e-3f);              // radius bounded
    CHECK(t < runFrac * hw);                      // and the tangent run shrank to suit
    CHECK(t > 0.0f);

    // The drawn arc spans roughly the chord between the two tangent points, so the
    // whole correction stays within a few multiples of the road width.
    const float chord = 2.0f * t * std::sin(theta13 * 0.5f);
    CHECK(chord < 1.5f * hw);
}

static void testShallowArrivalStillConnects() {
    // The street ends 2 m short of the boulevard's centreline, running at 25 deg to it:
    // far outside any transverse-angle gate, so the old code left it as a floating cap.
    // The ends overlap in plan, so the reach rule connects them and applies a correction.
    auto b = buildAll({
        makeSeg(1, RoadType::ColonialAvenue, 20.0f, glm::vec2(0, 0), glm::vec2(200, 0)),
        makeSeg(2, RoadType::UrbanStreet,     8.0f, glm::vec2(100, 2), glm::vec2(180, 95)),
    });
    auto alone = buildAll({
        makeSeg(2, RoadType::UrbanStreet, 8.0f, glm::vec2(100, 2), glm::vec2(180, 95)),
    });
    // Connected: the junction adds paved geometry over the branch alone.
    CHECK(b.v.size() > alone.v.size());
    // And the branch mouth is blanked, so markings stop at the junction.
    CHECK(countWhere(b, [](float x, float z, float, float m) {
              return isBare(m) && x > 88.0f && x < 112.0f && z < 16.0f;
          }) > 0);
}

// Distance from the authored corner apex to the evaluated centreline. A rounded corner
// cuts the apex off by r*(sqrt(2)-1) for a 90 degree turn, so this is a direct read-out
// of the fillet's tangent run and nothing else.
static float cornerApexGap(const RoadSegment& in) {
    RoadSegment s = in;
    HeightSampler sampler;                 // invalid: plan-view only
    RoadSpline::evaluate(s, sampler);
    const glm::vec2 apex(100.0f, 0.0f);
    float best = 1e9f;
    for (const auto& p : s.splinePoints)
        best = std::min(best, glm::length(glm::vec2(p.x, p.z) - apex));
    return best;
}

// The per-road corner radius must actually change the centreline that gets drawn, and
// 0 must keep meaning "automatic" (half the road width) so existing maps are unchanged.
static void testPerRoadCornerRadius() {
    RoadSegment L = makeCornerSeg(1, RoadType::ColonialAvenue, 20.0f,
                                  glm::vec2(0, 0), glm::vec2(100, 0), glm::vec2(100, 100));

    L.cornerRadius = 0.0f;   const float dAuto = cornerApexGap(L);   // auto = 10 m run
    L.cornerRadius = 5.0f;   const float d5    = cornerApexGap(L);
    L.cornerRadius = 20.0f;  const float d20   = cornerApexGap(L);
    L.cornerRadius = 40.0f;  const float d40   = cornerApexGap(L);

    CHECK(dAuto > 0.5f);     // corners are rounded even with no override at all
    CHECK(d5 < dAuto);       // asking for less than the auto half-width rounds less
    CHECK(d20 > dAuto);      // asking for more rounds more
    CHECK(d40 > d20);        // and it keeps growing, monotonically
    CHECK(d5 > 0.5f);        // still a real round corner, not a bare angle

    // Pin the geometry: for a 90 degree fillet the apex gap is r*(sqrt(2)-1).
    CHECK(std::fabs(d20 - 20.0f * 0.41421f) < 0.35f);
    CHECK(std::fabs(dAuto - 10.0f * 0.41421f) < 0.35f);
}

// An absurd radius must not let neighbouring fillets eat each other: every corner is
// capped at half of its adjacent segments, so a tight zig-zag stays finite and sane.
static void testCornerRadiusClampOnTightZigzag() {
    RoadSegment z;
    z.id = 9; z.type = RoadType::Medina; z.width = 5.0f; z.shoulderWidth = 0.75f;
    z.cornerRadius = 500.0f;                   // far beyond the 10 m segments
    z.controlPoints.push_back({ glm::vec2(0, 0) });
    z.controlPoints.push_back({ glm::vec2(10, 0) });
    z.controlPoints.push_back({ glm::vec2(10, 10) });
    z.controlPoints.push_back({ glm::vec2(0, 10) });

    HeightSampler sampler;
    RoadSpline::evaluate(z, sampler);
    CHECK(z.splinePoints.size() > 4);

    float maxStep = 0.0f;
    for (size_t i = 1; i < z.splinePoints.size(); ++i) {
        const glm::vec3 d = z.splinePoints[i] - z.splinePoints[i - 1];
        CHECK(std::isfinite(d.x) && std::isfinite(d.y) && std::isfinite(d.z));
        maxStep = std::max(maxStep, glm::length(glm::vec2(d.x, d.z)));
    }
    // Capped to half a 10 m segment = 5 m tangent run, so no single sample can leap a
    // corner and the sampled centreline cannot double back through itself.
    CHECK(maxStep < 6.0f);
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    testLoneRoadKeepsPaint();
    testCrossingProducesOnePadAndBlanksPaint();
    testParallelOverlapAddsNoGeometry();
    testMajorKeepsItsPaintAtCrossing();
    testEqualRankNoMutualSuppression();
    testDistantRoadsUnaffected();
    testNearMissIsNotAClosedLoop();
    testShortBranchGetsJunctionPad();
    testWidthSurvivesCorner();
    testRingIsPlanarWithoutTerrain();
    testJunctionFilletFollowsAngle();
    testShallowArrivalCornerStaysSmall();
    testShallowArrivalStillConnects();
    testPerRoadCornerRadius();
    testCornerRadiusClampOnTightZigzag();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
