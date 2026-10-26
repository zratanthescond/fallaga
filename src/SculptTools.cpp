#include "SculptTools.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace SculptTools {
namespace {
constexpr float kPi = 3.14159265358979323846f;

// Fast Linear Congruential Generator, matching the original erode brush so its
// gullies stay deterministic frame to frame.
inline unsigned int lcgNext(unsigned int& seed) {
    seed = seed * 1103515245u + 12345u;
    return (seed / 65536u) % 32768u;
}
inline float lcgUnit(unsigned int& seed) {
    return (float)lcgNext(seed) / 32768.0f;
}

bool validField(const HeightField& f) {
    return f.w >= 2 && f.h >= 2 &&
           f.heights.size() >= static_cast<std::size_t>(f.w) * static_cast<std::size_t>(f.h) &&
           std::isfinite(f.worldMinX) && std::isfinite(f.worldMaxX) &&
           std::isfinite(f.worldMinZ) && std::isfinite(f.worldMaxZ) &&
           f.worldMaxX > f.worldMinX && f.worldMaxZ > f.worldMinZ &&
           std::isfinite(f.minHeight) && std::isfinite(f.maxHeight) && f.maxHeight >= f.minHeight;
}

inline float worldPerSampleX(const HeightField& f) {
    return (f.worldMaxX - f.worldMinX) / static_cast<float>(f.w - 1);
}
inline float worldPerSampleZ(const HeightField& f) {
    return (f.worldMaxZ - f.worldMinZ) / static_cast<float>(f.h - 1);
}
inline float worldDistance(float x, float z, float cx, float cz, const HeightField& f) {
    const float dx = (x - cx) * worldPerSampleX(f);
    const float dz = (z - cz) * worldPerSampleZ(f);
    return std::sqrt(dx * dx + dz * dz);
}
} // namespace

float smoothstep01(float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }

float brushFalloff(float distance, float radius, FalloffMode mode, float hardness) {
    if (!std::isfinite(distance) || !std::isfinite(radius) || radius <= 0.0f || distance < 0.0f || distance > radius)
        return 0.0f;
    const float t = std::clamp(1.0f - distance / radius, 0.0f, 1.0f);
    const float h = std::clamp(std::isfinite(hardness) ? hardness : 0.0f, 0.0f, 0.95f);
    // Hardness keeps a full-strength core and remaps only the outer rim.
    const float remapped = h >= 0.999f ? t : std::clamp((t - h) / (1.0f - h), 0.0f, 1.0f);
    switch (mode) {
        case FalloffMode::Linear: return remapped;
        case FalloffMode::Spherical: return std::sqrt(std::max(0.0f, 1.0f - (1.0f - remapped) * (1.0f - remapped)));
        case FalloffMode::Constant: return 1.0f;
        case FalloffMode::Smooth:
        default: return smoothstep01(remapped);
    }
}

std::vector<BrushPoint> sampleStroke(float fromX, float fromZ, float toX, float toZ,
                                     float radius, float spacingFraction) {
    std::vector<BrushPoint> points;
    if (!std::isfinite(fromX) || !std::isfinite(fromZ) || !std::isfinite(toX) ||
        !std::isfinite(toZ) || !std::isfinite(radius) || radius <= 0.0f) return points;
    spacingFraction = std::clamp(std::isfinite(spacingFraction) ? spacingFraction : 0.2f, 0.05f, 2.0f);
    const float dx = toX - fromX, dz = toZ - fromZ;
    const float distance = std::sqrt(dx * dx + dz * dz);
    if (distance <= 1.0e-6f) {
        points.push_back({fromX, fromZ});
        return points;
    }
    const float spacing = std::max(radius * spacingFraction, 0.001f);
    const int segments = std::clamp(static_cast<int>(std::ceil(distance / spacing)), 1, 100000);
    points.reserve(static_cast<std::size_t>(segments) + 1);
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        points.push_back({fromX + dx * t, fromZ + dz * t});
    }
    return points;
}

void worldToGrid(const HeightField& f, float wx, float wz, float& gx, float& gz) {
    if (!validField(f) || !std::isfinite(wx) || !std::isfinite(wz)) { gx = gz = 0.0f; return; }
    const float ux = std::clamp((wx - f.worldMinX) / (f.worldMaxX - f.worldMinX), 0.0f, 1.0f);
    const float uz = std::clamp((wz - f.worldMinZ) / (f.worldMaxZ - f.worldMinZ), 0.0f, 1.0f);
    gx = ux * static_cast<float>(f.w - 1);
    gz = uz * static_cast<float>(f.h - 1);
}

