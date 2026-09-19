#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

#include "AudioSystem.h"
#include "Camera.h"
#include "Car.h"
#include "Character.h"
#include "ObjectModel.h"
#include "Shader.h"
#include "Terrain.h"

// ================================================================
// MESH RESOLVER
// ================================================================
// Shared resources across all Car instances to save memory and avoid redundant
// loading
static ObjModel *sharedCarModel = nullptr;
static Shader *sharedCarShader = nullptr;
static int carInstanceCount = 0;

void Car::resolveWheelMeshes() {
  if (!model)
    return;
  meshBody = meshFL = meshFR = meshBL = meshBR = -1;
  isPaintedModel = false;

  const int total = model->getMeshCount();
  std::cout << "[Car] " << total << " meshes:\n";
  for (int i = 0; i < total; i++)
    std::cout << "  [" << i << "] \"" << model->getMeshName(i) << "\"\n";

  // Sort meshes by their AABB volume to identify body vs wheels
  struct MeshInfo {
    int index;
    float volume;
    glm::vec3 center;
  };
  std::vector<MeshInfo> infos;
  for (int i = 0; i < total; i++) {
    glm::vec3 minB = model->getMeshMinBound(i);
    glm::vec3 maxB = model->getMeshMaxBound(i);
    float vol = (maxB.x - minB.x) * (maxB.y - minB.y) * (maxB.z - minB.z);
    infos.push_back({i, std::abs(vol), model->getMeshCenter(i)});
  }

  // Sort by volume descending
  std::sort(infos.begin(), infos.end(), [](const MeshInfo &a, const MeshInfo &b) {
    return a.volume > b.volume;
  });

  // The largest mesh is the body
  if (!infos.empty()) {
    meshBody = infos[0].index;
    std::cout << "[Car] Resolved Body mesh index: " << meshBody << " (volume=" << infos[0].volume << ")\n";
  }

  // Filter out the body (and any door/other large parts) to find wheels.
  // The wheels are the 4 meshes with the smallest, similar volumes.
  std::vector<MeshInfo> candidates;
  for (size_t i = 1; i < infos.size(); i++) {
    candidates.push_back(infos[i]);
  }

  // Sort candidates by volume ascending
  std::sort(candidates.begin(), candidates.end(), [](const MeshInfo &a, const MeshInfo &b) {
    return a.volume < b.volume;
  });

  // We select the 4 smallest meshes as wheels
  std::vector<MeshInfo> wheelInfos;
  for (size_t i = 0; i < std::min<size_t>(4, candidates.size()); i++) {
    wheelInfos.push_back(candidates[i]);
  }

  // Now assign FL, FR, BL, BR spatially from the selected 4 wheels
  if (wheelInfos.size() >= 4) {
    float minZ = 1e9, maxZ = -1e9, minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    for (const auto &w : wheelInfos) {
      minZ = std::min(minZ, w.center.z);
      maxZ = std::max(maxZ, w.center.z);
      minX = std::min(minX, w.center.x);
      maxX = std::max(maxX, w.center.x);
      minY = std::min(minY, w.center.y);
      maxY = std::max(maxY, w.center.y);
    }

    // Dynamically detect if X is height (for R40CVPainted.fbx) or X is side-to-side (for R40CV.fbx)
    isPaintedModel = (std::abs(maxX - minX) < std::abs(maxY - minY));
    std::cout << "[Car] Axis detection: isPaintedModel=" << (isPaintedModel ? "true" : "false") << "\n";

    auto remap = [this](const glm::vec3 &v) {
      if (isPaintedModel) {
        // R40CVPainted coordinate system:
        // X is vertical (up/down) -> std_y
        // Y is side-to-side (left/right) -> std_x (-v.y to match left/right direction correctly)
        // Z is front-to-back -> std_z
        return glm::vec3(-v.y, v.x, v.z);
      }
      return v;
    };

    // Calculate mz and mx in standard space
    float stdMinZ = 1e9, stdMaxZ = -1e9, stdMinX = 1e9, stdMaxX = -1e9;
    std::vector<glm::vec3> stdCenters;
    for (const auto &w : wheelInfos) {
      glm::vec3 stdC = remap(w.center);
      stdCenters.push_back(stdC);
      stdMinZ = std::min(stdMinZ, stdC.z);
      stdMaxZ = std::max(stdMaxZ, stdC.z);
      stdMinX = std::min(stdMinX, stdC.x);
      stdMaxX = std::max(stdMaxX, stdC.x);
    }
    float mz = (stdMinZ + stdMaxZ) * 0.5f;
    float mx = (stdMinX + stdMaxX) * 0.5f;

    for (size_t i = 0; i < wheelInfos.size(); i++) {
      const auto &w = wheelInfos[i];
      const glm::vec3 &stdC = stdCenters[i];
      bool f = stdC.z < mz; // Front (model faces negative Z)
      bool l = stdC.x < mx; // Left
      if (f && l) meshFL = w.index;
      else if (f && !l) meshFR = w.index;
      else if (!f && l) meshBL = w.index;
      else if (!f && !l) meshBR = w.index;
    }

    auto remapOffset = [this, remap](int meshIdx) {
      if (meshIdx < 0) return glm::vec3(0.0f);
      return remap(model->getMeshCenter(meshIdx));
    };

    wheelOffsetFL = remapOffset(meshFL);
    wheelOffsetFR = remapOffset(meshFR);
    wheelOffsetBL = remapOffset(meshBL);
    wheelOffsetBR = remapOffset(meshBR);
  }

  if (meshFL >= 0 && meshBL >= 0) {
    m_Wheelbase = std::abs(wheelOffsetFL.z - wheelOffsetBL.z) * SCALE_FACTOR;
    m_TrackWidth = (meshFR >= 0) ? std::abs(wheelOffsetFL.x - wheelOffsetFR.x) *
                                       SCALE_FACTOR
                                 : 1.4f;
  }
  if (m_Wheelbase < 0.5f)
    m_Wheelbase = 2.8f;
  if (m_TrackWidth < 0.3f)
    m_TrackWidth = 1.4f;

  int rs = (meshBL >= 0) ? meshBL : (meshFL >= 0 ? meshFL : -1);
  if (rs >= 0) {
    float raw_half_height = isPaintedModel ? 
        std::abs(model->getMeshCenter(rs).x - model->getMeshMinBound(rs).x) :
        std::abs(model->getMeshCenter(rs).y - model->getMeshMinBound(rs).y);
    float r = raw_half_height * SCALE_FACTOR;
    for (int i = 0; i < 4; i++)
      wheels[i].radius = (r < 0.1f || r > 1.0f) ? 0.35f : r;
    std::cout << "[Car] Wheel Radius: " << wheels[0].radius << "m\n";
  }

  for (int i = 0; i < 4; i++) {
    wheels[i].isFront = (i < 2);
    wheels[i].isDriven = true;
  }
  wheels[0].localPos = wheelOffsetFL;
  wheels[1].localPos = wheelOffsetFR;
  wheels[2].localPos = wheelOffsetBL;
  wheels[3].localPos = wheelOffsetBR;

  if (meshBL >= 0)
    m_VerticalOffset = -(wheelOffsetBL.y * SCALE_FACTOR);
  else if (meshFL >= 0)
    m_VerticalOffset = -(wheelOffsetFL.y * SCALE_FACTOR);

  carCenter =
      (wheelOffsetFL + wheelOffsetFR + wheelOffsetBL + wheelOffsetBR) * 0.25f;

  // Track width/wheelbase are already in meters because they use SCALE_FACTOR above.
  
  std::cout << "[Car] Body=" << meshBody << " FL=" << meshFL << " FR=" << meshFR
            << " BL=" << meshBL << " BR=" << meshBR
            << "\n[Car] WB=" << m_Wheelbase << "m TW=" << m_TrackWidth << "m\n";
}

