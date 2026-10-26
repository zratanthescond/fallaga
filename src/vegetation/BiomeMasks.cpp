#include "BiomeMasks.h"
#include "EnvironmentMap.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace env {
namespace {
inline float smooth(float a, float b, float x) {
  if (b <= a) return x >= b ? 1.0f : 0.0f;
  const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}
inline float hash01(int x, int y, unsigned seed) {
  unsigned h = unsigned(x) * 374761393u + unsigned(y) * 668265263u + seed * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return float((h ^ (h >> 16)) & 0xFFFFFFu) / float(0x1000000);
}
float vnoise(float x, float z, float freq, unsigned seed) {
  const float fx = x * freq, fz = z * freq;
  const int i = int(std::floor(fx)), j = int(std::floor(fz));
  const float tx = fx - i, tz = fz - j;
  const float sx = tx * tx * (3 - 2 * tx), sz = tz * tz * (3 - 2 * tz);
  const float a = hash01(i, j, seed), b = hash01(i + 1, j, seed);
  const float c = hash01(i, j + 1, seed), d = hash01(i + 1, j + 1, seed);
  return (a + (b - a) * sx) * (1 - sz) + (c + (d - c) * sx) * sz;
}
inline uint8_t to8(float v) { return uint8_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); }
} // namespace