float gridRadius(const HeightField& f, float radius) {
    if (!validField(f) || !std::isfinite(radius) || radius <= 0.0f) return 0.0f;
    const float rx = (radius / (f.worldMaxX - f.worldMinX)) * static_cast<float>(f.w - 1);
    const float rz = (radius / (f.worldMaxZ - f.worldMinZ)) * static_cast<float>(f.h - 1);
    return std::max(rx, rz);
}

IntBox footprint(const HeightField& f, float wx, float wz, float radius) {
    if (!validField(f) || !std::isfinite(wx) || !std::isfinite(wz) ||
        !std::isfinite(radius) || radius <= 0.0f) return {};
    float cx = 0.0f, cz = 0.0f;
    worldToGrid(f, wx, wz, cx, cz);
    const float rx = radius / worldPerSampleX(f);
    const float rz = radius / worldPerSampleZ(f);
    IntBox b;
    b.minX = std::max(0, static_cast<int>(std::floor(cx - rx)));
    b.maxX = std::min(f.w - 1, static_cast<int>(std::ceil(cx + rx)));
    b.minZ = std::max(0, static_cast<int>(std::floor(cz - rz)));
    b.maxZ = std::min(f.h - 1, static_cast<int>(std::ceil(cz + rz)));
    return b;
}

IntBox raiseLower(HeightField& f, const BrushParams& p, float dir) {
    if (!validField(f) || !std::isfinite(p.speed) || !std::isfinite(p.dt) ||
        !std::isfinite(dir) || p.speed <= 0.0f || p.dt <= 0.0f || p.radius <= 0.0f) return {};
    float cx = 0.0f, cz = 0.0f;
    worldToGrid(f, p.wx, p.wz, cx, cz);
    const IntBox b = footprint(f, p.wx, p.wz, p.radius);
    if (b.empty()) return {};
    const float amount = dir * std::min(p.speed, 100.0f) * 10.0f * std::min(p.dt, 0.1f);
    for (int z = b.minZ; z <= b.maxZ; ++z) {
        for (int x = b.minX; x <= b.maxX; ++x) {
            const float dist = worldDistance(static_cast<float>(x), static_cast<float>(z), cx, cz, f);
            if (dist > p.radius) continue;
            const float falloff = brushFalloff(dist, p.radius, p.falloff, p.hardness);
            float& h = f.heights[static_cast<std::size_t>(z) * f.w + x];
            h = std::clamp(h + amount * falloff, f.minHeight, f.maxHeight);
        }
    }
    return b;
}

IntBox smoothTerrain(HeightField& f, const BrushParams& p) {
    if (!validField(f) || !std::isfinite(p.speed) || !std::isfinite(p.dt) ||
        p.speed <= 0.0f || p.dt <= 0.0f || p.radius <= 0.0f) return {};
    float cx = 0.0f, cz = 0.0f;
    worldToGrid(f, p.wx, p.wz, cx, cz);
    const IntBox b = footprint(f, p.wx, p.wz, p.radius);
    if (b.empty()) return {};

    // Snapshot only the footprint plus the 1-sample neighborhood needed by the 3x3 kernel.
    const int sx0 = std::max(0, b.minX - 1), sz0 = std::max(0, b.minZ - 1);
    const int sx1 = std::min(f.w - 1, b.maxX + 1), sz1 = std::min(f.h - 1, b.maxZ + 1);
    const int sw = sx1 - sx0 + 1;
    std::vector<float> source(static_cast<std::size_t>(sw) * (sz1 - sz0 + 1));
    for (int z = sz0; z <= sz1; ++z)
        for (int x = sx0; x <= sx1; ++x)
            source[static_cast<std::size_t>(z - sz0) * sw + (x - sx0)] = f.heights[static_cast<std::size_t>(z) * f.w + x];

    const float blendScale = std::clamp(std::min(p.speed, 100.0f) * 2.0f * std::min(p.dt, 0.1f), 0.0f, 1.0f);
    for (int z = b.minZ; z <= b.maxZ; ++z) {
        for (int x = b.minX; x <= b.maxX; ++x) {
            const float dist = worldDistance(static_cast<float>(x), static_cast<float>(z), cx, cz, f);
            if (dist > p.radius) continue;
            float sum = 0.0f; int count = 0;
            for (int oz = -1; oz <= 1; ++oz) for (int ox = -1; ox <= 1; ++ox) {
                const int nx = x + ox, nz = z + oz;
                if (nx >= sx0 && nx <= sx1 && nz >= sz0 && nz <= sz1) {
                    sum += source[static_cast<std::size_t>(nz - sz0) * sw + (nx - sx0)];
                    ++count;
                }
            }
            const float avg = sum / static_cast<float>(std::max(count, 1));
            const float blend = blendScale * brushFalloff(dist, p.radius, p.falloff, p.hardness);
            float& h = f.heights[static_cast<std::size_t>(z) * f.w + x];
            h = std::clamp(h + (avg - h) * blend, f.minHeight, f.maxHeight);
        }
    }
    return b;
}

