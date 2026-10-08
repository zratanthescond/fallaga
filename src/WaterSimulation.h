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
        const auto poolCellCount = std::count(wetKind.begin(), wetKind.end(), static_cast<unsigned char>(2));
        const auto trickleCellCount = std::count(wetKind.begin(), wetKind.end(), static_cast<unsigned char>(3));
        // Report how many authored sources actually reach the water map. Without this the
        // log said "400 pools ... 378 cells" and nothing about why, which is how a
        // feature-destroying bug stayed invisible: the numbers looked plausible.
        std::cout << "[WaterSimulation] Ocean coverage: " << oceanCount << " / " << (SIM_SIZE * SIM_SIZE)
                  << " cells; wet sources: " << (wetNetwork ? wetNetwork->pools().size() : 0)
                  << " pools, " << (wetNetwork ? wetNetwork->trickles().size() : 0) << " trickles; rasterized cells: "
                  << poolCellCount << " pool, " << trickleCellCount << " trickle"
                  << "; pools emitting >0 cells: " << wetPoolsEmitted_ << " / " << wetPoolsTotal_
                  << ", trickles emitting >0 cells: " << wetTricklesEmitted_ << " / " << wetTricklesTotal_
                  << "; pool cells dropped: " << poolRejectShallow_ << " ground-above-surface, "
                  << poolRejectDeep_ << " deeper-than-declared, "
                  << poolRejectAllRoad_ << " all-road, "
                  << poolRejectSubCell_ << " sub-cell (no cell centre covered), "
                  << poolRejectDrowned_ << " covered-but-rejected"
                  << ".\n";
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

    // Area shared by a disc and an axis-aligned unit square, as a fraction of the
    // square. Used to give a wet feature a coverage that reflects how much of the cell
    // it really occupies.
    //
    // This replaces a step function: `(radius + cellRadius - distance) / cellRadius`
    // saturates to 1 for any cell whose CENTRE lies inside the disc, so a pool the size
    // of one grid cell painted that cell at full coverage and its neighbours at nothing.
    // Water therefore had either no edge or a one-cell-wide blocky edge, and -- as the
    // measurements showed -- 400 authored pools became 378 cells, less than one each.
    //
    // Computed by integrating the disc's radius along x across the square, which is a
    // numerically stable one-dimensional quadrature instead of the fiddly closed-form
    // corner decomposition. 16 samples is ample: the result only sets a blend weight.
    static float discSquareOverlap(float cx, float cy, float r) {
        if (r <= 0.0f) return 0.0f;
        const float ax = std::fabs(cx), ay = std::fabs(cy);
        // Quick rejects so most cells cost nothing.
        const float farCorner = std::sqrt((ax + 0.5f) * (ax + 0.5f) + (ay + 0.5f) * (ay + 0.5f));
        if (farCorner <= r) return 1.0f;
        if (ax - 0.5f >= r || ay - 0.5f >= r) return 0.0f;

        constexpr int kSamples = 16;
        const float step = 1.0f / (float)kSamples;
        float area = 0.0f;
        for (int i = 0; i < kSamples; ++i) {
            const float x = -0.5f + ((float)i + 0.5f) * step; // square-local x
            const float dx = x - cx;
            const float inside = r * r - dx * dx;
            if (inside <= 0.0f) continue;
            // Half-chord of the disc at this x, clipped to the square's y extent.
            const float half = std::sqrt(inside);
            const float lo = std::max(-0.5f, cy - half);
            const float hi = std::min(0.5f, cy + half);
            if (hi > lo) area += (hi - lo) * step;
        }
        return std::clamp(area, 0.0f, 1.0f);
    }

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
        wetPoolsTotal_ = (int)network.pools().size();
        wetTricklesTotal_ = (int)network.trickles().size();
        wetPoolsEmitted_ = 0;
        wetTricklesEmitted_ = 0;
        const float cellX = (worldMaxX - worldMinX) / (SIM_SIZE - 1);
        const float cellZ = (worldMaxZ - worldMinZ) / (SIM_SIZE - 1);
        const float cellRadius = 0.5f * std::sqrt(cellX * cellX + cellZ * cellZ);

        for (const WetPool& pool : network.pools()) {
            if (pool.radiusM <= 0.0f) continue;

            // Bed of the pool: the lowest ground actually inside its footprint.
            //
            // The previous version sampled a sparse disc at steps of radius/12 (2-12 m)
            // and then tested ACCEPTANCE at water-grid cell centres up to 54 m away,
            // requiring 0.025 < depth <= maxDepth + 0.25 there. On a median 4 m deep pool
            // a cell-centre bed more than 4.25 m off the pool floor killed the pool
            // entirely -- which is how 400 authored pools produced 378 cells.
            //
            // Sampling is at DEM resolution rather than in metres, so a basin is never
            // missed by a step that happens to straddle its low point.
            float localBed = std::numeric_limits<float>::infinity();
            {
                const float demStepX = std::max(2.0f, (worldMaxX - worldMinX) / 1253.0f);
                const float demStepZ = std::max(2.0f, (worldMaxZ - worldMinZ) / 1253.0f);
                const int steps = std::max(4, (int)std::ceil(2.0f * pool.radiusM /
                                                            std::min(demStepX, demStepZ)));
                const int half = steps / 2;
                for (int j = -half; j <= half; ++j) {
                    for (int i = -half; i <= half; ++i) {
                        const float dx = (float)i / (float)half * pool.radiusM;
                        const float dz = (float)j / (float)half * pool.radiusM;
                        if (dx * dx + dz * dz > pool.radiusM * pool.radiusM) continue;
                        const float wx = pool.center.x + dx, wz = pool.center.y + dz;
                        if (isRoad(tm, wx, wz)) continue;
                        localBed = std::min(localBed, tm->getHeightAt(wx, wz));
                    }
                }
            }
            if (!std::isfinite(localBed)) {
                const int x = worldToGridX(pool.center.x), z = worldToGridZ(pool.center.y);
                localBed = terrainHeights[z * SIM_SIZE + x];
                // Every bed sample was excluded, so the pool sits entirely on road
                // geometry (or off-map). Nothing to draw; count it so the total adds up.
                ++poolRejectAllRoad_;
            }

            // Preserve the authored depth above the source bed, then place that depth
            // relative to the newly sampled bed.
            const float sourceDepth = std::clamp(pool.levelM - pool.bedMinM,
                                                 std::min(0.15f, pool.maxDepthM), pool.maxDepthM);
            const float level = localBed + sourceDepth;

            // Emit over the pool's real footprint. Coverage is the fraction of the cell
            // the disc covers, so a pool smaller than a cell still paints a partial cell
            // rather than a full blocky one, and its edge is a genuine gradient.
            const float radiusCellsX = pool.radiusM / cellX;
            const float radiusCellsZ = pool.radiusM / cellZ;
            const float radiusCells = std::max(radiusCellsX, radiusCellsZ);
            const int spanX = (int)std::ceil(radiusCellsX) + 1;
            const int spanZ = (int)std::ceil(radiusCellsZ) + 1;
            const int cxg = worldToGridX(pool.center.x);
            const int czg = worldToGridZ(pool.center.y);

            int poolCells = 0;
            int poolCellsInFootprint = 0;
            for (int z = std::max(0, czg - spanZ); z <= std::min(SIM_SIZE - 1, czg + spanZ); ++z) {
                for (int x = std::max(0, cxg - spanX); x <= std::min(SIM_SIZE - 1, cxg + spanX); ++x) {
                    const float wx = gridToWorldX(x), wz = gridToWorldZ(z);
                    if (isRoad(tm, wx, wz)) continue;
                    // Offset of the pool centre from this cell's centre, in cell units.
                    const float u = (pool.center.x - wx) / cellX;
                    const float v = (pool.center.y - wz) / cellZ;
                    const float overlap = discSquareOverlap(u, v, radiusCells);
                    if (overlap <= 0.001f) continue;
                    ++poolCellsInFootprint;

                    const int idx = z * SIM_SIZE + x;
                    if (wetKind[idx] == 3) continue; // a stream already owns this cell
                    const float depth = level - terrainHeights[idx];
                    if (depth <= 0.025f) {
                        ++poolRejectShallow_; // ground at or above the pool surface
                        continue;
                    }
                    // A pool has a declared maximum depth; exceeding it means the cell's
                    // ground is far below the pool floor, i.e. outside the basin. Drop
                    // that cell rather than flooding a chasm the pool does not cover.
                    if (depth > pool.maxDepthM + 0.25f) {
                        ++poolRejectDeep_;
                        continue;
                    }

                    wetMask[idx] = true;
                    wetKind[idx] = 2;
                    wetCoverage[idx] = std::max(wetCoverage[idx], overlap);
                    waterDepths[idx] = std::max(waterDepths[idx], depth);
                    ++poolCells;
                }
            }
            if (poolCells > 0) ++wetPoolsEmitted_;
            else if (poolCellsInFootprint == 0) {
                // No water-grid cell is covered by the disc at all: the pool is smaller
                // than a cell and fell between cell centres. This is the resolution limit
                // rather than a rasterisation error, and is the number to drive down by
                // decoupling wet features from the 54.6 m ocean grid.
                ++poolRejectSubCell_;
            } else {
                // Covered cells exist but every one was rejected: the ground is above the
                // rebuilt surface (outside the basin) or far below it.
                ++poolRejectDrowned_;
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
            bool trickleEmitted = false;
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
                    int trickleCells = 0;
                    trickleCells += markTrickleSample(tm, point, surface, trickle.depthM,
                                                      trickle.widthM, speed, tangent, fade,
                                                      cellX, cellZ);
                    if (trickleCells > 0) trickleEmitted = true;
                }
                traveled += segmentLength;
            }
            if (trickleEmitted) ++wetTricklesEmitted_;
        }
    }

    int markTrickleSample(TerrainManager* tm, const glm::vec2& point, float surface,
                          float maxDepth, float width, float speed,
                          const glm::vec2& tangent, float fade,
                          float cellX, float cellZ) {
        int written = 0;
        // A trickle is a ~4 m wide strip, so it is 7% of a 54.6 m cell. The old version
        // looped only over the centre cell and its immediate neighbours while computing a
        // support radius of cellRadius + width/2 (~27 m) -- larger than the cell itself,
        // so the width parameter was effectively inert and the loop bound contradicted
        // the support it was testing against. Coverage is now the strip's area within the
        // cell, which is what makes a sub-cell stream visible as a thin ribbon instead of
        // either nothing or a full block.
        const float radiusCellsX = std::max(width * 0.5f, 0.5f * cellX) / cellX;
        const float radiusCellsZ = std::max(width * 0.5f, 0.5f * cellZ) / cellZ;
        const float radiusCells = std::max(radiusCellsX, radiusCellsZ);
        const int spanX = (int)std::ceil(radiusCellsX) + 1;
        const int spanZ = (int)std::ceil(radiusCellsZ) + 1;
        const int cxg = worldToGridX(point.x);
        const int czg = worldToGridZ(point.y);

        for (int gz = std::max(0, czg - spanZ); gz <= std::min(SIM_SIZE - 1, czg + spanZ); ++gz) {
            for (int gx = std::max(0, cxg - spanX); gx <= std::min(SIM_SIZE - 1, cxg + spanX); ++gx) {
                const float wx = gridToWorldX(gx), wz = gridToWorldZ(gz);
                if (isRoad(tm, wx, wz)) continue;
                const float u = (point.x - wx) / cellX;
                const float v = (point.y - wz) / cellZ;
                const float overlap = discSquareOverlap(u, v, radiusCells);
                if (overlap <= 0.001f) continue;

                const int idx = gz * SIM_SIZE + gx;
                if (wetKind[idx] == 2) continue; // a pool already owns this cell
                const float depth = surface - terrainHeights[idx];
                if (depth <= 0.025f || depth > maxDepth + 0.25f) continue;
                const float coverage = overlap * std::sqrt(std::clamp(fade, 0.0f, 1.0f));
                wetMask[idx] = true;
                wetKind[idx] = 3;
                wetCoverage[idx] = std::max(wetCoverage[idx], coverage);
                wetFlowSpeed[idx] = speed;
                waterDepths[idx] = std::max(waterDepths[idx], depth);
                ++written;
            }
        }
        return written;
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
            // Dry default: keep the vertex on the sampled ground rather than at zero, so
            // triangles straddling a wet/dry boundary cannot dive to sea level and bury a
            // nearby pool or stream surface.
            //
            // This used to be written as terrainHeights[i] followed by an unconditional
            // overwrite with 0,0,0,0, so channel 0 was assigned twice and the first write
            // was dead on every call -- and this runs at 4 Hz plus after every brush
            // stroke.
            textureData[out + 0] = terrainHeights[i];
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

    // How many authored sources actually reached the water map. A source that rasterises
    // to zero cells is invisible in game, and nothing used to report it.
    int wetPoolsTotal_ = 0, wetPoolsEmitted_ = 0;
    int wetTricklesTotal_ = 0, wetTricklesEmitted_ = 0;
    // Why pool cells were dropped, so a pool that emits nothing can be explained.
    int poolRejectShallow_ = 0, poolRejectDeep_ = 0, poolRejectAllRoad_ = 0;
    int poolRejectSubCell_ = 0, poolRejectDrowned_ = 0;
};

#endif // WATER_SIMULATION_H
