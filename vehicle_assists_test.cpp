#include "src/vehicle/VehicleBrakes.h"
#include "src/vehicle/VehicleConfig.h"
#include "src/vehicle/VehicleDrivetrain.h"
#include "src/vehicle/VehicleStability.h"
#include "src/vehicle/VehicleSurface.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

int main() {
  const std::array<float, 4> torques{2.0f, 2.0f, 4.0f, 4.0f};
  const std::array<float, 4> wheelSpeeds{20.0f, 10.0f, 10.0f, 20.0f};
  const auto open = applyLimitedSlipDifferentials(torques, wheelSpeeds, 0.0f, 0.0f);
  const auto locked = applyLimitedSlipDifferentials(torques, wheelSpeeds, 0.5f, 0.5f);
  if (open != torques || !(locked[0] < 2.0f && locked[1] > 2.0f) ||
      !(locked[2] > 4.0f && locked[3] < 4.0f) ||
      std::abs(locked[0] + locked[1] - 4.0f) > 1.0e-5f ||
      std::abs(locked[2] + locked[3] - 8.0f) > 1.0e-5f) {
    std::fprintf(stderr, "FAIL: limited-slip should bias torque and preserve axle totals\n");
    return 1;
  }

  if (std::abs(serviceBrakeScale(10.0f, 10.0f, true, 0.2f) - 1.0f) > 1.0e-5f ||
      serviceBrakeScale(10.0f, 0.0f, true, 0.2f) > 0.09f ||
      serviceBrakeScale(-10.0f, 0.0f, true, 0.2f) > 0.09f ||
      serviceBrakeScale(10.0f, 0.0f, false, 0.2f) != 1.0f ||
      serviceBrakeScale(0.5f, 0.0f, true, 0.2f) != 1.0f) {
    std::fprintf(stderr, "FAIL: ABS should release locked wheels above low speed\n");
    return 1;
  }

  const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
  const glm::vec3 tiltedUp =
      glm::angleAxis(glm::radians(70.0f), glm::vec3(0.0f, 0.0f, 1.0f)) * worldUp;
  const glm::vec3 antiFlip = calculateAntiFlipTorque(
      tiltedUp, glm::vec3(0.0f), true, 0.96f, 18000.0f, 4000.0f);
  const glm::vec3 mildTiltUp =
      glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 0.0f, 1.0f)) * worldUp;
  if (antiFlip.z >= 0.0f ||
      glm::length(calculateAntiFlipTorque(mildTiltUp, glm::vec3(0.0f), true,
                                           0.96f, 18000.0f, 4000.0f)) > 1.0e-5f ||
      glm::length(calculateAntiFlipTorque(tiltedUp, glm::vec3(0.0f), false,
                                           0.96f, 18000.0f, 4000.0f)) > 1.0e-5f) {
    std::fprintf(stderr, "FAIL: anti-flip should correct severe grounded tilt only\n");
    return 1;
  }

  VehicleConfig config;
  if (std::abs(vehicleSurfaceGrip(VehicleSurfaceType::Asphalt, config) - 1.0f) > 1.0e-5f ||
      std::abs(vehicleSurfaceGrip(VehicleSurfaceType::Cobblestone, config) - 0.9f) > 1.0e-5f ||
      std::abs(vehicleSurfaceGrip(VehicleSurfaceType::Dust, config) - 0.55f) > 1.0e-5f ||
      std::abs(vehicleSurfaceGrip(VehicleSurfaceType::Sand, config) - 0.5f) > 1.0e-5f ||
      std::abs(vehicleSurfaceGrip(VehicleSurfaceType::Water, config) - 0.2f) > 1.0e-5f) {
    std::fprintf(stderr, "FAIL: surface grip presets should stay independently tunable\n");
    return 1;
  }
  return 0;
}
