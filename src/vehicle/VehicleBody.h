#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <functional>
#include <future>
#include <memory>
#include <vector>

using TerrainHeightQuery = std::function<float(float, float)>;
class btBroadphaseInterface;
class btCollisionConfiguration;
class btCollisionDispatcher;
class btCollisionShape;
class btDiscreteDynamicsWorld;
class btSequentialImpulseConstraintSolver;
class btRigidBody;
class btTriangleMesh;
class btBvhTriangleMeshShape;
class btCollisionObject;

class VehicleBody {
public:
  struct State {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 angularVelocity{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
  };

  VehicleBody(float massKg, const glm::vec3 &halfExtentsM,
              float centerOfMassDropM, float yawInertiaKgM2);
  ~VehicleBody();

  VehicleBody(const VehicleBody &) = delete;
  VehicleBody &operator=(const VehicleBody &) = delete;

  State step(const State &state, const glm::vec3 &forceN,
             const glm::vec3 &torqueNm, float dt,
             const TerrainHeightQuery &heightAt);
  void prepareTerrainCollision(const TerrainHeightQuery &heightAt,
                               const glm::vec3 &center);
  bool raycastTerrain(const glm::vec3 &from, const glm::vec3 &to,
                      glm::vec3 &hitPosition, glm::vec3 &hitNormal) const;
  void synchronize(const State &state);

private:
  void rebuildTerrainCollision(const TerrainHeightQuery &heightAt,
                               const glm::vec3 &center);

  std::unique_ptr<btCollisionConfiguration> collisionConfiguration;
  std::unique_ptr<btCollisionDispatcher> dispatcher;
  std::unique_ptr<btBroadphaseInterface> broadphase;
  std::unique_ptr<btSequentialImpulseConstraintSolver> solver;
  std::unique_ptr<btDiscreteDynamicsWorld> world;
  std::unique_ptr<btCollisionShape> chassisChildShape;
  std::unique_ptr<btCollisionShape> chassisShape;
  std::unique_ptr<btRigidBody> body;

  std::unique_ptr<btTriangleMesh> terrainMesh;
  std::unique_ptr<btBvhTriangleMeshShape> terrainShape;
  std::unique_ptr<btCollisionObject> terrainObject;
  glm::vec2 terrainCenterXZ{0.0f};
  bool hasTerrainCollision = false;

  std::unique_ptr<btTriangleMesh> pendingTerrainMesh;
  std::vector<float> pendingTerrainHeights;
  glm::vec2 pendingTerrainCenterXZ{0.0f};
  int pendingNextSampleRow = 0;
  int pendingNextTriangleRow = 0;
  std::future<std::unique_ptr<btBvhTriangleMeshShape>>
      pendingTerrainShapeFuture;
};
