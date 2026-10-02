#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <glm/gtc/constants.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include <iostream>

#include "AudioSystem.h"
#include "Car.h"
#include "CarHandling.h"
#include "Character.h"
#include "Terrain.h"
#include "vehicle/VehicleBrakes.h"
#include "vehicle/VehicleBody.h"
#include "vehicle/VehicleDrivetrain.h"
#include "vehicle/VehicleStability.h"
#include "vehicle/VehicleSurface.h"
#include "vehicle/VehicleSuspension.h"


float Car::tireForce(float slip, float load) {
  // Sim-Cade Tire Parameters (High grip, forgiving slide)
  // B: Stiffness, C: Shape, E: Curvature
  const float B = drivingConfig.lateralStiffness, C = 1.3f, E = 1.0f;
  return load * std::sin(C * std::atan(B * slip -
                                       E * (B * slip - std::atan(B * slip))));
}


float Car::getSpeed() const {
  return std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
}

float Car::getForwardSpeed() const {
  return glm::dot(glm::vec3(velocity.x, 0.0f, velocity.z), getForwardVector());
}



static VehicleSurfaceType vehicleSurfaceForZone(Terrain::Zone zone) {
  switch (zone) {
  case Terrain::Zone::SEA: return VehicleSurfaceType::Water;
  case Terrain::Zone::BEACH: return VehicleSurfaceType::Sand;
  case Terrain::Zone::PLAIN: return VehicleSurfaceType::Dirt;
  case Terrain::Zone::WADI: return VehicleSurfaceType::Dust;
  case Terrain::Zone::PORT: return VehicleSurfaceType::Concrete;
  case Terrain::Zone::MEDINA:
  case Terrain::Zone::KASBAH: return VehicleSurfaceType::Cobblestone;
  case Terrain::Zone::VILLE_NOUVELLE: return VehicleSurfaceType::Asphalt;
  }
  return VehicleSurfaceType::Dirt;
}




void Car::updateSuspension(float sdt, Terrain *terrain, glm::vec3 &totalForce, glm::vec3 &totalTorque) {
  const glm::vec3 chassisUp =
      glm::normalize(rotation * glm::vec3(0.0f, 1.0f, 0.0f));
  std::array<SuspensionWheelInput, 4> inputs{};
  std::array<glm::vec3, 4> wheelPositions{};
  for (int i = 0; i < 4; i++) {
    const glm::vec3 wp = getWheelWorld(wheels[i].localPos);
    const glm::vec3 cp = getWheelContactPatch(wheels[i].localPos);
    wheelPositions[i] = wp;

    const float rayLength = wheels[i].suspensionRest + wheels[i].radius + 0.25f;
    glm::vec3 hitPosition(0.0f), hitNormal(0.0f, 1.0f, 0.0f);
    const bool rayIntersection = vehicleBody->raycastTerrain(
        wp, wp - chassisUp * rayLength, hitPosition, hitNormal);
    const bool rayHit = rayIntersection && glm::dot(hitNormal, chassisUp) > 0.1f;
    const float groundH = terrain->getGroundSurfaceHeight(cp.x, cp.z);
    const float distanceToGround = rayHit
        ? glm::dot(wp - hitPosition, chassisUp)
        : wp.y - groundH;
    const bool hasGroundContact = rayHit || distanceToGround <= rayLength;

    wheels[i].currentLength = glm::clamp(distanceToGround - wheels[i].radius,
                                         0.0f, wheels[i].suspensionRest);
    wheels[i].compression = wheels[i].suspensionRest - wheels[i].currentLength;

    const float compVel = sdt > 1.0e-6f
                              ? (wheels[i].prevLength - wheels[i].currentLength) / sdt
                              : 0.0f;
    wheels[i].prevLength = wheels[i].currentLength;
    inputs[i] = {wheels[i].compression, compVel, hasGroundContact};
  }

  const SuspensionSettings settings{
      drivingConfig.springRateNPerM,
      drivingConfig.compressionDampingNsPerM,
      drivingConfig.reboundDampingNsPerM,
      drivingConfig.frontAntiRollStiffnessNPerM,
      drivingConfig.rearAntiRollStiffnessNPerM,
      drivingConfig.bumpStopStartCompressionM,
      drivingConfig.bumpStopQuadraticStiffnessNPerM2,
      drivingConfig.massKg * 20.0f};
  const std::array<float, 4> loads = calculateSuspensionLoads(inputs, settings);
  for (int i = 0; i < 4; ++i) {
    const float springF = loads[i];
    wheels[i].load = springF;
    wheels[i].grounded = inputs[i].hasGroundContact && springF > 0.1f;
    isGrounded = isGrounded || wheels[i].grounded;

    glm::vec3 forceVec = chassisUp * springF;
    totalForce += forceVec;

    glm::vec3 rel = wheelPositions[i] - glm::vec3(position.x, position.y, position.z);
    totalTorque += glm::cross(rel, forceVec);
  }
}


