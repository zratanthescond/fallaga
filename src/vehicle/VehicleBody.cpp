#include "VehicleBody.h"

#include <btBulletDynamicsCommon.h>

#include <algorithm>
#include <chrono>
#include <cmath>
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
} // namespace

VehicleBody::VehicleBody(float massKg, const glm::vec3 &halfExtentsM,
                         float centerOfMassDropM, float yawInertiaKgM2) {
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
  if (world && terrainObject) world->removeCollisionObject(terrainObject.get());
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

void VehicleBody::rebuildTerrainCollision(const TerrainHeightQuery &heightAt,
                                         const glm::vec3 &center) {
  if (!heightAt) return;
  constexpr int kCells = 128;
  constexpr float kCellSizeM = 2.0f;
  constexpr float kHalfExtentM = kCells * kCellSizeM * 0.5f;
  constexpr int kRowsPerFrame = 16;
  const glm::vec2 nextCenter(center.x, center.z);

  // Building Bullet's BVH is also expensive, so do it away from the frame
  // thread. The active collider and its mesh stay alive until the worker has
  // finished and the replacement can be installed safely here.
  if (pendingTerrainShapeFuture.valid()) {
    if (pendingTerrainShapeFuture.wait_for(std::chrono::seconds(0)) !=
        std::future_status::ready)
      return;

    auto nextShape = pendingTerrainShapeFuture.get();
    auto nextObject = std::make_unique<btCollisionObject>();
    nextObject->setCollisionShape(nextShape.get());
    nextObject->setCollisionFlags(nextObject->getCollisionFlags() |
                                  btCollisionObject::CF_STATIC_OBJECT);
    btTransform identity;
    identity.setIdentity();
    nextObject->setWorldTransform(identity);
    if (terrainObject) {
      world->removeCollisionObject(terrainObject.get());
      terrainObject.reset();
    }
    // A BVH shape refers to its triangle mesh, so destroy the old shape before
    // releasing its mesh.
    terrainShape.reset();
    terrainMesh.reset();
    terrainMesh = std::move(pendingTerrainMesh);
    terrainShape = std::move(nextShape);
    terrainObject = std::move(nextObject);
    world->addCollisionObject(terrainObject.get(), btBroadphaseProxy::StaticFilter,
                              btBroadphaseProxy::DefaultFilter |
                                  btBroadphaseProxy::CharacterFilter);
    terrainCenterXZ = pendingTerrainCenterXZ;
    pendingTerrainHeights.clear();
    pendingNextSampleRow = 0;
    pendingNextTriangleRow = 0;
    hasTerrainCollision = true;
    return;
  }

  // Keep a 32 m edge margin for chassis and wheel rays while avoiding
  // synchronous BVH rebuilds on every short stretch of driving. Build the next
  // patch alongside the active one so the old surface remains queryable until
  // the replacement is complete.
  if (hasTerrainCollision && glm::distance(nextCenter, terrainCenterXZ) < 96.0f &&
      pendingTerrainMesh == nullptr)
    return;

  if (pendingTerrainMesh &&
      glm::distance(nextCenter, pendingTerrainCenterXZ) >= 96.0f) {
    pendingTerrainMesh.reset();
    pendingTerrainHeights.clear();
    pendingNextSampleRow = 0;
    pendingNextTriangleRow = 0;
  }

  if (!pendingTerrainMesh) {
    pendingTerrainMesh = std::make_unique<btTriangleMesh>(true, false);
    pendingTerrainHeights.resize(static_cast<size_t>(kCells + 1) * (kCells + 1));
    pendingTerrainCenterXZ = nextCenter;
    pendingNextSampleRow = 0;
    pendingNextTriangleRow = 0;
  }

  const float startX = pendingTerrainCenterXZ.x - kHalfExtentM;
  const float startZ = pendingTerrainCenterXZ.y - kHalfExtentM;
  const int sampleEndRow = std::min(kCells + 1,
                                    pendingNextSampleRow + kRowsPerFrame);
  for (int iz = pendingNextSampleRow; iz < sampleEndRow; ++iz) {
    for (int ix = 0; ix <= kCells; ++ix) {
      const float x = startX + ix * kCellSizeM;
      const float z = startZ + iz * kCellSizeM;
      pendingTerrainHeights[static_cast<size_t>(iz) * (kCells + 1) + ix] =
          heightAt(x, z);
    }
  }
  pendingNextSampleRow = sampleEndRow;

  const int triangleEndRow = std::min(kCells, pendingNextSampleRow - 1);
  const int row = kCells + 1;
  for (int iz = pendingNextTriangleRow; iz < triangleEndRow; ++iz) {
    for (int ix = 0; ix < kCells; ++ix) {
      const int a = iz * row + ix;
      const int b = a + 1;
      const int c = a + row;
      const int d = c + 1;
      const float x0 = startX + ix * kCellSizeM;
      const float x1 = x0 + kCellSizeM;
      const float z0 = startZ + iz * kCellSizeM;
      const float z1 = z0 + kCellSizeM;
      const float h00 = pendingTerrainHeights[static_cast<size_t>(a)];
      const float h10 = pendingTerrainHeights[static_cast<size_t>(b)];
      const float h01 = pendingTerrainHeights[static_cast<size_t>(c)];
      const float h11 = pendingTerrainHeights[static_cast<size_t>(d)];
      // Wind triangles so their normals point up (+Y).
      pendingTerrainMesh->addTriangle({x0, h00, z0}, {x0, h01, z1},
                                      {x1, h10, z0}, true);
      pendingTerrainMesh->addTriangle({x1, h10, z0}, {x0, h01, z1},
                                      {x1, h11, z1}, true);
    }
  }
  pendingNextTriangleRow = triangleEndRow;

  // Height sampling and triangle insertion are spread across rendered frames.
  if (pendingNextSampleRow <= kCells || pendingNextTriangleRow < kCells)
    return;

  btTriangleMesh *meshForBuild = pendingTerrainMesh.get();
  pendingTerrainShapeFuture = std::async(
      std::launch::async, [meshForBuild]() {
        auto shape = std::make_unique<btBvhTriangleMeshShape>(
            meshForBuild, true, true);
        shape->setMargin(0.04f);
        return shape;
      });
}

void VehicleBody::prepareTerrainCollision(const TerrainHeightQuery &heightAt,
                                         const glm::vec3 &center) {
  rebuildTerrainCollision(heightAt, center);
}

bool VehicleBody::raycastTerrain(const glm::vec3 &from, const glm::vec3 &to,
                                 glm::vec3 &hitPosition,
                                 glm::vec3 &hitNormal) const {
  if (!world || !terrainObject || !hasTerrainCollision) return false;

  struct TerrainOnlyRayCallback final
      : btCollisionWorld::ClosestRayResultCallback {
    const btCollisionObject *terrain;

    TerrainOnlyRayCallback(const btVector3 &fromPoint,
                           const btVector3 &toPoint,
                           const btCollisionObject *terrainObject)
        : btCollisionWorld::ClosestRayResultCallback(fromPoint, toPoint),
          terrain(terrainObject) {}

    bool needsCollision(btBroadphaseProxy *proxy) const override {
      if (!proxy || proxy->m_clientObject != terrain) return false;
      return btCollisionWorld::ClosestRayResultCallback::needsCollision(proxy);
    }
  } callback(toBullet(from), toBullet(to), terrainObject.get());

  world->rayTest(toBullet(from), toBullet(to), callback);
  if (!callback.hasHit()) return false;
  hitPosition = fromBullet(callback.m_hitPointWorld);
  hitNormal = glm::dot(fromBullet(callback.m_hitNormalWorld), fromBullet(callback.m_hitNormalWorld)) > 1.0e-8f
                  ? glm::normalize(fromBullet(callback.m_hitNormalWorld))
                  : glm::vec3(0.0f, 1.0f, 0.0f);
  return true;
}

VehicleBody::State VehicleBody::step(const State &state,
                                     const glm::vec3 &forceN,
                                     const glm::vec3 &torqueNm, float dt,
                                     const TerrainHeightQuery &) {
  synchronize(state);
  body->applyCentralForce(toBullet(forceN));
  body->applyTorque(toBullet(torqueNm));
  world->stepSimulation(std::max(0.0f, dt), 0, std::max(1.0e-4f, dt));

  const btTransform &transform = body->getWorldTransform();
  State result;
  result.position = fromBullet(transform.getOrigin());
  result.rotation = fromBullet(transform.getRotation());
  result.velocity = fromBullet(body->getLinearVelocity());
  result.angularVelocity = fromBullet(body->getAngularVelocity());
  return result;
}
