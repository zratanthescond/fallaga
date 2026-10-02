#include "VehicleBody.h"

#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionShapes/btHeightfieldTerrainShape.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <vector>

namespace {
btVector3 toBullet(const glm::vec3 &v) { return {v.x, v.y, v.z}; }

glm::vec3 fromBullet(const btVector3 &v) {
  return {static_cast<float>(v.x()), static_cast<float>(v.y()),
          static_cast<float>(v.z())};
}

btQuaternion toBullet(const glm::quat &q) {
  return {q.x, q.y, q.z, q.w};
}

glm::quat fromBullet(const btQuaternion &q) {
  return {static_cast<float>(q.w()), static_cast<float>(q.x()),
          static_cast<float>(q.y()), static_cast<float>(q.z())};
}

std::uint64_t terrainTileKey(int x, int z) {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
         static_cast<std::uint32_t>(z);
}
} // namespace

struct VehicleBody::TerrainTile {
  int tileX = 0;
  int tileZ = 0;
  int nextSampleRow = 0;
  float minHeight = std::numeric_limits<float>::max();
  float maxHeight = std::numeric_limits<float>::lowest();
  std::vector<float> heights;
  std::unique_ptr<btHeightfieldTerrainShape> shape;
  std::unique_ptr<btCollisionObject> object;
};

VehicleBody::VehicleBody(float massKg, const glm::vec3 &halfExtentsM,
                         float centerOfMassDropM, float yawInertiaKgM2)
    : chassisHalfExtentsM(halfExtentsM),
      centerOfMassDropM(centerOfMassDropM) {
  collisionConfiguration =
      std::make_unique<btDefaultCollisionConfiguration>();
  dispatcher = std::make_unique<btCollisionDispatcher>(collisionConfiguration.get());
  broadphase = std::make_unique<btDbvtBroadphase>();
  solver = std::make_unique<btSequentialImpulseConstraintSolver>();
  world = std::make_unique<btDiscreteDynamicsWorld>(
      dispatcher.get(), broadphase.get(), solver.get(),
      collisionConfiguration.get());
  world->setGravity(btVector3(0.0f, -9.81f, 0.0f));
  world->getSolverInfo().m_numIterations = 10;
  world->getSolverInfo().m_splitImpulse = true;

  auto compound = std::make_unique<btCompoundShape>();
  auto box = std::make_unique<btBoxShape>(toBullet(halfExtentsM));
  btTransform childTransform;
  childTransform.setIdentity();
  childTransform.setOrigin(btVector3(0.0f, centerOfMassDropM, 0.0f));
  compound->addChildShape(childTransform, box.get());
  chassisChildShape = std::move(box);
  chassisShape = std::move(compound);

  btVector3 inertia(0.0f, 0.0f, 0.0f);
  chassisShape->calculateLocalInertia(std::max(1.0f, massKg), inertia);
  inertia.setY(std::max(1.0f, yawInertiaKgM2));
  btRigidBody::btRigidBodyConstructionInfo bodyInfo(
      std::max(1.0f, massKg), nullptr, chassisShape.get(), inertia);
  bodyInfo.m_friction = 0.7f;
  bodyInfo.m_restitution = 0.05f;
  bodyInfo.m_linearDamping = 0.02f;
  bodyInfo.m_angularDamping = 0.18f;
  body = std::make_unique<btRigidBody>(bodyInfo);
  body->setActivationState(DISABLE_DEACTIVATION);
  body->setCcdMotionThreshold(0.5f);
  body->setCcdSweptSphereRadius(
      std::max(0.15f, std::min(halfExtentsM.x, halfExtentsM.y) * 0.5f));
  world->addRigidBody(body.get());
}

VehicleBody::~VehicleBody() {
  if (world && body) world->removeRigidBody(body.get());
  for (auto &entry : terrainTiles) removeTerrainTile(*entry.second);
}

void VehicleBody::removeTerrainTile(TerrainTile &tile) {
  if (world && tile.object) world->removeCollisionObject(tile.object.get());
  tile.object.reset();
  tile.shape.reset();
}

void VehicleBody::synchronize(const State &state) {
  btTransform transform;
  transform.setIdentity();
  transform.setOrigin(toBullet(state.position));
  transform.setRotation(toBullet(state.rotation));
  body->setWorldTransform(transform);
  body->setInterpolationWorldTransform(transform);
  if (body->getMotionState()) body->getMotionState()->setWorldTransform(transform);
  body->setLinearVelocity(toBullet(state.velocity));
  body->setAngularVelocity(toBullet(state.angularVelocity));
  body->clearForces();
  body->activate(true);
}

