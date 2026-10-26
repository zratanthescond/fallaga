#pragma once
// BiomeMasks: derives meadow / grove / flower / rock weights from the heightmap itself
// (slope, curvature, flow-accumulation moisture, distance to water), so vegetation and
// ground colour are rule-driven instead of hand-painted PNGs.
//
// Order: EnvironmentMap::build -> BiomeMasks::build (needs env for water distance)
//        -> rebuildVegetationMasks (consume meadow()/grove()/flowers()/rock())
//        -> EnvironmentMap::updateCover.
#include <cstdint>
#include <functional>
#include <vector>
#include <glm/glm.hpp>

namespace env {
class EnvironmentMap;

enum class LushRegionMode { WholeMap, AltitudeBand, PaintedMask };

struct BiomeParams {
  float minX = 0, maxX = 1, minZ = 0, maxZ = 1;
  int resolution = 1024;
  float waterLevel = 0.0f;
  std::function<float(float, float)> heightAt;     // required
  const EnvironmentMap* env = nullptr;             // optional: water distance, in-water test
  unsigned seed = 1337u;

  // Where the lush look applies. DEFAULT WholeMap; the owner has not decided yet
  // (whole map vs a region such as the lake valley), so keep it switchable.
  LushRegionMode regionMode = LushRegionMode::WholeMap;
  float bandMinAboveWater = 1.5f, bandMaxAboveWater = 220.0f;  // AltitudeBand
  std::function<float(float, float)> paintedRegionAt;          // PaintedMask, 0..1

  // Rules (degrees / metres).
  float rockStartDeg = 28.0f, rockFullDeg = 42.0f;
  float groveClusterMetres = 120.0f;
  float flowerPatchMetres = 28.0f;
  float moistureWaterReach = 120.0f;
};

class BiomeMasks {
public:
  bool build(const BiomeParams& p);
  bool ready() const { return n_ > 0; }
  uint32_t revision() const { return revision_; }

  float meadow(float x, float z) const  { return s(0, x, z); }
  float grove(float x, float z) const   { return s(1, x, z); }
  float flowers(float x, float z) const { return s(2, x, z); }
  float rock(float x, float z) const    { return s(3, x, z); }
  float moisture(float x, float z) const{ return s(4, x, z); }
  float slopeDeg(float x, float z) const{ return s(5, x, z) * 90.0f; }

  // R meadow, G grove, B flowers, A rock. Upload as a second GL_RGBA8 texture.
  const std::vector<uint8_t>& packedRGBA() const { return rgba_; }
  glm::vec4 worldRect() const { return {minX_, minZ_, maxX_ - minX_, maxZ_ - minZ_}; }
  int resolution() const { return n_; }

private:
  float s(int c, float x, float z) const;
  int n_ = 0;
  uint32_t revision_ = 0;
  float minX_ = 0, maxX_ = 1, minZ_ = 0, maxZ_ = 1;
  std::vector<float> ch_[6];     // meadow, grove, flowers, rock, moisture, slope/90
  std::vector<uint8_t> rgba_;
};
} // namespace env