// SECTION 4 — PACEJKA TIRE MODEL
// ================================================================
float Car::tireForce(float slip, float load) {
  // Sim-Cade Tire Parameters (High grip, forgiving slide)
  // B: Stiffness, C: Shape, E: Curvature
  const float B = 10.0f, C = 1.3f, E = 1.0f;
  return load * std::sin(C * std::atan(B * slip -
                                       E * (B * slip - std::atan(B * slip))));
}

float Car::getSpeed() const {
  return std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
}

// SECTION 8 — Surface grip from terrain zone
static float surfaceGrip(Terrain *t, float x, float z) {
  switch (t->getZoneAt(x, z)) {
  case Terrain::Zone::SEA:
    return 0.10f;
  case Terrain::Zone::BEACH:
    return 0.55f;
  case Terrain::Zone::PLAIN:
    return 0.85f;
  case Terrain::Zone::WADI:
    return 0.70f;
  case Terrain::Zone::PORT:
  case Terrain::Zone::MEDINA:
  case Terrain::Zone::VILLE_NOUVELLE:
  case Terrain::Zone::KASBAH:
    return 1.10f;
  default:
    return 0.90f;
  }
}

// Wheel contact patch XZ (YAW ONLY — pitch/roll are outputs, not inputs)
glm::vec3 Car::getWheelWorld(const glm::vec3 &lo) const {
  glm::vec3 rel = (lo - carCenter) * SCALE_FACTOR;
  glm::vec3 rotatedRel = rotation * rel;
  return glm::vec3(position.x + rotatedRel.x, position.y + rotatedRel.y,
                   position.z + rotatedRel.z);
}

