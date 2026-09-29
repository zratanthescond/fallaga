#include "VehicleDrivetrain.h"

#include <algorithm>
#include <cmath>

namespace {
void biasAxle(std::array<float, 4> &torques,
              const std::array<float, 4> &wheelAngularVelocities,
              int left, int right, float lock) {
  lock = std::clamp(lock, 0.0f, 1.0f);
  const float totalTorque = torques[left] + torques[right];
  const float baseMagnitude = std::abs(torques[left]) + std::abs(torques[right]);
  if (lock <= 0.0f || baseMagnitude < 1.0e-5f) return;

  const float leftBaseShare = std::abs(torques[left]) / baseMagnitude;
  const float leftSpeed = std::abs(wheelAngularVelocities[left]);
  const float rightSpeed = std::abs(wheelAngularVelocities[right]);
  const float averageSpeed = (leftSpeed + rightSpeed) * 0.5f;
  const float normalizedDifference =
      (leftSpeed - rightSpeed) / std::max(averageSpeed, 5.0f);
  const float transfer = 0.5f * lock *
                         std::clamp(normalizedDifference, -1.0f, 1.0f);
  const float leftShare = std::clamp(leftBaseShare - transfer, 0.0f, 1.0f);
  torques[left] = totalTorque * leftShare;
  torques[right] = totalTorque - torques[left];
}
} // namespace

std::array<float, 4> applyLimitedSlipDifferentials(
    const std::array<float, 4> &wheelTorques,
    const std::array<float, 4> &wheelAngularVelocities,
    float frontLock, float rearLock) {
  std::array<float, 4> result = wheelTorques;
  biasAxle(result, wheelAngularVelocities, 0, 1, frontLock);
  biasAxle(result, wheelAngularVelocities, 2, 3, rearLock);
  return result;
}
