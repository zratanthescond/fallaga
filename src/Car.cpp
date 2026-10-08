#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <cctype>

#include "AudioSystem.h"
#include "Camera.h"
#include "Car.h"
#include "CarHandling.h"
#include "Character.h"
#include "ObjectModel.h"
#include "Shader.h"
#include "Terrain.h"
#include "vehicle/VehicleBody.h"

// ================================================================
// MESH RESOLVER
// ================================================================
// Shared resources across all Car instances to save memory and avoid redundant
// loading
static ObjModel *sharedCarModel = nullptr;
static Shader *sharedCarShader = nullptr;
static int carInstanceCount = 0;

namespace {
struct RoadRoutePoint {
  glm::vec2 position{0.0f};
  glm::vec2 direction{1.0f, 0.0f};
  float curvature = 0.0f;
};

bool sampleRoadRoute(const std::vector<RoadNavigationSample> &samples,
                     float distance, RoadRoutePoint &out) {
  if (samples.size() < 2)
    return false;
  distance = glm::clamp(distance, samples.front().distance,
                        samples.back().distance);
  const auto upper = std::upper_bound(
      samples.begin(), samples.end(), distance,
      [](float d, const RoadNavigationSample &sample) {
        return d < sample.distance;
      });
  const size_t i1 = upper == samples.end()
                        ? samples.size() - 1
                        : static_cast<size_t>(upper - samples.begin());
  const size_t i0 = i1 > 0 ? i1 - 1 : 0;
  const float span = samples[i1].distance - samples[i0].distance;
  const float t = span > 1e-5f
                      ? glm::clamp((distance - samples[i0].distance) / span,
                                   0.0f, 1.0f)
                      : 0.0f;
  out.position = glm::mix(samples[i0].position, samples[i1].position, t);
  out.direction = glm::mix(samples[i0].direction, samples[i1].direction, t);
  const float directionLength = glm::length(out.direction);
  if (directionLength > 1e-5f)
    out.direction /= directionLength;
  else
    out.direction = samples[i0].direction;
  out.curvature = glm::mix(samples[i0].curvature,
                           samples[i1].curvature, t);
  return true;
}

uint32_t chooseRoadRouteConnector(
    const RoadLane &lane,
    const std::vector<RoadLaneConnector> &connectors,
    uint32_t &seed) {
  std::vector<uint32_t> options;
  for (uint32_t id : lane.outgoingConnectorIds) {
    if (id < connectors.size() && connectors[id].fromLaneId == lane.id)
      options.push_back(id);
  }
  if (options.empty())
    return UINT32_MAX;
  seed = seed * 1664525u + 1013904223u;
  return options[seed % options.size()];
}
} // namespace

