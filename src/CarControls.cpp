#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <cmath>
#include <glm/glm.hpp>

#include "AudioSystem.h"
#include "Car.h"
#include "CarHandling.h"
#include "Character.h"



void Car::processInput(float dt) {
  driverInput = {};
  if (!m_Driver || !m_Driver->isDriving()) {
    steerAngle += (0.0f - steerAngle) * (1.0f - std::exp(-7.5f * dt));
    return;
  }

  const auto keyDown = [this](char lower, char upper) {
    return m_Driver->keys[lower] || m_Driver->keys[upper];
  };
  driverInput.accelerate = keyDown('w', 'W') || keyDown('z', 'Z');
  driverInput.brake = keyDown('s', 'S');
  const bool left = keyDown('a', 'A') || keyDown('q', 'Q');
  const bool right = keyDown('d', 'D');
  driverInput.steering = (left ? 1.0f : 0.0f) - (right ? 1.0f : 0.0f);

  GLFWwindow *window = glfwGetCurrentContext();
  driverInput.handbrake = window && glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
  driverInput.horn = window && glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;

  // 1. Speed-Sensitive Steering (GTA Style)
  float speed = getSpeed();
  const float steerTarget = CarHandling::steeringTarget(speed, driverInput.steering);
  steerAngle = CarHandling::approachSteering(steerAngle, steerTarget, dt);

  // 2. Horn Input (F Key)
  if (driverInput.horn) {
    AudioSystem::getInstance().playHorn(glm::vec3(position.x, position.y, position.z));
  } else {
    AudioSystem::getInstance().stopHorn();
  }
}


void Car::updateTransmission(float dt, bool gas, bool braking) {
  const float currentSpeedKmh = std::abs(getForwardSpeed()) * 3.6f;
  const int previousGear = engine.gear;
  engine.gear = CarHandling::chooseAutomaticGear(
      engine.gear, engine.rpm, currentSpeedKmh, gas, braking);
  if (engine.gear > previousGear) engine.rpm = std::max(engine.minRPM, engine.rpm - 1500.0f);
  else if (engine.gear < previousGear) engine.rpm = std::min(engine.maxRPM, engine.rpm + 1000.0f);

  // Braking always wins over forward throttle; brake becomes reverse only
  // after the low-speed gear rule has selected reverse.
  const float targetThrottle =
      CarHandling::throttleForGear(engine.gear, gas, braking);
  engine.throttle = CarHandling::approachThrottle(engine.throttle,
                                                   targetThrottle, dt);

  float avgWheelAV =
      (wheels[2].angularVelocity + wheels[3].angularVelocity) * 0.5f;
  float targetRPM = std::abs(avgWheelAV * engine.gearRatios[engine.gear]) *
                    engine.finalDrive * 9.5493f;
  engine.rpm += (std::max(engine.minRPM, targetRPM) - engine.rpm) *
                (1.0f - std::exp(-5.0f * dt));
  engine.rpm = glm::clamp(engine.rpm, engine.minRPM, engine.maxRPM);
}
