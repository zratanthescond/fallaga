#ifndef WATER_SIMULATION_H
#define WATER_SIMULATION_H

#pragma once

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <queue>
#include <vector>

#include <glm/glm.hpp>

#include "RiverSystem.h"
#include "TerrainManager.h"
#include "WaterLevel.h"

// Existing water simulation and texture source for the shared ocean/water
// surface. Wet pools and trickles are rasterized into this grid; no per-river
// meshes or terrain paint are created.
class WaterSimulation {
public:
    static constexpr int SIM_SIZE = 512;
    static constexpr int CHANNELS = 4; // surface elevation, coverage, kind, flow speed

    WaterSimulation() {
        const size_t count = static_cast<size_t>(SIM_SIZE) * SIM_SIZE;
        terrainHeights.resize(count, 0.0f);
        waterDepths.resize(count, 0.0f);
        riverMask.resize(count, false);
        oceanMask.resize(count, false);
        wetMask.resize(count, false);
        wetCoverage.resize(count, 0.0f);
        wetKind.resize(count, 0);
        wetFlowSpeed.resize(count, 0.0f);
        netFlow.resize(count, 0.0f);
        textureData.resize(count * CHANNELS, 0.0f);
    }

    void initialize(TerrainManager* tm, float minX, float maxX, float minZ, float maxZ,
                    float baseWaterLevel, const WetWaterNetwork* wetNetwork = nullptr) {
        worldMinX = minX;
        worldMaxX = maxX;
        worldMinZ = minZ;
        worldMaxZ = maxZ;
        oceanWaterLevel = baseWaterLevel;
        std::fill(waterDepths.begin(), waterDepths.end(), 0.0f);
        std::fill(wetMask.begin(), wetMask.end(), false);
        std::fill(wetCoverage.begin(), wetCoverage.end(), 0.0f);
        std::fill(wetKind.begin(), wetKind.end(), 0);
        std::fill(wetFlowSpeed.begin(), wetFlowSpeed.end(), 0.0f);

        if (!tm) return;

        std::cout << "[WaterSimulation] Initializing on " << SIM_SIZE << "x" << SIM_SIZE << " grid...\n";
        for (int z = 0; z < SIM_SIZE; ++z) {
            const float wz = gridToWorldZ(z);
            for (int x = 0; x < SIM_SIZE; ++x) {
                const int idx = z * SIM_SIZE + x;
                const float wx = gridToWorldX(x);
                terrainHeights[idx] = tm->getHeightAt(wx, wz);
                riverMask[idx] = tm->getRiverAt(wx, wz) > 0.1f;
            }
        }

        oceanMask = floodFillOcean(oceanWaterLevel);
        int oceanCount = 0;
        for (bool b : oceanMask) if (b) ++oceanCount;
        for (int i = 0; i < SIM_SIZE * SIM_SIZE; ++i) {
            if (oceanMask[i])
                waterDepths[i] = std::max(0.0f, oceanWaterLevel - terrainHeights[i]);
            else if (riverMask[i] && terrainHeights[i] < oceanWaterLevel)
                waterDepths[i] = std::max(0.0f, oceanWaterLevel - terrainHeights[i]);
        }

        if (wetNetwork) rasterizeWetSources(tm, *wetNetwork);
        updateTextureData();
        std::cout << "[WaterSimulation] Ocean coverage: " << oceanCount << " / " << (SIM_SIZE * SIM_SIZE)
                  << " cells; wet sources: " << (wetNetwork ? wetNetwork->pools().size() : 0)
                  << " pools, " << (wetNetwork ? wetNetwork->trickles().size() : 0) << " trickles.\n";
    }

