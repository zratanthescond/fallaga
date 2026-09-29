#pragma once

#include "VehicleConfig.h"
#include <glm/glm.hpp>

struct VehicleDynamicsInput {
  float steerAngle = 0.0f;       // Positive is left.
  float throttle = 0.0f;         // 0..1
  float brake = 0.0f;            // 0..1
  bool handbrake = false;
  int gear = 2;                   // 0 is reverse, 2..7 are forward gears.
  float engineTorqueNm = 0.0f;
  float finalDrive = 4.1f;
  float wheelRadiusM = 0.35f;
  float gearRatio = 3.6f;
  float surfaceGrip = 1.0f;
  float frontSurfaceGrip = 1.0f;
  float rearSurfaceGrip = 1.0f;
};

struct VehicleDynamicsState {
  glm::vec2 velocityXZ = glm::vec2(0.0f); // x, z world velocity.
  float worldYaw = 0.0f;
  float worldYawRate = 0.0f;
  float driftAngle = 0.0f;
};

struct VehicleDynamicsOutput {
  VehicleDynamicsState state;
  float accelerationLong = 0.0f;
  float accelerationLateral = 0.0f;
  float frontTireLoadN = 0.0f;
  float rearTireLoadN = 0.0f;
  float frontSlipAngle = 0.0f;
  float rearSlipAngle = 0.0f;
  float frontLateralForceN = 0.0f;
  float rearLateralForceN = 0.0f;
};

VehicleDynamicsOutput stepVehicleDynamics(const VehicleDynamicsState &state,
                                          const VehicleDynamicsInput &input,
                                          const VehicleConfig &config,
                                          float dt);
