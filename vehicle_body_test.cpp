#include "src/vehicle/VehicleBody.h"

#include <cmath>
#include <cstdio>
#include <chrono>
#include <thread>

int main() {
  int failures = 0;
  const auto check = [&failures](bool condition, const char *message) {
    if (!condition) {
      std::fprintf(stderr, "FAIL: %s\n", message);
      ++failures;
    }
  };

  VehicleBody body(1400.0f, glm::vec3(0.8f, 0.5f, 1.4f), 0.12f, 2500.0f);
  VehicleBody::State state;
  state.position = glm::vec3(0.0f, 100.0f, 0.0f);
  for (int i = 0; i < 120; ++i) {
    state = body.step(state, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                      1.0f / 120.0f, {});
  }
  check(state.velocity.y < -7.0f,
        "Bullet rigid body must integrate gravity");

  VehicleBody drivenBody(1400.0f, glm::vec3(0.8f, 0.5f, 1.4f), 0.12f, 2500.0f);
  VehicleBody::State drivenState;
  drivenState.position = glm::vec3(0.0f, 100.0f, 0.0f);
  for (int i = 0; i < 120; ++i) {
    drivenState = drivenBody.step(
        drivenState, glm::vec3(0.0f, 0.0f, 2800.0f), glm::vec3(0.0f),
        1.0f / 120.0f, {});
  }
  check(drivenState.velocity.z > 1.5f && drivenState.position.z > 0.5f,
        "positive custom tire force must accelerate the chassis forward");

  VehicleBody turningBody(1400.0f, glm::vec3(0.8f, 0.5f, 1.4f), 0.12f, 2500.0f);
  VehicleBody::State turningState;
  turningState.position = glm::vec3(0.0f, 100.0f, 0.0f);
  for (int i = 0; i < 120; ++i) {
    turningState = turningBody.step(
        turningState, glm::vec3(0.0f), glm::vec3(0.0f, 3000.0f, 0.0f),
        1.0f / 120.0f, {});
  }
  check(turningState.angularVelocity.y > 0.1f,
        "positive chassis yaw torque must produce positive world yaw rate");

  VehicleBody groundedBody(1400.0f, glm::vec3(0.8f, 0.5f, 1.4f), 0.12f,
                           2500.0f);
  VehicleBody::State groundedState;
  groundedState.position = glm::vec3(0.0f, 10.0f, 0.0f);
  const TerrainHeightQuery flatGround = [](float, float) { return 0.0f; };
  // Nearby Bullet heightfield tiles are sampled incrementally, nearest first.
  for (int i = 0; i < 9; ++i)
    groundedBody.prepareTerrainCollision(flatGround, groundedState.position);
  glm::vec3 terrainHit(0.0f), terrainNormal(0.0f);
  bool terrainReady = false;
  for (int i = 0; i < 1000 && !terrainReady; ++i) {
    groundedBody.prepareTerrainCollision(flatGround, groundedState.position);
    terrainReady = groundedBody.raycastTerrain(
        glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f),
        terrainHit, terrainNormal);
    if (!terrainReady) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  check(terrainReady &&
        std::abs(terrainHit.y) < 0.01f && terrainNormal.y > 0.9f,
        "wheel raycast must hit the same Bullet terrain collision surface");
  for (int i = 0; i < 240; ++i) {
    groundedState = groundedBody.step(
        groundedState, glm::vec3(0.0f), glm::vec3(0.0f), 1.0f / 120.0f,
        flatGround);
  }
  check(groundedState.position.y > 0.30f && groundedState.position.y < 0.50f &&
        std::abs(groundedState.velocity.y) < 0.5f,
        "Bullet terrain collision must stop the chassis falling through ground");
  return failures == 0 ? 0 : 1;
}
