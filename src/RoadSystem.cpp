#include "RoadSystem.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <glm/gtc/type_ptr.hpp>

// ═════════════════════════════════════════════════════════════════════════════
//  HeightSampler
// ═════════════════════════════════════════════════════════════════════════════

void HeightSampler::init(const TerrainCoordinateMapper& mapper,
                         const float* heights, int w, int h) {
    m_mapper = mapper;
    m_heights = heights;
    m_w = w;
    m_h = h;
}

float HeightSampler::sample(float x, float z) const {
    if (!m_heights || m_w <= 0 || m_h <= 0) return 0.0f;

    // World → normalized UV [0,1]
    float ux = (x - m_mapper.bounds.minX) / (m_mapper.bounds.maxX - m_mapper.bounds.minX);
    float uz = (z - m_mapper.bounds.minZ) / (m_mapper.bounds.maxZ - m_mapper.bounds.minZ);

    ux = std::clamp(ux, 0.0f, 1.0f);
    uz = std::clamp(uz, 0.0f, 1.0f);

    // UV → pixel.  TerrainManager maps row 0 to worldMinZ.
    float fx = ux * (m_w - 1);
    float fz = uz * (m_h - 1);

    int x0 = (int)fx, x1 = std::min(x0 + 1, m_w - 1);
    int z0 = (int)fz, z1 = std::min(z0 + 1, m_h - 1);
    float tx = fx - x0, tz = fz - z0;

    // Bilinear interpolation
    float h00 = m_heights[z0 * m_w + x0];
    float h10 = m_heights[z0 * m_w + x1];
    float h01 = m_heights[z1 * m_w + x0];
    float h11 = m_heights[z1 * m_w + x1];

    return h00 * (1 - tx) * (1 - tz)
         + h10 *    tx    * (1 - tz)
         + h01 * (1 - tx) *    tz
         + h11 *    tx    *    tz;
}

// ═════════════════════════════════════════════════════════════════════════════
//  RoadGraph — JSON loading
// ═════════════════════════════════════════════════════════════════════════════
//
//  Minimal JSON parser for the known roads.json format:
//  { "roads": [ { "id":N, "type":"...", "width":F, "shoulder":F,
//                  "points":[ {"x":F,"z":F}, ... ] }, ... ] }

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return s.substr(start, s.find_last_not_of(" \t\r\n") - start + 1);
}

static RoadType parseRoadType(const std::string& s) {
    if (s == "colonial_avenue")  return RoadType::ColonialAvenue;
    if (s == "urban_street")     return RoadType::UrbanStreet;
    if (s == "country_highway")  return RoadType::CountryHighway;
    if (s == "country_dirt")     return RoadType::CountryDirt;
    if (s == "port")             return RoadType::Port;
    if (s == "medina")           return RoadType::Medina;
    return RoadType::CountryDirt;
}

static const char* roadTypeToString(RoadType t) {
    switch (t) {
        case RoadType::ColonialAvenue:  return "colonial_avenue";
        case RoadType::UrbanStreet:     return "urban_street";
        case RoadType::CountryHighway:  return "country_highway";
        case RoadType::CountryDirt:     return "country_dirt";
        case RoadType::Port:            return "port";
        case RoadType::Medina:          return "medina";
    }
    return "country_dirt";
}