bool BiomeMasks::build(const BiomeParams& p) {
  if (!p.heightAt || p.resolution < 16 || p.maxX <= p.minX || p.maxZ <= p.minZ) return false;
  const int N = p.resolution;
  const size_t count = size_t(N) * N;
  minX_ = p.minX; maxX_ = p.maxX; minZ_ = p.minZ; maxZ_ = p.maxZ;
  const float cx = (maxX_ - minX_) / float(N - 1), cz = (maxZ_ - minZ_) / float(N - 1);
  const float cell = 0.5f * (cx + cz);

  std::vector<float> h(count);
  for (int j = 0; j < N; ++j)
    for (int i = 0; i < N; ++i) h[size_t(j) * N + i] = p.heightAt(minX_ + i * cx, minZ_ + j * cz);
  auto H = [&](int i, int j) { return h[size_t(std::clamp(j, 0, N - 1)) * N + std::clamp(i, 0, N - 1)]; };

  // Slope (degrees) and curvature (+ = hollow, - = ridge).
  std::vector<float> slope(count), curv(count);
  for (int j = 0; j < N; ++j)
    for (int i = 0; i < N; ++i) {
      const float gx = (H(i + 1, j) - H(i - 1, j)) / (2 * cx);
      const float gz = (H(i, j + 1) - H(i, j - 1)) / (2 * cz);
      slope[size_t(j) * N + i] = glm::degrees(std::atan(std::sqrt(gx * gx + gz * gz)));
      curv[size_t(j) * N + i] =
          (H(i - 1, j) + H(i + 1, j) + H(i, j - 1) + H(i, j + 1) - 4 * H(i, j)) / (cell * cell);
    }

  // D8 flow accumulation: highest cells first, each passes its load to its lowest neighbour.
  std::vector<uint32_t> order(count);
  std::iota(order.begin(), order.end(), 0u);
  std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return h[a] > h[b]; });
  std::vector<float> acc(count, 1.0f);
  for (uint32_t k : order) {
    const int i = int(k % N), j = int(k / N);
    float best = h[k]; int bi = -1, bj = -1;
    for (int dj = -1; dj <= 1; ++dj)
      for (int di = -1; di <= 1; ++di) {
        if (!di && !dj) continue;
        const int ni = i + di, nj = j + dj;
        if (ni < 0 || nj < 0 || ni >= N || nj >= N) continue;
        const float hv = h[size_t(nj) * N + ni];
        if (hv < best) { best = hv; bi = ni; bj = nj; }
      }
    if (bi >= 0) acc[size_t(bj) * N + bi] += acc[k];
  }
  float accMax = 1.0f;
  for (float a : acc) accMax = std::max(accMax, a);
  const float logMax = std::log(1.0f + accMax);

  for (auto& c : ch_) c.assign(count, 0.0f);
  rgba_.assign(count * 4, 0);
  for (int j = 0; j < N; ++j)
    for (int i = 0; i < N; ++i) {
      const size_t k = size_t(j) * N + i;
      const float x = minX_ + i * cx, z = minZ_ + j * cz;
      const float above = h[k] - p.waterLevel;
      const bool wet = p.env ? p.env->inWater(x, z) : (above <= 0.0f);
      const float wdist = p.env ? p.env->waterDistance(x, z) : 1e4f;

      // Moisture: upstream flow + closeness to water + hollows.
      const float flow01 = std::log(1.0f + acc[k]) / logMax;
      const float waterProx = 1.0f - smooth(0.0f, p.moistureWaterReach, wdist);
      const float hollow = smooth(-0.002f, 0.01f, curv[k]);
      const float moisture = std::clamp(0.5f * flow01 + 0.35f * waterProx + 0.15f * hollow, 0.0f, 1.0f);

      // Region gate for the lush look.
      float region = 1.0f;
      if (p.regionMode == LushRegionMode::AltitudeBand)
        region = smooth(p.bandMinAboveWater, p.bandMinAboveWater + 4.0f, above) *
                 (1.0f - smooth(p.bandMaxAboveWater * 0.8f, p.bandMaxAboveWater, above));
      else if (p.regionMode == LushRegionMode::PaintedMask && p.paintedRegionAt)
        region = std::clamp(p.paintedRegionAt(x, z), 0.0f, 1.0f);
      if (wet) region = 0.0f;

      const float sd = slope[k];
      const float rock = smooth(p.rockStartDeg, p.rockFullDeg, sd);
      const float cluster = vnoise(x, z, 1.0f / p.groveClusterMetres, p.seed);
      // Groves like moderate slopes and flanks; a few scattered trees stay on flats.
      float grove = smooth(0.40f, 0.68f, cluster) * (0.35f + 0.65f * smooth(3.0f, 12.0f, sd)) *
                    (0.45f + 0.55f * moisture) * (1.0f - rock);
      grove *= region;
      float meadow = (1.0f - rock) * (1.0f - 0.85f * grove) * (0.35f + 0.65f * moisture) * region;
      const float patch = vnoise(x, z, 1.0f / p.flowerPatchMetres, p.seed + 11u);
      float flowers = smooth(0.62f, 0.78f, patch) * meadow * smooth(0.05f, 0.30f, moisture) *
                      (1.0f - smooth(8.0f, 16.0f, sd));

      ch_[0][k] = meadow; ch_[1][k] = grove; ch_[2][k] = flowers;
      ch_[3][k] = rock * (wet ? 0.0f : 1.0f); ch_[4][k] = moisture; ch_[5][k] = sd / 90.0f;
      rgba_[k * 4 + 0] = to8(meadow); rgba_[k * 4 + 1] = to8(grove);
      rgba_[k * 4 + 2] = to8(flowers); rgba_[k * 4 + 3] = to8(ch_[3][k]);
    }
  n_ = N;
  ++revision_;
  return true;
}

float BiomeMasks::s(int c, float x, float z) const {
  if (!ready()) return 0.0f;
  const int N = n_;
  const float fx = std::clamp((x - minX_) / (maxX_ - minX_), 0.0f, 1.0f) * (N - 1);
  const float fz = std::clamp((z - minZ_) / (maxZ_ - minZ_), 0.0f, 1.0f) * (N - 1);
  const int i0 = std::min(int(fx), N - 2), j0 = std::min(int(fz), N - 2);
  const float tx = fx - i0, tz = fz - j0;
  const auto& a = ch_[c];
  auto g = [&](int i, int j) { return a[size_t(j) * N + i]; };
  return (g(i0, j0) * (1 - tx) + g(i0 + 1, j0) * tx) * (1 - tz) +
         (g(i0, j0 + 1) * (1 - tx) + g(i0 + 1, j0 + 1) * tx) * tz;
}
} // namespace env
