#include "RoadSystem.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <glm/gtc/type_ptr.hpp>

namespace {
// Horizontal road frame helper:
// Computes purely horizontal right vectors perpendicular to the road tangent in the XZ plane.
// Roads on terrain must maintain constant width in world space and must not roll or twist
// on slopes, over crests, or around closed loops (roundabouts).
void computeRoadFrames(std::vector<glm::vec3>& rightVecs,
                       const std::vector<glm::vec3>& tangents,
                       bool closed) {
    const int m = (int)tangents.size();
    if (m <= 0) return;
    rightVecs.resize(m);
    for (int i = 0; i < m; ++i) {
        const glm::vec3& t = tangents[i];
        glm::vec2 t_xz(t.x, t.z);
        float len = glm::length(t_xz);
        if (len > 1e-5f) {
            t_xz /= len;
            rightVecs[i] = glm::vec3(t_xz.y, 0.0f, -t_xz.x);
        } else {
            rightVecs[i] = glm::vec3(1.0f, 0.0f, 0.0f);
        }
    }
    // Cyclic/moving-average smoothing pass
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<glm::vec3> smoothed = rightVecs;
        if (closed) {
            for (int i = 0; i < m; ++i) {
                int pr = (i - 1 + m) % m;
                int nx = (i + 1) % m;
                glm::vec3 r = rightVecs[pr] * 0.25f + rightVecs[i] * 0.50f + rightVecs[nx] * 0.25f;
                r.y = 0.0f;
                float rLen = glm::length(r);
                smoothed[i] = (rLen > 1e-5f) ? (r / rLen) : rightVecs[i];
            }
        } else {
            for (int i = 1; i < m - 1; ++i) {
                glm::vec3 r = rightVecs[i - 1] * 0.25f + rightVecs[i] * 0.50f + rightVecs[i + 1] * 0.25f;
                r.y = 0.0f;
                float rLen = glm::length(r);
                smoothed[i] = (rLen > 1e-5f) ? (r / rLen) : rightVecs[i];
            }
        }
        rightVecs = smoothed;
    }
}

// Backward-compatible forwarder
void computeParallelFrames(std::vector<glm::vec3>& rightVecs,
                           const std::vector<glm::vec3>& tangents,
                           int from) {
    (void)from;
    computeRoadFrames(rightVecs, tangents, false);
}
}  // namespace

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

    glm::vec2 uv = m_mapper.worldToUV(x, z);
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) {
        return 0.0f;
    }

    float fx = uv.x * float(m_w - 1);
    float fy = uv.y * float(m_h - 1);

    int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, m_w - 1);
    int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, m_h - 1);
    int x1 = std::min(x0 + 1, m_w - 1);
    int y1 = std::min(y0 + 1, m_h - 1);

    float tx = fx - float(x0);
    float ty = fy - float(y0);

    float h00 = m_heights[y0 * m_w + x0];
    float h10 = m_heights[y0 * m_w + x1];
    float h01 = m_heights[y1 * m_w + x0];
    float h11 = m_heights[y1 * m_w + x1];

    return h00 * (1.0f - tx) * (1.0f - ty)
         + h10 *    tx    * (1.0f - ty)
         + h01 * (1.0f - tx) *    ty
         + h11 *    tx    *    ty;
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

// Hierarchy-based road dimensions (world meters).  Used when a JSON segment
// omits width/shoulder so road sizes stay consistent with the road type.
static float roadTypeDefaultWidth(RoadType t) {
    switch (t) {
        case RoadType::ColonialAvenue:  return 11.5f;
        case RoadType::UrbanStreet:     return 8.0f;
        case RoadType::CountryHighway:  return 8.5f;
        case RoadType::CountryDirt:     return 6.0f;
        case RoadType::Port:            return 10.0f;
        case RoadType::Medina:          return 5.0f;
    }
    return 6.0f;
}