bool RoadGraph::loadFromJSON(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[RoadGraph] Failed to open: " << path << std::endl;
        return false;
    }

    // Read entire file into string
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    segments.clear();
    nodes.clear();

    // Simple state-machine parser for the known JSON structure
    // We parse road objects one at a time by finding key-value patterns
    size_t pos = 0;
    auto skipWS = [&]() { while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t' || content[pos] == '\r' || content[pos] == '\n' || content[pos] == ',')) pos++; };
    auto findChar = [&](char c) -> size_t { return content.find(c, pos); };
    auto expectChar = [&](char c) -> bool { skipWS(); if (pos < content.size() && content[pos] == c) { pos++; return true; } return false; };

    auto parseString = [&]() -> std::string {
        skipWS();
        if (pos >= content.size() || content[pos] != '"') return "";
        pos++; // skip opening quote
        size_t end = content.find('"', pos);
        if (end == std::string::npos) return "";
        std::string result = content.substr(pos, end - pos);
        pos = end + 1;
        return result;
    };

    auto parseNumber = [&]() -> float {
        skipWS();
        size_t start = pos;
        while (pos < content.size() && (content[pos] == '-' || content[pos] == '+' || content[pos] == '.' || (content[pos] >= '0' && content[pos] <= '9') || content[pos] == 'e' || content[pos] == 'E')) pos++;
        if (pos == start) return 0.0f;
        return std::stof(content.substr(start, pos - start));
    };

    // Find "roads" array
    size_t roadsPos = content.find("\"roads\"");
    if (roadsPos == std::string::npos) {
        std::cerr << "[RoadGraph] No 'roads' key found in JSON" << std::endl;
        return false;
    }
    pos = roadsPos + 7; // skip "roads"
    skipWS();
    if (!expectChar(':')) return false;
    skipWS();
    if (!expectChar('[')) return false;

    // Parse each road object
    while (pos < content.size()) {
        skipWS();
        if (pos < content.size() && content[pos] == ']') break;

        if (!expectChar('{')) break;

        RoadSegment seg;
        seg.width = 6.0f;
        seg.shoulderWidth = 1.5f;

        // Parse key-value pairs inside the object
        while (pos < content.size() && content[pos] != '}') {
            skipWS();
            if (content[pos] == '}') break;

            std::string key = parseString();
            skipWS();
            expectChar(':');

            if (key == "id") {
                seg.id = (uint32_t)parseNumber();
            } else if (key == "type") {
                std::string typeStr = parseString();
                seg.type = parseRoadType(typeStr);
            } else if (key == "width") {
                seg.width = parseNumber();
            } else if (key == "shoulder") {
                seg.shoulderWidth = parseNumber();
            } else if (key == "points") {
                skipWS();
                expectChar('[');
                while (pos < content.size() && content[pos] != ']') {
                    skipWS();
                    if (content[pos] == ']') break;
                    expectChar('{');

                    RoadPoint pt;
                    while (pos < content.size() && content[pos] != '}') {
                        skipWS();
                        if (content[pos] == '}') break;
                        std::string pkey = parseString();
                        skipWS();
                        expectChar(':');
                        float val = parseNumber();
                        if (pkey == "x") pt.pos.x = val;
                        else if (pkey == "z") pt.pos.y = val; // pos.y stores world Z
                        skipWS();
                    }
                    expectChar('}');
                    seg.controlPoints.push_back(pt);
                    skipWS();
                }
                expectChar(']');
            }
            skipWS();
        }
        expectChar('}');
        skipWS();

        if (seg.controlPoints.size() >= 2) {
            segments.push_back(std::move(seg));
        }
    }

    std::cout << "[RoadGraph] Loaded " << segments.size() << " road segments from " << path << std::endl;
    return !segments.empty();
}

bool RoadGraph::saveToJSON(const std::string& path) const {
    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << "{\n  \"roads\": [\n";
    for (size_t i = 0; i < segments.size(); ++i) {
        const auto& seg = segments[i];
        file << "    {\n";
        file << "      \"id\": " << seg.id << ",\n";
        file << "      \"type\": \"" << roadTypeToString(seg.type) << "\",\n";
        file << "      \"width\": " << seg.width << ",\n";
        file << "      \"shoulder\": " << seg.shoulderWidth << ",\n";
        file << "      \"points\": [\n";
        for (size_t j = 0; j < seg.controlPoints.size(); ++j) {
            file << "        {\"x\": " << seg.controlPoints[j].pos.x
                 << ", \"z\": " << seg.controlPoints[j].pos.y << "}";
            if (j + 1 < seg.controlPoints.size()) file << ",";
            file << "\n";
        }
        file << "      ]\n";
        file << "    }";
        if (i + 1 < segments.size()) file << ",";
        file << "\n";
    }
    file << "  ]\n}\n";
    return true;
}