void VehicleBody::prepareTerrainCollision(const TerrainHeightQuery &heightAt,
                                          const glm::vec3 &center,
                                          std::uint64_t surfaceRevision) {
  terrainHeightQuery = heightAt;
  if (terrainSurfaceRevision != surfaceRevision) {
    for (auto &entry : terrainTiles) removeTerrainTile(*entry.second);
    terrainTiles.clear();
    terrainSurfaceRevision = surfaceRevision;
  }
  ensureTerrainTiles(center);
}

void VehicleBody::ensureTerrainTiles(const glm::vec3 &center) {
  if (!terrainHeightQuery) return;
  constexpr int kTileCells = 32;
  constexpr int kTilePoints = kTileCells + 1;
  constexpr float kCellSizeM = 2.0f;
  constexpr float kTileSizeM = kTileCells * kCellSizeM;
  constexpr int kSamplesPerFrame = 256;

  const int centerTileX = static_cast<int>(std::floor(center.x / kTileSizeM));
  const int centerTileZ = static_cast<int>(std::floor(center.z / kTileSizeM));

  for (auto it = terrainTiles.begin(); it != terrainTiles.end();) {
    TerrainTile &tile = *it->second;
    if (std::abs(tile.tileX - centerTileX) > 1 ||
        std::abs(tile.tileZ - centerTileZ) > 1) {
      removeTerrainTile(tile);
      it = terrainTiles.erase(it);
    } else {
      ++it;
    }
  }

  for (int tileZ = centerTileZ - 1; tileZ <= centerTileZ + 1; ++tileZ) {
    for (int tileX = centerTileX - 1; tileX <= centerTileX + 1; ++tileX) {
      const std::uint64_t key = terrainTileKey(tileX, tileZ);
      if (terrainTiles.find(key) != terrainTiles.end()) continue;
      auto tile = std::make_unique<TerrainTile>();
      tile->tileX = tileX;
      tile->tileZ = tileZ;
      tile->heights.resize(static_cast<size_t>(kTilePoints) * kTilePoints);
      terrainTiles.emplace(key, std::move(tile));
    }
  }

  std::vector<TerrainTile *> orderedTiles;
  orderedTiles.reserve(terrainTiles.size());
  for (auto &entry : terrainTiles) orderedTiles.push_back(entry.second.get());
  const auto priority = [&](const TerrainTile *tile) {
    const bool containsCar = tile->tileX == centerTileX && tile->tileZ == centerTileZ;
    const float tileCenterX = (tile->tileX + 0.5f) * kTileSizeM;
    const float tileCenterZ = (tile->tileZ + 0.5f) * kTileSizeM;
    const float dx = tileCenterX - center.x;
    const float dz = tileCenterZ - center.z;
    return std::tuple<int, float, int, int>(containsCar ? 0 : 1, dx * dx + dz * dz,
                                           tile->tileZ, tile->tileX);
  };
  std::sort(orderedTiles.begin(), orderedTiles.end(),
            [&](const TerrainTile *a, const TerrainTile *b) {
              return priority(a) < priority(b);
            });

  int sampleBudget = kSamplesPerFrame;
  for (TerrainTile *tile : orderedTiles) {
    while (tile->nextSampleRow < kTilePoints &&
           sampleBudget >= kTilePoints) {
      const int row = tile->nextSampleRow++;
      const float z = tile->tileZ * kTileSizeM + row * kCellSizeM;
      for (int column = 0; column < kTilePoints; ++column) {
        const float x = tile->tileX * kTileSizeM + column * kCellSizeM;
        const float height = terrainHeightQuery(x, z);
        tile->heights[static_cast<size_t>(row) * kTilePoints + column] = height;
        tile->minHeight = std::min(tile->minHeight, height);
        tile->maxHeight = std::max(tile->maxHeight, height);
      }
      sampleBudget -= kTilePoints;
    }

    if (tile->nextSampleRow == kTilePoints && !tile->object) {
      const float minHeight = tile->minHeight - 0.05f;
      const float maxHeight = tile->maxHeight + 0.05f;
      auto shape = std::make_unique<btHeightfieldTerrainShape>(
          kTilePoints, kTilePoints, tile->heights.data(), minHeight,
          maxHeight, 1, false);
      shape->setLocalScaling(btVector3(kCellSizeM, 1.0f, kCellSizeM));
      shape->setMargin(0.04f);

      auto object = std::make_unique<btCollisionObject>();
      object->setCollisionShape(shape.get());
      object->setCollisionFlags(object->getCollisionFlags() |
                                btCollisionObject::CF_STATIC_OBJECT);
      btTransform transform;
      transform.setIdentity();
      transform.setOrigin(btVector3(
          (tile->tileX + 0.5f) * kTileSizeM,
          (tile->minHeight + tile->maxHeight) * 0.5f,
          (tile->tileZ + 0.5f) * kTileSizeM));
      object->setWorldTransform(transform);
      world->addCollisionObject(object.get(), btBroadphaseProxy::StaticFilter,
                                btBroadphaseProxy::DefaultFilter |
                                    btBroadphaseProxy::CharacterFilter);
      tile->shape = std::move(shape);
      tile->object = std::move(object);
    }

    if (sampleBudget < kTilePoints) break;
  }
}