void Car::calculateTireForces(float sdt, Terrain *terrain,
                              glm::vec3 &totalForce, glm::vec3 &totalTorque) {
  bool handbrake = false;
  bool braking = false;

  if (state == State::DRIVEN && m_Driver && m_Driver->isDriving()) {
    // In reverse, S is the accelerator. Keeping service-brake torque active
    // here locks the wheels against the negative reverse-gear torque.
    braking = driverInput.brake && engine.gear != 0;
    handbrake = driverInput.handbrake;
  }

  float engineTorque = engine.torqueCurve(engine.rpm) * engine.throttle;
  float driveTorque = engineTorque * engine.gearRatios[engine.gear] *
                      engine.finalDrive * engine.clutch *
                      drivingConfig.engineEfficiency;

  std::array<float, 4> torqueSplit{};
  for (int i = 0; i < 4; ++i)
    torqueSplit[i] = driveTorque * drivingConfig.driveTorqueSplit[i];
  std::array<float, 4> wheelAngularVelocities{};
  for (int i = 0; i < 4; ++i)
    wheelAngularVelocities[i] = wheels[i].angularVelocity;
  torqueSplit = applyLimitedSlipDifferentials(
      torqueSplit, wheelAngularVelocities,
      drivingConfig.frontDifferentialLock, drivingConfig.rearDifferentialLock);

  for (int i = 0; i < 4; i++) {
    glm::vec3 wp = getWheelWorld(wheels[i].localPos);
    glm::vec3 cp = getWheelContactPatch(wheels[i].localPos);
    glm::vec3 rel = wp - glm::vec3(position.x, position.y, position.z);
    
    // World velocity of the contact patch
    glm::vec3 wVel = glm::vec3(velocity.x, velocity.y, velocity.z) +
                     glm::cross(angularVel, rel);

    float wYaw = CarHandling::frontWheelYaw(
        yaw, wheels[i].isFront ? steerAngle : 0.0f);
    glm::vec3 tireFwd(-std::sin(wYaw), 0.0f, std::cos(wYaw));
    glm::vec3 tireRgt(std::cos(wYaw), 0.0f, std::sin(wYaw));

    float vLong = glm::dot(wVel, tireFwd);
    float vLat = glm::dot(wVel, tireRgt);

    float roadFactor = terrain->getRoadAt(cp.x, cp.z);
    const VehicleSurfaceType surface =
        vehicleSurfaceForZone(terrain->getZoneAt(cp.x, cp.z));
    float grip = drivingConfig.peakTireGrip *
                 vehicleSurfaceGrip(surface, drivingConfig) *
                 (1.0f + roadFactor * 0.4f);

    // --- REALISTIC WHEEL SPIN PHYSICS ---
    float omega = wheels[i].angularVelocity;
    
    // Calculate slip ratio
    const float slip = CarHandling::longitudinalSlip(
        omega * wheels[i].radius, vLong);
    
    // Pacejka longitudinal tire force
    float fx_tire = tireForce(slip, wheels[i].load) * grip;

    // Torques acting on the wheel
    float T_drive = wheels[i].isDriven ? torqueSplit[i] : 0.0f;
    
    float T_brake = 0.0f;
    float serviceBrakeTorque = 0.0f;
    if (handbrake && !wheels[i].isFront) {
        T_brake = drivingConfig.handbrakeTorqueNm;
    } else if (braking) {
        serviceBrakeTorque = wheels[i].isFront
                                 ? drivingConfig.frontBrakeTorqueNm
                                 : drivingConfig.rearBrakeTorqueNm;
        const float absScale = serviceBrakeScale(
            vLong, omega * wheels[i].radius, drivingConfig.absEnabled,
            drivingConfig.absSlipThreshold);
        T_brake = serviceBrakeTorque * absScale;
    } else if (std::abs(engine.throttle) < 0.1f && engine.gear != 0) {
        T_brake = drivingConfig.engineBrakeTorqueNm;
    }
    T_brake += drivingConfig.rollingResistanceTorqueNm;

    // Net torque on the wheel
    // The tire force creates a reaction torque opposing the wheel spin: -fx_tire * radius
    // Braking torque always opposes the spin direction: -T_brake * sign(omega)
    float signOmega = (omega > 0.05f) ? 1.0f : ((omega < -0.05f) ? -1.0f : 0.0f);
    const bool absReleasing = braking && !handbrake &&
                              drivingConfig.absEnabled && std::abs(vLong) >= 1.0f;
    if (signOmega == 0.0f && T_brake > std::abs(T_drive) && !absReleasing) {
        // If wheel is stopped and braking torque dominates, keep it locked
        omega = 0.0f;
    } else {
        float T_reaction = -fx_tire * wheels[i].radius;
        const float brakeDirection = signOmega != 0.0f
                                         ? signOmega
                                         : (absReleasing ? std::copysign(1.0f, vLong)
                                                         : 0.0f);
        float T_net = T_drive - T_brake * brakeDirection + T_reaction;
        
        // Wheel rotational inertia (I = 0.5 * m * r^2)
        // A typical car wheel has I ~ 1.2 kg*m^2
        float I_wheel = drivingConfig.wheelInertiaKgM2;
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

    // 2. Lateral force from a slip angle, using the same Pacejka model as the
    // longitudinal force. The old lateral-velocity damper (-vLat * 2.5) hit the
    // friction ceiling at only 0.8 m/s of slip, so the front tyres were already
    // saturated and steering harder produced no extra turn-in (understeer).
    float slipLatAngle = std::atan2(vLat, std::abs(vLong) + 0.3f);
    float latGripMult = 1.0f;
    if (handbrake && !wheels[i].isFront) {
      latGripMult = 0.35f; // Slide the rear!
    }
    // Peak of tireForce() is ~0.89*load, so scale to keep the lateral ceiling at
    // the old 2.0*load*grip friction limit: same peak grip, but now it builds
    // up progressively with slip angle instead of clipping flat.
    const float LAT_PEAK_SCALE = 2.2f;
    float fy = -tireForce(slipLatAngle, wheels[i].load) * LAT_PEAK_SCALE *
               latGripMult * grip;

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
    glm::vec3 tireTorque = glm::cross(torqueRel, wForce);
    totalTorque += tireTorque;

    // Store debug data
    wheels[i].fx = fx;
    wheels[i].fy = fy;
    wheels[i].slipLong = slip;
    wheels[i].slipLat = slipLatAngle;
  }
}


void Car::applyStabilityControl(float sdt, Terrain *terrain,
                                glm::vec3 &totalTorque) {
  float speed = getSpeed();
  
  glm::vec3 localAngularVel = glm::inverse(rotation) * angularVel;
  glm::vec3 localTorque(0.0f);

  if (!isGrounded) {
    // Aerial controls (GTA style)
    bool gas = driverInput.accelerate || engine.throttle > 0.5f;
    bool braking = driverInput.brake;
    bool left = driverInput.steering > 0.0f;
    bool right = driverInput.steering < 0.0f;

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
    float steerInput = driverInput.steering;

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
    float velocityLong = getForwardSpeed();
    // CarHandling returns the stored-yaw rate. Bullet integrates world angular
    // velocity, whose sign is opposite because the model's forward axis is
    // corrected by a half turn around Y.
    const float desiredStoredYawRate = CarHandling::desiredYawRate(
        steerAngle, velocityLong, m_Wheelbase);
    const float desiredWorldYawRate = -desiredStoredYawRate;
    const float escYawTorque =
        (desiredWorldYawRate - localAngularVel.y) *
        22000.0f * drivingConfig.stabilityAssist;
    localTorque.y += escYawTorque;
    localTorque.y -= localAngularVel.y *
                     22000.0f * drivingConfig.counterSteerAssist;

  }

  // Transform local torques to world space
  totalTorque += rotation * localTorque;
}


void Car::applyAerodynamics(float dt, glm::vec3 &totalForce) {
  float speedSq = velocity.x * velocity.x + velocity.z * velocity.z;
  if (speedSq > 0.1f) {
    glm::vec3 dragDir = glm::normalize(glm::vec3(-velocity.x, 0.0f, -velocity.z));
    float dragForce = drivingConfig.aerodynamicDrag * speedSq;
    float downForce = drivingConfig.downforceCoefficient * speedSq;
    totalForce += dragDir * dragForce;
    totalForce.y -= downForce;
  }
}


void Car::updateActivePhysics(float dt, Terrain *terrain) {
  using PerfClock = std::chrono::steady_clock;
  struct PerfStats {
    double simTime = 0.0;
    std::uint64_t steps = 0;
    double controlMs = 0.0;
    double suspensionMs = 0.0;
    double tiresMs = 0.0;
    double assistsMs = 0.0;
    double bulletMs = 0.0;
    double floorMs = 0.0;
    double propCollisionMs = 0.0;
    double postMs = 0.0;
  };
  static const bool profileEnabled =
      std::getenv("FALLAGA_CAR_PERF") != nullptr;
  static PerfStats perf;

  if (!terrain)
    return;

  PerfClock::time_point phaseStart{};
  if (profileEnabled) phaseStart = PerfClock::now();
  auto recordPhase = [&](double& bucketMs) {
    if (!profileEnabled) return;
    const auto now = PerfClock::now();
    bucketMs += std::chrono::duration<double, std::milli>(
                    now - phaseStart).count();
    phaseStart = now;
  };

  if (state == State::DRIVEN && m_Driver && m_Driver->isDriving()) {
    processInput(dt);
    updateTransmission(dt, driverInput.accelerate, driverInput.brake);
  } else {
    driverInput = {};
    updateTransmission(dt, engine.throttle > 0.1f, false);
  }
  recordPhase(perf.controlMs);

  // Bullet owns gravity and rigid-body integration. Suspension, tires, and
  // arcade assists still supply the game-specific forces and torques.
  glm::vec3 totalForce(0.0f);
  glm::vec3 totalTorque(0.0f);
  const TerrainHeightQuery heightAt = [terrain](float x, float z) {
    return terrain->getGroundSurfaceHeight(x, z);
  };

  isGrounded = false;
  updateSuspension(dt, terrain, totalForce, totalTorque);
  recordPhase(perf.suspensionMs);
  calculateTireForces(dt, terrain, totalForce, totalTorque);
  recordPhase(perf.tiresMs);
  applyStabilityControl(dt, terrain, totalTorque);
  totalTorque += calculateAntiFlipTorque(
      rotation * glm::vec3(0.0f, 1.0f, 0.0f), angularVel, isGrounded,
      drivingConfig.antiFlipThresholdRadians,
      drivingConfig.antiFlipStrengthNmPerRad,
      drivingConfig.antiFlipDampingNms);
  applyAerodynamics(dt, totalForce);
  recordPhase(perf.assistsMs);

  // Rigid-body integration runs in Bullet; custom wheel forces feed the body.
  glm::vec3 planarForward = getForwardVector();
  planarForward.y = 0.0f;
  const float forwardLengthSq = glm::dot(planarForward, planarForward);
  const bool hasPlanarForward = forwardLengthSq > 1e-6f;
  if (hasPlanarForward) planarForward *= glm::inversesqrt(forwardLengthSq);
  const float previousForwardSpeed = hasPlanarForward
      ? velocity.x * planarForward.x + velocity.z * planarForward.z
      : 0.0f;

  VehicleBody::State rigidState;
  rigidState.position = glm::vec3(position.x, position.y, position.z);
  rigidState.velocity = glm::vec3(velocity.x, velocity.y, velocity.z);
  rigidState.angularVelocity = angularVel;
  rigidState.rotation = rotation;
  const VehicleBody::State bodyState =
      vehicleBody->step(rigidState, totalForce, totalTorque, dt, heightAt);
  recordPhase(perf.bulletMs);
  position.x = bodyState.position.x;
  position.y = bodyState.position.y;
  position.z = bodyState.position.z;
  velocity.x = bodyState.velocity.x;
  velocity.y = bodyState.velocity.y;
  velocity.z = bodyState.velocity.z;
  angularVel = bodyState.angularVelocity;
  rotation = glm::normalize(bodyState.rotation);
  const glm::vec3 bodyForward = getForwardVector();
  yaw = std::atan2(-bodyForward.x, bodyForward.z);
  float modelYaw = 0.0f;
  glm::extractEulerAngleYXZ(glm::mat4_cast(rotation), modelYaw, pitchAngle,
                            rollAngle);

  // Keep the specified acceleration as a chassis limit. Capping wheel torque
  // here made launch torque too weak to overcome the wheel's own brake/drag
  // terms, leaving the vehicle stuck at rest.
  const bool propulsionActive = engine.throttle > 0.1f &&
                                (!driverInput.brake || engine.gear == 0);
  if (hasPlanarForward && propulsionActive) {
    const float driveDirection = engine.gear == 0 ? -1.0f : 1.0f;
    const float forwardSpeed = velocity.x * planarForward.x +
                               velocity.z * planarForward.z;
    const float directionalDelta =
        (forwardSpeed - previousForwardSpeed) * driveDirection;
    const float maxDelta = drivingConfig.maxAccelerationMps2 * dt;
    if (directionalDelta > maxDelta) {
      const float correction = (directionalDelta - maxDelta) * driveDirection;
      velocity.x -= planarForward.x * correction;
      velocity.z -= planarForward.z * correction;
    }
  }

  // Enforce the model's forward/reverse speed envelope while preserving
  // lateral drift velocity.
  if (hasPlanarForward) {
    const float forwardSpeed = velocity.x * planarForward.x +
                               velocity.z * planarForward.z;
    const float limitedSpeed = CarHandling::clampForwardSpeed(
        forwardSpeed, drivingConfig.topSpeedMps,
        drivingConfig.reverseSpeedMps);
    velocity.x += planarForward.x * (limitedSpeed - forwardSpeed);
    velocity.z += planarForward.z * (limitedSpeed - forwardSpeed);
  }

  // Hard floor snap to prevent chassis tunneling into ground
  {
    glm::vec3 hitPosition(0.0f), hitNormal(0.0f, 1.0f, 0.0f);
    const bool hitTerrain = vehicleBody->raycastTerrain(
        glm::vec3(position.x, position.y + 2.0f, position.z),
        glm::vec3(position.x, position.y - 2.0f, position.z),
        hitPosition, hitNormal);
    const float floorY = (hitTerrain ? hitPosition.y
                                     : terrain->getGroundSurfaceHeight(position.x,
                                                                      position.z)) + 0.16f;
    if (position.y < floorY) {
      position.y = floorY;
      if (velocity.y < 0.0f)
      velocity.y = 0.0f;
    }
  }
  recordPhase(perf.floorMs);

  // Resolve the chassis against nearby world props after movement, so both
  // player and roaming cars cannot pass through placed solid objects.
  glm::vec3 collisionPosition(position.x, position.y, position.z);
  glm::vec3 collisionVelocity(velocity.x, velocity.y, velocity.z);
  if (terrain->resolveCarCollision(collisionPosition, collisionVelocity, 1.2f)) {
    position.x = collisionPosition.x;
    position.z = collisionPosition.z;
    velocity.x = collisionVelocity.x;
    velocity.z = collisionVelocity.z;
  }
  recordPhase(perf.propCollisionMs);

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

  rigidState.position = glm::vec3(position.x, position.y, position.z);
  rigidState.velocity = glm::vec3(velocity.x, velocity.y, velocity.z);
  rigidState.angularVelocity = angularVel;
  rigidState.rotation = rotation;
  vehicleBody->synchronize(rigidState);

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

  if (profileEnabled) {
    recordPhase(perf.postMs);
    perf.simTime += dt;
    ++perf.steps;
    if (perf.simTime >= 0.5) {
      const double scale = 1.0 / static_cast<double>(perf.steps);
      const char* mode = state == State::ROAMING ? "roaming" : "driving";
      std::cout << "[CAR PERF " << mode << "] " << perf.steps
                << " steps avg ms: input=" << perf.controlMs * scale
                << " suspension=" << perf.suspensionMs * scale
                << " tires=" << perf.tiresMs * scale
                << " assists=" << perf.assistsMs * scale
                << " Bullet=" << perf.bulletMs * scale
                << " floor=" << perf.floorMs * scale
                << " props=" << perf.propCollisionMs * scale
                << " post=" << perf.postMs * scale << '\n';
      perf = {};
    }
  }
}

void Car::updatePlayerArcadePhysics(float dt, Terrain *terrain) {
  // Use the same per-wheel contact, grip, wheel-spin and brake-force solver
  // used by the physical vehicle path. The former player-only axle model
  // collapsed both front wheels and both rear wheels into shared forces,
  // bypassing each wheel's suspension load and angular velocity.
  updateActivePhysics(dt, terrain);
}