    // Flow is restricted to the connected sea and explicit wet-source cells.
    // Water cannot leak from a wadi into surrounding dry terrain.
    void update(float dt) {
        std::fill(netFlow.begin(), netFlow.end(), 0.0f);
        const float flowRate = std::min(std::max(dt, 0.0f) * 8.0f, 0.25f);
        if (flowRate <= 0.0f) return;

        for (int z = 1; z < SIM_SIZE - 1; ++z) {
            for (int x = 1; x < SIM_SIZE - 1; ++x) {
                const int curr = z * SIM_SIZE + x;
                if (!isWaterCell(curr) || waterDepths[curr] <= 0.001f) continue;

                const float currentSurface = terrainHeights[curr] + waterDepths[curr];
                const int neighbors[] = {curr - 1, curr + 1, curr - SIM_SIZE, curr + SIM_SIZE};
                float diffs[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                float totalDiff = 0.0f;
                for (int i = 0; i < 4; ++i) {
                    const int neighbor = neighbors[i];
                    if (!isWaterCell(neighbor)) continue;
                    const float neighborSurface = terrainHeights[neighbor] + waterDepths[neighbor];
                    const float difference = currentSurface - neighborSurface;
                    if (difference > 0.05f) {
                        diffs[i] = difference;
                        totalDiff += difference;
                    }
                }

                if (totalDiff > 0.0f) {
                    const float maxOutflow = waterDepths[curr] * flowRate;
                    for (int i = 0; i < 4; ++i) {
                        if (diffs[i] <= 0.0f) continue;
                        const float transfer = maxOutflow * (diffs[i] / totalDiff);
                        netFlow[curr] -= transfer;
                        netFlow[neighbors[i]] += transfer;
                    }
                }
            }
        }

        for (int i = 0; i < SIM_SIZE * SIM_SIZE; ++i) {
            if (isWaterCell(i)) waterDepths[i] = std::max(0.0f, waterDepths[i] + netFlow[i]);
        }
        updateTextureData();
    }

    void updateTerrainArea(TerrainManager* tm, float centerWx, float centerWz, float radius) {
        if (!tm) return;
        const int minGx = std::max(0, worldToGridX(centerWx - radius));
        const int maxGx = std::min(SIM_SIZE - 1, worldToGridX(centerWx + radius));
        const int minGz = std::max(0, worldToGridZ(centerWz - radius));
        const int maxGz = std::min(SIM_SIZE - 1, worldToGridZ(centerWz + radius));

        for (int gz = minGz; gz <= maxGz; ++gz) {
            const float wz = gridToWorldZ(gz);
            for (int gx = minGx; gx <= maxGx; ++gx) {
                const float wx = gridToWorldX(gx);
                const float dx = wx - centerWx;
                const float dz = wz - centerWz;
                if (dx * dx + dz * dz > radius * radius) continue;

                const int idx = gz * SIM_SIZE + gx;
                const float oldSurface = terrainHeights[idx] + waterDepths[idx];
                terrainHeights[idx] = tm->getHeightAt(wx, wz);
                if (terrainHeights[idx] > oldSurface)
                    waterDepths[idx] = 0.0f;
                else
                    waterDepths[idx] = oldSurface - terrainHeights[idx];
            }
        }
        updateTextureData();
    }

    const float* getTextureData() const { return textureData.data(); }

    void setOceanWaterLevel(float level) {
        if (std::abs(oceanWaterLevel - level) < 0.001f) return;
        oceanWaterLevel = level;
        oceanMask = floodFillOcean(oceanWaterLevel);
        for (int i = 0; i < SIM_SIZE * SIM_SIZE; ++i) {
            if (oceanMask[i])
                waterDepths[i] = std::max(0.0f, oceanWaterLevel - terrainHeights[i]);
            else if (!wetMask[i])
                waterDepths[i] = 0.0f;
        }
        updateTextureData();
    }

private:
    bool isWaterCell(int idx) const { return oceanMask[idx] || wetMask[idx]; }

    float gridToWorldX(int x) const {
        return worldMinX + static_cast<float>(x) / (SIM_SIZE - 1) * (worldMaxX - worldMinX);
    }
    float gridToWorldZ(int z) const {
        return worldMinZ + static_cast<float>(z) / (SIM_SIZE - 1) * (worldMaxZ - worldMinZ);
    }
    int worldToGridX(float x) const {
        const float u = std::clamp((x - worldMinX) / (worldMaxX - worldMinX), 0.0f, 1.0f);
        return static_cast<int>(std::lround(u * (SIM_SIZE - 1)));
    }
    int worldToGridZ(float z) const {
        const float v = std::clamp((z - worldMinZ) / (worldMaxZ - worldMinZ), 0.0f, 1.0f);
        return static_cast<int>(std::lround(v * (SIM_SIZE - 1)));
    }

    bool isRoad(TerrainManager* tm, float x, float z) const {
        return tm && (tm->getRoadAt(x, z) > 0.25f ||
                      tm->getRoadGeometryAt(x, z) > 0.25f);
    }

    void rasterizeWetSources(TerrainManager* tm, const WetWaterNetwork& network) {
        const float cellX = (worldMaxX - worldMinX) / (SIM_SIZE - 1);
        const float cellZ = (worldMaxZ - worldMinZ) / (SIM_SIZE - 1);
        const float cellRadius = 0.5f * std::sqrt(cellX * cellX + cellZ * cellZ);

        for (const WetPool& pool : network.pools()) {
            if (pool.radiusM <= 0.0f) continue;
            const int minX = std::max(0, worldToGridX(pool.center.x - pool.radiusM - cellRadius));
            const int maxX = std::min(SIM_SIZE - 1, worldToGridX(pool.center.x + pool.radiusM + cellRadius));
            const int minZ = std::max(0, worldToGridZ(pool.center.y - pool.radiusM - cellRadius));
            const int maxZ = std::min(SIM_SIZE - 1, worldToGridZ(pool.center.y + pool.radiusM + cellRadius));

            float localBed = std::numeric_limits<float>::infinity();
            for (int z = minZ; z <= maxZ; ++z) {
                for (int x = minX; x <= maxX; ++x) {
                    const float wx = gridToWorldX(x), wz = gridToWorldZ(z);
                    const float dx = wx - pool.center.x, dz = wz - pool.center.y;
                    if (dx * dx + dz * dz > pool.radiusM * pool.radiusM || isRoad(tm, wx, wz)) continue;
                    localBed = std::min(localBed, terrainHeights[z * SIM_SIZE + x]);
                }
            }
            if (!std::isfinite(localBed)) {
                const int x = worldToGridX(pool.center.x), z = worldToGridZ(pool.center.y);
                localBed = terrainHeights[z * SIM_SIZE + x];
            }
            const float level = std::min(pool.levelM, localBed + pool.maxDepthM);
            if (level <= localBed + 0.025f) continue;

            for (int z = minZ; z <= maxZ; ++z) {
                for (int x = minX; x <= maxX; ++x) {
                    const float wx = gridToWorldX(x), wz = gridToWorldZ(z);
                    const float dx = wx - pool.center.x, dz = wz - pool.center.y;
                    const float distance = std::sqrt(dx * dx + dz * dz);
                    if (distance > pool.radiusM + cellRadius || isRoad(tm, wx, wz)) continue;
                    const int idx = z * SIM_SIZE + x;
                    const float depth = level - terrainHeights[idx];
                    if (depth <= 0.025f || depth > pool.maxDepthM + 0.25f) continue;
                    if (wetKind[idx] == 3) continue; // Preserve the stream path where it enters a pool.
                    wetMask[idx] = true;
                    wetKind[idx] = 2;
                    wetCoverage[idx] = std::max(wetCoverage[idx],
                        std::clamp((pool.radiusM + cellRadius - distance) / cellRadius, 0.0f, 1.0f));
                    waterDepths[idx] = depth;
                }
            }
        }

        const float sampleStep = std::max(4.0f, std::min(cellX, cellZ) * 0.35f);
        for (const WetTrickle& trickle : network.trickles()) {
            if (trickle.points.empty() || trickle.wetLengthM <= 0.0f) continue;
            std::vector<glm::vec2> line;
            line.reserve(trickle.points.size() + 1);
            line.push_back(trickle.spring);
            for (const glm::vec2& point : trickle.points) {
                if (glm::length(point - line.back()) > 0.05f) line.push_back(point);
            }
            if (line.size() < 2) continue;

            const float speed = std::clamp(
                trickle.flowM3s / std::max(trickle.widthM * trickle.depthM, 0.05f), 0.04f, 2.0f);
            float traveled = 0.0f;
            float previousSurface = std::numeric_limits<float>::infinity();
            for (size_t segment = 1; segment < line.size() && traveled < trickle.wetLengthM; ++segment) {
                const glm::vec2 a = line[segment - 1], b = line[segment];
                const glm::vec2 delta = b - a;
                const float segmentLength = glm::length(delta);
                if (segmentLength <= 0.01f) continue;
                const glm::vec2 tangent = delta / segmentLength;
                const int steps = std::max(1, static_cast<int>(std::ceil(segmentLength / sampleStep)));
                for (int step = segment == 1 ? 0 : 1; step <= steps; ++step) {
                    const float along = segmentLength * static_cast<float>(step) / steps;
                    const float distanceAlong = traveled + along;
                    if (distanceAlong > trickle.wetLengthM) break;
                    const float t = along / segmentLength;
                    const glm::vec2 point = a + delta * t;
                    const float fade = std::clamp(1.0f - distanceAlong / trickle.wetLengthM, 0.0f, 1.0f);
                    const float depth = std::clamp(trickle.depthM * (0.45f + 0.55f * fade), 0.05f, 0.5f);
                    float surface = tm->getHeightAt(point.x, point.y) + depth;
                    if (std::isfinite(previousSurface))
                        surface = std::min(surface, previousSurface - std::max(0.002f, sampleStep * 0.001f));
                    previousSurface = surface;
                    markTrickleSample(tm, point, surface, trickle.depthM, trickle.widthM,
                                      speed, tangent, fade, cellRadius);
                }
                traveled += segmentLength;
            }
        }
    }

    void markTrickleSample(TerrainManager* tm, const glm::vec2& point, float surface,
                           float maxDepth, float width, float speed,
                           const glm::vec2& tangent, float fade, float cellRadius) {
        const int x = worldToGridX(point.x), z = worldToGridZ(point.y);
        const float support = cellRadius + width * 0.5f;
        for (int gz = std::max(0, z - 1); gz <= std::min(SIM_SIZE - 1, z + 1); ++gz) {
            for (int gx = std::max(0, x - 1); gx <= std::min(SIM_SIZE - 1, x + 1); ++gx) {
                const float wx = gridToWorldX(gx), wz = gridToWorldZ(gz);
                const float dx = wx - point.x, dz = wz - point.y;
                const float distance = std::sqrt(dx * dx + dz * dz);
                if (distance > support || isRoad(tm, wx, wz)) continue;
                const int idx = gz * SIM_SIZE + gx;
                if (wetKind[idx] == 2) continue;
                const float depth = surface - terrainHeights[idx];
                if (depth <= 0.025f || depth > maxDepth + 0.25f) continue;
                const float coverage = std::clamp(1.0f - distance / support, 0.0f, 1.0f) * std::sqrt(fade);
                wetMask[idx] = true;
                wetKind[idx] = 3;
                wetCoverage[idx] = std::max(wetCoverage[idx], coverage);
                wetFlowSpeed[idx] = speed;
                waterDepths[idx] = std::max(waterDepths[idx], depth);
            }
        }
    }

    std::vector<bool> floodFillOcean(float level) const {
        std::vector<bool> visited(SIM_SIZE * SIM_SIZE, false);
        std::queue<int> queue;
        auto trySeed = [&](int idx) {
            if (!visited[idx] && terrainHeights[idx] < level) {
                visited[idx] = true;
                queue.push(idx);
            }
        };
        for (int z = 0; z < SIM_SIZE; ++z) trySeed(z * SIM_SIZE + (SIM_SIZE - 1));
        for (int i = 0; i < SIM_SIZE * SIM_SIZE; ++i) if (riverMask[i]) trySeed(i);
        while (!queue.empty()) {
            const int curr = queue.front();
            queue.pop();
            const int x = curr % SIM_SIZE, z = curr / SIM_SIZE;
            const int neighbors[4] = {curr - 1, curr + 1, curr - SIM_SIZE, curr + SIM_SIZE};
            const int nx[4] = {x - 1, x + 1, x, x};
            const int nz[4] = {z, z, z - 1, z + 1};
            for (int i = 0; i < 4; ++i) {
                if (nx[i] < 0 || nx[i] >= SIM_SIZE || nz[i] < 0 || nz[i] >= SIM_SIZE) continue;
                trySeed(neighbors[i]);
            }
        }
        return visited;
    }

    void updateTextureData() {
        for (int i = 0; i < SIM_SIZE * SIM_SIZE; ++i) {
            const size_t out = static_cast<size_t>(i) * CHANNELS;
            textureData[out + 0] = oceanWaterLevel;
            textureData[out + 1] = 0.0f;
            textureData[out + 2] = 0.0f;
            textureData[out + 3] = 0.0f;

            if (oceanMask[i]) {
                textureData[out + 0] = terrainHeights[i] + waterDepths[i];
                textureData[out + 1] = 1.0f;
                textureData[out + 2] = 1.0f; // Connected sea.
            } else if (wetMask[i] && waterDepths[i] > 0.015f) {
                textureData[out + 0] = terrainHeights[i] + waterDepths[i];
                textureData[out + 1] = std::clamp(wetCoverage[i], 0.0f, 1.0f);
                textureData[out + 2] = static_cast<float>(wetKind[i]);
                textureData[out + 3] = wetFlowSpeed[i];
            } else if (riverMask[i] && terrainHeights[i] <= oceanWaterLevel + 0.5f) {
                textureData[out + 0] = oceanWaterLevel;
                textureData[out + 1] = 1.0f;
                textureData[out + 2] = 3.0f;
            }
        }
    }

    std::vector<float> terrainHeights;
    std::vector<float> waterDepths;
    std::vector<bool> riverMask;
    std::vector<bool> oceanMask;
    std::vector<bool> wetMask;
    std::vector<float> wetCoverage;
    std::vector<unsigned char> wetKind;
    std::vector<float> wetFlowSpeed;
    std::vector<float> netFlow;
    std::vector<float> textureData;

    float worldMinX = 0.0f, worldMaxX = 0.0f;
    float worldMinZ = 0.0f, worldMaxZ = 0.0f;
    float oceanWaterLevel = TerrainConfig::kBaseWaterLevel;
};

#endif // WATER_SIMULATION_H