// Yaw-only wheel XZ position — used for terrain height queries.
// Deliberately excludes pitch/roll to break the circular dependency where
// pitch/roll angles would shift the contact patch, changing the sampled
// height, which would then change the angles again.
glm::vec3 Car::getWheelContactPatch(const glm::vec3 &lo) const {
  glm::vec3 rel = (lo - carCenter) * SCALE_FACTOR;
  glm::quat q = glm::angleAxis(-yaw + glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
  glm::vec3 rotatedRel = q * rel;
  return glm::vec3(position.x + rotatedRel.x, position.y, position.z + rotatedRel.z);
}

// ================================================================
// CONSTRUCTOR / DESTRUCTOR
// ================================================================
Car::Car(State initialState) : isGrounded(true), state(initialState) {
  position = Vec3(4110.0f, -46.5f, -60.0f);
  velocity = Vec3(0, 0, 0);
  angularVel = glm::vec3(0);
  rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  yaw = steerAngle = driftAngle = bodyShake = pitchAngle = rollAngle = 0.0f;
  m_VerticalOffset = 0.0f;
  m_Driver = nullptr;

  engine.rpm = 800.0f;
  engine.gear = 2; // Start in 1st (gear[1]=N, gear[2]=1st)

  // AI initialization
  aiTargetYaw = yaw;
  aiThinkTimer = 0.0f;
  aiLostTimer = 0.0f;
  needsInitialSnap = true;

  for (int i = 0; i < 4; i++) {
    wheels[i].prevLength = wheels[i].suspensionRest;
    wheels[i].currentLength = wheels[i].suspensionRest;
    wheels[i].spinAngle = 0.0f;
    wheels[i].angularVelocity = 0.0f;
  }

  // Use shared resources if available
  if (!sharedCarModel) {
    sharedCarModel = new ObjModel("assets/cars/R40CVPainted.fbx");
    sharedCarShader =
        new Shader("src/shaders/model.vert", "src/shaders/model.frag");
  }

  model = sharedCarModel;
  carShader = sharedCarShader;
  carInstanceCount++;

  resolveWheelMeshes();
  std::cout << "[Car] Instance " << carInstanceCount << " Ready.\n";
}
Car::~Car() {
  carInstanceCount--;
  if (carInstanceCount <= 0) {
    delete sharedCarModel;
    delete sharedCarShader;
    sharedCarModel = nullptr;
    sharedCarShader = nullptr;
  }
}

// ================================================================
// IDLE PHYSICS
// ================================================================
void Car::updateIdlePhysics(float dt, Terrain *terrain) {
  // ── Sample terrain under each wheel using YAW-ONLY positions ────────────
  float h[4];
  for (int i = 0; i < 4; i++) {
    glm::vec3 cp = getWheelContactPatch(wheels[i].localPos);
    h[i] = terrain->getHeight(cp.x, cp.z);
  }

  // ── Vertical position: snap car center so wheels rest on ground ─────────
  float avgH = (h[0] + h[1] + h[2] + h[3]) * 0.25f;
  float equilibriumCompression = (MASS * 9.81f * 0.25f) / SPRING_K;
  float rideHeight = wheels[0].suspensionRest - equilibriumCompression;
  float targetY = avgH + wheels[0].radius + rideHeight;

  // Fast exponential convergence — feels like stiff springs at rest
  float snapK = 12.0f;
  if (needsInitialSnap) {
    position.y = targetY;
    needsInitialSnap = false;
  } else {
    position.y += (targetY - position.y) * (1.0f - std::exp(-snapK * dt));
  }
  velocity = Vec3(0.0f, 0.0f, 0.0f);
  angularVel = glm::vec3(0.0f);

  // ── Pitch: front/rear height difference ─────────────────────────────────
  float frontH = (h[0] + h[1]) * 0.5f;
  float rearH = (h[2] + h[3]) * 0.5f;
  float tgtPitch = std::atan2(frontH - rearH, m_Wheelbase);
  pitchAngle += (tgtPitch - pitchAngle) * (1.0f - std::exp(-10.0f * dt));

  // ── Roll: left/right height difference ──────────────────────────────────
  float leftH = (h[0] + h[2]) * 0.5f;
  float rightH = (h[1] + h[3]) * 0.5f;
  float tgtRoll = std::atan2(rightH - leftH, m_TrackWidth);
  rollAngle += (tgtRoll - rollAngle) * (1.0f - std::exp(-10.0f * dt));

  pitchAngle = glm::clamp(pitchAngle, -0.35f, 0.35f);
  rollAngle = glm::clamp(rollAngle, -0.40f, 0.40f);

  // Reconstruct rotation
  rotation = glm::angleAxis(-yaw + glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f)) *
             glm::angleAxis(pitchAngle, glm::vec3(1.0f, 0.0f, 0.0f)) *
             glm::angleAxis(rollAngle, glm::vec3(0.0f, 0.0f, 1.0f));

  isGrounded = true;
}

void Car::updateRoamingAI(float dt, Terrain *terrain, float totalTime) {
  constexpr float PI = 3.14159265f;
  constexpr int RING_SAMPLES = 16;
  constexpr float RING_RADIUS = 15.0f; // sample ring distance

  // 1. Sample road values in a ring around the car
  float ringVal[RING_SAMPLES];
  float ringAngle[RING_SAMPLES];
  for (int i = 0; i < RING_SAMPLES; i++) {
    float a = (float)i * (2.0f * PI / RING_SAMPLES);
    ringAngle[i] = a;
    float sx = position.x + std::sin(a) * RING_RADIUS;
    float sz = position.z - std::cos(a) * RING_RADIUS;
    ringVal[i] = terrain->getRoadAt(sx, sz);
  }

  // 2. Find the best sample direction
  int bestIdx = 0;
  for (int i = 1; i < RING_SAMPLES; i++) {
    if (ringVal[i] > ringVal[bestIdx])
      bestIdx = i;
  }

  float bestVal = ringVal[bestIdx];

  if (bestVal < 0.2f) {
    // No road found anywhere nearby — become parked
    aiLostTimer += dt;
    if (aiLostTimer > 10.0f) {
      state = State::PARKED;
      engine.throttle = 0.0f;
      return;
    }
    // Coast and sweep
    engine.throttle = 0.0f;
    steerAngle = std::sin(totalTime * 0.5f) * 0.3f;
  } else {
    aiLostTimer = 0.0f;

    // 3. Compute desired heading: steer toward the best road sample,
    //    but prefer the direction closest to current heading to avoid U-turns
    float desiredYaw = ringAngle[bestIdx];

    // Also check the opposite direction on the ring for a "road corridor"
    int oppIdx = (bestIdx + RING_SAMPLES / 2) % RING_SAMPLES;
    if (ringVal[oppIdx] > 0.3f) {
      float diffBest = desiredYaw - yaw;
      while (diffBest > PI)
        diffBest -= 2.0f * PI;
      while (diffBest < -PI)
        diffBest += 2.0f * PI;

      float diffOpp = ringAngle[oppIdx] - yaw;
      while (diffOpp > PI)
        diffOpp -= 2.0f * PI;
      while (diffOpp < -PI)
        diffOpp += 2.0f * PI;

      if (std::abs(diffOpp) < std::abs(diffBest)) {
        desiredYaw = ringAngle[oppIdx];
      }
    }

    // 4. Steer toward the desired heading
    float yawDiff = desiredYaw - yaw;
    while (yawDiff > PI)
      yawDiff -= 2.0f * PI;
    while (yawDiff < -PI)
      yawDiff += 2.0f * PI;

    // Set steering angle for physics (inverted because negative steerAngle is turn right, positive is turn left)
    float targetSteer =
        -glm::clamp(yawDiff * 1.5f, -MAX_STEER_ANGLE, MAX_STEER_ANGLE);
    steerAngle += (targetSteer - steerAngle) * (1.0f - std::exp(-5.0f * dt));

    // Throttle control
    float roadQuality = terrain->getRoadAt(position.x, position.z);
    float targetSpeed = 12.0f; // AI cruising speed
    if (roadQuality < 0.4f)
      targetSpeed = 5.0f;

    if (getSpeed() < targetSpeed) {
      engine.throttle = 0.5f;
    } else {
      engine.throttle = 0.0f;
    }
  }
}