// ═════════════════════════════════════════════════════════════════════════════
//  RoadGraph — Topology
// ═════════════════════════════════════════════════════════════════════════════

void RoadGraph::buildTopology(float snapTolerance) {
    nodes.clear();

    // Collect all endpoints
    struct EndpointRef {
        uint32_t segIdx;
        bool isEnd; // false = start, true = end
        glm::vec2 pos;
    };
    std::vector<EndpointRef> endpoints;

    for (uint32_t i = 0; i < (uint32_t)segments.size(); ++i) {
        auto& seg = segments[i];
        if (seg.controlPoints.empty()) continue;
        endpoints.push_back({i, false, seg.controlPoints.front().pos});
        endpoints.push_back({i, true,  seg.controlPoints.back().pos});
    }

    // Cluster endpoints within tolerance
    std::vector<int> clusterID(endpoints.size(), -1);
    int nextCluster = 0;

    for (size_t i = 0; i < endpoints.size(); ++i) {
        if (clusterID[i] >= 0) continue;
        clusterID[i] = nextCluster;

        for (size_t j = i + 1; j < endpoints.size(); ++j) {
            if (clusterID[j] >= 0) continue;
            float d = glm::length(endpoints[i].pos - endpoints[j].pos);
            if (d <= snapTolerance) {
                clusterID[j] = nextCluster;
            }
        }
        nextCluster++;
    }

    // Create nodes from clusters
    nodes.resize(nextCluster);
    std::vector<glm::vec2> clusterSum(nextCluster, glm::vec2(0.0f));
    std::vector<int> clusterCount(nextCluster, 0);

    for (size_t i = 0; i < endpoints.size(); ++i) {
        int c = clusterID[i];
        clusterSum[c] += endpoints[i].pos;
        clusterCount[c]++;
        nodes[c].connectedSegments.push_back(endpoints[i].segIdx);
    }

    for (int i = 0; i < nextCluster; ++i) {
        nodes[i].id = (uint32_t)i;
        glm::vec2 avg = clusterSum[i] / float(clusterCount[i]);
        nodes[i].position = glm::vec3(avg.x, 0.0f, avg.y);
    }

    // Count junctions (nodes with 3+ connections)
    int junctions = 0;
    for (auto& n : nodes) {
        if (n.connectedSegments.size() >= 3) junctions++;
    }

    std::cout << "[RoadGraph] Topology: " << nodes.size() << " nodes, "
              << junctions << " junctions" << std::endl;
}

// ═════════════════════════════════════════════════════════════════════════════
//  RoadSpline — Catmull-Rom evaluation
// ═════════════════════════════════════════════════════════════════════════════

