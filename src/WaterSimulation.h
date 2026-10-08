#ifndef WATER_SIMULATION_H
#define WATER_SIMULATION_H

#pragma once
#include <vector>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <queue>
#include <glm/glm.hpp>
#include "TerrainManager.h"
#include "WaterLevel.h"

class WaterSimulation {
public:
    static constexpr int SIM_SIZE = 512; // 512x512 grid resolves narrow coastal barriers
    
    WaterSimulation() {
        terrainHeights.resize(SIM_SIZE * SIM_SIZE, 0.0f);
        waterDepths.resize(SIM_SIZE * SIM_SIZE, 0.0f);
        riverMask.resize(SIM_SIZE * SIM_SIZE, false);
        oceanMask.resize(SIM_SIZE * SIM_SIZE, false);
        netFlow.resize(SIM_SIZE * SIM_SIZE, 0.0f);
        textureData.resize(SIM_SIZE * SIM_SIZE, 0.0f);
    }

    void initialize(TerrainManager* tm, float minX, float maxX, float minZ, float maxZ, float baseWaterLevel) {
        worldMinX = minX;
        worldMaxX = maxX;
        worldMinZ = minZ;
        worldMaxZ = maxZ;
        oceanWaterLevel = baseWaterLevel;

        if (!tm) return;

        std::cout << "[WaterSimulation] Initializing on " << SIM_SIZE << "x" << SIM_SIZE << " grid...\n";

        for (int z = 0; z < SIM_SIZE; ++z) {
            float fz = (float)z / (SIM_SIZE - 1);
            float wz = worldMinZ + fz * (worldMaxZ - worldMinZ);
            for (int x = 0; x < SIM_SIZE; ++x) {
                float fx = (float)x / (SIM_SIZE - 1);
                float wx = worldMinX + fx * (worldMaxX - worldMinX);

                int idx = z * SIM_SIZE + x;
                float th = tm->getHeightAt(wx, wz);
                terrainHeights[idx] = th;

                // Mark if it is a river from the roadMask (blue channel)
                riverMask[idx] = (tm->getRiverAt(wx, wz) > 0.1f);
            }
        }

        // Only fill areas that are actually connected to the ocean (or a painted
        // river) with water. Isolated dry terrain stays 100% dry.
        oceanMask = floodFillOcean(oceanWaterLevel);
        int oceanCount = 0;
        for (bool b : oceanMask) if (b) oceanCount++;
        std::cout << "[WaterSimulation] Ocean coverage: " << oceanCount << " / " << (SIM_SIZE * SIM_SIZE)
                  << " cells (" << (100.0f * oceanCount / (SIM_SIZE * SIM_SIZE)) << "%)\n";
        for (int i = 0; i < SIM_SIZE * SIM_SIZE; ++i) {
            if (oceanMask[i]) {
                waterDepths[i] = std::max(0.0f, oceanWaterLevel - terrainHeights[i]);
            } else if (riverMask[i]) {
                waterDepths[i] = 0.25f; // Small natural wadi / spring depth
            } else {
                waterDepths[i] = 0.0f;
            }
        }

        updateTextureData();
    }

    // Flow physics for the sea/wadis: redistributes water depth via simple
    // height-difference flow, but ONLY between cells that are part of the
    // connected ocean/river mask computed at initialize()/setOceanWaterLevel().
    // This lets water animate (settle, ripple toward equilibrium in wadis)
    // without ever spreading into new dry land - which is what happened when
    // this used to run unconstrained (see git history).
    void update(float dt) {
        std::fill(netFlow.begin(), netFlow.end(), 0.0f);
        float flowRate = std::min(dt * 8.0f, 0.25f);

        for (int z = 1; z < SIM_SIZE - 1; ++z) {
            for (int x = 1; x < SIM_SIZE - 1; ++x) {
                int curr = z * SIM_SIZE + x;
                if (!oceanMask[curr]) continue;

                float w_curr = waterDepths[curr];
                if (w_curr <= 0.001f) continue;

                float h_curr = terrainHeights[curr] + w_curr;

                int neighbors[] = { curr - 1, curr + 1, curr - SIM_SIZE, curr + SIM_SIZE };
                float diffs[4] = {0.0f};
                float total_diff = 0.0f;
                int active_neighbors = 0;

                for (int i = 0; i < 4; ++i) {
                    int n = neighbors[i];
                    if (!oceanMask[n]) continue; // never flow into cells outside the mask
                    float h_n = terrainHeights[n] + waterDepths[n];
                    float diff = h_curr - h_n;
                    if (diff > 0.05f) {
                        diffs[i] = diff;
                        total_diff += diff;
                        active_neighbors++;
                    }
                }

                if (active_neighbors > 0) {
                    float max_outflow = w_curr * flowRate;
                    for (int i = 0; i < 4; ++i) {
                        if (diffs[i] > 0.0f) {
                            float transfer = max_outflow * (diffs[i] / total_diff);
                            netFlow[curr] -= transfer;
                            netFlow[neighbors[i]] += transfer;
                        }
                    }
                }
            }
        }

        for (int i = 0; i < SIM_SIZE * SIM_SIZE; ++i) {
            if (!oceanMask[i]) continue;
            waterDepths[i] = std::max(0.0f, waterDepths[i] + netFlow[i]);
        }

        updateTextureData();
    }

