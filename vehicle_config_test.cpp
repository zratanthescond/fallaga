#include "src/vehicle/VehicleConfig.h"

#include <cmath>
#include <cstdio>

int main() {
  VehicleConfig config;
  if (!loadVehicleConfigJson("../assets/cars/renault_40cv.json", config)) {
    std::fprintf(stderr, "FAIL: Renault config should load and validate\n");
    return 1;
  }
  if (config.id != "renault_40cv_jv1" || config.massKg <= 0.0f ||
      std::abs(config.topSpeedMps - 130.0f / 3.6f) > 0.01f ||
      config.gearRatios[2] <= 0.0f || config.gearRatios[0] >= 0.0f ||
      config.engineTorqueCurve.size() < 2 ||
      std::abs(config.engineTorqueCurve.front().torqueNm - 524.0f) > 0.1f ||
      std::abs(config.frontAntiRollStiffnessNPerM - 5000.0f) > 0.1f ||
      std::abs(config.rearAntiRollStiffnessNPerM - 3500.0f) > 0.1f ||
      std::abs(config.reboundDampingNsPerM - 6300.0f) > 0.1f ||
      std::abs(config.aerodynamicDrag - 0.42f) > 0.01f ||
      std::abs(config.downforceCoefficient - 1.5f) > 0.01f ||
      !config.absEnabled || std::abs(config.absSlipThreshold - 0.20f) > 0.01f ||
      std::abs(config.rearDifferentialLock - 0.25f) > 0.01f ||
      std::abs(config.antiFlipThresholdRadians - 0.96f) > 0.01f ||
      std::abs(config.sandGrip - 0.5f) > 0.01f ||
      config.bumpStopQuadraticStiffnessNPerM2 <= 0.0f ||
      config.driveTorqueSplit[2] <= config.driveTorqueSplit[0]) {
    std::fprintf(stderr, "FAIL: vehicle-specific performance data is incomplete\n");
    return 1;
  }
  return 0;
}
