#include "VehicleStability.h"

#include <algorithm>
#include <cmath>

glm::vec3 calculateAntiFlipTorque(const glm::vec3 &chassisUpInput,
                                  const glm::vec3 &worldAngularVelocity,
                                  bool grounded,
                                  float thresholdRadians,
                                  float strengthNmPerRad,
                                  float dampingNms,
                                  float maximumTorqueNm) {
  if (!grounded || glm::dot(chassisUpInput, chassisUpInput) < 1.0e-8f)
    return glm::vec3(0.0f);

  const glm::vec3 chassisUp = glm::normalize(chassisUpInput);
  const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
  const float upDot = std::clamp(glm::dot(chassisUp, worldUp), -1.0f, 1.0f);
  const float tiltRadians = std::acos(upDot);
  if (tiltRadians <= std::max(0.0f, thresholdRadians))
    return glm::vec3(0.0f);

  glm::vec3 correctionAxis = glm::cross(chassisUp, worldUp);
  if (glm::dot(correctionAxis, correctionAxis) < 1.0e-6f)
    correctionAxis = glm::vec3(1.0f, 0.0f, 0.0f);
  else
    correctionAxis = glm::normalize(correctionAxis);

  const float correctionRate = glm::dot(worldAngularVelocity, correctionAxis);
  const float maxTorque = std::max(0.0f, maximumTorqueNm);
  const float torqueMagnitude = std::clamp(
      (tiltRadians - thresholdRadians) * std::max(0.0f, strengthNmPerRad) -
          correctionRate * std::max(0.0f, dampingNms),
      -maxTorque, maxTorque);
  return correctionAxis * torqueMagnitude;
}