IntBox angleSlope(HeightField& f, const BrushParams& p) {
    if (!validField(f) || !std::isfinite(p.speed) || !std::isfinite(p.dt) ||
        !std::isfinite(p.gradeDeg) || !std::isfinite(p.directionDeg) || p.dt <= 0.0f) return {};
    const float r = gridRadius(f, p.radius);
    if (r <= 0.0f) return {};
    float cx = 0.0f, cz = 0.0f;
    worldToGrid(f, p.wx, p.wz, cx, cz);
    const IntBox b = footprint(f, p.wx, p.wz, p.radius);

    const int centerX = std::clamp((int)std::lround(cx), 0, f.w - 1);
    const int centerZ = std::clamp((int)std::lround(cz), 0, f.h - 1);
    const float anchorHeight = f.heights[(std::size_t)centerZ * f.w + centerX];
    const float direction = std::fmod(std::max(p.directionDeg, 0.0f), 360.0f) * kPi / 180.0f;
    const float slopeDirX = std::cos(direction);
    const float slopeDirZ = std::sin(direction);
    const float grade = std::tan(std::clamp(p.gradeDeg, 0.0f, 60.0f) * kPi / 180.0f);
    const float worldPerPixelX = (f.worldMaxX - f.worldMinX) / (float)(f.w - 1);
    const float worldPerPixelZ = (f.worldMaxZ - f.worldMinZ) / (float)(f.h - 1);
    const float blendPerSecond = std::clamp(p.speed * 2.0f, 0.0f, 100.0f);
    const float worldRadius = std::max(p.radius, 0.001f);
    for (int z = b.minZ; z <= b.maxZ; ++z) {
        for (int x = b.minX; x <= b.maxX; ++x) {
            const float worldDx = ((float)x - cx) * worldPerPixelX;
            const float worldDz = ((float)z - cz) * worldPerPixelZ;
            const float distance = std::sqrt(worldDx * worldDx + worldDz * worldDz);
            if (distance > worldRadius) continue;
            const float alongSlope = worldDx * slopeDirX + worldDz * slopeDirZ;
            const float planeHeight = anchorHeight + grade * alongSlope;
            // Keep the requested angle exact across most of the brush, then feather
            // only its outer rim so it joins the untouched terrain cleanly.
            const float influence = brushFalloff(distance, worldRadius, p.falloff, p.hardness);
            float& h = f.heights[(std::size_t)z * f.w + x];
            const float target = planeHeight;
            const float blend = std::clamp(blendPerSecond * p.dt * influence, 0.0f, 1.0f);
            h = std::clamp(h * (1.0f - blend) + target * blend, f.minHeight, f.maxHeight);
        }
    }
    return b;
}

IntBox flatten(HeightField& f, const BrushParams& p) {
    if (!validField(f) || !std::isfinite(p.speed) || !std::isfinite(p.dt) ||
        !std::isfinite(p.targetHeight) || p.speed <= 0.0f || p.dt <= 0.0f ||
        p.radius <= 0.0f) return {};
    float cx = 0.0f, cz = 0.0f;
    worldToGrid(f, p.wx, p.wz, cx, cz);
    const IntBox b = footprint(f, p.wx, p.wz, p.radius);
    if (b.empty()) return {};

    // Same strength ramp as Angle Slope, so a flat pad builds at a comparable rate.
    const float blendPerSecond = std::clamp(p.speed * 2.0f, 0.0f, 100.0f);
    const float worldRadius = std::max(p.radius, 0.001f);
    for (int z = b.minZ; z <= b.maxZ; ++z) {
        for (int x = b.minX; x <= b.maxX; ++x) {
            const float dist = worldDistance(static_cast<float>(x), static_cast<float>(z), cx, cz, f);
            if (dist > worldRadius) continue;
            const float influence = brushFalloff(dist, worldRadius, p.falloff, p.hardness);
            const float blend = std::clamp(blendPerSecond * p.dt * influence, 0.0f, 1.0f);
            float& h = f.heights[static_cast<std::size_t>(z) * f.w + x];
            h = std::clamp(h + (p.targetHeight - h) * blend, f.minHeight, f.maxHeight);
        }
    }
    return b;
}

