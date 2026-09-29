#pragma once

#include <glm/glm.hpp>

glm::vec3 calculateAntiFlipTorque(const glm::vec3 &chassisUp,
                                  const glm::vec3 &worldAngularVelocity,
                                  bool grounded,
                                  float thresholdRadians,
                                  float strengthNmPerRad,
                                  float dampingNms,
                                  float maximumTorqueNm = 50000.0f);