void Car::update(float dt, Terrain *terrain, float totalTime, Camera *camera) {
  if (!terrain)
    return;

  // ── One-time initial snap to terrain ─────────────────────────────────────
  if (needsInitialSnap) {
    needsInitialSnap = false;
    
    // Get terrain heights at all 4 wheel contact patches
    float h[4];
    for (int i = 0; i < 4; i++) {
      glm::vec3 cp = getWheelContactPatch(wheels[i].localPos);
      h[i] = terrain->getHeight(cp.x, cp.z);
    }
    float avgH = (h[0] + h[1] + h[2] + h[3]) * 0.25f;
    
    // Seed pitch/roll from terrain (frontH - rearH lowers nose on downhill slope)
    pitchAngle = std::atan2((h[0] + h[1]) * 0.5f - (h[2] + h[3]) * 0.5f, m_Wheelbase);
    rollAngle = std::atan2((h[1] + h[3]) * 0.5f - (h[0] + h[2]) * 0.5f, m_TrackWidth);
    
    // Calculate position.y to place wheels at equilibrium ride height
    float equilibriumCompression = (MASS * (-GRAVITY) * 0.25f) / SPRING_K;
    float rideHeight = wheels[0].suspensionRest - equilibriumCompression;
    
    // Start from center ground height + radius + ride height
    position.y = avgH + wheels[0].radius + rideHeight;
    velocity = Vec3(0, 0, 0);
    angularVel = glm::vec3(0.0f);
    
    // Reset each wheel's suspension state so there's no damping spike
    for (int i = 0; i < 4; i++) {
      glm::vec3 wp = getWheelWorld(wheels[i].localPos);
      float distToGround = wp.y - h[i];
      float wheelHeight = distToGround - wheels[i].radius;
      wheels[i].currentLength = glm::clamp(wheelHeight, 0.0f, wheels[i].suspensionRest);
      wheels[i].prevLength = wheels[i].currentLength;
      wheels[i].spinAngle = 0.0f;
      wheels[i].angularVelocity = 0.0f;
      wheels[i].grounded = true;
    }

    rotation = glm::angleAxis(-yaw + glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f)) *
               glm::angleAxis(pitchAngle, glm::vec3(1.0f, 0.0f, 0.0f)) *
               glm::angleAxis(rollAngle, glm::vec3(0.0f, 0.0f, 1.0f));

    std::cout << "[Car Snap] pos.y=" << position.y << " avgH=" << avgH
              << " rideH=" << rideHeight << " equilComp=" << equilibriumCompression
              << "\n  h={";
    for (int i = 0; i < 4; i++) std::cout << h[i] << (i<3?",":"");
    std::cout << "} pitch=" << pitchAngle << " roll=" << rollAngle
              << " MASS=" << MASS << " SPRING_K=" << SPRING_K << "\n";
  }

  if (state == State::ROAMING) {
    updateRoamingAI(dt, terrain, totalTime);
  }

  // Fixed 120Hz update loop
  physicsAccumulator += dt;
  const float physicsStep = 1.0f / 120.0f;
  
  if (physicsAccumulator > 0.25f) {
    physicsAccumulator = 0.25f;
  }

  while (physicsAccumulator >= physicsStep) {
    if (state == State::PARKED) {
      updateIdlePhysics(physicsStep, terrain);
    } else {
      updateActivePhysics(physicsStep, terrain);
    }
    physicsAccumulator -= physicsStep;
  }
}

// ================================================================
// ACTIVE PHYSICS (MODULAR SUBSYSTEMS)
// ================================================================

glm::vec3 Car::getForwardVector() const {
  return rotation * glm::vec3(0.0f, 0.0f, -1.0f);
}
glm::vec3 Car::getRightVector() const {
  return rotation * glm::vec3(1.0f, 0.0f, 0.0f);
}

void Car::processInput(float dt, bool &gas, bool &braking, bool &handbrake) {
  if (!m_Driver || !m_Driver->isDriving()) return;


  gas = m_Driver->keys['w'] || m_Driver->keys['W'] || m_Driver->keys['z'] ||
        m_Driver->keys['Z'];
  braking = m_Driver->keys['s'] || m_Driver->keys['S'];
  bool left = m_Driver->keys['a'] || m_Driver->keys['A'] ||
              m_Driver->keys['q'] || m_Driver->keys['Q'];
  bool right = m_Driver->keys['d'] || m_Driver->keys['D'];
  handbrake = glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_SPACE) == GLFW_PRESS;

  // 1. Speed-Sensitive Steering (GTA Style)
  float speed = getSpeed();
  float speedFactor = glm::clamp(1.0f - (speed * 0.022f), 0.35f, 1.0f);
  float targetSteer = (left ? 1.0f : 0.0f) - (right ? 1.0f : 0.0f);
  
  float maxSteer = MAX_STEER_ANGLE * speedFactor;
  float steerSpeed = 7.5f;
  
  steerAngle += (targetSteer * maxSteer - steerAngle) * (1.0f - std::exp(-steerSpeed * dt));

  // 2. Horn Input (F Key)
  if (glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_F) == GLFW_PRESS) {
    AudioSystem::getInstance().playHorn(glm::vec3(position.x, position.y, position.z));
  } else {
    AudioSystem::getInstance().stopHorn();
  }
}