IntBox level(HeightField& f, const BrushParams& p) {
    if (!validField(f) || !std::isfinite(p.speed) || !std::isfinite(p.dt) ||
        p.speed <= 0.0f || p.dt <= 0.0f || p.radius <= 0.0f) return {};
    float cx = 0.0f, cz = 0.0f;
    worldToGrid(f, p.wx, p.wz, cx, cz);
    const IntBox b = footprint(f, p.wx, p.wz, p.radius);
    if (b.empty()) return {};

    // Falloff-weighted mean, read before anything is written so the target is the
    // surface as the user sees it this frame (not one already pulled toward itself).
    const float worldRadius = std::max(p.radius, 0.001f);
    double sum = 0.0, weightSum = 0.0;
    for (int z = b.minZ; z <= b.maxZ; ++z) {
        for (int x = b.minX; x <= b.maxX; ++x) {
            const float dist = worldDistance(static_cast<float>(x), static_cast<float>(z), cx, cz, f);
            if (dist > worldRadius) continue;
            const float influence = brushFalloff(dist, worldRadius, p.falloff, p.hardness);
            if (influence <= 0.0f) continue;
            sum += static_cast<double>(f.heights[static_cast<std::size_t>(z) * f.w + x]) * influence;
            weightSum += influence;
        }
    }
    if (weightSum <= 0.0) return {};
    const float target = static_cast<float>(sum / weightSum);

    const float blendPerSecond = std::clamp(p.speed * 2.0f, 0.0f, 100.0f);
    for (int z = b.minZ; z <= b.maxZ; ++z) {
        for (int x = b.minX; x <= b.maxX; ++x) {
            const float dist = worldDistance(static_cast<float>(x), static_cast<float>(z), cx, cz, f);
            if (dist > worldRadius) continue;
            const float influence = brushFalloff(dist, worldRadius, p.falloff, p.hardness);
            const float blend = std::clamp(blendPerSecond * p.dt * influence, 0.0f, 1.0f);
            float& h = f.heights[static_cast<std::size_t>(z) * f.w + x];
            h = std::clamp(h + (target - h) * blend, f.minHeight, f.maxHeight);
        }
    }
    return b;
}

