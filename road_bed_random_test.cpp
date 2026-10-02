// Isolated randomized regression test for the terrain mask against the road
// ribbon's actual mitered cross-sections. This includes the production helper
// without constructing Terrain or requiring an OpenGL context.
#include "src/RoadSystem.cpp"

#include <cmath>
#include <cstdio>
#include <random>

int main() {
    std::mt19937 rng(0x5EEDBEEFu);
    std::uniform_real_distribution<float> start(-500.0f, 500.0f);
    std::uniform_real_distribution<float> lengthDist(18.0f, 45.0f);
    std::uniform_real_distribution<float> turnDist(-0.35f, 0.35f);
    std::uniform_real_distribution<float> widthDist(6.0f, 18.0f);
    std::uniform_real_distribution<float> elevDist(-20.0f, 60.0f);
    std::uniform_real_distribution<float> tDist(0.08f, 0.92f);
    std::uniform_real_distribution<float> sideDist(-0.98f, 0.98f);

    constexpr int kRoads = 1200;
    constexpr int kSamplesPerEdge = 12;
    constexpr float kCoverageTolerance = 0.20f;
    int sampleCount = 0;
    int uncovered = 0;
    int oldCenterlineMaskMisses = 0;
    float largestMiss = 0.0f;
    int gradeViolations = 0;
    int endpointDrift = 0;
    int overlapOrderFailures = 0;
    int closedSeamGradeViolations = 0;

    for (int r = 0; r < kRoads; ++r) {
        RoadSegment road;
        road.id = (uint32_t)r;
        road.width = widthDist(rng);
        road.shoulderWidth = 3.0f;
        const int count = 3 + (int)(rng() % 10);
        float x = start(rng), z = start(rng);
        float heading = turnDist(rng) * 8.0f;
        for (int i = 0; i < count; ++i) {
            if (i > 0) heading += turnDist(rng);
            const float y = elevDist(rng);
            road.splinePoints.emplace_back(x, y, z);
            x += std::cos(heading) * lengthDist(rng);
            z += std::sin(heading) * lengthDist(rng);
        }

        road.tangents.resize(road.splinePoints.size());
        for (size_t i = 0; i < road.splinePoints.size(); ++i) {
            const size_t a = (i == 0) ? i : i - 1;
            const size_t b = (i + 1 < road.splinePoints.size()) ? i + 1 : i;
            glm::vec2 tangent(road.splinePoints[b].x - road.splinePoints[a].x,
                              road.splinePoints[b].z - road.splinePoints[a].z);
            tangent = glm::normalize(tangent);
            road.tangents[i] = glm::vec3(tangent.x, 0.0f, tangent.y);
        }
        computeRoadFrames(road.rightVecs, road.tangents, false);

        for (size_t i = 0; i + 1 < road.splinePoints.size(); ++i) {
            for (int ti = 0; ti < kSamplesPerEdge; ++ti) {
                const float t = tDist(rng);
                const float side = sideDist(rng);
                const glm::vec3 center = glm::mix(road.splinePoints[i], road.splinePoints[i + 1], t);
                const glm::vec3 right = glm::mix(road.rightVecs[i], road.rightVecs[i + 1], t);
                const float hw0 = RoadSpline::meshHalfWidth(road, (int)i, false, side, road.width * 0.5f);
                const float hw1 = RoadSpline::meshHalfWidth(road, (int)i + 1, false, side, road.width * 0.5f);
                const float useHw = glm::mix(hw0, hw1, t);
                // Same cross-section equation as RoadMeshBuilder, including
                // its inner-fold correction at sharp bends.
                const glm::vec3 edgePoint = center + right * (side * useHw);
                const RoadBedSample sample = nearestRoadBed(road, edgePoint.x, edgePoint.z);
                ++sampleCount;
                // Baseline: the old circular mask used width/2 from the
                // centerline and did not account for miter expansion.
                if (sample.centerDistance > road.width * 0.5f + kCoverageTolerance)
                    ++oldCenterlineMaskMisses;
                if (sample.edgeDistance > kCoverageTolerance) {
                    ++uncovered;
                    largestMiss = std::max(largestMiss, sample.edgeDistance);
                    if (uncovered <= 8)
                        std::printf(" miss road=%d edge=%d t=%.3f side=%.3f center=%.3f bedEdge=%.3f half=%.3f width=%.2f\n",
                                    r, (int)i, t, side, sample.centerDistance, sample.edgeDistance,
                                    sample.halfWidth, road.width);
                }
            }
        }
    }

    // Feed noisy elevation profiles through the same production smoother used
    // by RoadSpline::evaluate and verify grade limits plus anchored endpoints.
    std::uniform_real_distribution<float> profileY(-120.0f, 180.0f);
    std::uniform_real_distribution<float> profileGrade(-0.04f, 0.04f);
    std::uniform_real_distribution<float> profileNoise(-4.0f, 4.0f);
    for (int r = 0; r < 1000; ++r) {
        std::vector<glm::vec3> profile;
        const int count = 8 + (int)(rng() % 48);
        float x = 0.0f, z = 0.0f;
        const float baseY = profileY(rng);
        const float grade = profileGrade(rng);
        float distance = 0.0f;
        for (int i = 0; i < count; ++i) {
            const float noise = (i == 0 || i == count - 1) ? 0.0f : profileNoise(rng);
            profile.emplace_back(x, baseY + grade * distance + noise, z);
            const float dx = lengthDist(rng);
            const float dz = turnDist(rng) * 10.0f;
            distance += std::sqrt(dx * dx + dz * dz);
            x += dx;
            z += dz;
        }
        const float firstY = profile.front().y;
        const float lastY = profile.back().y;
        RoadSpline::smoothElevation(profile, 0.08f, 3.0f);
        if (std::fabs(profile.front().y - firstY) > 1e-5f ||
            std::fabs(profile.back().y - lastY) > 1e-5f)
            ++endpointDrift;
        for (size_t i = 1; i < profile.size(); ++i) {
            const float run = glm::length(glm::vec2(profile[i].x - profile[i - 1].x,
                                                    profile[i].z - profile[i - 1].z));
            if (run > 0.01f && std::fabs(profile[i].y - profile[i - 1].y) / run > 0.0801f)
                ++gradeViolations;
        }
    }

    // Closed roads use cyclic distance smoothing and must satisfy grade across
    // the final-to-first segment too, including irregularly sampled rings.
    for (int r = 0; r < 500; ++r) {
        const int count = 12 + (int)(rng() % 89);
        const float radius = 20.0f + (float)(rng() % 100);
        const float phase = profileY(rng);
        std::vector<glm::vec3> loop;
        loop.reserve(count);
        for (int i = 0; i < count; ++i) {
            const float angle = 6.28318530718f * i / count;
            const float noise = profileNoise(rng) * 0.1f;
            loop.emplace_back(radius * std::cos(angle),
                              phase + 2.0f * std::sin(angle * 2.0f) + noise,
                              radius * std::sin(angle));
        }
        RoadSpline::smoothElevation(loop, 0.08f, 3.0f, true);
        const float seamRun = glm::length(glm::vec2(loop.front().x - loop.back().x,
                                                    loop.front().z - loop.back().z));
        if (seamRun > 0.01f &&
            std::fabs(loop.front().y - loop.back().y) / seamRun > 0.0801f)
            ++closedSeamGradeViolations;
    }

    std::uniform_real_distribution<float> bedY(-80.0f, 150.0f);
    std::uniform_real_distribution<float> influenceDist(0.01f, 1.0f);
    for (int r = 0; r < 5000; ++r) {
        const float base = bedY(rng);
        const int overlaps = 2 + (int)(rng() % 7);
        std::vector<std::pair<float, float>> candidates;
        float expected = 1e9f;
        for (int i = 0; i < overlaps; ++i) {
            const float target = bedY(rng);
            const float influence = influenceDist(rng);
            candidates.emplace_back(target, influence);
            expected = std::min(expected, glm::mix(base, target, influence));
        }
        std::shuffle(candidates.begin(), candidates.end(), rng);
        float offset = 0.0f, influence = 0.0f;
        for (const auto& candidate : candidates)
            accumulateLowestRoadBed(base, candidate.first, candidate.second, offset, influence);
        if (std::fabs((base + offset) - expected) > 1e-4f)
            ++overlapOrderFailures;
    }

    std::printf("Random road-bed checks: roads=%d edgeSamples=%d oldCenterlineMisses=%d newUncovered=%d largestMiss=%.4fm; profiles=1000 gradeViolations=%d endpointDrift=%d; closedLoops=500 seamGradeViolations=%d; overlapSets=5000 orderFailures=%d\n",
                kRoads, sampleCount, oldCenterlineMaskMisses, uncovered, largestMiss,
                gradeViolations, endpointDrift, closedSeamGradeViolations, overlapOrderFailures);
    return uncovered == 0 && gradeViolations == 0 && endpointDrift == 0 &&
           closedSeamGradeViolations == 0 && overlapOrderFailures == 0 ? 0 : 1;
}