void Car::updateTransmission(float dt, bool gas, bool braking) {
  float currentSpeedKmh = getSpeed() * 3.6f;

  if (currentSpeedKmh < 2.0f) {
    if (gas && engine.gear <= 1)
      engine.gear = 2; // Shift to 1st
    if (braking && engine.gear != 0)
      engine.gear = 0; // Shift to Reverse
  }

  if (engine.gear >= 2) {
    if (engine.rpm > 6000.0f && engine.gear < 7) {
      engine.gear++;
      engine.rpm -= 1500.0f;
    } else if (engine.rpm < 2500.0f && engine.gear > 2 &&
               currentSpeedKmh > 10.0f) {
      engine.gear--;
      engine.rpm += 1000.0f;
    }
  }

  if (engine.gear == 0) { // Reverse
    engine.throttle = braking ? 1.0f : 0.0f;
  } else { // Forward
    engine.throttle = gas ? 1.0f : 0.0f;
  }

  float avgWheelAV =
      (wheels[2].angularVelocity + wheels[3].angularVelocity) * 0.5f;
  float targetRPM = std::abs(avgWheelAV * engine.gearRatios[engine.gear]) *
                    engine.finalDrive * 9.5493f;
  engine.rpm += (std::max(engine.minRPM, targetRPM) - engine.rpm) *
                (1.0f - std::exp(-5.0f * dt));
  engine.rpm = glm::clamp(engine.rpm, engine.minRPM, engine.maxRPM);
}

void Car::updateSuspension(float sdt, Terrain *terrain, glm::vec3 &totalForce, glm::vec3 &totalTorque) {
  glm::vec3 chassisUp = rotation * glm::vec3(0.0f, 1.0f, 0.0f);

  for (int i = 0; i < 4; i++) {
    glm::vec3 wp = getWheelWorld(wheels[i].localPos);
    glm::vec3 cp = getWheelContactPatch(wheels[i].localPos);
    float groundH = terrain->getHeight(cp.x, cp.z);

    float distToGround = wp.y - groundH;
    wheels[i].currentLength = glm::clamp(distToGround - wheels[i].radius, 0.0f,
                                         wheels[i].suspensionRest);
    wheels[i].compression = wheels[i].suspensionRest - wheels[i].currentLength;

    float compVel = (wheels[i].prevLength - wheels[i].currentLength) / sdt;
    wheels[i].prevLength = wheels[i].currentLength;

    float damping = (compVel > 0.0f) ? SPRING_DAMPING : (SPRING_DAMPING * 1.5f);
    float springF = wheels[i].compression * SPRING_K + compVel * damping;

    // Cap the force to prevent physics explosions
    float maxSpringF = MASS * 20.0f;
    if (springF > maxSpringF)
      springF = maxSpringF;
    if (springF < 0.0f)
      springF = 0.0f;

    wheels[i].load = springF;
    
    if (wheels[i].compression > 0.02f) {
      wheels[i].grounded = true;
      isGrounded = true;
    } else {
      wheels[i].grounded = false;
    }

    glm::vec3 forceVec = chassisUp * springF;
    totalForce += forceVec;

    glm::vec3 rel = wp - glm::vec3(position.x, position.y, position.z);
    totalTorque += glm::cross(rel, forceVec);
  }
}