    void updateTerrainArea(TerrainManager* tm, float centerWx, float centerWz, float radius) {
        if (!tm) return;
        // Find grid bounding box
        float minX = centerWx - radius;
        float maxX = centerWx + radius;
        float minZ = centerWz - radius;
        float maxZ = centerWz + radius;
        
        // Map to grid coordinates
        int minGx = std::max(0, (int)(((minX - worldMinX) / (worldMaxX - worldMinX)) * (SIM_SIZE - 1)));
        int maxGx = std::min(SIM_SIZE - 1, (int)(((maxX - worldMinX) / (worldMaxX - worldMinX)) * (SIM_SIZE - 1)));
        int minGz = std::max(0, (int)(((minZ - worldMinZ) / (worldMaxZ - worldMinZ)) * (SIM_SIZE - 1)));
        int maxGz = std::min(SIM_SIZE - 1, (int)(((maxZ - worldMinZ) / (worldMaxZ - worldMinZ)) * (SIM_SIZE - 1)));
        
        for (int gz = minGz; gz <= maxGz; ++gz) {
            float fz = (float)gz / (SIM_SIZE - 1);
            float wz = worldMinZ + fz * (worldMaxZ - worldMinZ);
            for (int gx = minGx; gx <= maxGx; ++gx) {
                float fx = (float)gx / (SIM_SIZE - 1);
                float wx = worldMinX + fx * (worldMaxX - worldMinX);
                
                // If it is within radius, update
                float dx = wx - centerWx;
                float dz = wz - centerWz;
                if (dx*dx + dz*dz <= radius*radius) {
                    int idx = gz * SIM_SIZE + gx;
                    float newHeight = tm->getHeightAt(wx, wz);
                    float oldHeight = terrainHeights[idx];
                    terrainHeights[idx] = newHeight;
                    
                    // If we raised the terrain, push the water away
                    float waterSurface = oldHeight + waterDepths[idx];
                    if (newHeight > waterSurface) {
                        waterDepths[idx] = 0.0f;
                    } else {
                        // Keep water surface level, depth decreases
                        waterDepths[idx] = waterSurface - newHeight;
                    }
                }
            }
        }
        updateTextureData();
    }

    const float* getTextureData() const {
        return textureData.data();
    }

    void setOceanWaterLevel(float level) {
        if (std::abs(oceanWaterLevel - level) < 0.001f) return;
        oceanWaterLevel = level;

        // Recalculate water depths for entire grid based on new ocean level,
        // restricted to areas connected to the ocean/border (see initialize()).
        oceanMask = floodFillOcean(oceanWaterLevel);
        for (int i = 0; i < SIM_SIZE * SIM_SIZE; ++i) {
            waterDepths[i] = oceanMask[i] ? (oceanWaterLevel - terrainHeights[i]) : 0.0f;
        }
        updateTextureData();
    }

private:
    // BFS flood-fill from the grid border (and any painted river cell) through
    // terrain that stays below `level`, so only water bodies actually connected
    // to the ocean/rivers get filled - isolated dips in the terrain stay dry.
    std::vector<bool> floodFillOcean(float level) const {
        std::vector<bool> visited(SIM_SIZE * SIM_SIZE, false);
        std::queue<int> q;

        auto trySeed = [&](int idx) {
            if (!visited[idx] && terrainHeights[idx] < level) {
                visited[idx] = true;
                q.push(idx);
            }
        };

        // Ocean enters from the east border only; painted rivers are separate seeds.
        for (int z = 0; z < SIM_SIZE; ++z) {
            trySeed(z * SIM_SIZE + (SIM_SIZE - 1)); // East border
        }
        for (int i = 0; i < SIM_SIZE * SIM_SIZE; ++i) {
            if (riverMask[i]) trySeed(i);
        }

        while (!q.empty()) {
            int curr = q.front(); q.pop();
            int cx = curr % SIM_SIZE, cz = curr / SIM_SIZE;
            int nbrs[4][2] = { {cx-1,cz}, {cx+1,cz}, {cx,cz-1}, {cx,cz+1} };
            for (auto& n : nbrs) {
                int nx = n[0], nz = n[1];
                if (nx < 0 || nx >= SIM_SIZE || nz < 0 || nz >= SIM_SIZE) continue;
                trySeed(nz * SIM_SIZE + nx);
            }
        }

        return visited;
    }

    void updateTextureData() {
        for (int i = 0; i < SIM_SIZE * SIM_SIZE; ++i) {
            if (oceanMask[i] || (riverMask[i] && terrainHeights[i] <= oceanWaterLevel + 0.5f)) {
                // Ocean body and coastal wadi estuary: constant sea level
                textureData[i] = oceanWaterLevel;
            } else if (terrainHeights[i] < oceanWaterLevel) {
                // Isolated dry depression below sea level: mask it out so it doesn't flood!
                textureData[i] = -1000.0f;
            } else {
                // Normal dry ground above sea level: keep oceanWaterLevel so bilinear filtering
                // at the coastline NEVER creates a fake vertical sinkhole!
                textureData[i] = oceanWaterLevel;
            }
        }
    }

    std::vector<float> terrainHeights;
    std::vector<float> waterDepths;
    std::vector<bool> riverMask;
    std::vector<bool> oceanMask;
    std::vector<float> netFlow;
    std::vector<float> textureData;

    float worldMinX = 0.0f, worldMaxX = 0.0f;
    float worldMinZ = 0.0f, worldMaxZ = 0.0f;
    float oceanWaterLevel = TerrainConfig::kBaseWaterLevel;
};

#endif // WATER_SIMULATION_H
