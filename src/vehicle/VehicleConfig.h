#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

struct EngineTorquePoint {
  float rpm = 0.0f;
  float torqueNm = 0.0f;
};

struct VehicleConfig {
  std::string id = "default";
  float massKg = 1400.0f;
  float yawInertiaKgM2 = 2500.0f;
  float wheelbaseM = 2.8f;
  float trackWidthM = 1.6f;
  float centerOfMassHeightM = 0.55f;
  float centerOfMassDropM = 0.12f;
  float frontWeightShare = 0.54f;
  float peakTireGrip = 1.05f;
  float lateralStiffness = 7.0f;
  float springRateNPerM = 38000.0f;
  float compressionDampingNsPerM = 4200.0f;
  float reboundDampingNsPerM = 6300.0f;
  float frontAntiRollStiffnessNPerM = 5000.0f;
  float rearAntiRollStiffnessNPerM = 3500.0f;
  float bumpStopStartCompressionM = 0.16f;
  float bumpStopQuadraticStiffnessNPerM2 = 180000.0f;
  float engineEfficiency = 0.82f;
  float frontBrakeTorqueNm = 5000.0f;
  float rearBrakeTorqueNm = 3000.0f;
  float handbrakeTorqueNm = 8000.0f;
  bool absEnabled = true;
  float absSlipThreshold = 0.20f;
  float frontDifferentialLock = 0.0f;
  float rearDifferentialLock = 0.25f;
  float antiFlipThresholdRadians = 0.96f;
  float antiFlipStrengthNmPerRad = 18000.0f;
  float antiFlipDampingNms = 4000.0f;
  float engineBrakeTorqueNm = 400.0f;
  float rollingResistanceTorqueNm = 50.0f;
  float wheelInertiaKgM2 = 1.2f;
  float idleRpm = 800.0f;
  float maxRpm = 7500.0f;
  float finalDrive = 4.1f;
  std::array<float, 8> gearRatios{{-3.1f, 0.0f, 3.6f, 2.2f,
                                    1.5f, 1.1f, 0.9f, 0.7f}};
  std::array<float, 4> driveTorqueSplit{{0.15f, 0.15f, 0.35f, 0.35f}};
  std::vector<EngineTorquePoint> engineTorqueCurve;
  // Model performance limits. Acceleration is a gameplay tuning value unless
  // the vehicle's period specification publishes a measured figure.
  float maxAccelerationMps2 = 2.5f;
  float brakeForceN = 24000.0f;
  float handbrakeForceN = 13000.0f;
  float rollingResistance = 0.018f;
  float aerodynamicDrag = 0.42f;
  float downforceCoefficient = 0.012f;
  float topSpeedMps = 52.0f;
  float reverseSpeedMps = 14.0f;
  float stabilityAssist = 0.75f;
  float counterSteerAssist = 0.18f;
  float handbrakeRearGrip = 0.32f;
  float asphaltGrip = 1.0f;
  float cobblestoneGrip = 0.9f;
  float concreteGrip = 0.9f;
  float dirtGrip = 0.62f;
  float dustGrip = 0.55f;
  float sandGrip = 0.5f;
  float waterGrip = 0.2f;
};

bool loadVehicleConfigJson(const std::string &path, VehicleConfig &config);