void Car::calculateTireForces(float sdt, Terrain *terrain,
                              glm::vec3 &totalForce, glm::vec3 &totalTorque) {
  bool handbrake = false;
  bool braking = false;
  bool gas = false;

  if (state == State::DRIVEN && m_Driver && m_Driver->isDriving()) {
    gas = m_Driver->keys['w'] || m_Driver->keys['W'] || m_Driver->keys['z'] ||
          m_Driver->keys['Z'];
    braking = m_Driver->keys['s'] || m_Driver->keys['S'];
    handbrake = glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_SPACE) == GLFW_PRESS;
  } else if (state == State::ROAMING) {
    gas = (engine.throttle > 0.1f);
  }

  float engineTorque = engine.torqueCurve(engine.rpm) * engine.throttle;
  float driveTorque = engineTorque * engine.gearRatios[engine.gear] *
                      engine.finalDrive * engine.clutch;

  // Power split: RWD-biased AWD
  float torqueSplit[4] = {
    driveTorque * 0.15f, // FL
    driveTorque * 0.15f, // FR
    driveTorque * 0.35f, // RL
    driveTorque * 0.35f  // RR
  };

  for (int i = 0; i < 4; i++) {
    glm::vec3 wp = getWheelWorld(wheels[i].localPos);
    glm::vec3 cp = getWheelContactPatch(wheels[i].localPos);
    glm::vec3 rel = wp - glm::vec3(position.x, position.y, position.z);
    
    // World velocity of the contact patch
    glm::vec3 wVel = glm::vec3(velocity.x, velocity.y, velocity.z) +
                     glm::cross(angularVel, rel);

    float wYaw = yaw + (wheels[i].isFront ? steerAngle : 0.0f);
    glm::vec3 tireFwd(-std::sin(wYaw), 0.0f, std::cos(wYaw));
    glm::vec3 tireRgt(std::cos(wYaw), 0.0f, std::sin(wYaw));

    float vLong = glm::dot(wVel, tireFwd);
    float vLat = glm::dot(wVel, tireRgt);

    float roadFactor = terrain->getRoadAt(cp.x, cp.z);
    float grip = surfaceGrip(terrain, cp.x, cp.z) * (1.0f + roadFactor * 0.4f);

    // --- REALISTIC WHEEL SPIN PHYSICS ---
    float omega = wheels[i].angularVelocity;
    
    // Calculate slip ratio
    float slip = 0.0f;
    float absVLong = std::abs(vLong);
    if (absVLong > 0.1f) {
        slip = (omega * wheels[i].radius - vLong) / absVLong;
    } else {
        // Low speed slip approximation to avoid division by zero
        slip = (omega * wheels[i].radius - vLong) / 0.1f;
    }
    
    // Pacejka longitudinal tire force
    float fx_tire = tireForce(slip, wheels[i].load) * grip;

    // Torques acting on the wheel
    float T_drive = wheels[i].isDriven ? torqueSplit[i] : 0.0f;
    
    float T_brake = 0.0f;
    if (handbrake && !wheels[i].isFront) {
        T_brake = 8000.0f;
    } else if (braking) {
        T_brake = wheels[i].isFront ? 5000.0f : 3000.0f;
    } else if (std::abs(engine.throttle) < 0.1f) {
        T_brake = 400.0f; // Engine drag
    }
    T_brake += 50.0f; // Rolling resistance torque

    // Net torque on the wheel
    // The tire force creates a reaction torque opposing the wheel spin: -fx_tire * radius
    // Braking torque always opposes the spin direction: -T_brake * sign(omega)
    float signOmega = (omega > 0.05f) ? 1.0f : ((omega < -0.05f) ? -1.0f : 0.0f);
    if (signOmega == 0.0f && T_brake > std::abs(T_drive)) {
        // If wheel is stopped and braking torque dominates, keep it locked
        omega = 0.0f;
    } else {
        float T_reaction = -fx_tire * wheels[i].radius;
        float T_net = T_drive - T_brake * signOmega + T_reaction;
        
        // Wheel rotational inertia (I = 0.5 * m * r^2)
        // A typical car wheel has I ~ 1.2 kg*m^2
        float I_wheel = 1.2f;
        float omega_dot = T_net / I_wheel;
        
        omega += omega_dot * sdt;
        
        // If we cross zero while braking, lock the wheel
        if (signOmega != 0.0f && ((omega > 0.0f) != (signOmega > 0.0f)) && T_brake > std::abs(T_drive)) {
            omega = 0.0f;
        }
    }
    
    wheels[i].angularVelocity = omega;
    wheels[i].spinAngle += omega * sdt;

    // Apply the real tire force to the chassis
    float fx = fx_tire;

    // 2. Calculate lateral force (-Vs * Grip)
    float latGripMult = 1.0f;
    if (handbrake && !wheels[i].isFront) {
      latGripMult = 0.35f; // Slide the rear!
    }
    float fy = -vLat * 2.5f * latGripMult * wheels[i].load * grip;

    // 3. Friction Circle limit
    float maxF = wheels[i].load * grip * 2.0f;
    float totalTireF = std::sqrt(fx * fx + fy * fy);
    if (totalTireF > maxF && totalTireF > 0.001f) {
      float ratio = maxF / totalTireF;
      fx *= ratio;
      fy *= ratio;
    }

    glm::vec3 wForce = tireFwd * fx + tireRgt * fy;
    totalForce += wForce;

    // Torque: lower the center of mass leverage arm to prevent roll-overs
    glm::vec3 torqueRel = rel;
    torqueRel.y *= 0.3f;
    totalTorque += glm::cross(torqueRel, wForce);

    // Store debug data
    wheels[i].fx = fx;
    wheels[i].fy = fy;
    wheels[i].slipLong = slip;
    wheels[i].slipLat = std::atan2(vLat, std::abs(vLong) + 0.3f);
  }
}

void Car::applyStabilityControl(float sdt, Terrain *terrain,
                                glm::vec3 &totalTorque) {
  float speed = getSpeed();
  
  glm::vec3 localAngularVel = glm::inverse(rotation) * angularVel;
  glm::vec3 localTorque(0.0f);

  if (!isGrounded) {
    // Aerial controls (GTA style)
    bool gas = engine.throttle > 0.5f;
    bool braking = m_Driver ? (m_Driver->keys['s'] || m_Driver->keys['S']) : false;
    bool left = m_Driver ? (m_Driver->keys['a'] || m_Driver->keys['A'] ||
                            m_Driver->keys['q'] || m_Driver->keys['Q']) : false;
    bool right = m_Driver ? (m_Driver->keys['d'] || m_Driver->keys['D']) : false;

    float pitchInput = (braking ? 1.0f : 0.0f) - (gas ? 1.0f : 0.0f);
    float rollInput = (right ? 1.0f : 0.0f) - (left ? 1.0f : 0.0f);
    float yawInput = (left ? 1.0f : 0.0f) - (right ? 1.0f : 0.0f);

    localTorque.x += pitchInput * 25000.0f;
    localTorque.z += rollInput * 25000.0f;
    localTorque.y += yawInput * 15000.0f;

    // Passive leveling in air
    localTorque.x += -pitchAngle * 8000.0f;
    localTorque.z += -rollAngle * 8000.0f;
  } else {
    // Grounded stability control (ESC and steering assist)
    bool left = m_Driver ? (m_Driver->keys['a'] || m_Driver->keys['A'] ||
                            m_Driver->keys['q'] || m_Driver->keys['Q']) : false;
    bool right = m_Driver ? (m_Driver->keys['d'] || m_Driver->keys['D']) : false;
    float steerInput = (left ? 1.0f : 0.0f) - (right ? 1.0f : 0.0f);

    float roadFactor = terrain->getRoadAt(position.x, position.z);
    
    // Straight-line stabilization
    if (speed > 5.0f && std::abs(steerInput) < 0.1f) {
      glm::vec3 fwd = getForwardVector();
      glm::vec3 velDir = glm::normalize(glm::vec3(velocity.x, 0.0f, velocity.z));
      float dotVal = glm::clamp(glm::dot(fwd, velDir), -1.0f, 1.0f);
      if (dotVal > 0.2f) {
        float alignSign = fwd.x * velDir.z - fwd.z * velDir.x;
        localTorque.y -= alignSign * 18000.0f * (1.0f + roadFactor * 2.0f);
      }
    }

    // Kinematic yaw rate targeting (ESC)
    float velocityLong = glm::dot(glm::vec3(velocity.x, velocity.y, velocity.z), getForwardVector());
    float desiredLocalYawRate = -steerAngle * (velocityLong / m_Wheelbase);
    
    float escYawTorque = (desiredLocalYawRate - localAngularVel.y) * 22000.0f;
    localTorque.y += escYawTorque;

    // Steering assist
    if (std::abs(steerInput) > 0.1f) {
      localTorque.y -= steerInput * speed * 3000.0f;
    }
  }

  // Transform local torques to world space
  totalTorque += rotation * localTorque;
}

