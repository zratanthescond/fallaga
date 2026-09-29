#include "VehicleDynamics.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kGravity = 9.81f;
constexpr float kPi = 3.14159265358979323846f;

float pacejkaLateral(float slipAngle, float normalLoad, float grip,
                     float stiffness) {
  const float B = stiffness;
  const float C = 1.25f;
  const float E = 0.85f;
  const float x = B * slipAngle;
  const float force = normalLoad * grip *
      std::sin(C * std::atan(x - E * (x - std::atan(x))));
  return force;
}

float clampTireForce(float longitudinal, float lateral, float limit,
                     float &outLongitudinal, float &outLateral) {
  const float magnitude = std::sqrt(longitudinal * longitudinal + lateral * lateral);
  if (magnitude > limit && magnitude > 1e-4f) {
    const float scale = limit / magnitude;
    longitudinal *= scale;
    lateral *= scale;
  }
  outLongitudinal = longitudinal;
  outLateral = lateral;
  return std::sqrt(longitudinal * longitudinal + lateral * lateral);
}
}

VehicleDynamicsOutput stepVehicleDynamics(const VehicleDynamicsState &previous,
                                          const VehicleDynamicsInput &input,
                                          const VehicleConfig &config,
                                          float dt) {
  VehicleDynamicsOutput out;
  out.state = previous;
  if (dt <= 0.0f || config.massKg <= 0.0f) return out;

  const float sinYaw = std::sin(previous.worldYaw);
  const float cosYaw = std::cos(previous.worldYaw);
  const glm::vec2 forward(-sinYaw, cosYaw);
  const glm::vec2 left(cosYaw, sinYaw);
  const glm::vec2 right = -left;
  const float forwardSpeed = glm::dot(previous.velocityXZ, forward);
  const float speed = glm::length(previous.velocityXZ);

  const float wheelbase = std::max(config.wheelbaseM, 0.5f);
  const float frontDistance = wheelbase * (1.0f - config.frontWeightShare);
  const float rearDistance = wheelbase * config.frontWeightShare;
  // Axle offsets run along the chassis forward axis. Using the lateral basis
  // here put the front and rear tire contact points on the car's sides, which
  // breaks slip velocity and yaw torque calculations.
  const float frontX = forward.x * frontDistance;
  const float frontZ = forward.y * frontDistance;
  const float rearX = -forward.x * rearDistance;
  const float rearZ = -forward.y * rearDistance;

  const float throttle = std::clamp(input.throttle, 0.0f, 1.0f);
  const float brake = std::clamp(input.brake, 0.0f, 1.0f);
  const bool reversing = input.gear == 0;
  const float wheelRadius = std::max(input.wheelRadiusM, 0.1f);
  const float signedGearRatio = reversing ? -std::abs(input.gearRatio)
                                           : std::abs(input.gearRatio);
  const float driveForce = input.engineTorqueNm * throttle * signedGearRatio *
      input.finalDrive * config.engineEfficiency / wheelRadius;

  float brakeForce = 0.0f;
  float handbrakeForce = 0.0f;
  if (brake > 0.0f && !reversing) {
    const float direction = (std::abs(forwardSpeed) > 0.25f)
        ? std::copysign(1.0f, forwardSpeed) : 0.0f;
    brakeForce = -direction * config.brakeForceN * brake;
  }
  if (input.handbrake) {
    const float direction = (std::abs(forwardSpeed) > 0.25f)
        ? std::copysign(1.0f, forwardSpeed) : 0.0f;
    handbrakeForce = -direction * config.handbrakeForceN;
  }

  const float longitudinalDrive = driveForce + brakeForce + handbrakeForce;
  const float drag = config.rollingResistance * config.massKg * kGravity +
                     config.aerodynamicDrag * speed * speed;
  const glm::vec2 dragForce = speed > 0.01f
      ? -glm::normalize(previous.velocityXZ) * drag
      : glm::vec2(0.0f);
  const float estimatedLongAccel = longitudinalDrive / config.massKg;
  const float downforce = config.downforceCoefficient * speed * speed;
  const float transfer = config.massKg * config.centerOfMassHeightM *
      estimatedLongAccel / wheelbase;
  const float totalNormalLoad = config.massKg * kGravity + downforce;
  const float frontShare = std::clamp(
      config.frontWeightShare - transfer / std::max(totalNormalLoad, 1.0f),
      0.20f, 0.80f);
  out.frontTireLoadN = totalNormalLoad * frontShare;
  out.rearTireLoadN = totalNormalLoad - out.frontTireLoadN;

  const float steer = std::clamp(input.steerAngle, -0.62f, 0.62f);
  const glm::vec2 frontForward = forward * std::cos(steer) + left * std::sin(steer);
  const glm::vec2 frontRight(-frontForward.y, frontForward.x);
  const glm::vec2 rearForward = forward;
  const glm::vec2 rearRight = right;

  const glm::vec2 frontContactVelocity = previous.velocityXZ +
      glm::vec2(-previous.worldYawRate * frontZ,
                previous.worldYawRate * frontX);
  const glm::vec2 rearContactVelocity = previous.velocityXZ +
      glm::vec2(-previous.worldYawRate * rearZ,
                previous.worldYawRate * rearX);
  const float frontLongSpeed = glm::dot(frontContactVelocity, frontForward);
  const float rearLongSpeed = glm::dot(rearContactVelocity, rearForward);
  out.frontSlipAngle = std::atan2(glm::dot(frontContactVelocity, frontRight),
                                  std::abs(frontLongSpeed) + 1.0f);
  out.rearSlipAngle = std::atan2(glm::dot(rearContactVelocity, rearRight),
                                 std::abs(rearLongSpeed) + 1.0f);

  const float baseGrip = std::max(0.05f, config.peakTireGrip * input.surfaceGrip);
  const float frontGrip = std::max(0.05f, baseGrip * input.frontSurfaceGrip);
  const float rearRoadGrip = std::max(0.05f, baseGrip * input.rearSurfaceGrip);
  const float rearGrip = input.handbrake
      ? rearRoadGrip * config.handbrakeRearGrip : rearRoadGrip;
  float frontFy = -pacejkaLateral(out.frontSlipAngle, out.frontTireLoadN,
                                  frontGrip, config.lateralStiffness);
  float rearFy = -pacejkaLateral(out.rearSlipAngle, out.rearTireLoadN,
                                 rearGrip, config.lateralStiffness);

  // AWD torque split is rear biased. Service braking uses a 60/40 front/rear
  // balance; the handbrake acts on the rear axle only.
  const float frontLong = driveForce * 0.30f + brakeForce * 0.60f;
  const float rearLong = driveForce * 0.70f + brakeForce * 0.40f + handbrakeForce;
  float frontFx = 0.0f, rearFx = 0.0f;
  clampTireForce(frontLong, frontFy, out.frontTireLoadN * frontGrip, frontFx, frontFy);
  clampTireForce(rearLong, rearFy, out.rearTireLoadN * rearGrip, rearFx, rearFy);
  out.frontLateralForceN = frontFy;
  out.rearLateralForceN = rearFy;

  const glm::vec2 force = frontForward * frontFx + frontRight * frontFy +
      rearForward * rearFx + rearRight * rearFy + dragForce;
  const float forceLong = glm::dot(force, forward);
  const float forceLateral = glm::dot(force, left);
  out.accelerationLong = forceLong / config.massKg;
  out.accelerationLateral = forceLateral / config.massKg;
  out.state.velocityXZ += force * (dt / config.massKg);

  // This yaw value is Car::yaw, whose rendered orientation is (-yaw + pi).
  // Positive left steering decreases this yaw. Physical Y torque has the
  // opposite sign to Car::yaw acceleration, so invert tire torque here.
  const float tireYawTorque =
      frontZ * frontRight.x * frontFy - frontX * frontRight.y * frontFy +
      rearZ * rearRight.x * rearFy - rearX * rearRight.y * rearFy;
  const float targetYawRate = -steer * forwardSpeed / wheelbase;
  out.state.worldYawRate +=
      (-tireYawTorque / std::max(config.yawInertiaKgM2, 1.0f) +
       (targetYawRate - previous.worldYawRate) * config.stabilityAssist -
       previous.worldYawRate * config.counterSteerAssist) * dt;
  out.state.worldYawRate = std::clamp(out.state.worldYawRate, -2.8f, 2.8f);
  out.state.worldYaw += out.state.worldYawRate * dt;
  while (out.state.worldYaw > kPi) out.state.worldYaw -= 2.0f * kPi;
  while (out.state.worldYaw < -kPi) out.state.worldYaw += 2.0f * kPi;

  const glm::vec2 updatedForward(-std::sin(out.state.worldYaw),
                                  std::cos(out.state.worldYaw));
  float updatedForwardSpeed = glm::dot(out.state.velocityXZ, updatedForward);
  if (updatedForwardSpeed > config.topSpeedMps) {
    out.state.velocityXZ -= updatedForward *
        (updatedForwardSpeed - config.topSpeedMps);
  } else if (updatedForwardSpeed < -config.reverseSpeedMps) {
    out.state.velocityXZ += updatedForward *
        (-config.reverseSpeedMps - updatedForwardSpeed);
  }
  out.state.driftAngle = std::atan2(
      glm::dot(out.state.velocityXZ, glm::vec2(std::cos(out.state.worldYaw),
                                               std::sin(out.state.worldYaw))),
      std::abs(updatedForwardSpeed) + 0.1f);
  return out;
}