bool VehicleBody::hasTerrainTileAt(float x, float z) const {
  constexpr float kTileSizeM = 64.0f;
  const int tileX = static_cast<int>(std::floor(x / kTileSizeM));
  const int tileZ = static_cast<int>(std::floor(z / kTileSizeM));
  const auto tile = terrainTiles.find(terrainTileKey(tileX, tileZ));
  return tile != terrainTiles.end() && tile->second->object != nullptr;
}

bool VehicleBody::raycastTerrain(const glm::vec3 &from, const glm::vec3 &to,
                                 glm::vec3 &hitPosition,
                                 glm::vec3 &hitNormal) const {
  if (!world || terrainTiles.empty()) return false;

  struct TerrainOnlyRayCallback final
      : btCollisionWorld::ClosestRayResultCallback {
    const btCollisionObject *chassis;

    TerrainOnlyRayCallback(const btVector3 &fromPoint,
                           const btVector3 &toPoint,
                           const btCollisionObject *chassisObject)
        : btCollisionWorld::ClosestRayResultCallback(fromPoint, toPoint),
          chassis(chassisObject) {}

    bool needsCollision(btBroadphaseProxy *proxy) const override {
      if (!proxy || proxy->m_clientObject == chassis) return false;
      return btCollisionWorld::ClosestRayResultCallback::needsCollision(proxy);
    }
  } callback(toBullet(from), toBullet(to), body.get());

  world->rayTest(toBullet(from), toBullet(to), callback);
  if (!callback.hasHit()) return false;
  hitPosition = fromBullet(callback.m_hitPointWorld);
  const glm::vec3 normal = fromBullet(callback.m_hitNormalWorld);
  hitNormal = glm::dot(normal, normal) > 1.0e-8f
                  ? glm::normalize(normal)
                  : glm::vec3(0.0f, 1.0f, 0.0f);
  return true;
}

VehicleBody::State VehicleBody::step(const State &state,
                                     const glm::vec3 &forceN,
                                     const glm::vec3 &torqueNm, float dt,
                                     const TerrainHeightQuery &heightAt) {
  synchronize(state);
  body->applyCentralForce(toBullet(forceN));
  body->applyTorque(toBullet(torqueNm));
  world->stepSimulation(std::max(0.0f, dt), 0, std::max(1.0e-4f, dt));

  // Keep a direct-query safety net while the heightfield tile under the car is
  // still being sampled. Once it is loaded, Bullet handles chassis contact.
  if (heightAt && !hasTerrainTileAt(state.position.x, state.position.z)) {
    btTransform corrected = body->getWorldTransform();
    float correction = 0.0f;
    for (int iz = -1; iz <= 1; ++iz) {
      for (int ix = -1; ix <= 1; ++ix) {
        const btVector3 localPoint(
            static_cast<btScalar>(ix) * chassisHalfExtentsM.x,
            centerOfMassDropM - chassisHalfExtentsM.y,
            static_cast<btScalar>(iz) * chassisHalfExtentsM.z);
        const btVector3 worldPoint = corrected * localPoint;
        const float groundY = heightAt(static_cast<float>(worldPoint.x()),
                                       static_cast<float>(worldPoint.z()));
        correction = std::max(correction,
                              groundY - static_cast<float>(worldPoint.y()));
      }
    }
    if (correction > 0.0f) {
      btVector3 origin = corrected.getOrigin();
      origin.setY(origin.y() + correction);
      corrected.setOrigin(origin);
      body->setWorldTransform(corrected);
      body->setInterpolationWorldTransform(corrected);
      if (body->getMotionState()) body->getMotionState()->setWorldTransform(corrected);
      btVector3 velocity = body->getLinearVelocity();
      if (velocity.y() < 0.0f) velocity.setY(0.0f);
      body->setLinearVelocity(velocity);
      world->updateSingleAabb(body.get());
    }
  }

  const btTransform &transform = body->getWorldTransform();
  State result;
  result.position = fromBullet(transform.getOrigin());
  result.rotation = fromBullet(transform.getRotation());
  result.velocity = fromBullet(body->getLinearVelocity());
  result.angularVelocity = fromBullet(body->getAngularVelocity());
  return result;
}
