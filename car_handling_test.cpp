#include "src/CarHandling.h"

#include <cmath>
#include <cstdio>

namespace {
bool near(float a, float b, float epsilon = 1e-4f) {
  return std::abs(a - b) <= epsilon;
}
}

int main() {
  int failures = 0;
  const auto check = [&failures](bool condition, const char *message) {
    if (!condition) {
      std::fprintf(stderr, "FAIL: %s\n", message);
      ++failures;
    }
  };

  const float lowSpeedLeft = CarHandling::steeringTarget(0.0f, 1.0f);
  const float highSpeedLeft = CarHandling::steeringTarget(25.0f, 1.0f);
  check(lowSpeedLeft > 0.0f, "left input must produce positive steering");
  check(near(CarHandling::steeringTarget(0.0f, -1.0f), -lowSpeedLeft),
        "right input must mirror left input");
  check(highSpeedLeft > 0.0f && highSpeedLeft < lowSpeedLeft,
        "steering must reduce with speed while remaining responsive");
  check(near(CarHandling::steeringTarget(100.0f, 1.0f),
             CarHandling::kMaximumSteerAngle * CarHandling::kMinimumHighSpeedSteer),
        "high-speed steering must respect its configured lower bound");
  check(std::abs(CarHandling::approachSteering(0.5f, 0.0f, 0.1f)) < 0.5f,
        "released steering must return toward center");
  check(CarHandling::approachSteering(0.0f, lowSpeedLeft, 0.1f) < 0.25f,
        "steering buildup must not snap to full lock");
  check(CarHandling::desiredYawRate(lowSpeedLeft, 10.0f, 2.8f) < 0.0f,
        "left input while moving forward must request left stored yaw");
  check(std::abs(CarHandling::desiredYawRate(lowSpeedLeft, 36.0f, 2.8f)) <=
            CarHandling::maxYawRate(36.0f) + 1e-5f,
        "high-speed steering yaw rate must stay within the lateral grip budget");
  check(near(CarHandling::desiredYawRate(0.0f, 36.0f, 2.8f), 0.0f),
        "straight-ahead input must request no yaw rate at speed");
  check(CarHandling::desiredYawRate(-lowSpeedLeft, 10.0f, 2.8f) > 0.0f,
        "right input while moving forward must request opposite yaw");
  check(CarHandling::desiredYawRate(lowSpeedLeft, -10.0f, 2.8f) > 0.0f,
        "reverse motion must reverse steering yaw response");
  check(CarHandling::visibleWheelSteerAngle(lowSpeedLeft) > 0.0f &&
        CarHandling::frontWheelYaw(0.0f, lowSpeedLeft) < 0.0f,
        "model-space wheel angle and stored tire heading must map positive input to vehicle-left");
  check(CarHandling::visibleWheelSpinAngle(2.0f) < 0.0f &&
        CarHandling::visibleWheelSpinAngle(-2.0f) > 0.0f,
        "wheel visual spin must compensate for the FBX axle direction");
  check(CarHandling::modelSpaceXIsVehicleLeft(-1.0f, 0.0f) &&
        !CarHandling::modelSpaceXIsVehicleLeft(1.0f, 0.0f),
        "model-space wheel sides must account for the body's 180-degree yaw rotation");
  check(CarHandling::approachThrottle(0.0f, 1.0f, 0.1f) > 0.0f,
        "throttle must respond smoothly when pressed");
  check(CarHandling::approachThrottle(1.0f, 0.0f, 0.1f) < 1.0f,
        "throttle must decay smoothly when released");
  check(near(CarHandling::clampForwardSpeed(40.0f, 130.0f / 3.6f, 8.0f),
             130.0f / 3.6f),
        "forward speed must not exceed the model's top speed");
  check(near(CarHandling::clampForwardSpeed(-12.0f, 130.0f / 3.6f, 8.0f),
             -8.0f),
        "reverse speed must respect its configured limit");
  check(near(CarHandling::longitudinalSlip(2.0f, 0.0f), 1.0f) &&
        near(CarHandling::longitudinalSlip(-2.0f, 0.0f), -1.0f),
        "low-speed wheelspin slip must stay bounded and retain its direction");

  check(CarHandling::chooseAutomaticGear(2, 900.0f, 0.0f, false, true) == 0,
        "braking from rest must select reverse");
  check(CarHandling::chooseAutomaticGear(0, 900.0f, 0.0f, true, false) == 2,
        "accelerating from reverse at rest must select forward");
  check(CarHandling::chooseAutomaticGear(3, 4000.0f, 30.0f, false, true) == 3,
        "braking at speed must keep the forward gear");
  check(CarHandling::chooseAutomaticGear(3, 1200.0f, 1.0f, false, true) == 3,
        "direction changes must wait until the car is nearly stopped");
  check(CarHandling::chooseAutomaticGear(3, 900.0f, 0.2f, false, true) == 0,
        "braking near a stop must select reverse");
  check(CarHandling::chooseAutomaticGear(3, 6500.0f, 30.0f, true, false) == 4,
        "automatic transmission must upshift at high RPM");
  check(CarHandling::chooseAutomaticGear(4, 2000.0f, 30.0f, false, false) == 3,
        "automatic transmission must downshift at low RPM and speed");
  check(near(CarHandling::throttleForGear(2, true, true), 0.0f),
        "braking must override forward throttle");
  check(near(CarHandling::throttleForGear(0, false, true), 1.0f),
        "braking in reverse must provide reverse throttle");
  check(near(CarHandling::throttleForGear(0, true, false), 0.0f),
        "accelerating in reverse must not add reverse throttle");

  std::printf("Car handling control checks: %s (%d failures)\n",
              failures == 0 ? "PASS" : "FAIL", failures);
  return failures == 0 ? 0 : 1;
}
