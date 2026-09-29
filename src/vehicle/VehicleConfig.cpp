#include "VehicleConfig.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <regex>
#include <sstream>

namespace {
bool readNumber(const std::string &json, const char *key, float &value) {
  const std::regex field("\\\"" + std::string(key) +
                         "\\\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)");
  std::smatch match;
  if (!std::regex_search(json, match, field)) return false;
  try {
    value = std::stof(match[1].str());
    return std::isfinite(value);
  } catch (...) {
    return false;
  }
}

bool readBool(const std::string &json, const char *key, bool &value) {
  const std::regex field("\\\"" + std::string(key) +
                         "\\\"\\s*:\\s*(true|false)");
  std::smatch match;
  if (!std::regex_search(json, match, field)) return false;
  value = match[1].str() == "true";
  return true;
}

std::vector<float> readNumberArray(const std::string &json, const char *key) {
  std::vector<float> values;
  const std::regex arrayField("\\\"" + std::string(key) +
                              "\\\"\\s*:\\s*\\[([^\\]]*)\\]");
  std::smatch arrayMatch;
  if (!std::regex_search(json, arrayMatch, arrayField)) return values;
  const std::regex number("-?[0-9]+(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?");
  const std::string contents = arrayMatch[1].str();
  for (auto it = std::sregex_iterator(contents.begin(), contents.end(), number);
       it != std::sregex_iterator(); ++it) {
    try {
      const float value = std::stof((*it)[0].str());
      if (!std::isfinite(value)) return {};
      values.push_back(value);
    } catch (...) {
      return {};
    }
  }
  return values;
}
} // namespace