void Car::resolveWheelMeshes() {
  if (!model)
    return;
  meshBody = meshFL = meshFR = meshBL = meshBR = -1;
  isPaintedModel = false;
  wheelLayoutResolved = false;

  const int total = model->getMeshCount();
  std::cout << "[Car] " << total << " meshes:\n";
  for (int i = 0; i < total; i++)
    std::cout << "  [" << i << "] node=\"" << model->getMeshNodeName(i)
              << "\" mesh=\"" << model->getMeshName(i) << "\"\n";

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

  // Prefer the explicit FBX part names. The generic mesh names (Mesh.001,
  // Mesh.002, ...) do not identify the wheels or body in this asset.
  const auto containsToken = [](std::string value, const char *token) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value.find(token) != std::string::npos;
  };
  std::vector<MeshInfo> namedWheels;
  for (const auto &info : infos) {
    const std::string nodeName = model->getMeshNodeName(info.index);
    if (containsToken(nodeName, "body")) meshBody = info.index;
    if (containsToken(nodeName, "weel") || containsToken(nodeName, "wheel"))
      namedWheels.push_back(info);
  }

  // The largest mesh is the fallback body when an asset has no named body.
  if (!infos.empty()) {
    if (meshBody < 0) meshBody = infos[0].index;
    std::cout << "[Car] Resolved Body mesh index: " << meshBody << " (volume=" << infos[0].volume << ")\n";
  }

  // Filter out the body (and any door/other large parts) to find wheels.
  // The wheels are the 4 meshes with the smallest, similar volumes.
  std::vector<MeshInfo> candidates = namedWheels;
  if (candidates.size() != 4) {
    candidates.clear();
    for (const auto &info : infos)
      if (info.index != meshBody) candidates.push_back(info);
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
      // The body render transform includes a 180-degree Y rotation, so
      // negative model-space X becomes the vehicle's world-space left side.
      bool l = CarHandling::modelSpaceXIsVehicleLeft(stdC.x, mx);
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
    wheelLayoutResolved = true;
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

  if (!wheelLayoutResolved) {
    // Wheel detection is heuristic (part-name tokens, then the four smallest
    // AABBs), so it can legitimately fail on an unfamiliar model. Fall back to a
    // symmetric layout derived from the configured wheelbase/track width instead
    // of driving the tire solver from indeterminate hub positions.
    const float halfTrack = std::max(0.5f, m_TrackWidth * 0.5f) / SCALE_FACTOR;
    const float halfBase = std::max(1.0f, m_Wheelbase * 0.5f) / SCALE_FACTOR;
    wheelOffsetFL = glm::vec3(-halfTrack, 0.0f, -halfBase);
    wheelOffsetFR = glm::vec3(halfTrack, 0.0f, -halfBase);
    wheelOffsetBL = glm::vec3(-halfTrack, 0.0f, halfBase);
    wheelOffsetBR = glm::vec3(halfTrack, 0.0f, halfBase);
    std::cerr << "[Car] Wheel detection failed; falling back to a synthetic "
              << m_Wheelbase << "m x " << m_TrackWidth << "m wheel layout.\n";
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
// ================================================================// SECTION 8 — Surface grip from terrain zone// Wheel contact patch XZ (YAW ONLY — pitch/roll are outputs, not inputs)
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
  if (!loadVehicleConfigJson("assets/cars/renault_40cv.json", drivingConfig))
    std::cerr << "[Car] Failed to load Renault 40 CV tuning; using defaults.\n";
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
  m_Wheelbase = drivingConfig.wheelbaseM;
  m_TrackWidth = drivingConfig.trackWidthM;
  SPRING_K = drivingConfig.springRateNPerM;
  SPRING_DAMPING = drivingConfig.compressionDampingNsPerM;
  engine.minRPM = drivingConfig.idleRpm;
  engine.maxRPM = drivingConfig.maxRpm;
  engine.finalDrive = drivingConfig.finalDrive;
  engine.gearRatios = drivingConfig.gearRatios;
  engine.torquePoints = drivingConfig.engineTorqueCurve;
  engine.rpm = engine.minRPM;
  const glm::vec3 chassisHalfExtents(
      std::max(0.55f, m_TrackWidth * 0.48f), 0.48f,
      std::max(0.9f, m_Wheelbase * 0.48f));
  vehicleBody = std::make_unique<VehicleBody>(
      drivingConfig.massKg, chassisHalfExtents,
      drivingConfig.centerOfMassDropM, drivingConfig.yawInertiaKgM2);
  std::cout << "[Car] Instance " << carInstanceCount << " Ready. Config="
            << drivingConfig.id << " Mass=" << drivingConfig.massKg
            << " kg.\n";
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
    h[i] = terrain->getGroundSurfaceHeight(cp.x, cp.z);
  }

  // ── Vertical position: snap car center so wheels rest on ground ─────────
  float avgH = (h[0] + h[1] + h[2] + h[3]) * 0.25f;
  float equilibriumCompression =
      (drivingConfig.massKg * 9.81f * 0.25f) / SPRING_K;
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

bool Car::startRoadRoute(Terrain *terrain, uint32_t laneId,
                         float startDistance, uint32_t routeSeed) {
  if (!terrain)
    return false;
  const RoadNavigationGraph &navigation = terrain->getRoadNavigationGraph();
  const auto &lanes = navigation.lanes();
  if (!navigation.isBuilt() || laneId >= lanes.size())
    return false;
  const RoadLane &lane = lanes[laneId];
  RoadRoutePoint routePoint;
  if (lane.samples.size() < 2 ||
      !sampleRoadRoute(lane.samples, startDistance, routePoint))
    return false;

  roadRouteEnabled = true;
  roadRouteStopped = false;
  roadRouteOnConnector = false;
  roadRouteLaneId = laneId;
  roadRouteConnectorId = 0;
  roadRouteSeed = routeSeed ? routeSeed : 1;
  roadRouteNextConnectorId = lane.closed
      ? UINT32_MAX
      : chooseRoadRouteConnector(lane, navigation.laneConnectors(),
                                 roadRouteSeed);
  roadRouteLaneCount = navigation.lanes().size();
  roadRouteConnectorCount = navigation.laneConnectors().size();
  roadRouteProgress = glm::clamp(startDistance, 0.0f, lane.length);
  roadRouteConnectorProgress = 0.0f;
  roadRouteSpeedMps = 0.0f;
  roadRouteTrafficSpeedCapMps = 100.0f;
  state = State::ROAMING;
  needsInitialSnap = false;
  hasPreviousRenderState = false;
  physicsAccumulator = 0.0f;
  velocity = Vec3(0.0f, 0.0f, 0.0f);
  angularVel = glm::vec3(0.0f);
  engine.throttle = 0.0f;

  position.x = routePoint.position.x;
  position.z = routePoint.position.y;
  const glm::vec2 direction = glm::normalize(routePoint.direction);
  yaw = std::atan2(-direction.x, direction.y);

  const glm::vec2 right(direction.y, -direction.x);
  const float frontHeight = terrain->getGroundSurfaceHeight(
      position.x + direction.x * (m_Wheelbase * 0.5f),
      position.z + direction.y * (m_Wheelbase * 0.5f));
  const float rearHeight = terrain->getGroundSurfaceHeight(
      position.x - direction.x * (m_Wheelbase * 0.5f),
      position.z - direction.y * (m_Wheelbase * 0.5f));
  const float leftHeight = terrain->getGroundSurfaceHeight(
      position.x - right.x * (m_TrackWidth * 0.5f),
      position.z - right.y * (m_TrackWidth * 0.5f));
  const float rightHeight = terrain->getGroundSurfaceHeight(
      position.x + right.x * (m_TrackWidth * 0.5f),
      position.z + right.y * (m_TrackWidth * 0.5f));
  pitchAngle = glm::clamp(std::atan2(frontHeight - rearHeight, m_Wheelbase),
                          -0.35f, 0.35f);
  rollAngle = glm::clamp(std::atan2(rightHeight - leftHeight, m_TrackWidth),
                         -0.40f, 0.40f);
  position.y = terrain->getGroundSurfaceHeight(position.x, position.z) +
               wheels[0].radius + wheels[0].suspensionRest - 0.08f;
  rotation = glm::angleAxis(-yaw + glm::pi<float>(),
                            glm::vec3(0.0f, 1.0f, 0.0f)) *
             glm::angleAxis(pitchAngle, glm::vec3(1.0f, 0.0f, 0.0f)) *
             glm::angleAxis(rollAngle, glm::vec3(0.0f, 0.0f, 1.0f));
  return true;
}

Car::RoadTrafficStatus Car::getRoadTrafficStatus(
    const RoadNavigationGraph &navigation) const {
  RoadTrafficStatus status;
  if (!roadRouteEnabled)
    return status;
  const auto &lanes = navigation.lanes();
  if (roadRouteLaneId >= lanes.size())
    return status;
  const RoadLane &lane = lanes[roadRouteLaneId];
  status.routeActive = roadRouteEnabled;
  status.laneId = roadRouteLaneId;
  status.laneProgress = roadRouteProgress;
  status.laneLength = lane.length;
  status.laneClosed = lane.closed;
  status.onConnector = roadRouteOnConnector;
  status.connectorId = roadRouteOnConnector
      ? roadRouteConnectorId : roadRouteNextConnectorId;
  status.hasConnector = roadRouteOnConnector ||
      roadRouteNextConnectorId != UINT32_MAX;
  status.distanceToConnector = roadRouteOnConnector
      ? 0.0f : std::max(0.0f, lane.length - roadRouteProgress);
  status.connectorProgress = roadRouteOnConnector
      ? roadRouteConnectorProgress : 0.0f;
  status.speedMps = roadRouteSpeedMps;
  return status;
}

void Car::setRoadTrafficSpeedCap(float speedMps) {
  roadRouteTrafficSpeedCapMps =
      std::isfinite(speedMps) ? std::max(0.0f, speedMps) : 100.0f;
}

bool Car::updateRoadRoute(float dt, Terrain *terrain) {
  if (!roadRouteEnabled || !terrain)
    return false;
  const RoadNavigationGraph &navigation = terrain->getRoadNavigationGraph();
  const auto &lanes = navigation.lanes();
  const auto &connectors = navigation.laneConnectors();
  if (!navigation.isBuilt() || lanes.empty()) {
    roadRouteEnabled = false;
    state = State::PARKED;
    return false;
  }

  auto rebindToNearestLane = [&]() {
    float bestDistanceSq = 80.0f * 80.0f;
    uint32_t bestLaneId = UINT32_MAX;
    float bestProgress = 0.0f;
    const glm::vec2 current(position.x, position.z);
    for (const RoadLane &candidate : lanes) {
      if (candidate.length < 2.0f || candidate.samples.size() < 2 ||
          (!candidate.closed && candidate.outgoingConnectorIds.empty()))
        continue;
      for (const RoadNavigationSample &sample : candidate.samples) {
        const float distanceSq = glm::dot(sample.position - current,
                                          sample.position - current);
        if (distanceSq < bestDistanceSq) {
          bestDistanceSq = distanceSq;
          bestLaneId = candidate.id;
          bestProgress = sample.distance;
        }
      }
    }
    if (bestLaneId == UINT32_MAX)
      return false;
    const uint32_t seed = roadRouteSeed;
    return startRoadRoute(terrain, bestLaneId, bestProgress, seed);
  };

  if (roadRouteLaneCount != lanes.size() ||
      roadRouteConnectorCount != connectors.size() ||
      roadRouteLaneId >= lanes.size() ||
      (roadRouteOnConnector && roadRouteConnectorId >= connectors.size())) {
    if (!rebindToNearestLane()) {
      roadRouteEnabled = false;
      state = State::PARKED;
      return false;
    }
  }

  const std::vector<RoadNavigationSample> *activeSamples = nullptr;
  float activeProgress = 0.0f;
  if (roadRouteOnConnector) {
    const RoadLaneConnector &connector = connectors[roadRouteConnectorId];
    activeSamples = &connector.samples;
    activeProgress = roadRouteConnectorProgress;
  } else {
    const RoadLane &lane = lanes[roadRouteLaneId];
    activeSamples = &lane.samples;
    activeProgress = roadRouteProgress;
  }
  RoadRoutePoint expectedPoint;
  if (!activeSamples || !sampleRoadRoute(*activeSamples, activeProgress,
                                         expectedPoint) ||
      glm::length(expectedPoint.position - glm::vec2(position.x, position.z)) >
          80.0f) {
    if (!rebindToNearestLane()) {
      roadRouteEnabled = false;
      state = State::PARKED;
      return false;
    }
  }

  const float safeDt = glm::clamp(dt, 0.0f, 0.1f);
  const float maxTravelSpeed = 8.0f;
  auto targetSpeedForLane = [&](uint32_t laneId) {
    const float roadSpeed = lanes[laneId].speedLimit > 0.0f
                                ? lanes[laneId].speedLimit
                                : maxTravelSpeed;
    return glm::clamp(roadSpeed, 2.0f, maxTravelSpeed);
  };
  const float routeSpeed = roadRouteOnConnector
                               ? std::min(5.0f,
                                   targetSpeedForLane(roadRouteLaneId))
                               : targetSpeedForLane(roadRouteLaneId);
  const float targetSpeed = roadRouteStopped
      ? 0.0f : std::min(routeSpeed, roadRouteTrafficSpeedCapMps);
  const float speedDelta = targetSpeed - roadRouteSpeedMps;
  roadRouteSpeedMps += glm::clamp(speedDelta, -4.0f * safeDt,
                                  2.0f * safeDt);

  float remaining = roadRouteSpeedMps * safeDt;
  for (int transitions = 0; remaining > 1e-4f && transitions < 8;
       ++transitions) {
    if (roadRouteStopped)
      break;
    const std::vector<RoadNavigationSample> *samples = nullptr;
    float *progress = nullptr;
    if (roadRouteOnConnector) {
      const RoadLaneConnector &connector = connectors[roadRouteConnectorId];
      samples = &connector.samples;
      progress = &roadRouteConnectorProgress;
    } else {
      const RoadLane &lane = lanes[roadRouteLaneId];
      samples = &lane.samples;
      progress = &roadRouteProgress;
    }
    if (!samples || samples->size() < 2) {
      roadRouteStopped = true;
      roadRouteSpeedMps = 0.0f;
      break;
    }
    const float pathLength = samples->back().distance;
    const float toEnd = std::max(0.0f, pathLength - *progress);
    if (remaining < toEnd) {
      *progress += remaining;
      remaining = 0.0f;
      break;
    }
    remaining -= toEnd;
    *progress = pathLength;
    if (roadRouteOnConnector) {
      const RoadLaneConnector &connector = connectors[roadRouteConnectorId];
      roadRouteLaneId = connector.toLaneId;
      roadRouteProgress = 0.0f;
      roadRouteConnectorProgress = 0.0f;
      roadRouteOnConnector = false;
      if (roadRouteLaneId >= lanes.size()) {
        roadRouteStopped = true;
        roadRouteSpeedMps = 0.0f;
      } else {
        const RoadLane &nextLane = lanes[roadRouteLaneId];
        roadRouteNextConnectorId = nextLane.closed
            ? UINT32_MAX
            : chooseRoadRouteConnector(nextLane, connectors,
                                       roadRouteSeed);
      }
    } else {
      const RoadLane &lane = lanes[roadRouteLaneId];
      if (lane.closed && pathLength > 1e-4f) {
        roadRouteProgress = 0.0f;
      } else if (roadRouteNextConnectorId == UINT32_MAX ||
                 roadRouteNextConnectorId >= connectors.size() ||
                 connectors[roadRouteNextConnectorId].fromLaneId != lane.id) {
        roadRouteStopped = true;
        roadRouteSpeedMps = 0.0f;
      } else {
        roadRouteConnectorId = roadRouteNextConnectorId;
        roadRouteNextConnectorId = UINT32_MAX;
        if (connectors[roadRouteConnectorId].toLaneId >= lanes.size()) {
          roadRouteStopped = true;
          roadRouteSpeedMps = 0.0f;
        } else {
          roadRouteConnectorProgress = 0.0f;
          roadRouteOnConnector = true;
        }
      }
    }
  }

  const std::vector<RoadNavigationSample> *samples = roadRouteOnConnector
      ? &connectors[roadRouteConnectorId].samples
      : &lanes[roadRouteLaneId].samples;
  const float progress = roadRouteOnConnector
      ? roadRouteConnectorProgress : roadRouteProgress;
  RoadRoutePoint routePoint;
  if (!sampleRoadRoute(*samples, progress, routePoint))
    return true;

  const glm::vec2 direction = glm::normalize(routePoint.direction);
  position.x = routePoint.position.x;
  position.z = routePoint.position.y;
  const glm::vec2 right(direction.y, -direction.x);
  const float halfWheelbase = m_Wheelbase * 0.5f;
  const float halfTrack = m_TrackWidth * 0.5f;
  const float frontHeight = terrain->getGroundSurfaceHeight(
      position.x + direction.x * halfWheelbase,
      position.z + direction.y * halfWheelbase);
  const float rearHeight = terrain->getGroundSurfaceHeight(
      position.x - direction.x * halfWheelbase,
      position.z - direction.y * halfWheelbase);
  const float leftHeight = terrain->getGroundSurfaceHeight(
      position.x - right.x * halfTrack, position.z - right.y * halfTrack);
  const float rightHeight = terrain->getGroundSurfaceHeight(
      position.x + right.x * halfTrack, position.z + right.y * halfTrack);
  pitchAngle = glm::clamp(std::atan2(frontHeight - rearHeight, m_Wheelbase),
                          -0.35f, 0.35f);
  rollAngle = glm::clamp(std::atan2(rightHeight - leftHeight, m_TrackWidth),
                         -0.40f, 0.40f);
  position.y = terrain->getGroundSurfaceHeight(position.x, position.z) +
               wheels[0].radius + wheels[0].suspensionRest - 0.08f;
  yaw = std::atan2(-direction.x, direction.y);
  rotation = glm::angleAxis(-yaw + glm::pi<float>(),
                            glm::vec3(0.0f, 1.0f, 0.0f)) *
             glm::angleAxis(pitchAngle, glm::vec3(1.0f, 0.0f, 0.0f)) *
             glm::angleAxis(rollAngle, glm::vec3(0.0f, 0.0f, 1.0f));
  steerAngle = glm::clamp(std::atan(m_Wheelbase * routePoint.curvature),
                          -MAX_STEER_ANGLE, MAX_STEER_ANGLE);
  velocity = Vec3(direction.x * roadRouteSpeedMps, 0.0f,
                  direction.y * roadRouteSpeedMps);
  for (Wheel &wheel : wheels) {
    wheel.angularVelocity = roadRouteSpeedMps / std::max(wheel.radius, 0.1f);
    wheel.spinAngle += wheel.angularVelocity * safeDt;
    wheel.grounded = true;
    wheel.currentLength = wheel.suspensionRest;
  }
  return true;
}

void Car::update(float dt, Terrain *terrain, float totalTime, Camera *camera) {
  if (!terrain)
    return;

  if (roadRouteEnabled && updateRoadRoute(dt, terrain))
    return;

  // ── One-time initial snap to terrain ─────────────────────────────────────
  if (needsInitialSnap) {
    needsInitialSnap = false;
    
    // Get terrain heights at all 4 wheel contact patches
    float h[4];
    for (int i = 0; i < 4; i++) {
      glm::vec3 cp = getWheelContactPatch(wheels[i].localPos);
      h[i] = terrain->getGroundSurfaceHeight(cp.x, cp.z);
    }
    float avgH = (h[0] + h[1] + h[2] + h[3]) * 0.25f;
    
    // Seed pitch/roll from terrain (frontH - rearH lowers nose on downhill slope)
    pitchAngle = std::atan2((h[0] + h[1]) * 0.5f - (h[2] + h[3]) * 0.5f, m_Wheelbase);
    rollAngle = std::atan2((h[1] + h[3]) * 0.5f - (h[0] + h[2]) * 0.5f, m_TrackWidth);
    
    // Calculate position.y to place wheels at equilibrium ride height
    float equilibriumCompression =
        (drivingConfig.massKg * (-GRAVITY) * 0.25f) / SPRING_K;
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
              << " MASS=" << drivingConfig.massKg
              << " SPRING_K=" << SPRING_K << "\n";
  }

  if (state == State::ROAMING) {
    updateRoamingAI(dt, terrain, totalTime);
  }

  const TerrainHeightQuery heightAt = [terrain](float x, float z) {
    return terrain->getGroundSurfaceHeight(x, z);
  };
  vehicleBody->prepareTerrainCollision(
      heightAt, glm::vec3(position.x, position.y, position.z),
      terrain->getCollisionSurfaceRevision());

  // Mounting and dismounting are animation-only transitions. Keep the parked
  // chassis still until the enter animation finishes, and while exiting.
  if (state == State::DRIVEN && m_Driver && !m_Driver->isDriving()) {
    physicsAccumulator = 0.0f;
    return;
  }

  // Fixed 120Hz update loop. Bound catch-up work so one hitch cannot force a
  // long burst of physics steps and make the next frame miss its deadline too.
  const float physicsStep = 1.0f / 120.0f;
  constexpr int maxPhysicsStepsPerFrame = 8;
  const float maxPhysicsBacklog = physicsStep * maxPhysicsStepsPerFrame;
  physicsAccumulator = std::min(
      physicsAccumulator + std::max(0.0f, dt), maxPhysicsBacklog);

  int physicsSteps = 0;
  while (physicsAccumulator >= physicsStep &&
         physicsSteps < maxPhysicsStepsPerFrame) {
    previousRenderPosition = glm::vec3(position.x, position.y, position.z);
    previousRenderRotation = rotation;
    previousRenderSteerAngle = steerAngle;
    previousRenderDriftAngle = driftAngle;
    for (int i = 0; i < 4; ++i) {
      previousRenderWheelLength[i] = wheels[i].currentLength;
      previousRenderWheelSpin[i] = wheels[i].spinAngle;
    }
    hasPreviousRenderState = true;

    if (state == State::PARKED) {
      updateIdlePhysics(physicsStep, terrain);
    } else if (state == State::DRIVEN && m_Driver && m_Driver->isDriving()) {
      updatePlayerArcadePhysics(physicsStep, terrain);
    } else {
      updateActivePhysics(physicsStep, terrain);
    }
    physicsAccumulator -= physicsStep;
    ++physicsSteps;
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
}// ================================================================
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

  constexpr float physicsStep = 1.0f / 120.0f;
  const float alpha = hasPreviousRenderState
      ? glm::clamp(physicsAccumulator / physicsStep, 0.0f, 1.0f)
      : 1.0f;
  const glm::vec3 currentPosition(position.x, position.y, position.z);
  const glm::vec3 renderPosition = hasPreviousRenderState
      ? glm::mix(previousRenderPosition, currentPosition, alpha)
      : currentPosition;
  const glm::quat renderRotation = hasPreviousRenderState
      ? glm::slerp(previousRenderRotation, rotation, alpha)
      : rotation;
  const float renderSteerAngle = hasPreviousRenderState
      ? glm::mix(previousRenderSteerAngle, steerAngle, alpha)
      : steerAngle;
  const float renderDriftAngle = hasPreviousRenderState
      ? glm::mix(previousRenderDriftAngle, driftAngle, alpha)
      : driftAngle;

  glm::mat4 base(1.0f);
  base = glm::translate(base, glm::vec3(renderPosition.x,
                                        renderPosition.y + bodyShake,
                                        renderPosition.z));

  // Combine physics rotation with visual drift angle
  glm::quat visualRotation = renderRotation * glm::angleAxis(
      renderDriftAngle * 0.1f, glm::vec3(0.0f, 1.0f, 0.0f));
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
    float equilComp =
        (drivingConfig.massKg * (-GRAVITY) * 0.25f) / SPRING_K;
    float rideLength = wheels[wi].suspensionRest - equilComp;
    const float renderLength = hasPreviousRenderState
        ? glm::mix(previousRenderWheelLength[wi], wheels[wi].currentLength, alpha)
        : wheels[wi].currentLength;
    const float renderSpin = hasPreviousRenderState
        ? glm::mix(previousRenderWheelSpin[wi], wheels[wi].spinAngle, alpha)
        : wheels[wi].spinAngle;
    float suspY = -(renderLength - rideLength) / SCALE_FACTOR;

    glm::mat4 wm = base;
    wm = glm::translate(wm, ctr + glm::vec3(0, suspY, 0));
    if (front)
      // Positive steerAngle is left input; the model's local -Z forward axis
      // is flipped by base, so use the opposite local angle for visible left.
      wm = glm::rotate(wm, CarHandling::visibleWheelSteerAngle(renderSteerAngle),
                       glm::vec3(0, 1, 0));
    wm = glm::rotate(wm, CarHandling::visibleWheelSpinAngle(renderSpin),
                     glm::vec3(1, 0, 0));
    
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
