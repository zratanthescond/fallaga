#pragma once

#include <algorithm>
#include <cmath>

namespace CarHandling {

// Arcade handling values live together, away from suspension and body physics.
constexpr float kMaximumSteerAngle = 0.61f;
constexpr float kSteerFalloffPerMps = 0.022f;
constexpr float kMinimumHighSpeedSteer = 0.18f;
constexpr float kMaxLateralAccelerationMps2 = 6.0f;
constexpr float kSteerInRate = 5.0f;
constexpr float kSteerReturnRate = 6.0f;
constexpr float kReverseEngageSpeedKmh = 0.75f;
constexpr float kThrottleRiseRate = 7.0f;
constexpr float kThrottleFallRate = 12.0f;

inline float steeringTarget(float horizontalSpeedMps, float normalizedInput) {
  const float speedScale = std::clamp(
      1.0f - horizontalSpeedMps * kSteerFalloffPerMps,
      kMinimumHighSpeedSteer, 1.0f);
  return std::clamp(normalizedInput, -1.0f, 1.0f) *
         kMaximumSteerAngle * speedScale;
}

inline float approachSteering(float current, float target, float dt) {
  const float rate = (std::abs(target) > 0.001f) ? kSteerInRate : kSteerReturnRate;
  const float blend = 1.0f - std::exp(-rate * std::max(0.0f, dt));
  return current + (target - current) * blend;
}

// Car::yaw increases opposite to its world-space rotation. Positive steering
// is left, so forward motion requests negative stored yaw; reversing swaps it.
inline float desiredYawRate(float steerAngle, float signedForwardSpeed,
                            float wheelbase) {
  if (wheelbase <= 0.001f) return 0.0f;
  const float kinematicRate = -steerAngle * signedForwardSpeed / wheelbase;
  const float maxRate = kMaxLateralAccelerationMps2 /
      std::max(std::abs(signedForwardSpeed), 1.0f);
  return std::clamp(kinematicRate, -maxRate, maxRate);
}

inline float maxYawRate(float speedMps) {
  return kMaxLateralAccelerationMps2 / std::max(std::abs(speedMps), 1.0f);
}

inline float frontWheelYaw(float bodyYaw, float steerAngle) {
  // The car's stored yaw is opposite the rendered world heading; positive
  // left steering therefore subtracts from this model-space heading.
  return bodyYaw - steerAngle;
}

inline float visibleWheelSteerAngle(float steerAngle) {
  // The model's -Z forward axis is turned by the body's 180-degree Y fixup.
  // Positive local wheel rotation then points the tire toward vehicle-left.
  return steerAngle;
}

inline float visibleWheelSpinAngle(float wheelAngularVelocity) {
  // The rendered FBX's local axle direction is opposite the solver's positive
  // wheel-spin convention after the chassis half-turn.
  return -wheelAngularVelocity;
}

// The rendered car base adds a 180-degree Y rotation. With the model facing
// local -Z, negative model-space X maps to vehicle-left in world space.
inline bool modelSpaceXIsVehicleLeft(float modelX, float centerX) {
  return modelX < centerX;
}

inline int chooseAutomaticGear(int currentGear, float rpm, float speedKmh,
                               bool accelerate, bool brake) {
  currentGear = std::clamp(currentGear, 0, 7);
  if (speedKmh < kReverseEngageSpeedKmh) {
    if (brake) return 0;
    if (accelerate && currentGear <= 1) return 2;
  }

  if (currentGear >= 2) {
    if (rpm > 6000.0f && currentGear < 7) return currentGear + 1;
    if (rpm < 2500.0f && currentGear > 2 && speedKmh > 10.0f)
      return currentGear - 1;
  }
  return currentGear;
}

inline float throttleForGear(int gear, bool accelerate, bool brake) {
  if (gear == 0) return brake ? 1.0f : 0.0f;
  return (accelerate && !brake) ? 1.0f : 0.0f;
}

inline float approachThrottle(float current, float target, float dt) {
  const float rate = target > current ? kThrottleRiseRate : kThrottleFallRate;
  const float blend = 1.0f - std::exp(-rate * std::max(0.0f, dt));
  return current + (target - current) * blend;
}

inline float clampForwardSpeed(float forwardSpeedMps, float maxForwardMps,
                               float maxReverseMps) {
  return std::clamp(forwardSpeedMps, -std::max(0.0f, maxReverseMps),
                    std::max(0.0f, maxForwardMps));
}

inline float longitudinalSlip(float wheelSpeedMps, float contactSpeedMps) {
  const float denominator = std::max(
      1.0f, std::max(std::abs(wheelSpeedMps), std::abs(contactSpeedMps)));
  return std::clamp((wheelSpeedMps - contactSpeedMps) / denominator,
                    -1.0f, 1.0f);
}

} // namespace CarHandling
