#include "src/vehicle/VehicleDynamics.h"
#include "src/CarHandling.h"

#include <cmath>
#include <cstdio>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

namespace {
void advance(VehicleDynamicsState &state, const VehicleDynamicsInput &input,
             const VehicleConfig &config, int steps) {
  constexpr float dt = 1.0f / 120.0f;
  for (int i = 0; i < steps; ++i)
    state = stepVehicleDynamics(state, input, config, dt).state;
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

  VehicleConfig config;
  VehicleConfig tireOnlyConfig = config;
  tireOnlyConfig.stabilityAssist = 0.0f;
  tireOnlyConfig.counterSteerAssist = 0.0f;
  VehicleDynamicsInput input;
  input.gear = 2;
  input.gearRatio = 3.6f;
  input.engineTorqueNm = 650.0f;
  input.finalDrive = 4.1f;
  input.wheelRadiusM = 0.35f;

  VehicleDynamicsState acceleration;
  input.throttle = 1.0f;
  advance(acceleration, input, config, 240);
  check(acceleration.velocityXZ.y > 5.0f,
        "engine torque should accelerate the car forward");
  check(acceleration.velocityXZ.x > -0.01f && acceleration.velocityXZ.x < 0.01f,
        "straight acceleration should not create lateral velocity");

  VehicleDynamicsState leftTurn;
  leftTurn.velocityXZ.y = 15.0f;
  input.throttle = 0.0f;
  input.steerAngle = 0.18f;
  advance(leftTurn, input, config, 90);
  check(leftTurn.worldYaw < -0.02f,
        "positive steering at forward speed should yaw the car left");
  check(leftTurn.velocityXZ.x > 0.0f,
        "left turn should produce leftward lateral motion");

  VehicleDynamicsState rightTurn;
  rightTurn.velocityXZ.y = 15.0f;
  input.steerAngle = -0.18f;
  advance(rightTurn, input, config, 90);
  check(rightTurn.worldYaw > 0.02f,
        "negative steering at forward speed should yaw the car right");

  VehicleDynamicsState tireDrivenLeft;
  tireDrivenLeft.velocityXZ.y = 15.0f;
  input.steerAngle = 0.18f;
  const auto tireLeftStep = stepVehicleDynamics(
      tireDrivenLeft, input, tireOnlyConfig, 1.0f / 120.0f);
  check(tireLeftStep.state.worldYawRate < 0.0f,
        "front/rear axle geometry must produce left yaw from tire forces without steering assists");

  // Match the car's actual render convention: local model forward is -Z and
  // its chassis rotation is angleAxis(pi - Car::yaw, worldUp).
  const glm::quat bodyRotation = glm::angleAxis(
      glm::pi<float>() - leftTurn.worldYaw, glm::vec3(0.0f, 1.0f, 0.0f));
  const glm::vec3 bodyForward = bodyRotation * glm::vec3(0.0f, 0.0f, -1.0f);
  const glm::quat visualWheelSteer = glm::angleAxis(
      CarHandling::visibleWheelSteerAngle(0.18f),
      glm::vec3(0.0f, 1.0f, 0.0f));
  const glm::vec3 frontWheelForward =
      bodyRotation * visualWheelSteer * glm::vec3(0.0f, 0.0f, -1.0f);
  check(bodyForward.x > 0.0f && frontWheelForward.x > bodyForward.x,
        "composed chassis and FBX wheel transforms must visibly point left for left input");

  VehicleDynamicsState gripState;
  gripState.velocityXZ.y = 12.0f;
  VehicleDynamicsInput highGripInput;
  highGripInput.steerAngle = 0.20f;
  VehicleDynamicsInput lowGripInput = highGripInput;
  lowGripInput.frontSurfaceGrip = 0.25f;
  lowGripInput.rearSurfaceGrip = 0.25f;
  const auto highGripStep = stepVehicleDynamics(
      gripState, highGripInput, config, 1.0f / 120.0f);
  const auto lowGripStep = stepVehicleDynamics(
      gripState, lowGripInput, config, 1.0f / 120.0f);
  check(std::abs(lowGripStep.frontLateralForceN) <
            std::abs(highGripStep.frontLateralForceN),
        "lower terrain grip must reduce the tire's available lateral force");

  VehicleDynamicsState reverse;
  input.steerAngle = 0.0f;
  input.gear = 0;
  input.gearRatio = 3.1f;
  input.throttle = 1.0f;
  advance(reverse, input, config, 120);
  check(reverse.velocityXZ.y < -1.0f,
        "reverse gear and throttle should move the car backward");

  VehicleDynamicsState braking;
  braking.velocityXZ.y = 25.0f;
  input.gear = 2;
  input.throttle = 0.0f;
  input.brake = 1.0f;
  advance(braking, input, config, 360);
  check(braking.velocityXZ.y < 1.0f,
        "service brakes should bring the car close to a stop");
  check(braking.velocityXZ.y >= -0.05f,
        "service brakes should not reverse the car through zero speed");

  std::printf("Vehicle dynamics checks: %s (%d failures)\n",
              failures == 0 ? "PASS" : "FAIL", failures);
  return failures == 0 ? 0 : 1;
}