void Car::applyAerodynamics(float dt, glm::vec3 &totalForce) {
  float speedSq = velocity.x * velocity.x + velocity.z * velocity.z;
  if (speedSq > 0.1f) {
    glm::vec3 dragDir = glm::normalize(glm::vec3(-velocity.x, 0.0f, -velocity.z));
    float dragForce = 0.35f * speedSq;
    float downForce = 1.5f * speedSq;
    totalForce += dragDir * dragForce;
    totalForce.y -= downForce;
  }
}

void Car::updateActivePhysics(float dt, Terrain *terrain) {
  if (!terrain)
    return;

  bool gas = false, braking = false, handbrake = false;
  if (state == State::DRIVEN && m_Driver && m_Driver->isDriving()) {
    processInput(dt, gas, braking, handbrake);
    updateTransmission(dt, gas, braking);
  } else {
    updateTransmission(dt, engine.throttle > 0.1f, false);
  }

  glm::vec3 totalForce(0.0f, MASS * GRAVITY, 0.0f);
  glm::vec3 totalTorque(0.0f);

  isGrounded = false;
  updateSuspension(dt, terrain, totalForce, totalTorque);
  calculateTireForces(dt, terrain, totalForce, totalTorque);
  applyStabilityControl(dt, terrain, totalTorque);
  applyAerodynamics(dt, totalForce);

  // Integrate linear motion
  velocity.x += (totalForce.x / MASS) * dt;
  velocity.y += (totalForce.y / MASS) * dt;
  velocity.z += (totalForce.z / MASS) * dt;

  // velocity.y damping removed to allow natural suspension physics

  position.x += velocity.x * dt;
  position.y += velocity.y * dt;
  position.z += velocity.z * dt;

  // Integrate angular motion
  angularVel.x += (totalTorque.x / INERTIA_PITCH) * dt;
  angularVel.y += (totalTorque.y / INERTIA_YAW) * dt;
  angularVel.z += (totalTorque.z / INERTIA_ROLL) * dt;
  angularVel *= BODY_DAMPING;

  // Update orientation
  if (!isGrounded) {
    yaw += angularVel.y * dt;
    pitchAngle += angularVel.x * dt;
    rollAngle += angularVel.z * dt;
    pitchAngle = glm::clamp(pitchAngle, -0.5f, 0.5f);
    rollAngle = glm::clamp(rollAngle, -0.5f, 0.5f);

    rotation = glm::angleAxis(-yaw + glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f)) *
               glm::angleAxis(pitchAngle, glm::vec3(1.0f, 0.0f, 0.0f)) *
               glm::angleAxis(rollAngle, glm::vec3(0.0f, 0.0f, 1.0f));
  } else {
    yaw += angularVel.y * dt;

    float fwdAccel = 0.0f;
    float latAccel = 0.0f;
    if (dt > 1e-6f) {
      fwdAccel = glm::dot(glm::vec3(velocity.x, 0.0f, velocity.z) -
                              glm::vec3(prevVel.x, 0.0f, prevVel.z),
                          getForwardVector()) / dt;
      latAccel = glm::dot(glm::vec3(velocity.x, 0.0f, velocity.z) - prevVel,
                          getRightVector()) / dt;
    }
    prevVel = glm::vec3(velocity.x, velocity.y, velocity.z);

    float h[4];
    for (int i = 0; i < 4; i++) {
      glm::vec3 cp = getWheelContactPatch(wheels[i].localPos);
      h[i] = terrain->getHeight(cp.x, cp.z);
    }

    float frontH = (h[0] + h[1]) * 0.5f;
    float rearH = (h[2] + h[3]) * 0.5f;
    float leftH = (h[0] + h[2]) * 0.5f;
    float rightH = (h[1] + h[3]) * 0.5f;

    float terrainPitch = std::atan2(frontH - rearH, m_Wheelbase);
    float terrainRoll = std::atan2(rightH - leftH, m_TrackWidth);

    float wtPitch = fwdAccel * 0.0035f;
    float wtRoll = latAccel * 0.0045f;

    float targetPitch = terrainPitch + wtPitch;
    float targetRoll = terrainRoll + wtRoll;

    pitchAngle += (targetPitch - pitchAngle) * (1.0f - std::exp(-15.0f * dt));
    rollAngle += (targetRoll - rollAngle) * (1.0f - std::exp(-15.0f * dt));

    pitchAngle = glm::clamp(pitchAngle, -0.35f, 0.35f);
    rollAngle = glm::clamp(rollAngle, -0.40f, 0.40f);

    rotation = glm::angleAxis(-yaw + glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f)) *
               glm::angleAxis(pitchAngle, glm::vec3(1.0f, 0.0f, 0.0f)) *
               glm::angleAxis(rollAngle, glm::vec3(0.0f, 0.0f, 1.0f));
  }

  // Hard floor snap to prevent chassis tunneling into ground
  {
    float groundH = terrain->getHeight(position.x, position.z);
    float floorY = groundH + 0.16f;
    if (position.y < floorY) {
      position.y = floorY;
      if (velocity.y < 0.0f)
        velocity.y = 0.0f;
    }
  }

  driftAngle += (-(glm::dot(glm::vec3(velocity.x, 0.0f, velocity.z), getRightVector())) * 0.25f - driftAngle) * (1.0f - std::exp(-6.0f * dt));

  float currentSpeed = getSpeed();
  bool allWheelsStopped = true;
  for (int i = 0; i < 4; i++) {
    if (std::abs(wheels[i].angularVelocity) > 0.1f)
      allWheelsStopped = false;
  }
  if (allWheelsStopped && currentSpeed < 1.0f && std::abs(engine.throttle) < 0.1f) {
    velocity = Vec3(0.0f, 0.0f, 0.0f);
    angularVel = glm::vec3(0.0f);
  } else if (std::abs(engine.throttle) < 0.1f && currentSpeed < 0.4f) {
    velocity = velocity * 0.8f;
    angularVel *= 0.8f;
    if (currentSpeed < 0.05f) {
      velocity = Vec3(0.0f, 0.0f, 0.0f);
    }
  }

  if (state == State::DRIVEN) {
    AudioSystem::getInstance().updateEngine(engine.rpm, engine.throttle, engine.maxRPM, glm::vec3(position.x, position.y, position.z));
  }

  bodyShake = 0.0f;
  if (engine.rpm >= engine.maxRPM - 300.0f) {
    bodyShake = ((rand() % 100) / 100.0f - 0.5f) * 0.018f;
  }

  if (m_Driver && m_Driver->isDriving()) {
    glm::vec3 seat = getDriverSeatWorldPosition();
    m_Driver->setPosition(Vec3(seat.x, seat.y, seat.z));
    m_Driver->setRotation(yaw, glm::vec3(0, 1, 0));
  }
}