static glm::vec2 catmullRom(const glm::vec2& p0, const glm::vec2& p1,
                             const glm::vec2& p2, const glm::vec2& p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) +
                   (-p0 + p2) * t +
                   (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

void RoadSpline::evaluate(RoadSegment& seg, const HeightSampler& sampler,
                          float samplesPerMeter) {
    const auto& cps = seg.controlPoints;
    int n = (int)cps.size();
    if (n < 2) return;

    seg.splinePoints.clear();
    seg.tangents.clear();
    seg.rightVecs.clear();
    seg.cumulativeDist.clear();

    // Collect 2D control points
    std::vector<glm::vec2> pts(n);
    for (int i = 0; i < n; ++i)
        pts[i] = cps[i].pos;

    // Generate Catmull-Rom samples between each pair of control points
    std::vector<glm::vec2> sampled;
    sampled.push_back(pts[0]); // always include first point

    for (int i = 0; i < n - 1; ++i) {
        // Catmull-Rom needs 4 points: p[i-1], p[i], p[i+1], p[i+2]
        glm::vec2 p0 = pts[std::max(0, i - 1)];
        glm::vec2 p1 = pts[i];
        glm::vec2 p2 = pts[std::min(n - 1, i + 1)];
        glm::vec2 p3 = pts[std::min(n - 1, i + 2)];

        // Estimate segment length for adaptive sampling
        float segLen = glm::length(p2 - p1);
        int numSamples = std::max(2, (int)(segLen * samplesPerMeter));

        for (int s = 1; s <= numSamples; ++s) {
            float t = float(s) / float(numSamples);
            glm::vec2 pt = catmullRom(p0, p1, p2, p3, t);

            // Avoid duplicate points
            if (glm::length(pt - sampled.back()) > 0.01f) {
                sampled.push_back(pt);
            }
        }
    }

    // Sample heightmap for elevation
    int m = (int)sampled.size();
    seg.splinePoints.resize(m);
    for (int i = 0; i < m; ++i) {
        float y = sampler.isValid() ? sampler.sample(sampled[i].x, sampled[i].y) : 0.0f;
        seg.splinePoints[i] = glm::vec3(sampled[i].x, y, sampled[i].y);
    }

    // Smooth elevation
    smoothElevation(seg.splinePoints, 0.08f, 3.0f);

    // Compute tangents (central differences)
    seg.tangents.resize(m);
    for (int i = 0; i < m; ++i) {
        glm::vec3 fwd;
        if (i == 0)
            fwd = seg.splinePoints[1] - seg.splinePoints[0];
        else if (i == m - 1)
            fwd = seg.splinePoints[m - 1] - seg.splinePoints[m - 2];
        else
            fwd = (seg.splinePoints[i + 1] - seg.splinePoints[i - 1]) * 0.5f;

        float len = glm::length(fwd);
        seg.tangents[i] = (len > 1e-6f) ? (fwd / len) : glm::vec3(1, 0, 0);
    }

    // Smooth tangents (Gaussian-like: 3-tap moving average, 2 passes)
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<glm::vec3> smoothed = seg.tangents;
        for (int i = 1; i < m - 1; ++i) {
            smoothed[i] = glm::normalize(
                seg.tangents[i - 1] * 0.25f +
                seg.tangents[i]     * 0.50f +
                seg.tangents[i + 1] * 0.25f
            );
        }
        seg.tangents = smoothed;
    }

    // Compute right vectors (tangent × up)
    seg.rightVecs.resize(m);
    for (int i = 0; i < m; ++i) {
        glm::vec3 up(0, 1, 0);
        glm::vec3 right = glm::normalize(glm::cross(up, seg.tangents[i]));
        if (glm::length(right) < 0.01f) right = glm::vec3(1, 0, 0);
        seg.rightVecs[i] = right;
    }

    // Cumulative distance
    seg.cumulativeDist.resize(m);
    seg.cumulativeDist[0] = 0.0f;
    for (int i = 1; i < m; ++i) {
        seg.cumulativeDist[i] = seg.cumulativeDist[i - 1] +
            glm::length(seg.splinePoints[i] - seg.splinePoints[i - 1]);
    }

    // Curvature (discrete curvature = |T[i+1] - T[i-1]| / (2*ds))
    seg.curvature.resize(m, 0.0f);
    for (int i = 1; i < m - 1; ++i) {
        glm::vec3 dT = seg.tangents[i + 1] - seg.tangents[i - 1];
        float ds = seg.cumulativeDist[i + 1] - seg.cumulativeDist[i - 1];
        seg.curvature[i] = (ds > 0.01f) ? (glm::length(dT) / ds) : 0.0f;
    }
}

