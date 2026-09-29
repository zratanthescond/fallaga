#include "src/vehicle/VehicleSuspension.h"

#include <array>
#include <cmath>
#include <cstdio>

int main() {
  SuspensionSettings settings;
  settings.compressionDampingNsPerM = 0.0f;
  settings.frontAntiRollStiffnessNPerM = 5000.0f;
  settings.rearAntiRollStiffnessNPerM = 3500.0f;

  std::array<SuspensionWheelInput, 4> wheels{};
  wheels[0] = {0.12f, 0.0f, true};
  wheels[1] = {0.04f, 0.0f, true};
  const auto loads = calculateSuspensionLoads(wheels, settings);
  if (std::abs(loads[0] - 4160.0f) > 0.1f ||
      std::abs(loads[1] - 1920.0f) > 0.1f ||
      std::abs(loads[0] + loads[1] - 6080.0f) > 0.1f ||
      loads[2] != 0.0f || loads[3] != 0.0f) {
    std::fprintf(stderr, "FAIL: anti-roll should transfer front axle load only\n");
    return 1;
  }

  wheels = {};
  wheels[0] = {0.22f, 0.0f, true};
  wheels[1] = {0.22f, 0.0f, true};
  const auto bumpLoads = calculateSuspensionLoads(wheels, settings);
  if (std::abs(bumpLoads[0] - 9008.0f) > 0.1f ||
      std::abs(bumpLoads[1] - 9008.0f) > 0.1f) {
    std::fprintf(stderr, "FAIL: bump stops should add progressive compression force\n");
    return 1;
  }

  wheels[0].hasGroundContact = false;
  wheels[1].hasGroundContact = false;
  const auto airborneLoads = calculateSuspensionLoads(wheels, settings);
  if (airborneLoads[0] != 0.0f || airborneLoads[1] != 0.0f) {
    std::fprintf(stderr, "FAIL: airborne wheels should not carry tire load\n");
    return 1;
  }

  wheels = {};
  wheels[0] = {0.10f, -0.5f, true};
  wheels[1] = {0.10f, -0.5f, true};
  settings.compressionDampingNsPerM = 4200.0f;
  settings.reboundDampingNsPerM = 6300.0f;
  const auto reboundLoads = calculateSuspensionLoads(wheels, settings);
  if (std::abs(reboundLoads[0] - 650.0f) > 0.1f ||
      std::abs(reboundLoads[1] - 650.0f) > 0.1f) {
    std::fprintf(stderr, "FAIL: rebound damping should use its own coefficient\n");
    return 1;
  }
  return 0;
}