// ================================================================
// MOUNT / DISMOUNT
// ================================================================
void Car::mountCharacter(Character *c) {
  if (!c || m_Driver)
    return;
  m_Driver = c;
  state = State::DRIVEN;
  c->setMountedCar(this);
  angularVel = glm::vec3(0);
  for (int i = 0; i < 4; i++)
    wheels[i].prevLength = wheels[i].currentLength;

  AudioSystem::getInstance().playIgnition(glm::vec3(position.x, position.y, position.z));
  std::cout << "[Car] Mounted.\n";
}
void Car::dismountCharacter(Character *c) {
  if (m_Driver != c)
    return;
  m_Driver = nullptr;
  state = State::PARKED;
  engine.throttle = 0;
  AudioSystem::getInstance().playStop(glm::vec3(position.x, position.y, position.z));
  std::cout << "[Car] Dismounted.\n";
}

// ================================================================
// RENDER
// ================================================================
void Car::render(const glm::mat4 &projection, const glm::mat4 &view,
                 Terrain *terrain) {
  if (!model || !carShader)
    return;

  carShader->use();
  carShader->setMat4("projection", projection);
  carShader->setMat4("view", view);
  carShader->setBool("isInstanced", false);
  carShader->setBool("deformToTerrain", false);

  glm::mat4 base(1.0f);
  base = glm::translate(base, glm::vec3(position.x, position.y + bodyShake, position.z));

  // Combine physics rotation with visual drift angle
  glm::quat visualRotation = rotation * glm::angleAxis(driftAngle * 0.1f, glm::vec3(0.0f, 1.0f, 0.0f));
  base = base * glm::mat4_cast(visualRotation);

  base = glm::scale(base, glm::vec3(SCALE_FACTOR));
  base = glm::translate(base, -carCenter); // Center model on hubs

  glm::mat4 R = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));

  if (meshBody >= 0) {
    glm::mat4 bodyMat = base;
    if (isPaintedModel) {
      bodyMat = bodyMat * R;
    }
    carShader->setMat4("model", bodyMat);
    model->renderMesh(meshBody, *carShader);
  }

  auto renderWheel = [&](int idx, int wi, bool front, const glm::vec3 &ctr) {
    if (idx < 0)
      return;

    // Suspension offset relative to equilibrium ride height (0 at rest)
    float equilComp = (MASS * (-GRAVITY) * 0.25f) / SPRING_K;
    float rideLength = wheels[wi].suspensionRest - equilComp;
    float suspY = -(wheels[wi].currentLength - rideLength) / SCALE_FACTOR;

    glm::mat4 wm = base;
    wm = glm::translate(wm, ctr + glm::vec3(0, suspY, 0));
    if (front)
      wm = glm::rotate(wm, steerAngle, glm::vec3(0, 1, 0));
    wm = glm::rotate(wm, -wheels[wi].spinAngle, glm::vec3(1, 0, 0));
    
    if (isPaintedModel) {
      wm = wm * R;
      // Translate back by the raw wheel center
      glm::vec3 ctr_raw = model->getMeshCenter(idx);
      wm = glm::translate(wm, -ctr_raw);
    } else {
      wm = glm::translate(wm, -ctr);
    }

    carShader->setMat4("model", wm);
    model->renderMesh(idx, *carShader);
  };

  renderWheel(meshFL, 0, true, wheelOffsetFL);
  renderWheel(meshFR, 1, true, wheelOffsetFR);
  renderWheel(meshBL, 2, false, wheelOffsetBL);
  renderWheel(meshBR, 3, false, wheelOffsetBR);

  for (int i = 0; i < model->getMeshCount(); i++)
    if (i != meshBody && i != meshFL && i != meshFR && i != meshBL &&
        i != meshBR) {
      glm::mat4 extraMat = base;
      if (isPaintedModel) {
        extraMat = extraMat * R;
      }
      carShader->setMat4("model", extraMat);
      model->renderMesh(i, *carShader);
    }
}