void RoadSpline::smoothElevation(std::vector<glm::vec3>& positions,
                                  float maxGrade, float smoothSigma) {
    int n = (int)positions.size();
    if (n < 3) return;

    // 1. Gaussian smoothing on Y (elevation)
    int halfKernel = (int)(smoothSigma * 2.0f);
    std::vector<float> smoothedY(n);

    for (int i = 0; i < n; ++i) {
        float sumW = 0.0f, sumY = 0.0f;
        for (int j = std::max(0, i - halfKernel); j <= std::min(n - 1, i + halfKernel); ++j) {
            float d = float(j - i);
            float w = expf(-0.5f * (d * d) / (smoothSigma * smoothSigma));
            sumW += w;
            sumY += w * positions[j].y;
        }
        smoothedY[i] = sumY / sumW;
    }

    // Preserve endpoints
    smoothedY[0] = positions[0].y;
    smoothedY[n - 1] = positions[n - 1].y;

    // 2. Enforce max grade
    // Forward pass: clamp rise
    for (int i = 1; i < n; ++i) {
        float dx = positions[i].x - positions[i - 1].x;
        float dz = positions[i].z - positions[i - 1].z;
        float dist = sqrtf(dx * dx + dz * dz);
        if (dist > 0.01f) {
            float maxDy = maxGrade * dist;
            float dy = smoothedY[i] - smoothedY[i - 1];
            if (dy > maxDy) smoothedY[i] = smoothedY[i - 1] + maxDy;
            else if (dy < -maxDy) smoothedY[i] = smoothedY[i - 1] - maxDy;
        }
    }
    // Backward pass
    for (int i = n - 2; i >= 0; --i) {
        float dx = positions[i + 1].x - positions[i].x;
        float dz = positions[i + 1].z - positions[i].z;
        float dist = sqrtf(dx * dx + dz * dz);
        if (dist > 0.01f) {
            float maxDy = maxGrade * dist;
            float dy = smoothedY[i] - smoothedY[i + 1];
            if (dy > maxDy) smoothedY[i] = smoothedY[i + 1] + maxDy;
            else if (dy < -maxDy) smoothedY[i] = smoothedY[i + 1] - maxDy;
        }
    }

    for (int i = 0; i < n; ++i)
        positions[i].y = smoothedY[i];
}

// ═════════════════════════════════════════════════════════════════════════════
//  RoadDeformer — Non-destructive terrain deformation
// ═════════════════════════════════════════════════════════════════════════════

// Helper: find nearest point on a polyline and return distance + nearest elevation
static float nearestOnPolyline(const std::vector<glm::vec3>& pts,
                                float qx, float qz,
                                float& outElevation) {
    float bestDist = 1e9f;
    outElevation = 0.0f;

    for (int i = 0; i < (int)pts.size() - 1; ++i) {
        float ax = pts[i].x,   az = pts[i].z;
        float bx = pts[i+1].x, bz = pts[i+1].z;
        float dx = bx - ax, dz = bz - az;
        float len2 = dx * dx + dz * dz;
        float t = 0.0f;
        if (len2 > 1e-8f) {
            t = ((qx - ax) * dx + (qz - az) * dz) / len2;
            t = std::clamp(t, 0.0f, 1.0f);
        }
        float px = ax + t * dx;
        float pz = az + t * dz;
        float dist = sqrtf((qx - px) * (qx - px) + (qz - pz) * (qz - pz));

        if (dist < bestDist) {
            bestDist = dist;
            outElevation = pts[i].y * (1.0f - t) + pts[i+1].y * t;
        }
    }
    return bestDist;
}

