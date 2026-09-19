#pragma once
#include <glm/glm.hpp>
#include <cmath>

// ================================================================
// COLLISION SYSTEM — AABB + Sphere broad-phase, impulse resolution
//
// Shapes:
//   AABB  : axis-aligned box (position = center)
//   Sphere: sphere (position = center, radius)
//
// Usage:
//   CollisionShape car   = CollisionShape::box(pos, {2.2, 0.8, 4.5});
//   CollisionShape player= CollisionShape::sphere(pos, 0.4f);
//   CollisionResult r    = CollisionSystem::test(car, player);
//   if (r.hit) CollisionSystem::resolve(posA, velA, massA, posB, velB, massB, r);
// ================================================================

struct CollisionShape {
    enum Type { BOX, SPHERE } type;
    glm::vec3 center;
    glm::vec3 halfExtents; // for BOX
    float     radius;      // for SPHERE

    static CollisionShape box(glm::vec3 c, glm::vec3 half) {
        CollisionShape s; s.type = BOX; s.center = c; s.halfExtents = half; s.radius = 0; return s;
    }
    static CollisionShape sphere(glm::vec3 c, float r) {
        CollisionShape s; s.type = SPHERE; s.center = c; s.radius = r; s.halfExtents = glm::vec3(r); return s;
    }
};

struct CollisionResult {
    bool      hit    = false;
    glm::vec3 normal = glm::vec3(0,1,0); // from B toward A (push A out)
    float     depth  = 0.0f;             // penetration depth
};

class CollisionSystem {
public:
    // ── Test two shapes ────────────────────────────────────────────
    static CollisionResult test(const CollisionShape& a, const CollisionShape& b) {
        if (a.type == CollisionShape::BOX   && b.type == CollisionShape::BOX)    return boxBox(a, b);
        if (a.type == CollisionShape::SPHERE && b.type == CollisionShape::SPHERE) return sphereSphere(a, b);
        if (a.type == CollisionShape::BOX   && b.type == CollisionShape::SPHERE) return boxSphere(a, b);
        if (a.type == CollisionShape::SPHERE && b.type == CollisionShape::BOX)   {
            CollisionResult r = boxSphere(b, a);
            r.normal = -r.normal;  // flip: push A (sphere) out of B (box)
            return r;
        }
        return {};
    }

    // ── Resolve collision — moves both objects apart with impulse ──
    // massB = 0 means B is immovable (wall/monument)
    static void resolve(
        glm::vec3& posA, glm::vec3& velA, float massA,
        glm::vec3& posB, glm::vec3& velB, float massB,
        const CollisionResult& r,
        float restitution = 0.3f)
    {
        if (!r.hit) return;

        float totalMass = massA + massB;
        float ratioA = (massB > 0) ? massB / totalMass : 1.0f;
        float ratioB = (massB > 0) ? massA / totalMass : 0.0f;

        // Positional correction (push out of penetration)
        const float slop  = 0.01f;
        const float pct   = 0.8f;
        float corr = std::max(r.depth - slop, 0.0f) * pct;
        posA += r.normal * corr * ratioA;
        if (massB > 0) posB -= r.normal * corr * ratioB;

        // Velocity impulse
        glm::vec3 relVel = velA - velB;
        float vAlongN = glm::dot(relVel, r.normal);
        if (vAlongN > 0.0f) return; // already separating

        float impulseMag = -(1.0f + restitution) * vAlongN;
        if (massB > 0) impulseMag /= (1.0f / massA + 1.0f / massB);
        else           impulseMag *= massA;

        glm::vec3 impulse = r.normal * impulseMag;
        velA += impulse / massA;
        if (massB > 0) velB -= impulse / massB;
    }

    // Overload: A moves, B is static (wall / building)
    static void resolveStatic(
        glm::vec3& posA, glm::vec3& velA, float massA,
        const CollisionResult& r,
        float restitution = 0.25f)
    {
        glm::vec3 dummyPos(0), dummyVel(0);
        resolve(posA, velA, massA, dummyPos, dummyVel, 0.0f, r, restitution);
    }

