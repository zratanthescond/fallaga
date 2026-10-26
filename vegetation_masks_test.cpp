#include "vegetation/VegetationMasks.h"

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

bool near(float a, float b, float epsilon = 0.001f) {
    return std::fabs(a - b) <= epsilon;
}

} // namespace

int main() {
    vegetation::BuildParams params;
    params.minX = 0.0f;
    params.maxX = 640.0f;
    params.minZ = 0.0f;
    params.maxZ = 640.0f;
    params.resolution = 64;
    params.waterLevel = 0.0f;

    params.heightAt = [](float, float) { return 10.0f; };
    params.zoneAt = [](float x, float) {
        if (x < 20.0f) return 0; // SEA
        if (x < 100.0f) return 2; // MEDINA
        if (x < 160.0f) return 1; // PORT
        if (x < 220.0f) return 3; // WADI
        if (x < 280.0f) return 6; // BEACH
        return 5;                 // PLAIN
    };
    params.roadAt = [](float, float z) {
        return z >= 300.0f && z <= 330.0f;
    };
    params.steppeAt = [](float x, float) {
        return x > 320.0f ? 1.0f : 0.0f;
    };
    params.oliveAt = [](float x, float) {
        return x > 500.0f ? 0.9f : 0.0f;
    };
    params.duneAt = [](float x, float) {
        return (x > 160.0f && x < 340.0f) ? 0.8f : 0.0f;
    };
    params.exclusionAt = [](float, float z) {
        return (z > 500.0f && z < 540.0f) ? 1.0f : 0.0f;
    };

    vegetation::BuildingFootprint building;
    building.center = glm::vec2(320.0f, 220.0f);
    building.halfExtents = glm::vec2(18.0f, 12.0f);
    params.buildings.push_back(building);

    vegetation::BuildingFootprint rotatedBuilding;
    rotatedBuilding.center = glm::vec2(400.0f, 400.0f);
    rotatedBuilding.halfExtents = glm::vec2(20.0f, 10.0f);
    rotatedBuilding.cosAngle = 0.0f;
    rotatedBuilding.sinAngle = 1.0f;
    params.buildings.push_back(rotatedBuilding);

    vegetation::VegetationMasks masks;
    check(masks.build(params), "mask build succeeds");
    check(masks.ready(), "mask is ready");
    check(masks.width() == 64 && masks.height() == 64, "mask resolution");
    check(masks.indexAt(5.0f, 5.0f) == 0, "min edge maps to first cell");
    check(masks.indexAt(635.0f, 635.0f) == 63 * 64 + 63,
          "max edge maps to last cell");
    check(masks.habitatAt(50.0f, 50.0f) == vegetation::Habitat::Urban,
          "urban zone classification");
    check(masks.habitatAt(120.0f, 50.0f) == vegetation::Habitat::Port,
          "port zone classification");
    check(masks.habitatAt(180.0f, 50.0f) == vegetation::Habitat::Wadi,
          "wadi zone classification");
    check(masks.habitatAt(240.0f, 50.0f) == vegetation::Habitat::Beach,
          "beach zone classification");
    check(masks.habitatAt(360.0f, 50.0f) == vegetation::Habitat::Steppe,
          "steppe classification");
    check(masks.habitatAt(10.0f, 50.0f) == vegetation::Habitat::Water,
          "water zone classification");

    check(masks.blockedAt(10.0f, 50.0f), "water is blocked");
    check(!masks.blockedAt(50.0f, 50.0f),
          "urban zone is sparse, not hard-blocked");
    check(masks.blockedAt(120.0f, 50.0f), "port is blocked");
    check(masks.blockedAt(200.0f, 310.0f), "road is blocked");
    check(masks.blockedAt(320.0f, 220.0f), "building is blocked");
    check(masks.blockedAt(405.0f, 400.0f),
          "rotated building local X axis is blocked");
    check(masks.blockedAt(400.0f, 405.0f),
          "rotated building local Z axis is blocked");
    check(!masks.blockedAt(420.0f, 400.0f),
          "outside rotated building is not blocked");
    check(masks.blockedAt(200.0f, 520.0f), "painted exclusion is blocked");
    check(!masks.blockedAt(180.0f, 50.0f),
          "wadi is sparse, not hard-blocked");
    check(!masks.blockedAt(240.0f, 50.0f),
          "beach is sparse, not hard-blocked");

    check(masks.densityAt(360.0f, 50.0f) > 0.0f, "open steppe has density");
    check(masks.densityAt(180.0f, 50.0f) > 0.0f, "wadi keeps some cover");
    check(masks.densityAt(240.0f, 50.0f) > 0.0f, "beach keeps sparse cover");
    check(masks.densityAt(180.0f, 50.0f) >
              masks.densityAt(240.0f, 50.0f),
          "wadi is denser than beach");
    check(masks.densityAt(360.0f, 50.0f) >
              masks.densityAt(180.0f, 50.0f),
          "open steppe is denser than wadi");
    check(near(masks.densityAt(10.0f, 50.0f), 0.0f),
          "water density is suppressed");
    check(near(masks.densityAt(120.0f, 50.0f), 0.0f),
          "port density is suppressed");
    check(near(masks.densityAt(200.0f, 310.0f), 0.0f),
          "road density is suppressed");
    check(masks.densityAt(540.0f, 50.0f) > 0.0f,
          "olive grove has density");

    check(masks.exclusionAt(200.0f, 520.0f) > 0.9f,
          "exclusion mask value");
    check(masks.flagsAt(200.0f, 310.0f) & vegetation::MaskRoad,
          "road flag");
    check(masks.flagsAt(320.0f, 220.0f) & vegetation::MaskBuilding,
          "building flag");

    const auto& pixels = masks.debugPixels(0);
    check(pixels.size() == 64u * 64u * 4u, "debug pixel buffer size");
    check(masks.debugPixels(2).size() == pixels.size(),
          "debug layer switch size");
    check(masks.habitatName(vegetation::Habitat::Cultivated) != nullptr,
          "habitat name lookup");

    if (failures == 0) {
        std::printf("vegetation_masks_test: OK\n");
    }
    return failures == 0 ? 0 : 1;
}