void RoadDeformer::build(const std::vector<RoadSegment>& segments,
                         const HeightSampler& sampler,
                         const TerrainCoordinateMapper& mapper,
                         int deformW, int deformH) {
    m_mapper = mapper;
    m_mapW = deformW;
    m_mapH = deformH;
    m_deformMap.assign(deformW * deformH, 0.0f);
    m_influenceMap.assign(deformW * deformH, 0.0f);

    std::cout << "[RoadDeformer] Building deformation field (" << deformW << "x" << deformH << ")..." << std::endl;

    float worldW = mapper.bounds.maxX - mapper.bounds.minX;
    float worldH = mapper.bounds.maxZ - mapper.bounds.minZ;
    float pixelSizeX = worldW / float(deformW - 1);
    float pixelSizeZ = worldH / float(deformH - 1);

    // Segment-centric approach: for each road segment, only rasterize pixels
    // within its bounding box + influence radius.  This avoids the O(pixels × segments)
    // brute-force that was causing 60+ second startup times.
    for (const auto& seg : segments) {
        if (seg.splinePoints.empty()) continue;

        float halfWidth = seg.width * 0.5f;
        float shoulder  = seg.shoulderWidth;
        float maxReach  = halfWidth + shoulder + 5.0f;  // max influence distance

        // Compute world-space AABB of this segment's spline points + margin
        float segMinX = 1e9f, segMaxX = -1e9f;
        float segMinZ = 1e9f, segMaxZ = -1e9f;
        for (const auto& pt : seg.splinePoints) {
            segMinX = std::min(segMinX, pt.x);
            segMaxX = std::max(segMaxX, pt.x);
            segMinZ = std::min(segMinZ, pt.z);
            segMaxZ = std::max(segMaxZ, pt.z);
        }
        segMinX -= maxReach;  segMaxX += maxReach;
        segMinZ -= maxReach;  segMaxZ += maxReach;

        // Convert AABB to pixel range in the deformation grid
        // Column: u = (wx - minX) / worldW  →  col = u * (deformW - 1)
        int colMin = std::max(0, (int)floorf((segMinX - mapper.bounds.minX) / worldW * (deformW - 1)));
        int colMax = std::min(deformW - 1, (int)ceilf((segMaxX - mapper.bounds.minX) / worldW * (deformW - 1)));
        // Row: TerrainManager's row 0 is the south edge (minZ).
        int rowMin = std::max(0, (int)floorf((segMinZ - mapper.bounds.minZ) / worldH * (deformH - 1)));
        int rowMax = std::min(deformH - 1, (int)ceilf((segMaxZ - mapper.bounds.minZ) / worldH * (deformH - 1)));

        // Rasterize only the local pixel region around this road segment
        for (int row = rowMin; row <= rowMax; ++row) {
            for (int col = colMin; col <= colMax; ++col) {
                // Pixel → World
                float u = float(col) / float(deformW - 1);
                float v = float(row) / float(deformH - 1);
                float wx = mapper.bounds.minX + u * worldW;
                float wz = mapper.bounds.minZ + v * worldH;

                float roadElev;
                float dist = nearestOnPolyline(seg.splinePoints, wx, wz, roadElev);

                float outerEdge = halfWidth + shoulder;
                if (dist > outerEdge + 5.0f) continue;

                // Influence: 1.0 at center, smooth falloff in shoulder
                float influence = 0.0f;
                if (dist <= halfWidth) {
                    influence = 1.0f;
                } else if (dist <= outerEdge) {
                    float t = (dist - halfWidth) / shoulder;
                    t = std::clamp(t, 0.0f, 1.0f);
                    influence = 1.0f - (t * t * (3.0f - 2.0f * t)); // smoothstep
                }

                if (influence > 0.001f) {
                    float baseH = sampler.sample(wx, wz);
                    float deform = (roadElev - baseH) * influence;

                    int idx = row * deformW + col;
                    // Keep the strongest influence if multiple roads overlap
                    if (influence > m_influenceMap[idx]) {
                        m_deformMap[idx] = deform;
                        m_influenceMap[idx] = influence;
                    }
                }
            }
        }
    }

    // Count affected pixels
    int affected = 0;
    for (int i = 0; i < deformW * deformH; ++i) {
        if (m_influenceMap[i] > 0.001f) affected++;
    }

    if (m_deformTexture == 0) glGenTextures(1, &m_deformTexture);
    glBindTexture(GL_TEXTURE_2D, m_deformTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, deformW, deformH, 0, GL_RED,
                 GL_FLOAT, m_deformMap.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    std::cout << "[RoadDeformer] Done. " << affected << " pixels affected ("
              << (100.0f * affected / (deformW * deformH)) << "%)" << std::endl;
}

RoadDeformer::~RoadDeformer() {
    if (m_deformTexture) glDeleteTextures(1, &m_deformTexture);
}

float RoadDeformer::getDeformation(float x, float z) const {
    if (m_deformMap.empty()) return 0.0f;

    float ux = (x - m_mapper.bounds.minX) / (m_mapper.bounds.maxX - m_mapper.bounds.minX);
    float uz = (z - m_mapper.bounds.minZ) / (m_mapper.bounds.maxZ - m_mapper.bounds.minZ);
    ux = std::clamp(ux, 0.0f, 1.0f);
    uz = std::clamp(uz, 0.0f, 1.0f);

    float fx = ux * (m_mapW - 1);
    float fz = uz * (m_mapH - 1);

    int x0 = (int)fx, x1 = std::min(x0 + 1, m_mapW - 1);
    int z0 = (int)fz, z1 = std::min(z0 + 1, m_mapH - 1);
    float tx = fx - x0, tz = fz - z0;

    float d00 = m_deformMap[z0 * m_mapW + x0];
    float d10 = m_deformMap[z0 * m_mapW + x1];
    float d01 = m_deformMap[z1 * m_mapW + x0];
    float d11 = m_deformMap[z1 * m_mapW + x1];

    return d00 * (1 - tx) * (1 - tz)
         + d10 *    tx    * (1 - tz)
         + d01 * (1 - tx) *    tz
         + d11 *    tx    *    tz;
}

float RoadDeformer::getRoadInfluence(float x, float z) const {
    if (m_influenceMap.empty()) return 0.0f;

    float ux = (x - m_mapper.bounds.minX) / (m_mapper.bounds.maxX - m_mapper.bounds.minX);
    float uz = (z - m_mapper.bounds.minZ) / (m_mapper.bounds.maxZ - m_mapper.bounds.minZ);
    ux = std::clamp(ux, 0.0f, 1.0f);
    uz = std::clamp(uz, 0.0f, 1.0f);

    float fx = ux * (m_mapW - 1);
    float fz = uz * (m_mapH - 1);

    int x0 = (int)fx, x1 = std::min(x0 + 1, m_mapW - 1);
    int z0 = (int)fz, z1 = std::min(z0 + 1, m_mapH - 1);
    float tx = fx - x0, tz = fz - z0;

    float i00 = m_influenceMap[z0 * m_mapW + x0];
    float i10 = m_influenceMap[z0 * m_mapW + x1];
    float i01 = m_influenceMap[z1 * m_mapW + x0];
    float i11 = m_influenceMap[z1 * m_mapW + x1];

    return i00 * (1 - tx) * (1 - tz)
         + i10 *    tx    * (1 - tz)
         + i01 * (1 - tx) *    tz
         + i11 *    tx    *    tz;
}

// ═════════════════════════════════════════════════════════════════════════════
//  RoadMeshBuilder — Runtime ribbon mesh generation
// ═════════════════════════════════════════════════════════════════════════════

RoadMeshBuilder::~RoadMeshBuilder() {
    destroy();
}

void RoadMeshBuilder::destroy() {
    if (m_VAO) { glDeleteVertexArrays(1, &m_VAO); m_VAO = 0; }
    if (m_VBO) { glDeleteBuffers(1, &m_VBO); m_VBO = 0; }
    if (m_EBO) { glDeleteBuffers(1, &m_EBO); m_EBO = 0; }
    m_indexCount = 0;
    m_vertices.clear();
    m_indices.clear();
}

void RoadMeshBuilder::build(const std::vector<RoadSegment>& segments,
                            const HeightSampler& sampler,
                            const RoadDeformer* deformer) {
    m_vertices.clear();
    m_indices.clear();

    const float Z_BIAS = 0.12f;     // Tiny offset above terrain to prevent z-fighting
    const float CAMBER = 0.035f;    // Road crown height at center
    const float TEX_SCALE = 0.20f;  // UV repeat scale along road

    for (const auto& seg : segments) {
        int n = (int)seg.splinePoints.size();
        if (n < 2) continue;

        float hw = seg.width * 0.5f;
        float rtype = float((int)seg.type);
        uint32_t baseIdx = (uint32_t)m_vertices.size();

        for (int i = 0; i < n; ++i) {
            const glm::vec3& center = seg.splinePoints[i];
            const glm::vec3& right  = seg.rightVecs[i];

            // Left, center, right positions
            glm::vec3 leftPos  = center - right * hw;
            glm::vec3 rightPos = center + right * hw;

            // Sample terrain height for each vertex
            float ly = sampler.isValid() ? sampler.sample(leftPos.x, leftPos.z) : center.y;
            float cy = center.y;
            float ry = sampler.isValid() ? sampler.sample(rightPos.x, rightPos.z) : center.y;

            if (deformer && deformer->isBuilt()) {
                ly += deformer->getDeformation(leftPos.x, leftPos.z);
                cy += deformer->getDeformation(center.x, center.z);
                ry += deformer->getDeformation(rightPos.x, rightPos.z);
            }

            // Blend with road elevation + bias
            leftPos.y  = ly + Z_BIAS;
            rightPos.y = ry + Z_BIAS;
            float centerY = cy + Z_BIAS + CAMBER;

            glm::vec3 centerPos = glm::vec3(center.x, centerY, center.z);

            // UV: u = across [0, 0.5, 1], v = distance along road
            float v = seg.cumulativeDist[i] * TEX_SCALE;

            // Normal (approximate: up vector, will be refined by shader)
            glm::vec3 norm(0.0f, 1.0f, 0.0f);

            // Compute proper normal from cross-section
            glm::vec3 leftToRight = rightPos - leftPos;
            glm::vec3 tangent = seg.tangents[i];
            glm::vec3 surfNorm = glm::normalize(glm::cross(tangent, leftToRight));
            if (surfNorm.y < 0.0f) surfNorm = -surfNorm;

            // Emit 3 vertices per ring: left, center, right
            m_vertices.push_back({leftPos,   surfNorm, glm::vec2(0.0f, v), rtype});
            m_vertices.push_back({centerPos, surfNorm, glm::vec2(0.5f, v), rtype});
            m_vertices.push_back({rightPos,  surfNorm, glm::vec2(1.0f, v), rtype});
        }

        // Generate indices (two quads per ring pair: left-center and center-right)
        for (int i = 0; i < n - 1; ++i) {
            uint32_t r0 = baseIdx + i * 3;
            uint32_t r1 = baseIdx + (i + 1) * 3;

            // Left-center quad
            m_indices.push_back(r0 + 0); m_indices.push_back(r1 + 0); m_indices.push_back(r0 + 1);
            m_indices.push_back(r0 + 1); m_indices.push_back(r1 + 0); m_indices.push_back(r1 + 1);

            // Center-right quad
            m_indices.push_back(r0 + 1); m_indices.push_back(r1 + 1); m_indices.push_back(r0 + 2);
            m_indices.push_back(r0 + 2); m_indices.push_back(r1 + 1); m_indices.push_back(r1 + 2);
        }
    }

    m_indexCount = (int)m_indices.size();
    std::cout << "[RoadMeshBuilder] Generated " << m_vertices.size() << " vertices, "
              << (m_indexCount / 3) << " triangles" << std::endl;
}

void RoadMeshBuilder::upload() {
    if (m_vertices.empty() || m_indices.empty()) return;

    // Cleanup previous
    if (m_VAO) glDeleteVertexArrays(1, &m_VAO);
    if (m_VBO) glDeleteBuffers(1, &m_VBO);
    if (m_EBO) glDeleteBuffers(1, &m_EBO);

    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glGenBuffers(1, &m_EBO);

    glBindVertexArray(m_VAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, m_vertices.size() * sizeof(RoadVertex),
                 m_vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_indices.size() * sizeof(uint32_t),
                 m_indices.data(), GL_STATIC_DRAW);

    // Layout 0: vec3 position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RoadVertex),
                          (void*)offsetof(RoadVertex, position));

    // Layout 1: vec3 normal
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(RoadVertex),
                          (void*)offsetof(RoadVertex, normal));

    // Layout 2: vec2 uv
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(RoadVertex),
                          (void*)offsetof(RoadVertex, uv));

    // Layout 3: float roadType
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(RoadVertex),
                          (void*)offsetof(RoadVertex, roadType));

    glBindVertexArray(0);

    std::cout << "[RoadMeshBuilder] Uploaded to GPU: VAO=" << m_VAO << std::endl;
}

void RoadMeshBuilder::render(Shader& shader) const {
    if (m_VAO == 0 || m_indexCount == 0) return;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-3.0f, -3.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindVertexArray(m_VAO);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glDisable(GL_POLYGON_OFFSET_FILL);
}