static float roadTypeDefaultShoulder(RoadType t) {
    return (t == RoadType::Medina) ? 0.75f : 1.5f;
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
        bool hasWidth = false, hasShoulder = false;

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
                hasWidth = true;
            } else if (key == "shoulder") {
                seg.shoulderWidth = parseNumber();
                hasShoulder = true;
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

        // Resolve missing width/shoulder from the road-type hierarchy so sizes
        // are always well-defined in world meters.
        if (!hasWidth)  seg.width = roadTypeDefaultWidth(seg.type);
        if (!hasShoulder) seg.shoulderWidth = roadTypeDefaultShoulder(seg.type);

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

    // Detect a closed loop (round road / roundabout): first control point lands back
    // on the last one. Such roads are rendered as continuous rings.
    seg.closed = (n >= 3 &&
        glm::length(pts.front() - pts.back()) < glm::max(seg.width, 10.0f));

    // ── Adaptive sampling ────────────────────────────────────────────────
    // Straight stretches resample at the requested base step (samplesPerMeter);
    // curved sections (the rounded corners) tessellate by turn angle instead, so
    // tight Medina turns get far more samples than long straight avenues without
    // wasting triangle density on the straights.
    const float baseStep = glm::max(0.5f, 1.0f / glm::max(samplesPerMeter, 0.1f));
    const float maxArcAngle = 0.045f;  // one sample per ~2.6 degrees of corner turn

    auto rsewTo = [&](std::vector<glm::vec2>& out, const glm::vec2& p, float step) {
        if (out.empty()) { out.push_back(p); return; }
        glm::vec2 a = out.back();
        float len = glm::length(p - a);
        if (len < 0.01f) return;
        int c = std::max(1, (int)std::ceil(len / glm::max(step, 0.25f)));
        for (int k = 1; k <= c; ++k) {
            glm::vec2 q = glm::mix(a, p, float(k) / float(c));
            if (glm::length(q - out.back()) > 0.01f) out.push_back(q);
        }
    };

    // ── Build the smooth centerline path ──────────────────────────────────
    //  * open roads : circular fillet arcs round every interior corner, so the
    //    ribbon edge-lines and curbs curve through the turn like real streets.
    //  * closed loops: sampled around the ring continuously; the seam corner
    //    (between the last and first anchors) is rounded exactly like the others
    //    instead of letting the closing sliver cut straight across the corner.
    std::vector<glm::vec2> path;
    auto emitCorner = [&](std::vector<glm::vec2>& out, const glm::vec2& a,
                          const glm::vec2& b, const glm::vec2& c, float step) {
        // Returns true when the corner at b is rounded by a fillet arc.
        if (glm::length(b - a) < 1e-4f || glm::length(c - b) < 1e-4f) return false;
        glm::vec2 u1 = glm::normalize(b - a);
        glm::vec2 u2 = glm::normalize(c - b);
        const float dotC = glm::clamp(glm::dot(u1, u2), -1.0f, 1.0f);
        float r = (dotC < 0.93f)
            ? glm::min(seg.width * 0.5f,
                       glm::min(glm::length(b - a) * 0.5f, glm::length(c - b) * 0.5f))
            : 0.0f;
        if (r < 0.15f) r = 0.0f;
        if (r <= 0.0f) return false;
        const glm::vec2 T1 = b - u1 * r;            // arc touch on incoming edge
        const glm::vec2 T2 = b + u2 * r;            // arc touch on outgoing edge
        rsewTo(out, T1, step);
        const float theta = std::acos(dotC);
        // Circle center from the chord T1T2 (exact for any corner angle; the
        // old r/cos(theta/2) bisector guess drifts off the arc off 90 degrees).
        const glm::vec2 mid = (T1 + T2) * 0.5f;
        const glm::vec2 chord = T2 - T1;
        const float halfChord = glm::length(chord) * 0.5f;
        glm::vec2 C;
        if (halfChord <= r) {
            const float h = std::sqrt(std::max(r * r - halfChord * halfChord, 0.0f));
            glm::vec2 perp = glm::dot(chord, chord) > 1e-8f
                ? glm::normalize(glm::vec2(-chord.y, chord.x)) : glm::vec2(0.0f);
            glm::vec2 c1 = mid + perp * h;
            glm::vec2 c2 = mid - perp * h;
            C = (glm::dot(c1 - b, c1 - b) > glm::dot(c2 - b, c2 - b)) ? c1 : c2; // bulge away from apex
        } else {
            C = mid;                                  // degenerate: straight across
        }
        const float a0 = std::atan2(T1.y - C.y, T1.x - C.x);
        const float ccw = (u1.x * u2.y - u1.y * u2.x >= 0.0f) ? 1.0f : -1.0f;
        const int na = std::max(2, (int)std::ceil(theta / maxArcAngle));
        for (int k = 1; k <= na; ++k) {
            const float an = a0 + ccw * (theta * float(k) / float(na));
            const glm::vec2 p = C + glm::vec2(std::cos(an), std::sin(an)) * r;
            if (glm::length(p - out.back()) > 0.01f) out.push_back(p);
        }
        return true;
    };

    if (seg.closed) {
        // Collect unique loop control points (discard duplicate closing point if present)
        std::vector<glm::vec2> loopPts;
        loopPts.reserve(n);
        for (int i = 0; i < n; ++i) {
            if (i == n - 1 && glm::length(pts[i] - pts[0]) < glm::max(seg.width, 15.0f))
                continue;
            loopPts.push_back(pts[i]);
        }
        const int N = (int)loopPts.size();
        if (N >= 3) {
            // Periodic Catmull-Rom spline around the loop: perfectly smooth curve, zero seam kinks!
            for (int k = 0; k < N; ++k) {
                const glm::vec2& p0 = loopPts[(k - 1 + N) % N];
                const glm::vec2& p1 = loopPts[k];
                const glm::vec2& p2 = loopPts[(k + 1) % N];
                const glm::vec2& p3 = loopPts[(k + 2) % N];

                float chordLen = glm::length(p2 - p1);
                int numSteps = std::max(2, (int)std::ceil(chordLen / baseStep));
                for (int s = 0; s < numSteps; ++s) {
                    float t = float(s) / float(numSteps);
                    glm::vec2 pt = catmullRom(p0, p1, p2, p3, t);
                    if (path.empty() || glm::length(pt - path.back()) > 0.05f) {
                        path.push_back(pt);
                    }
                }
            }
        } else {
            for (const auto& p : pts) path.push_back(p);
        }
    } else {
        path.push_back(pts[0]);
        for (int j = 1; j < n - 1; ++j) {
            const glm::vec2& a = pts[j - 1];
            const glm::vec2& b = pts[j];
            const glm::vec2& c = pts[j + 1];
            if (!emitCorner(path, a, b, c, baseStep))
                rsewTo(path, b, baseStep);
        }
        rsewTo(path, pts.back(), baseStep);
    }

    // Sampling at >0.5/m is already done above; store as sampled geometry.
    // Sample heightmap for elevation directly without alteration
    int m = (int)path.size();
    seg.splinePoints.resize(m);
    for (int i = 0; i < m; ++i) {
        float y = sampler.isValid() ? sampler.sample(path[i].x, path[i].y) : 0.0f;
        seg.splinePoints[i] = glm::vec3(path[i].x, y, path[i].y);
    }

    // smoothElevation disabled initially for direct terrain projection verification
    // smoothElevation(seg.splinePoints, 0.08f, 3.0f);

    // Compute tangents (central differences, wrapping for closed rings)
    seg.tangents.resize(m);
    for (int i = 0; i < m; ++i) {
        glm::vec3 fwd;
        if (seg.closed) {
            int pr = (i - 1 + m) % m;
            int nx = (i + 1) % m;
            fwd = (seg.splinePoints[nx] - seg.splinePoints[pr]) * 0.5f;
        } else if (i == 0)
            fwd = seg.splinePoints[1] - seg.splinePoints[0];
        else if (i == m - 1)
            fwd = seg.splinePoints[m - 1] - seg.splinePoints[m - 2];
        else
            fwd = (seg.splinePoints[i + 1] - seg.splinePoints[i - 1]) * 0.5f;

        float len = glm::length(glm::vec2(fwd.x, fwd.z));
        seg.tangents[i] = (len > 1e-6f) ? glm::normalize(glm::vec3(fwd.x, 0.0f, fwd.z)) : glm::vec3(1, 0, 0);
    }

    // Smooth tangents (Gaussian-like: 3-tap moving average, 2 passes)
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<glm::vec3> smoothed = seg.tangents;
        if (seg.closed) {
            // cyclic smoothing so the ring seam is as smooth as the rest
            for (int i = 0; i < m; ++i) {
                int pv = (i - 1 + m) % m;
                int nx = (i + 1) % m;
                glm::vec3 t = seg.tangents[pv] * 0.25f +
                              seg.tangents[i] * 0.50f +
                              seg.tangents[nx] * 0.25f;
                float tLen = glm::length(glm::vec2(t.x, t.z));
                smoothed[i] = (tLen > 1e-6f) ? glm::normalize(glm::vec3(t.x, 0.0f, t.z)) : seg.tangents[i];
            }
        } else {
            for (int i = 1; i < m - 1; ++i) {
                glm::vec3 t = seg.tangents[i - 1] * 0.25f +
                              seg.tangents[i]     * 0.50f +
                              seg.tangents[i + 1] * 0.25f;
                float tLen = glm::length(glm::vec2(t.x, t.z));
                smoothed[i] = (tLen > 1e-6f) ? glm::normalize(glm::vec3(t.x, 0.0f, t.z)) : seg.tangents[i];
            }
        }
        seg.tangents = smoothed;
    }

    // Compute right vectors with horizontal perpendicular (no holonomy, no twist)
    computeRoadFrames(seg.rightVecs, seg.tangents, seg.closed);

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

