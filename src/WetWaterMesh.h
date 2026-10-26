// WetWaterMesh.h - real geometry for wadi pools and spring-fed trickles.
// Replaces drawing wet water through the 512x512 ocean grid (54.6 m between vertices, far coarser than a 1-4 m trickle).
// Every vertex stores the terrain height under it (CPU-sampled from the SAME getHeightAt the game uses), so the fragment
// shader computes the true water depth: no floating sheets, soft shores, depth-based volume.
//
// Templated on the terrain / network types so it compiles against your TerrainManager and WetWaterNetwork unchanged:
//   TM  needs: float getHeightAt(float x, float z)
//   Net needs: pools()    -> range of { glm::vec2 center; float radiusM, levelM, bedMinM, maxDepthM; }
//              trickles() -> range of { glm::vec2 spring; std::vector<glm::vec2> points; float wetLengthM, flowM3s, widthM, depthM; }
#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <limits>
#include <queue>
#include <functional>

namespace wetwater {

struct Vertex {                 // 8 floats = 32 bytes
    float x, y, z;              // world position, y = water surface height
    float bedY;                 // terrain height under this vertex
    float flowX, flowZ;         // flow direction * speed in m/s (0 = still water)
    float kind;                 // 2 = pool, 3 = trickle
    float life;                 // 1 = full, -> 0 where a trickle dies out
};
struct Mesh { std::vector<Vertex> verts; std::vector<uint32_t> idx; };

// Cut these into the visual height grid BEFORE building the water, so the water sits in a real basin / channel.
// (The channel bottom is a smooth profile, so micro-relief of the DEM can never break the water into dry gaps.)
// targetY = the height the terrain should have at the centre of the stamp (apply as min(current, targetY) with a smooth bank falloff).
struct CarveStamp { float x, z, radius, targetY; bool pool; };

struct Params {
    float poolCellMin = 0.75f, poolCellMax = 2.0f;   // metres between pool mesh vertices
    float poolMargin = 3.0f;                         // extra ring so shores can fade out
    float trickleStep = 2.5f;                        // metres between cross sections
    int   crossSections = 9;                         // vertices across a trickle
    float edgeMargin = 1.5f;                         // extra width each side beyond the wet width
    float minDropPerStep = 0.004f;                   // water surface always falls downstream
    float minSpeed = 0.04f, maxSpeed = 2.0f;
};

namespace detail {
inline float clamp01(float v) { return std::min(1.0f, std::max(0.0f, v)); }
inline std::vector<glm::vec2> dedupe(const glm::vec2& spring, const std::vector<glm::vec2>& pts) {
    std::vector<glm::vec2> line; line.push_back(spring);
    for (const auto& p : pts) if (glm::length(p - line.back()) > 0.05f) line.push_back(p);
    return line;
}
struct PathSample { glm::vec2 p, t; float dist; };
inline std::vector<PathSample> resample(const std::vector<glm::vec2>& line, float maxLen, float step) {
    std::vector<float> cum(line.size(), 0.0f);
    for (size_t i = 1; i < line.size(); ++i) cum[i] = cum[i - 1] + glm::length(line[i] - line[i - 1]);
    const float total = std::min(maxLen, cum.back());
    std::vector<PathSample> out;
    if (total <= 0.01f) return out;
    const int n = std::max(2, (int)std::ceil(total / step) + 1);
    size_t seg = 1;
    for (int i = 0; i < n; ++i) {
        const float d = total * (float)i / (float)(n - 1);
        while (seg + 1 < line.size() && cum[seg] < d) ++seg;
        const float segLen = std::max(cum[seg] - cum[seg - 1], 1e-4f);
        const float u = clamp01((d - cum[seg - 1]) / segLen);
        PathSample s; s.p = line[seg - 1] + (line[seg] - line[seg - 1]) * u; s.dist = d;
        s.t = glm::normalize(line[seg] - line[seg - 1]);
        out.push_back(s);
    }
    for (size_t i = 1; i + 1 < out.size(); ++i) {            // smooth tangents so ribbons don't kink
        glm::vec2 t = out[i - 1].t + out[i].t * 2.0f + out[i + 1].t;
        if (glm::length(t) > 1e-5f) out[i].t = glm::normalize(t);
    }
    return out;
}
template <class TM>
inline std::vector<float> smoothedBed(TM* tm, const std::vector<PathSample>& s, int halfWindow = 4) {
    std::vector<float> raw(s.size()), out(s.size());
    for (size_t i = 0; i < s.size(); ++i) raw[i] = tm->getHeightAt(s[i].p.x, s[i].p.y);
    for (size_t i = 0; i < s.size(); ++i) {
        float sum = 0; int n = 0;
        for (int k = -halfWindow; k <= halfWindow; ++k) { long j = (long)i + k; if (j >= 0 && j < (long)s.size()) { sum += raw[j]; ++n; } }
        out[i] = sum / (float)n;
    }
    return out;
}
}  // namespace detail

// LOD presets: near = full quality, far = ~6x cheaper (used beyond ~500 m from the camera).
inline Params nearParams() { return Params(); }
inline Params farParams() { Params p; p.poolCellMin = 2.0f; p.poolCellMax = 4.0f; p.trickleStep = 5.0f; p.crossSections = 5; return p; }

// ---- cost in terrain-height lookups (what you pay on the CPU when building a feature)
template <class P> inline int poolGrid(const P& pool, const Params& prm) {
    const float R = pool.radiusM, reach = R + prm.poolMargin;
    const float cell = std::min(prm.poolCellMax, std::max(prm.poolCellMin, R / 14.0f));
    return std::max(4, (int)std::ceil(2.0f * reach / cell) + 1);
}
template <class P> inline int poolCost(const P& pool, const Params& prm) { const int n = poolGrid(pool, prm); return n * n; }
template <class T> inline int trickleRows(const T& tr, const Params& prm) { return std::max(2, (int)std::ceil(tr.wetLengthM / prm.trickleStep) + 1); }
template <class T> inline int trickleCost(const T& tr, const Params& prm) { const int r = trickleRows(tr, prm); return r + r * std::max(3, prm.crossSections); }

// 3x3 binomial blur (twice): removes the stair-steps that coarse / nearest-sampled terrain gives, so shorelines come out smooth.
inline void smoothGrid(std::vector<float>& g, int n) {
    std::vector<float> t(g.size());
    for (int pass = 0; pass < 2; ++pass) {
        for (int j = 0; j < n; ++j) for (int i = 0; i < n; ++i) {
            float s = 0, w = 0;
            for (int dj = -1; dj <= 1; ++dj) for (int di = -1; di <= 1; ++di) {
                const int ii = i + di, jj = j + dj; if (ii < 0 || jj < 0 || ii >= n || jj >= n) continue;
                const float k = (di == 0 ? 2.f : 1.f) * (dj == 0 ? 2.f : 1.f); s += g[(size_t)jj * n + ii] * k; w += k;
            }
            t[(size_t)j * n + i] = s / w;
        }
        g.swap(t);
    }
}

// Returns false (and adds nothing) when the terrain has no real basin here - e.g. the carve stamps were not applied.
// A pool is only drawn if the water is ENCLOSED: it fills from the lowest point up to its spill level, never beyond.
template <class TM, class P>
bool buildPool(TM* tm, const P& pool, const Params& prm, Mesh& m) {
    using namespace detail;
    if (pool.radiusM <= 0.0f) return false;
    const float R = pool.radiusM, reach = R + prm.poolMargin;
    const int n = poolGrid(pool, prm);
    const float x0 = pool.center.x - reach, z0 = pool.center.y - reach, d = 2.0f * reach / (float)(n - 1);
    std::vector<float> bed((size_t)n * n);
    for (int j = 0; j < n; ++j) for (int i = 0; i < n; ++i) bed[(size_t)j * n + i] = tm->getHeightAt(x0 + i * d, z0 + j * d);
    smoothGrid(bed, n);

    int minI = -1, minJ = -1; float localBed = std::numeric_limits<float>::infinity();
    for (int j = 0; j < n; ++j) for (int i = 0; i < n; ++i)
        if (std::hypot(x0 + i * d - pool.center.x, z0 + j * d - pool.center.y) <= R && bed[(size_t)j * n + i] < localBed) { localBed = bed[(size_t)j * n + i]; minI = i; minJ = j; }
    if (minI < 0) return false;

    // spill level = lowest rim over which water could escape (minimax path from the lowest cell to the edge of the grid)
    std::vector<float> cost((size_t)n * n, std::numeric_limits<float>::infinity());
    using QE = std::pair<float, int>; std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
    cost[(size_t)minJ * n + minI] = bed[(size_t)minJ * n + minI]; pq.push({cost[(size_t)minJ * n + minI], minJ * n + minI});
    float spill = std::numeric_limits<float>::infinity();
    while (!pq.empty()) {
        auto [c, id] = pq.top(); pq.pop(); if (c > cost[id]) continue;
        const int i = id % n, j = id / n;
        if (i == 0 || j == 0 || i == n - 1 || j == n - 1) { spill = c; break; }
        static const int di[4] = {1, -1, 0, 0}, dj[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; ++k) { const int ni = i + di[k], nj = j + dj[k]; const size_t nid = (size_t)nj * n + ni;
            const float nc = std::max(c, bed[nid]); if (nc < cost[nid]) { cost[nid] = nc; pq.push({nc, nj * n + ni}); } }
    }
    const float maxDepth = std::min(pool.maxDepthM, 1.2f);                        // rain pools are puddles, not lakes
    const float level = std::min(spill - 0.03f, localBed + maxDepth);
    if (level - localBed < 0.08f) return false;                                   // no basin: draw nothing rather than a sheet on a slope

    std::vector<char> region((size_t)n * n, 0); std::vector<int> stack{minJ * n + minI}; region[(size_t)minJ * n + minI] = 1;
    while (!stack.empty()) {
        const int id = stack.back(); stack.pop_back(); const int i = id % n, j = id / n;
        static const int di[4] = {1, -1, 0, 0}, dj[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; ++k) { const int ni = i + di[k], nj = j + dj[k]; if (ni < 0 || nj < 0 || ni >= n || nj >= n) continue;
            const size_t nid = (size_t)nj * n + ni; if (!region[nid] && bed[nid] < level) { region[nid] = 1; stack.push_back(nj * n + ni); } }
    }
    const uint32_t base = (uint32_t)m.verts.size();
    for (int j = 0; j < n; ++j) for (int i = 0; i < n; ++i) {
        const size_t id = (size_t)j * n + i; Vertex v{};
        v.x = x0 + i * d; v.y = level; v.z = z0 + j * d; v.kind = 2.0f; v.life = 1.0f;
        v.bedY = (region[id] || bed[id] >= level) ? bed[id] : level + 0.02f;      // other low pockets stay dry; no artificial cliffs at the shore
        m.verts.push_back(v);
    }
    bool any = false;
    for (int j = 0; j + 1 < n; ++j) for (int i = 0; i + 1 < n; ++i) {
        const uint32_t a = base + j * n + i, b = a + 1, c = a + n, e = c + 1;
        if (!(region[(size_t)j * n + i] || region[(size_t)j * n + i + 1] || region[(size_t)(j + 1) * n + i] || region[(size_t)(j + 1) * n + i + 1])) continue;
        m.idx.insert(m.idx.end(), {a, c, b, b, c, e}); any = true;
    }
    return any;
}

template <class TM, class T>
bool buildTrickle(TM* tm, const T& tr, const Params& prm, Mesh& m) {
    using namespace detail;
    if (tr.points.empty() || tr.wetLengthM <= 0.0f) return false;
    const auto samples = resample(dedupe(tr.spring, tr.points), tr.wetLengthM, prm.trickleStep);
    if (samples.size() < 2) return false;
    const int M = std::max(3, prm.crossSections);
    const float speed0 = std::min(prm.maxSpeed, std::max(prm.minSpeed, tr.flowM3s / std::max(tr.widthM * tr.depthM, 0.05f)));
    const uint32_t base = (uint32_t)m.verts.size();
    const std::vector<float> bedProfile = smoothedBed(tm, samples, std::max(1, (int)std::lround(10.0f / prm.trickleStep)));
    float prevSurface = std::numeric_limits<float>::infinity();
    std::vector<char> rowOk(samples.size(), 0); size_t okRows = 0;
    for (size_t r = 0; r < samples.size(); ++r) {
        const auto& s = samples[r];
        const float fade = clamp01(1.0f - s.dist / tr.wetLengthM);
        const float width = std::max(tr.widthM * (0.55f + 0.45f * fade), 0.6f);
        const float depth = std::min(0.5f, std::max(0.05f, tr.depthM * (0.45f + 0.55f * fade)));
        float surface = bedProfile[r] + depth;
        if (std::isfinite(prevSurface)) surface = std::min(surface, prevSurface - prm.minDropPerStep * (prm.trickleStep / 2.5f));
        prevSurface = surface;
        const glm::vec2 perp(-s.t.y, s.t.x);
        const float half = width * 0.5f + prm.edgeMargin;
        for (int k = 0; k < M; ++k) {
            const float u = (float)k / (float)(M - 1) * 2.0f - 1.0f;
            const glm::vec2 p = s.p + perp * (u * half);
            Vertex v{};
            v.x = p.x; v.y = surface; v.z = p.y; v.bedY = tm->getHeightAt(p.x, p.y);
            const float sp = speed0 * (0.4f + 0.6f * fade);
            v.flowX = s.t.x * sp; v.flowZ = s.t.y * sp; v.kind = 3.0f;
            v.life = (r + 1 == samples.size()) ? 0.0f : std::sqrt(fade);
            m.verts.push_back(v);
        }
        // a channel must really exist here: both banks higher than the bed, and some water over the bed
        const Vertex& cv = m.verts[base + r * M + M / 2]; const float bank = std::min(m.verts[base + r * M].bedY, m.verts[base + r * M + M - 1].bedY);
        rowOk[r] = (bank - cv.bedY >= 0.06f && cv.y - cv.bedY > 0.02f) ? 1 : 0; okRows += rowOk[r];
    }
    if (okRows < samples.size() / 3) { m.verts.resize(base); return false; }          // no channel in the terrain: draw nothing (apply the carve stamps)
    const size_t idxStart = m.idx.size(); (void)idxStart;
    for (size_t r = 0; r + 1 < samples.size(); ++r) for (int k = 0; k + 1 < M; ++k) {
        if (!rowOk[r] && !rowOk[r + 1]) continue;
        const uint32_t a = base + (uint32_t)(r * M + k), b = a + 1, c = a + M, e = c + 1;
        const float dmax = std::max(std::max(m.verts[a].y - m.verts[a].bedY, m.verts[b].y - m.verts[b].bedY),
                                    std::max(m.verts[c].y - m.verts[c].bedY, m.verts[e].y - m.verts[e].bedY));
        if (dmax <= 0.0f) continue;
        m.idx.insert(m.idx.end(), {a, c, b, b, c, e});
    }
    return true;
}

// Whole network at once - ONLY for tools/tests. In the game use WetWaterStreamer.h (building everything at once costs ~1M height lookups and 1.5M triangles).
template <class TM, class Net>
Mesh build(TM* tm, const Net& net, const Params& prm = Params()) {
    Mesh m;
    for (const auto& pool : net.pools()) buildPool(tm, pool, prm, m);
    for (const auto& tr : net.trickles()) buildTrickle(tm, tr, prm, m);
    return m;
}

// Stamps for your height-grid deformation (same mechanism you use for road beds). Apply before build().
template <class TM, class Net>
std::vector<CarveStamp> carveStamps(TM* tm, const Net& net, float step = 2.5f) {
    using namespace detail;
    std::vector<CarveStamp> out;
    for (const auto& pool : net.pools()) {
        if (pool.radiusM <= 0.0f) continue;
        float lo = std::numeric_limits<float>::infinity();
        for (int k = 0; k < 9; ++k) { const float a = k * 0.7853982f; const float rr = (k == 8 ? 0.0f : pool.radiusM * 0.5f);
            lo = std::min(lo, tm->getHeightAt(pool.center.x + rr * std::cos(a), pool.center.y + rr * std::sin(a))); }
        out.push_back({pool.center.x, pool.center.y, pool.radiusM, lo - std::min(pool.maxDepthM, 1.5f) * 0.5f, true});
    }
    for (const auto& tr : net.trickles()) {
        if (tr.points.empty() || tr.wetLengthM <= 0.0f) continue;
        const auto samples = resample(dedupe(tr.spring, tr.points), tr.wetLengthM, step);
        const auto bed = smoothedBed(tm, samples);
        float prevTarget = std::numeric_limits<float>::infinity();
        for (size_t i = 0; i < samples.size(); ++i) {
            const float fade = clamp01(1.0f - samples[i].dist / tr.wetLengthM);
            const float width = std::max(tr.widthM * (0.55f + 0.45f * fade), 0.6f);
            const float depth = std::min(0.5f, std::max(0.05f, tr.depthM * (0.45f + 0.55f * fade)));
            float target = bed[i] - (depth + 0.15f);                  // channel is 0.15 m deeper than the water so banks stand above it
            if (std::isfinite(prevTarget)) target = std::min(target, prevTarget - 0.004f);   // always runs downhill
            prevTarget = target;
            out.push_back({samples[i].p.x, samples[i].p.y, width * 0.5f + 1.5f, target, false});
        }
    }
    return out;
}

}  // namespace wetwater
