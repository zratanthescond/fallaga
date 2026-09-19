#pragma once

#include "Shader.h"
#include "Vec3.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

class Character;
class Camera;
class Terrain;
class ObjModel;

class Car {
public:
  // ── NESTED TYPES ──────────────────────────────
  enum class State {
    PARKED,
    ROAMING, // AI logic
    DRIVEN   // Player logic
  };

  struct Engine {
    float rpm = 800.0f;
    int gear = 2; // 0=R, 1=N, 2=1st, 3=2nd, etc.
    float throttle = 0.0f;
    float clutch = 1.0f; // 1.0 = engaged

    float minRPM = 800.0f;
    float maxRPM = 7500.0f;
    float gearRatios[8] = {-3.1f, 0.0f, 3.6f, 2.2f, 1.5f, 1.1f, 0.9f, 0.7f}; // R, N, 1-5, 6
    float finalDrive = 4.1f;

    float torqueCurve(float currentRPM) {
      float x = currentRPM / maxRPM;
      return 650.0f * (1.0f - 0.8f * (x - 0.6f) * (x - 0.6f)); // Punchy torque curve
    }
  };

  struct Wheel {
    glm::vec3 localPos;
    glm::vec3 worldPos;
    float radius = 0.35f;
    float spinAngle = 0.0f;
    float angularVelocity = 0.0f;

    float suspensionRest = 0.25f;
    float currentLength = 0.6f;
    float prevLength = 0.6f;
    float load = 0.0f;
    float compression = 0.0f;

    bool isFront = false;
    bool isDriven = true;

    // Debug & Physics Data
    float fx = 0.0f, fy = 0.0f;
    float slipLong = 0.0f, slipLat = 0.0f;
    bool grounded = false;
  };

  Car(State initialState = State::DRIVEN);
  ~Car();

  void update(float deltaTime, Terrain *terrain, float totalTime,
              Camera *camera = nullptr);
  void render(const glm::mat4 &projection, const glm::mat4 &view,
              Terrain *terrain);

  void mountCharacter(Character *character);
  void dismountCharacter(Character *character);

