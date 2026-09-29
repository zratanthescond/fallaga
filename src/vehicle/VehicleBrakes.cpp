#include "VehicleBrakes.h"

#include <algorithm>
#include <cmath>

float serviceBrakeScale(float contactSpeedMps, float wheelSpeedMps,
                        bool absEnabled, float slipThreshold) {
  const float contactSpeed = std::abs(contactSpeedMps);
  if (!absEnabled || contactSpeed < 1.0f) return 1.0f;

  const float threshold = std::clamp(slipThreshold, 0.0f, 0.99f);
  const bool rollingOpposite = contactSpeedMps * wheelSpeedMps < -0.1f;
  const float lockedSlip = rollingOpposite
      ? 1.0f
      : std::clamp((contactSpeed - std::abs(wheelSpeedMps)) /
                       std::max(contactSpeed, 1.0f),
                   0.0f, 1.0f);
  if (lockedSlip <= threshold) return 1.0f;

  const float release = (lockedSlip - threshold) / (1.0f - threshold);
  return 1.0f - 0.92f * std::clamp(release, 0.0f, 1.0f);
}