bool loadVehicleConfigJson(const std::string &path, VehicleConfig &config) {
  std::ifstream file(path);
  if (!file) return false;
  std::ostringstream contents;
  contents << file.rdbuf();
  const std::string json = contents.str();

  readNumber(json, "massKg", config.massKg);
  readNumber(json, "yawInertiaKgM2", config.yawInertiaKgM2);
  readNumber(json, "wheelbaseM", config.wheelbaseM);
  readNumber(json, "trackWidthM", config.trackWidthM);
  readNumber(json, "centerOfMassHeightM", config.centerOfMassHeightM);
  readNumber(json, "centerOfMassDropM", config.centerOfMassDropM);
  readNumber(json, "frontWeightShare", config.frontWeightShare);
  readNumber(json, "peakTireGrip", config.peakTireGrip);
  readNumber(json, "lateralStiffness", config.lateralStiffness);
  readNumber(json, "springRateNPerM", config.springRateNPerM);
  readNumber(json, "compressionDampingNsPerM", config.compressionDampingNsPerM);
  float legacyReboundMultiplier =
      config.reboundDampingNsPerM / std::max(config.compressionDampingNsPerM, 1.0f);
  if (!readNumber(json, "reboundDampingNsPerM", config.reboundDampingNsPerM) &&
      readNumber(json, "reboundDampingMultiplier", legacyReboundMultiplier)) {
    config.reboundDampingNsPerM =
        config.compressionDampingNsPerM * legacyReboundMultiplier;
  }
  readNumber(json, "frontAntiRollStiffnessNPerM", config.frontAntiRollStiffnessNPerM);
  readNumber(json, "rearAntiRollStiffnessNPerM", config.rearAntiRollStiffnessNPerM);
  readNumber(json, "bumpStopStartCompressionM", config.bumpStopStartCompressionM);
  readNumber(json, "bumpStopQuadraticStiffnessNPerM2",
             config.bumpStopQuadraticStiffnessNPerM2);
  readNumber(json, "engineEfficiency", config.engineEfficiency);
  readNumber(json, "frontBrakeTorqueNm", config.frontBrakeTorqueNm);
  readNumber(json, "rearBrakeTorqueNm", config.rearBrakeTorqueNm);
  readNumber(json, "handbrakeTorqueNm", config.handbrakeTorqueNm);
  readBool(json, "absEnabled", config.absEnabled);
  readNumber(json, "absSlipThreshold", config.absSlipThreshold);
  readNumber(json, "frontDifferentialLock", config.frontDifferentialLock);
  readNumber(json, "rearDifferentialLock", config.rearDifferentialLock);
  readNumber(json, "antiFlipThresholdRadians", config.antiFlipThresholdRadians);
  readNumber(json, "antiFlipStrengthNmPerRad", config.antiFlipStrengthNmPerRad);
  readNumber(json, "antiFlipDampingNms", config.antiFlipDampingNms);
  readNumber(json, "engineBrakeTorqueNm", config.engineBrakeTorqueNm);
  readNumber(json, "rollingResistanceTorqueNm", config.rollingResistanceTorqueNm);
  readNumber(json, "wheelInertiaKgM2", config.wheelInertiaKgM2);
  readNumber(json, "idleRpm", config.idleRpm);
  readNumber(json, "maxRpm", config.maxRpm);
  readNumber(json, "finalDrive", config.finalDrive);
  readNumber(json, "maxAccelerationMps2", config.maxAccelerationMps2);
  readNumber(json, "brakeForceN", config.brakeForceN);
  readNumber(json, "handbrakeForceN", config.handbrakeForceN);
  readNumber(json, "rollingResistance", config.rollingResistance);
  readNumber(json, "aerodynamicDrag", config.aerodynamicDrag);
  readNumber(json, "downforceCoefficient", config.downforceCoefficient);
  readNumber(json, "topSpeedMps", config.topSpeedMps);
  readNumber(json, "reverseSpeedMps", config.reverseSpeedMps);
  readNumber(json, "stabilityAssist", config.stabilityAssist);
  readNumber(json, "counterSteerAssist", config.counterSteerAssist);
  readNumber(json, "handbrakeRearGrip", config.handbrakeRearGrip);
  readNumber(json, "asphaltGrip", config.asphaltGrip);
  readNumber(json, "cobblestoneGrip", config.cobblestoneGrip);
  readNumber(json, "concreteGrip", config.concreteGrip);
  readNumber(json, "dirtGrip", config.dirtGrip);
  readNumber(json, "dustGrip", config.dustGrip);
  readNumber(json, "sandGrip", config.sandGrip);
  readNumber(json, "waterGrip", config.waterGrip);

  const auto gears = readNumberArray(json, "gearRatios");
  if (gears.size() == config.gearRatios.size())
    std::copy(gears.begin(), gears.end(), config.gearRatios.begin());
  const auto torqueSplit = readNumberArray(json, "driveTorqueSplit");
  if (torqueSplit.size() == config.driveTorqueSplit.size())
    std::copy(torqueSplit.begin(), torqueSplit.end(), config.driveTorqueSplit.begin());

  const std::regex idField("\\\"id\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
  std::smatch idMatch;
  if (std::regex_search(json, idMatch, idField)) config.id = idMatch[1].str();

  const auto curveValues = readNumberArray(json, "engineTorqueCurveNm");
  config.engineTorqueCurve.clear();
  if (curveValues.size() >= 4 && curveValues.size() % 2 == 0) {
    for (size_t i = 0; i < curveValues.size(); i += 2)
      config.engineTorqueCurve.push_back({curveValues[i], curveValues[i + 1]});
  }

  if (config.massKg <= 0.0f || config.maxRpm <= config.idleRpm ||
      config.wheelbaseM <= 0.0f || config.springRateNPerM <= 0.0f ||
      config.frontAntiRollStiffnessNPerM < 0.0f ||
      config.rearAntiRollStiffnessNPerM < 0.0f ||
      config.reboundDampingNsPerM < 0.0f ||
      config.bumpStopStartCompressionM < 0.0f ||
      config.bumpStopQuadraticStiffnessNPerM2 < 0.0f ||
      config.frontDifferentialLock < 0.0f || config.frontDifferentialLock > 1.0f ||
      config.rearDifferentialLock < 0.0f || config.rearDifferentialLock > 1.0f ||
      config.antiFlipThresholdRadians < 0.0f ||
      config.antiFlipStrengthNmPerRad < 0.0f || config.antiFlipDampingNms < 0.0f ||
      config.asphaltGrip < 0.0f || config.cobblestoneGrip < 0.0f ||
      config.concreteGrip < 0.0f || config.dirtGrip < 0.0f ||
      config.dustGrip < 0.0f || config.sandGrip < 0.0f || config.waterGrip < 0.0f ||
      config.absSlipThreshold < 0.0f || config.absSlipThreshold >= 1.0f)
    return false;
  return true;
}