    // ── Segment vs Sphere ───────────────────────────────────────────
    static CollisionResult segmentSphere(glm::vec3 p1, glm::vec3 p2, const CollisionShape& sphere) {
        CollisionResult r;
        glm::vec3 segDir = p2 - p1;
        float segLenSq = glm::dot(segDir, segDir);
        
        glm::vec3 closest;
        if (segLenSq < 1e-6f) {
            closest = p1;
        } else {
            float t = glm::dot(sphere.center - p1, segDir) / segLenSq;
            t = glm::clamp(t, 0.0f, 1.0f);
            closest = p1 + t * segDir;
        }
        
        glm::vec3 diff = sphere.center - closest;
        float dist2 = glm::dot(diff, diff);
        if (dist2 >= sphere.radius * sphere.radius) return r;
        
        float dist = std::sqrt(dist2);
        r.hit = true;
        r.depth = sphere.radius - dist;
        r.normal = (dist > 0.0001f) ? diff / dist : glm::vec3(0, 1, 0);
        return r;
    }

    // ── OBB vs Point ───────────────────────────────────────────
    static CollisionResult obbPoint(glm::vec3 obbCenter, glm::vec3 obbHalf, glm::vec3 obbRgt, glm::vec3 obbFwd, glm::vec3 point) {
        CollisionResult r;
        glm::vec3 delta = point - obbCenter;
        
        // Project onto local axes
        float lx = glm::dot(delta, obbRgt);
        float ly = delta.y;
        float lz = glm::dot(delta, obbFwd);
        
        if (std::abs(lx) > obbHalf.x || std::abs(ly) > obbHalf.y || std::abs(lz) > obbHalf.z) return r;
        
        r.hit = true;
        float dx = obbHalf.x - std::abs(lx);
        float dy = obbHalf.y - std::abs(ly);
        float dz = obbHalf.z - std::abs(lz);
        
        if (dx < dy && dx < dz) {
            r.depth = dx;
            r.normal = (lx > 0 ? 1.0f : -1.0f) * obbRgt;
        } else if (dy < dz) {
            r.depth = dy;
            r.normal = glm::vec3(0, ly > 0 ? 1.0f : -1.0f, 0);
        } else {
            r.depth = dz;
            r.normal = (lz > 0 ? 1.0f : -1.0f) * obbFwd;
        }
        return r;
    }

private:
    // BOX vs BOX (SAT on AABB — fast, no rotation)
    static CollisionResult boxBox(const CollisionShape& a, const CollisionShape& b) {
        CollisionResult r;
        glm::vec3 delta = a.center - b.center;
        glm::vec3 overlap = a.halfExtents + b.halfExtents - glm::abs(delta);

        if (overlap.x <= 0 || overlap.y <= 0 || overlap.z <= 0) return r;

        r.hit = true;
        // Find minimum penetration axis
        if (overlap.x < overlap.y && overlap.x < overlap.z) {
            r.depth = overlap.x;
            r.normal = glm::vec3(delta.x < 0 ? -1.f : 1.f, 0, 0);
        } else if (overlap.y < overlap.z) {
            r.depth = overlap.y;
            r.normal = glm::vec3(0, delta.y < 0 ? -1.f : 1.f, 0);
        } else {
            r.depth = overlap.z;
            r.normal = glm::vec3(0, 0, delta.z < 0 ? -1.f : 1.f);
        }
        return r;
    }

    // SPHERE vs SPHERE
    static CollisionResult sphereSphere(const CollisionShape& a, const CollisionShape& b) {
        CollisionResult r;
        glm::vec3 delta = a.center - b.center;
        float dist2 = glm::dot(delta, delta);
        float minDist = a.radius + b.radius;
        if (dist2 >= minDist * minDist) return r;

        float dist = std::sqrt(dist2);
        r.hit    = true;
        r.depth  = minDist - dist;
        r.normal = (dist > 0.0001f) ? delta / dist : glm::vec3(0, 1, 0);
        return r;
    }

    // BOX vs SPHERE (closest point on box)
    static CollisionResult boxSphere(const CollisionShape& box, const CollisionShape& sphere) {
        CollisionResult r;
        glm::vec3 delta = sphere.center - box.center;

        // Closest point on box to sphere center
        glm::vec3 closest = glm::clamp(delta, -box.halfExtents, box.halfExtents);
        glm::vec3 diff    = delta - closest;
        float dist2 = glm::dot(diff, diff);

        if (dist2 >= sphere.radius * sphere.radius) return r;

        float dist = std::sqrt(dist2);
        r.hit    = true;
        r.depth  = sphere.radius - dist;
        r.normal = (dist > 0.0001f) ? diff / dist : glm::vec3(0, 1, 0);
        return r;
    }
};
