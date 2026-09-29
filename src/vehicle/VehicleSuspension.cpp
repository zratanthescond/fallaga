#include "VehicleSuspension.h"

#include <algorithm>
#include <cmath>

namespace {
float clampLoad(float load, float maximum) {
  return std::clamp(load, 0.0f, std::max(0.0f, maximum));
}

void applyAntiRoll(const std::array<SuspensionWheelInput, 4> &wheels,
                   std::array<float, 4> &loads, int left, int right,
                   float stiffness, float maximum) {
  if (!wheels[left].hasGroundContact || !wheels[right].hasGroundContact)
    return;
  const float transfer =
      (wheels[left].compressionM - wheels[right].compressionM) * stiffness;
  loads[left] = clampLoad(loads[left] - transfer, maximum);
  loads[right] = clampLoad(loads[right] + transfer, maximum);
}
} // namespace

std::array<float, 4> calculateSuspensionLoads(
    const std::array<SuspensionWheelInput, 4> &wheels,
    const SuspensionSettings &settings) {
  std::array<float, 4> loads{};
  for (size_t i = 0; i < wheels.size(); ++i) {
    const auto &wheel = wheels[i];
    if (!wheel.hasGroundContact) continue;

    const float damping = wheel.compressionVelocityMps >= 0.0f
                              ? settings.compressionDampingNsPerM
                              : settings.reboundDampingNsPerM;
    float load = wheel.compressionM * settings.springRateNPerM +
                 wheel.compressionVelocityMps * damping;
    const float bumpCompression =
        std::max(0.0f, wheel.compressionM - settings.bumpStopStartCompressionM);
    load += bumpCompression * bumpCompression *
            settings.bumpStopQuadraticStiffnessNPerM2;
    loads[i] = clampLoad(load, settings.maxLoadN);
  }

  applyAntiRoll(wheels, loads, 0, 1,
                settings.frontAntiRollStiffnessNPerM, settings.maxLoadN);
  applyAntiRoll(wheels, loads, 2, 3,
                settings.rearAntiRollStiffnessNPerM, settings.maxLoadN);
  return loads;
}