void erode(HeightField& f, int numDroplets, float brushX, float brushZ, float radius, bool isLocal) {
    if (!validField(f) || numDroplets <= 0 || !std::isfinite(radius) || radius <= 0.0f ||
        !std::isfinite(brushX) || !std::isfinite(brushZ) || (!isLocal && (f.w < 6 || f.h < 6))) return;

    const int maxLifetime = 30;
    const float inertia = 0.15f;
    const float capacityFactor = 4.0f;
    const float depositionRate = 0.3f;
    const float erosionRate = 0.3f;
    const float evaporationRate = 0.02f;
    const float gravity = 4.0f;
    const int erosionRadius = 3;

    unsigned int lcgSeed = 1337u;
    constexpr int kernelCapacity = (erosionRadius * 2 + 1) * (erosionRadius * 2 + 1);
    int nearXs[kernelCapacity], nearZs[kernelCapacity];
    float weights[kernelCapacity];

    for (int i = 0; i < numDroplets; ++i) {
        float posX, posZ;

        if (isLocal) {
            const float angle = lcgUnit(lcgSeed) * 2.0f * kPi;
            const float rr = std::sqrt(lcgUnit(lcgSeed)) * radius;
            const float wx = brushX + std::cos(angle) * rr;
            const float wz = brushZ + std::sin(angle) * rr;
            const float ux = std::clamp((wx - f.worldMinX) / (f.worldMaxX - f.worldMinX), 0.0f, 1.0f);
            const float uz = std::clamp((wz - f.worldMinZ) / (f.worldMaxZ - f.worldMinZ), 0.0f, 1.0f);
            posX = ux * (float)(f.w - 1);
            posZ = uz * (float)(f.h - 1);
        } else {
            posX = 2.0f + lcgUnit(lcgSeed) * (float)(f.w - 5);
            posZ = 2.0f + lcgUnit(lcgSeed) * (float)(f.h - 5);
        }

        float dirX = 0.0f, dirZ = 0.0f;
        float speed = 1.0f;
        float water = 1.0f;
        float sediment = 0.0f;

        for (int lifetime = 0; lifetime < maxLifetime; ++lifetime) {
            const int x0 = (int)posX;
            const int z0 = (int)posZ;
            const float tx = posX - (float)x0;
            const float tz = posZ - (float)z0;

            if (x0 < 1 || x0 >= f.w - 2 || z0 < 1 || z0 >= f.h - 2) break;

            const int idx00 = z0 * f.w + x0;
            const int idx10 = idx00 + 1;
            const int idx01 = (z0 + 1) * f.w + x0;
            const int idx11 = idx01 + 1;

            const float h00 = f.heights[idx00];
            const float h10 = f.heights[idx10];
            const float h01 = f.heights[idx01];
            const float h11 = f.heights[idx11];

            const float height = h00 * (1.0f - tx) * (1.0f - tz) +
                                 h10 * tx * (1.0f - tz) +
                                 h01 * (1.0f - tx) * tz +
                                 h11 * tx * tz;

            const float gradX = (h10 - h00) * (1.0f - tz) + (h11 - h01) * tz;
            const float gradZ = (h01 - h00) * (1.0f - tx) + (h11 - h10) * tx;

            dirX = dirX * inertia - gradX * (1.0f - inertia);
            dirZ = dirZ * inertia - gradZ * (1.0f - inertia);

            const float len = std::sqrt(dirX * dirX + dirZ * dirZ);
            if (len > 0.0001f) {
                dirX /= len;
                dirZ /= len;
            } else {
                const float randAngle = lcgUnit(lcgSeed) * 2.0f * kPi;
                dirX = std::cos(randAngle);
                dirZ = std::sin(randAngle);
            }

            const float newX = posX + dirX;
            const float newZ = posZ + dirZ;

            const int nx0 = (int)newX;
            const int nz0 = (int)newZ;
            if (nx0 < 1 || nx0 >= f.w - 2 || nz0 < 1 || nz0 >= f.h - 2) break;

            const float n_tx = newX - (float)nx0;
            const float n_tz = newZ - (float)nz0;
            const float nh00 = f.heights[nz0 * f.w + nx0];
            const float nh10 = f.heights[nz0 * f.w + nx0 + 1];
            const float nh01 = f.heights[(nz0 + 1) * f.w + nx0];
            const float nh11 = f.heights[(nz0 + 1) * f.w + nx0 + 1];
            const float newHeight = nh00 * (1.0f - n_tx) * (1.0f - n_tz) +
                                    nh10 * n_tx * (1.0f - n_tz) +
                                    nh01 * (1.0f - n_tx) * n_tz +
                                    nh11 * n_tx * n_tz;

            const float deltaHeight = newHeight - height;
            const float capacity = std::max(0.0f, -deltaHeight) * speed * water * capacityFactor;

            if (sediment > capacity || deltaHeight > 0.0f) {
                const float depositAmount = (deltaHeight > 0.0f)
                    ? std::min(deltaHeight, sediment)
                    : (sediment - capacity) * depositionRate;
                sediment -= depositAmount;

                f.heights[idx00] = std::clamp(f.heights[idx00] + depositAmount * (1.0f - tx) * (1.0f - tz), f.minHeight, f.maxHeight);
                f.heights[idx10] = std::clamp(f.heights[idx10] + depositAmount * tx * (1.0f - tz), f.minHeight, f.maxHeight);
                f.heights[idx01] = std::clamp(f.heights[idx01] + depositAmount * (1.0f - tx) * tz, f.minHeight, f.maxHeight);
                f.heights[idx11] = std::clamp(f.heights[idx11] + depositAmount * tx * tz, f.minHeight, f.maxHeight);
            } else {
                const float erodeAmount = std::min((capacity - sediment) * erosionRate, -deltaHeight);
                sediment += erodeAmount;

                float totalWeight = 0.0f;
                int weightCount = 0;

                for (int sz = z0 - erosionRadius; sz <= z0 + erosionRadius; ++sz) {
                    for (int sx = x0 - erosionRadius; sx <= x0 + erosionRadius; ++sx) {
                        if (sx >= 0 && sx < f.w && sz >= 0 && sz < f.h) {
                            const float dx = (float)sx - posX;
                            const float dz = (float)sz - posZ;
                            const float d = std::sqrt(dx * dx + dz * dz);
                            if (d <= (float)erosionRadius) {
                                const float w = std::max(0.0f, (float)erosionRadius - d);
                                nearXs[weightCount] = sx;
                                nearZs[weightCount] = sz;
                                weights[weightCount] = w;
                                ++weightCount;
                                totalWeight += w;
                            }
                        }
                    }
                }

                if (totalWeight > 0.0f) {
                    const float invTotalWeight = 1.0f / totalWeight;
                    for (int k = 0; k < weightCount; ++k) {
                        const int sx = nearXs[k];
                        const int sz = nearZs[k];
                        f.heights[sz * f.w + sx] = std::clamp(
                            f.heights[sz * f.w + sx] - erodeAmount * weights[k] * invTotalWeight,
                            f.minHeight, f.maxHeight);
                    }
                }
            }

            const float nextSpeedSqr = speed * speed + deltaHeight * gravity;
            if (nextSpeedSqr <= 0.0f || std::isnan(nextSpeedSqr)) break;
            speed = std::sqrt(nextSpeedSqr);
            water *= (1.0f - evaporationRate);

            posX = newX;
            posZ = newZ;
        }
    }
}

