#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <functional>
#include <cstdint>
#include <memory>
#include <unordered_map>

using TerrainHeightQuery = std::function<float(float, float)>;
class btBroadphaseInterface;
class btCollisionConfiguration;
class btCollisionDispatcher;
class btCollisionShape;
class btHeightfieldTerrainShape;
class btCollisionObject;
class btDiscreteDynamicsWorld;
class btSequentialImpulseConstraintSolver;
class btRigidBody;

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
                               const glm::vec3 &center,
                               std::uint64_t surfaceRevision = 0);
  bool raycastTerrain(const glm::vec3 &from, const glm::vec3 &to,
                      glm::vec3 &hitPosition, glm::vec3 &hitNormal) const;
  void synchronize(const State &state);

private:
  struct TerrainTile;
  void ensureTerrainTiles(const glm::vec3 &center);
  void removeTerrainTile(TerrainTile &tile);
  bool hasTerrainTileAt(float x, float z) const;

  std::unique_ptr<btCollisionConfiguration> collisionConfiguration;
  std::unique_ptr<btCollisionDispatcher> dispatcher;
  std::unique_ptr<btBroadphaseInterface> broadphase;
  std::unique_ptr<btSequentialImpulseConstraintSolver> solver;
  std::unique_ptr<btDiscreteDynamicsWorld> world;
  std::unique_ptr<btCollisionShape> chassisChildShape;
  std::unique_ptr<btCollisionShape> chassisShape;
  std::unique_ptr<btRigidBody> body;

  glm::vec3 chassisHalfExtentsM{0.0f};
  float centerOfMassDropM = 0.0f;
  TerrainHeightQuery terrainHeightQuery;
  std::unordered_map<std::uint64_t, std::unique_ptr<TerrainTile>> terrainTiles;
  std::uint64_t terrainSurfaceRevision = 0;
};