  // ── PUBLIC ACCESSORS ───────────────────────────
  Vec3 getPosition() const { return position; }
  Vec3 getVelocity() const { return velocity; }
  void setVelocity(const Vec3 &v) { velocity = v; }
  void setPositionXZ(float x, float z) {
    position.x = x;
    position.z = z;
  }
  void setPosition(const Vec3 &p) { position = p; }
  bool hasDriver() const { return m_Driver != nullptr; }
  Character *getDriver() const { return m_Driver; }
  float getSpeed() const;
  float getYaw() const { return yaw; }
  float getDriftAngle() const { return driftAngle; }
  float getPitchAngle() const { return pitchAngle; }
  float getRollAngle() const { return rollAngle; }
  State getState() const { return state; }
  void setState(State s) { state = s; }
  int getGear() const { return engine.gear; }
  float getRPM() const { return engine.rpm; }
  float getThrottle() const { return engine.throttle; }
  const Wheel &getWheel(int i) const { return wheels[i]; }
  Shader &getShader() { return *carShader; }
  bool isDriving() const { return m_Driver != nullptr; }
  glm::quat getRotation() const { return rotation; }
  glm::quat getWorldOrientation() const {
    return glm::angleAxis(-yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
           glm::angleAxis(pitchAngle, glm::vec3(1.0f, 0.0f, 0.0f)) *
           glm::angleAxis(rollAngle, glm::vec3(0.0f, 0.0f, 1.0f));
  }
  glm::vec3 getDriverSeatLocalOffset() const { return glm::vec3(-0.24f, -0.22f, -0.08f); }
  glm::vec3 getDriverSeatWorldPosition() const {
    return glm::vec3(position.x, position.y, position.z) + getWorldOrientation() * getDriverSeatLocalOffset();
  }
  glm::vec3 getDriverDoorWorldPosition() const {
    return glm::vec3(position.x, position.y, position.z) + getWorldOrientation() * glm::vec3(-0.95f, -0.22f, -0.08f);
  }
  glm::vec3 getDriverOutsideWorldPosition() const {
    return glm::vec3(position.x, position.y, position.z) + getWorldOrientation() * glm::vec3(-1.40f, -0.22f, -0.08f);
  }
  glm::vec3 getDriverExitWorldPosition() const {
    return glm::vec3(position.x, position.y, position.z) + getWorldOrientation() * glm::vec3(-1.40f, -0.22f, -0.08f);
  }

  // ── UNIFIED COORDINATE BASIS ──────────────────
  glm::vec3 getForwardVector() const;
  glm::vec3 getRightVector() const;


private:
  Wheel wheels[4]; // 0:FL, 1:FR, 2:RL, 3:RR
  Engine engine;

  // ── PHYSICS STATE ──────────────────────────────
  Vec3 position;
  Vec3 velocity;
  glm::vec3 angularVel = glm::vec3(0.0f);
  glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

  glm::vec3 accumulatedForce = glm::vec3(0.0f);
  glm::vec3 accumulatedTorque = glm::vec3(0.0f);

  float yaw = 0.0f;
  float steerAngle = 0.0f;
  float driftAngle = 0.0f;

  // AI roaming
  State state;
  float aiTargetYaw = 0.0f;
  float aiThinkTimer = 0.0f;
  float aiLostTimer = 0.0f;
  float aiBestScanYaw = 0.0f;
  bool needsInitialSnap = true;

  float pitchAngle = 0.0f;
  float rollAngle = 0.0f;
  float bodyShake = 0.0f;

  glm::vec3 prevVel = glm::vec3(0.0f);
  int teleLog = 0;

  bool isGrounded = true;
  bool isPaintedModel = false;
  Character *m_Driver = nullptr;

  ObjModel *model = nullptr;
  Shader *carShader = nullptr;

  int meshBody = -1;
  int meshFL = -1, meshFR = -1, meshBL = -1, meshBR = -1;
  glm::vec3 wheelOffsetFL, wheelOffsetFR, wheelOffsetBL, wheelOffsetBR;
  glm::vec3 carCenter;

  float m_Wheelbase = 2.8f;
  float m_TrackWidth = 1.6f;
  float m_VerticalOffset = 0.0f;

  float physicsAccumulator = 0.0f;

  static constexpr float MASS = 1400.0f; // GTA default mass (Stage 1)
  static constexpr float GRAVITY = -9.81f;
  static constexpr float MAX_STEER_ANGLE = 0.61f; // ~35 degrees (Stage 8)
  static constexpr float SCALE_FACTOR = 0.010f;
  static constexpr float MAX_SUSPENSION_TRAVEL = 0.12f;

  float SPRING_K = 38000.0f; // Tuned spring strength for 0.010 scale
  float SPRING_DAMPING = 4200.0f; // Tuned damper strength for 0.010 scale
  float INERTIA_PITCH = 1500.0f;
  float INERTIA_ROLL = 1000.0f;
  float INERTIA_YAW = 2500.0f;
  float BODY_DAMPING = 0.95f;

  void resolveWheelMeshes();
  glm::vec3 getWheelWorld(const glm::vec3 &localOffset) const;
  glm::vec3 getWheelContactPatch(const glm::vec3 &localOffset) const;

  // ── PHYSICS SUBSYSTEMS ────────────────────────
  void updateActivePhysics(float dt, Terrain *terrain);
  void updateIdlePhysics(float dt, Terrain *terrain);
  void updateRoamingAI(float dt, Terrain *terrain, float totalTime);

  void processInput(float dt, bool &gas, bool &braking, bool &handbrake);
  void updateTransmission(float dt, bool gas, bool braking);
  void updateSuspension(float sdt, Terrain *terrain, glm::vec3 &totalForce,
                        glm::vec3 &totalTorque);
  void calculateTireForces(float sdt, Terrain *terrain, glm::vec3 &totalForce,
                           glm::vec3 &totalTorque);
  void applyStabilityControl(float sdt, Terrain *terrain,
                             glm::vec3 &totalTorque);
  void applyAerodynamics(float dt, glm::vec3 &totalForce);

  float tireForce(float slip, float load);
};