int dropletCount(float radius, float speed, float dt) {
    if (!std::isfinite(radius) || !std::isfinite(speed) || !std::isfinite(dt) ||
        radius <= 0.0f || speed <= 0.0f || dt <= 0.0f) return 0;
    // Hard cap prevents accidental multi-million-droplet stalls on a long frame.
    const double requested = static_cast<double>(radius) * speed * dt * 30.0;
    return static_cast<int>(std::clamp(requested, 1.0, 50000.0));
}

IntBox apply(HeightField& f, int mode, const BrushParams& p) {
    switch (mode) {
        case 1: return raiseLower(f, p, +1.0f);
        case 2: return raiseLower(f, p, -1.0f);
        case 3: return smoothTerrain(f, p);
        case 4:
            erode(f, dropletCount(p.radius, p.speed, p.dt), p.wx, p.wz, p.radius, true);
            return footprint(f, p.wx, p.wz, p.radius);
        case 5: return angleSlope(f, p);
        case 9: return flatten(f, p);
        case 10: return level(f, p);
        default: return {};
    }
}

int addGeometry(TerrainRefinement& ref, HeightField& f, const BrushParams& p, int level,
                IntBox& outCellBox) {
    outCellBox = IntBox{};
    if (f.w <= 1 || f.h <= 1 || p.radius <= 0.0f) return 0;
    if (ref.empty()) ref.init(f.w - 1, f.h - 1);

    int minX = 0, maxX = -1, minZ = 0, maxZ = -1;
    const int gained = ref.add(p.wx, p.wz, p.radius, f.worldMinX, f.worldMaxX,
                               f.worldMinZ, f.worldMaxZ, f.heights, f.w, f.h, level,
                               &minX, &maxX, &minZ, &maxZ);
    if (gained > 0) outCellBox = IntBox{minX, maxX, minZ, maxZ};
    return gained;
}

void smoothFine(TerrainRefinement& ref, HeightField& f, const BrushParams& p,
                IntBox& outCellBox) {
    outCellBox = IntBox{};
    if (f.w <= 1 || f.h <= 1 || p.radius <= 0.0f) return;
    if (ref.empty() || !ref.any()) return; // caller decides the fallback

    const int S = TerrainRefinement::kScale;
    float gx = 0.0f, gz = 0.0f;
    worldToGrid(f, p.wx, p.wz, gx, gz);
    const int fcx = (int)(gx * (float)S);
    const int fcz = (int)(gz * (float)S);
    const int frx = (int)((p.radius / (f.worldMaxX - f.worldMinX)) * (float)(f.w - 1) * (float)S) + 1;
    const int frz = (int)((p.radius / (f.worldMaxZ - f.worldMinZ)) * (float)(f.h - 1) * (float)S) + 1;
    const int fr = std::max(frx, frz);

    ref.smooth(p.wx, p.wz, p.radius, f.worldMinX, f.worldMaxX, f.worldMinZ, f.worldMaxZ,
               p.speed, p.dt);
    // The smoothed fine surface becomes authoritative where it lands on grid
    // samples, so height queries, physics and road conformance agree with the mesh.
    ref.mirrorIntoBase(f.heights, f.w, f.h, fcx - fr, fcx + fr, fcz - fr, fcz + fr);

    outCellBox.minX = std::max(0, (fcx - fr) / S);
    outCellBox.maxX = (fcx + fr) / S;
    outCellBox.minZ = std::max(0, (fcz - fr) / S);
    outCellBox.maxZ = (fcz + fr) / S;
}

} // namespace SculptTools