void RoadMeshBuilder::build(std::vector<RoadSegment> segments,
                            const HeightSampler& sampler,
                            const RoadDeformer* deformer) {
    m_vertices.clear();
    m_indices.clear();

    const float Z_BIAS = 0.25f;     // Clean clearance above terrain for projection testing
    const float CAMBER = 0.0f;      // Flat cross-section to eliminate edge twisting
    const float TEX_SCALE = 0.20f;  // UV repeat scale along road
    const float JN_TOL = 2.5f;      // junction merge tolerance (meters)
    const float PI = 3.14159265f;

    // Dense cross-section so lane markings / edges get crisp geometry.
    // Vertex pillars sit exactly on marking boundaries (u across road).
    static const float kCross[] = {
        0.000f,  // left rim
        0.055f,  // left edge-line outer
        0.105f,  // left edge-line inner
        0.435f,  // center zone
        0.455f,  // double-yellow left outer
        0.495f,  // double-yellow left inner
        0.505f,  // double-yellow right inner
        0.545f,  // double-yellow right outer
        0.565f,  // center zone
        0.895f,  // right edge-line inner
        0.945f,  // right edge-line outer
        1.000f   // right rim
    };
    static const int kCrossCount = (int)(sizeof(kCross) / sizeof(kCross[0]));

    const int S = (int)segments.size();
    if (S == 0) { m_indexCount = 0; return; }

    auto segLen = [&](const RoadSegment& s) {
        if (s.cumulativeDist.size() < 2) return 0.0f;
        return s.cumulativeDist.back() - s.cumulativeDist[0];
    };
    auto isPaved = [&](int t) { return t == 0 || t == 1 || t == 4; };

    std::vector<char> branchLift(S, 0);

    // ─── 0. Branch alignment to host edge ──────────────────────────────────
    // An open endpoint that lands INSIDE another road's corridor is aligned
    // to that road's near edge corridor, so the branch fuses edge-to-edge
    // with the host without burying or twisting its ribbon.
    for (int s = 0; s < S; ++s) {
        RoadSegment& seg = segments[s];
        const int n = (int)seg.splinePoints.size();
        if (seg.closed || n < 2) continue;

        for (int end = 0; end < 2; ++end) {
            const glm::vec2 pos = (end == 0)
                ? glm::vec2(seg.splinePoints.front().x, seg.splinePoints.front().z)
                : glm::vec2(seg.splinePoints.back().x,  seg.splinePoints.back().z);

            // Check if already shared with another endpoint
            bool sharedEnd = false;
            for (int h = 0; h < S && !sharedEnd; ++h) {
                const RoadSegment& ho = segments[h];
                if (h == s || ho.splinePoints.size() < 2) continue;
                glm::vec2 a(ho.splinePoints.front().x, ho.splinePoints.front().z);
                glm::vec2 b(ho.splinePoints.back().x,  ho.splinePoints.back().z);
                if (glm::length(a - pos) <= JN_TOL + 0.05f ||
                    glm::length(b - pos) <= JN_TOL + 0.05f) sharedEnd = true;
            }
            if (sharedEnd) continue;

            // Check if endpoint lands inside another road's corridor (T-junction)
            int hSeg = -1; float hDist = 1e9f;
            glm::vec3 hP; glm::vec3 hR; float hHw = 0.0f;
            for (int h = 0; h < S; ++h) {
                if (h == s) continue;
                const RoadSegment& ho = segments[h];
                if (ho.splinePoints.size() < 2) continue;
                const float tol = ho.width * 0.5f + 1.5f;
                for (int k = 0; k < (int)ho.splinePoints.size(); ++k) {
                    float d = glm::length(glm::vec2(ho.splinePoints[k].x - pos.x,
                                                    ho.splinePoints[k].z - pos.y));
                    if (d < tol && d < hDist) {
                        hDist = d; hSeg = h;
                        hP = ho.splinePoints[k]; hR = ho.rightVecs[k]; hHw = ho.width * 0.5f;
                    }
                }
            }
            if (hSeg < 0) continue;

            branchLift[s] = 1; // merge onto host road -> slight lift to eliminate z-fighting
            // Align endpoint to host road edge
            glm::vec2 dP(pos.x - hP.x, pos.y - hP.z);
            float sgn = (glm::dot(dP, glm::vec2(hR.x, hR.z)) >= 0.0f) ? 1.0f : -1.0f;
            glm::vec2 edgePt = glm::vec2(hP.x, hP.z) + glm::vec2(hR.x, hR.z) * (sgn * hHw);
            float newY = sampler.isValid() ? sampler.sample(edgePt.x, edgePt.y) : hP.y;

            if (end == 0) {
                seg.splinePoints[0] = glm::vec3(edgePt.x, newY, edgePt.y);
            } else {
                seg.splinePoints.back() = glm::vec3(edgePt.x, newY, edgePt.y);
            }
        }
    }

    // ─── 1. Endpoint clustering ─────────────────────────────────────────────
    struct EndRef { int seg; bool isBack; glm::vec2 pos; float hw; };
    std::vector<EndRef> ends;
    std::vector<std::pair<int, int>> segEndRef(S, { -1, -1 });
    for (int s = 0; s < S; ++s) {
        const RoadSegment& seg = segments[s];
        if (seg.splinePoints.size() < 2) continue;
        const int fi = (int)ends.size();
        ends.push_back({ s, false, glm::vec2(seg.splinePoints.front().x, seg.splinePoints.front().z), seg.width * 0.5f });
        ends.push_back({ s, true,  glm::vec2(seg.splinePoints.back().x,  seg.splinePoints.back().z),  seg.width * 0.5f });
        segEndRef[s] = { fi, fi + 1 };
    }
    const int E = (int)ends.size();
    std::vector<int> cid(E, -1);
    int nc = 0;
    for (int i = 0; i < E; ++i) {
        if (cid[i] >= 0) continue;
        cid[i] = nc;
        for (int j = i + 1; j < E; ++j)
            if (cid[j] < 0 && glm::length(ends[i].pos - ends[j].pos) <= JN_TOL) cid[j] = nc;
        ++nc;
    }
    std::vector<std::vector<int>> cluster(nc);
    for (int i = 0; i < E; ++i) cluster[cid[i]].push_back(i);

    auto countRealSegsInCluster = [&](const std::vector<int>& cl) {
        int seen = 0;
        for (size_t a = 0; a < cl.size(); ++a) {
            bool dup = false;
            for (size_t b = 0; b < a; ++b) if (ends[cl[b]].seg == ends[cl[a]].seg) { dup = true; break; }
            if (!dup) ++seen;
        }
        return seen;
    };

    // ─── 2. Network-continuous texture distance v ───────────────────────────
    std::vector<float> vStart(S, 0.0f);
    std::vector<bool>  vDone(S, false);
    for (int s = 0; s < S && !vDone[s]; ++s) {
        if (segments[s].closed) { vDone[s] = true; continue; }
        vStart[s] = 0.0f; vDone[s] = true;
        bool ch = true;
        while (ch) {
            ch = false;
            for (int c = 0; c < nc; ++c) {
                if (countRealSegsInCluster(cluster[c]) != 2) continue;
                int known = -1;
                for (int id : cluster[c])
                    if (vDone[ends[id].seg]) { known = id; break; }
                if (known < 0) continue;
                const EndRef& ek = ends[known];
                float vK = vStart[ek.seg] + (ek.isBack ? segLen(segments[ek.seg]) : 0.0f);
                for (int id : cluster[c]) {
                    const EndRef& eu = ends[id];
                    if (vDone[eu.seg] || segments[eu.seg].closed) continue;
                    vStart[eu.seg] = vK - (eu.isBack ? segLen(segments[eu.seg]) : 0.0f);
                    vDone[eu.seg] = true;
                    ch = true;
                }
            }
        }
    }

    int capCount = 0, jnCount = 0, jointCount = 0;
    std::vector<uint32_t> firstRingBase(S, 0);
    std::vector<uint32_t> lastRingBase(S, 0);

    // ─── 3. Ribbon rings per segment ────────────────────────────────────────
    for (int s = 0; s < S; ++s) {
        const RoadSegment& seg = segments[s];
        int n = (int)seg.splinePoints.size();
        if (n < 2) continue;

        const bool closed = seg.closed;
        const float hw = seg.width * 0.5f;
        const float rtype = float((int)seg.type);
        const float vOff = vStart[s];
        const float stripLift = branchLift[s] ? 0.02f : 0.0f;
        const uint32_t baseIdx = (uint32_t)m_vertices.size();
        firstRingBase[s] = baseIdx;

        if (closed) {
            float closingDist = glm::length(seg.splinePoints[0] - seg.splinePoints[n - 1]);
            float totalDist = seg.cumulativeDist[n - 1] + closingDist;
            const float dashCycle = 2.3f;
            float targetCycles = std::max(1.0f, std::round((totalDist * TEX_SCALE) / dashCycle));
            float ringTexScale = (targetCycles * dashCycle) / totalDist;

            // Emit rings 0 .. n - 1
            for (int i = 0; i < n; ++i) {
                const glm::vec3& center = seg.splinePoints[i];
                const glm::vec3& right  = seg.rightVecs[i];
                glm::vec3 across = right * (2.0f * hw);
                float v = seg.cumulativeDist[i] * ringTexScale;

                glm::vec3 tangent = seg.tangents[i];
                glm::vec3 surfNorm = glm::normalize(glm::cross(tangent, across));
                if (surfNorm.y < 0.0f) surfNorm = -surfNorm;

                for (int j = 0; j < kCrossCount; ++j) {
                    float uc = kCross[j];
                    float off = uc * 2.0f - 1.0f;

                    glm::vec3 pos = center + right * (off * hw);
                    float y = sampler.isValid() ? sampler.sample(pos.x, pos.z) : center.y;
                    if (deformer && deformer->isBuilt())
                        y += deformer->getDeformation(pos.x, pos.z);
                    pos.y = y + Z_BIAS + CAMBER + stripLift;

                    m_vertices.push_back({pos, surfNorm, glm::vec2(uc, v), rtype});
                }
            }

            // Emit closing ring n: identical position and normal to ring 0, but v = totalDist * ringTexScale
            lastRingBase[s] = (uint32_t)m_vertices.size();
            {
                const glm::vec3& center = seg.splinePoints[0];
                const glm::vec3& right  = seg.rightVecs[0];
                glm::vec3 across = right * (2.0f * hw);
                float v = totalDist * ringTexScale;

                glm::vec3 tangent = seg.tangents[0];
                glm::vec3 surfNorm = glm::normalize(glm::cross(tangent, across));
                if (surfNorm.y < 0.0f) surfNorm = -surfNorm;

                for (int j = 0; j < kCrossCount; ++j) {
                    float uc = kCross[j];
                    float off = uc * 2.0f - 1.0f;

                    glm::vec3 pos = center + right * (off * hw);
                    float y = sampler.isValid() ? sampler.sample(pos.x, pos.z) : center.y;
                    if (deformer && deformer->isBuilt())
                        y += deformer->getDeformation(pos.x, pos.z);
                    pos.y = y + Z_BIAS + CAMBER + stripLift;

                    m_vertices.push_back({pos, surfNorm, glm::vec2(uc, v), rtype});
                }
            }

            // Build quads for i = 0 .. n - 1 (last connects ring n-1 to closing ring n)
            for (int i = 0; i < n; ++i) {
                uint32_t r0 = baseIdx + (uint32_t)i * kCrossCount;
                uint32_t r1 = baseIdx + (uint32_t)(i + 1) * kCrossCount;
                for (int j = 0; j < kCrossCount - 1; ++j) {
                    uint32_t a0 = r0 + j, a1 = r0 + j + 1;
                    uint32_t b0 = r1 + j, b1 = r1 + j + 1;
                    m_indices.push_back(a0); m_indices.push_back(b0); m_indices.push_back(a1);
                    m_indices.push_back(a1); m_indices.push_back(b0); m_indices.push_back(b1);
                }
            }
            continue; // Closed loop is complete and self-contained!
        }

        // Open road: emit rings 0 .. n - 1
        lastRingBase[s] = baseIdx + (uint32_t)(n - 1) * kCrossCount;
        for (int i = 0; i < n; ++i) {
            const glm::vec3& center = seg.splinePoints[i];
            const glm::vec3& right  = seg.rightVecs[i];
            glm::vec3 across = right * (2.0f * hw);
            float v = (seg.cumulativeDist[i] - seg.cumulativeDist[0] + vOff) * TEX_SCALE;

            glm::vec3 tangent = seg.tangents[i];
            glm::vec3 surfNorm = glm::normalize(glm::cross(tangent, across));
            if (surfNorm.y < 0.0f) surfNorm = -surfNorm;

            for (int j = 0; j < kCrossCount; ++j) {
                float uc = kCross[j];
                float off = uc * 2.0f - 1.0f;

                glm::vec3 pos = center + right * (off * hw);
                float y = sampler.isValid() ? sampler.sample(pos.x, pos.z) : center.y;
                if (deformer && deformer->isBuilt())
                    y += deformer->getDeformation(pos.x, pos.z);
                pos.y = y + Z_BIAS + CAMBER + stripLift;

                m_vertices.push_back({pos, surfNorm, glm::vec2(uc, v), rtype});
            }
        }

        // Quads for open road: i = 0 .. n - 2
        for (int i = 0; i < n - 1; ++i) {
            uint32_t r0 = baseIdx + (uint32_t)i * kCrossCount;
            uint32_t r1 = baseIdx + (uint32_t)(i + 1) * kCrossCount;
            for (int j = 0; j < kCrossCount - 1; ++j) {
                uint32_t a0 = r0 + j, a1 = r0 + j + 1;
                uint32_t b0 = r1 + j, b1 = r1 + j + 1;
                m_indices.push_back(a0); m_indices.push_back(b0); m_indices.push_back(a1);
                m_indices.push_back(a1); m_indices.push_back(b0); m_indices.push_back(b1);
            }
        }

        // ─── 4. Rounded end caps for true dead ends ──────────────────────────
        for (int end = 0; end < 2; ++end) {
            int refId = (end == 0) ? segEndRef[s].first : segEndRef[s].second;
            if (refId < 0) continue;
            int c = cid[refId];
            if (c >= 0 && countRealSegsInCluster(cluster[c]) > 1) continue; // connects to another road!
            if (branchLift[s]) continue; // branch mouth meets host road!

            const int i0 = (end == 0) ? 0 : n - 1;
            const glm::vec3& P = seg.splinePoints[i0];
            glm::vec3 tang = seg.tangents[i0];
            glm::vec3 right = seg.rightVecs[i0];
            glm::vec3 outward = (end == 0) ? -tang : tang;
            glm::vec3 surfNorm = glm::normalize(glm::cross(tang, right * (2.0f * hw)));
            if (surfNorm.y < 0.0f) surfNorm = -surfNorm;

            const int M = 12;
            const float vEnd = (seg.cumulativeDist[i0] - seg.cumulativeDist[0] + vOff) * TEX_SCALE;

            glm::vec3 capO = P; capO.y += Z_BIAS + CAMBER + stripLift;
            const uint32_t capBase = (uint32_t)m_vertices.size();
            m_vertices.push_back({capO, surfNorm, glm::vec2(0.5f, vEnd), rtype});

            for (int k = 0; k <= M; ++k) {
                float ang = float(k) / float(M) * PI;
                glm::vec3 d = -right * hw * std::cos(ang) + outward * hw * std::sin(ang);
                glm::vec3 pos = P + d;
                float y = (sampler.isValid()) ? sampler.sample(pos.x, pos.z) : pos.y;
                if (deformer && deformer->isBuilt()) y += deformer->getDeformation(pos.x, pos.z);
                pos.y = y + Z_BIAS + CAMBER + stripLift;
                float uu = float(k) / float(M);
                float vv = vEnd + (hw * std::sin(ang)) * TEX_SCALE;
                m_vertices.push_back({pos, surfNorm, glm::vec2(uu, vv), rtype});
            }

            for (int k = 0; k < M; ++k) {
                m_indices.push_back(capBase);
                m_indices.push_back(capBase + 1 + k);
                m_indices.push_back(capBase + 1 + k + 1);
            }
            ++capCount;
        }
    }

    // ─── 4b. End-to-End Joints (connecting two roads cleanly) ───────────────
    // Connects matching cross-section pillars between two adjacent segments.
    // Preserves lane lines, center double-yellows, and curbs without any grey blotches.
    for (int c = 0; c < nc; ++c) {
        if (countRealSegsInCluster(cluster[c]) != 2) continue;
        int idA = cluster[c][0];
        int idB = cluster[c][1];
        int segA = ends[idA].seg;
        int segB = ends[idB].seg;
        if (segA == segB || segments[segA].closed || segments[segB].closed) continue;

        bool isBackA = ends[idA].isBack;
        bool isBackB = ends[idB].isBack;
        uint32_t ringBaseA = isBackA ? lastRingBase[segA] : firstRingBase[segA];
        uint32_t ringBaseB = isBackB ? lastRingBase[segB] : firstRingBase[segB];

        // Harmonize pillar alignment:
        // When one segment enters and the other leaves (isBackA != isBackB),
        // left meets left (j connects to j).
        // When both enter or both leave, directions are opposite, so left meets right
        // relative to the segment's local frame: j connects to (kCrossCount - 1 - j).
        bool flipB = (isBackA == isBackB);

        for (int j = 0; j < kCrossCount - 1; ++j) {
            uint32_t a0 = ringBaseA + j;
            uint32_t a1 = ringBaseA + j + 1;
            uint32_t b0, b1;
            if (!flipB) {
                b0 = ringBaseB + j;
                b1 = ringBaseB + j + 1;
            } else {
                b0 = ringBaseB + (kCrossCount - 1 - j);
                b1 = ringBaseB + (kCrossCount - 2 - j);
            }
            m_indices.push_back(a0); m_indices.push_back(b0); m_indices.push_back(a1);
            m_indices.push_back(a1); m_indices.push_back(b0); m_indices.push_back(b1);
            m_indices.push_back(a0); m_indices.push_back(a1); m_indices.push_back(b0);
            m_indices.push_back(a1); m_indices.push_back(b1); m_indices.push_back(b0);
        }
        ++jointCount;
    }

    // ─── 5. Multi-way Branching Junctions (3+ roads meeting) ─────────────────
    for (int c = 0; c < nc; ++c) {
        if (countRealSegsInCluster(cluster[c]) < 3) continue;

        glm::vec2 C(0.0f);
        float maxHw = 0.0f;
        int widestType = -1;
        for (int id : cluster[c]) {
            const EndRef& e = ends[id];
            C += e.pos;
            maxHw = std::max(maxHw, e.hw);
            if (e.hw >= maxHw || widestType < 0) widestType = (int)segments[e.seg].type;
        }
        C /= (float)cluster[c].size();

        int jnType = isPaved(widestType) ? widestType : (int)segments[ends[cluster[c][0]].seg].type;
        for (int id : cluster[c]) if (isPaved((int)segments[ends[id].seg].type)) { jnType = (int)segments[ends[id].seg].type; break; }
        if (jnType < 0) jnType = 1;

        struct JnM { glm::vec2 rimL, rimR, tan; float hw; };
        std::vector<JnM> mem;
        mem.reserve(cluster[c].size());
        for (int id : cluster[c]) {
            const EndRef& e = ends[id];
            const RoadSegment& s = segments[e.seg];
            int n = (int)s.splinePoints.size();
            if (n < 2) continue;
            const int i0 = e.isBack ? n - 1 : 0;
            glm::vec2 p(s.splinePoints[i0].x, s.splinePoints[i0].z);
            glm::vec3 r3 = s.rightVecs[i0];
            glm::vec2 r2(r3.x, r3.z);
            float lenR = glm::length(r2);
            if (lenR > 0.01f) r2 /= lenR; else r2 = glm::vec2(1, 0);
            glm::vec3 t3 = s.tangents[i0];
            glm::vec2 tanOut = e.isBack ? -glm::vec2(t3.x, t3.z) : glm::vec2(t3.x, t3.z);
            if (glm::length(tanOut) > 0.01f) tanOut = glm::normalize(tanOut); else tanOut = glm::vec2(1, 0);

            JnM m = { p - r2 * e.hw, p + r2 * e.hw, tanOut, e.hw };
            mem.push_back(m);
        }
        const int M = (int)mem.size();
        if (M < 3) continue;

        struct Pt { int m; float ang; glm::vec2 p; };
        std::vector<Pt> ring;
        ring.reserve(M * 2);
        for (int i = 0; i < M; ++i) {
            ring.push_back({ i, 0.0f, mem[i].rimR });
            ring.push_back({ i, 0.0f, mem[i].rimL });
        }
        for (auto& pt : ring) pt.ang = std::atan2(pt.p.y - C.y, pt.p.x - C.x);
        std::sort(ring.begin(), ring.end(),
                  [](const Pt& a, const Pt& b) { return a.ang < b.ang; });

        const glm::vec3 up(0, 1, 0);

        auto emitPoly = [&](glm::vec2 origin, const std::vector<glm::vec2>& bnd, float lift = 0.0f) {
            if (bnd.size() < 2) return;
            uint32_t wBase = (uint32_t)m_vertices.size();
            auto add = [&](const glm::vec2& p2) {
                float y = sampler.isValid() ? sampler.sample(p2.x, p2.y) : 0.0f;
                if (deformer && deformer->isBuilt()) y += deformer->getDeformation(p2.x, p2.y);
                // u = 0.35f gives clean asphalt without center or edge marking lines
                m_vertices.push_back({ glm::vec3(p2.x, y + Z_BIAS + CAMBER + lift, p2.y), up,
                                       glm::vec2(0.35f, glm::length(p2) * TEX_SCALE), (float)jnType });
            };
            add(origin);
            for (const auto& p2 : bnd) add(p2);
            uint32_t wN = (uint32_t)bnd.size() + 1;
            for (uint32_t k = 1; k + 1 < wN; ++k) {
                m_indices.push_back(wBase);
                m_indices.push_back(wBase + k);
                m_indices.push_back(wBase + k + 1);
            }
            ++jnCount;
        };

        const int RINGN = (int)ring.size();
        for (int i = 0; i < RINGN; ++i) {
            const Pt& A = ring[i];
            const Pt& B = ring[(i + 1) % RINGN];
            if (A.m == B.m) continue;

            const glm::vec2 va = A.p, vb = B.p;
            if (glm::length(va - vb) < 0.5f) continue;

            std::vector<glm::vec2> bnd;
            bnd.push_back(va);
            bnd.push_back(vb);
            emitPoly(C, bnd, 0.015f);
        }
    }

    m_indexCount = (int)m_indices.size();
    std::cout << "[RoadMeshBuilder] Generated " << m_vertices.size() << " vertices, "
              << (m_indexCount / 3) << " triangles (" << capCount << " caps, "
              << jointCount << " joints, " << jnCount << " junction wedges)" << std::endl;
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
