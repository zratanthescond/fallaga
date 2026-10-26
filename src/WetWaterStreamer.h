// WetWaterStreamer.h - builds and draws wadi water ONLY near the camera, with a hard CPU budget per frame.
// Building the whole network at once is ~1.06M terrain-height lookups, 0.83M vertices (27 MB) and ~1.5M triangles drawn every frame:
// that is the lag. The streamer keeps a few dozen features resident, spends at most `budgetLookups` height lookups per frame,
// and hands you a short draw list (distance + frustum culled).
#pragma once
#include "WetWaterMesh.h"
#include <functional>
#include <algorithm>

namespace wetwater {

struct Feature {
    bool isPool = false; int index = 0;
    float minX = 0, minZ = 0, maxX = 0, maxZ = 0;
    int builtLod = -1;                       // -1 = not resident, 0 = near detail, 1 = far detail
    Mesh mesh;                               // CPU copy (free it in onUpload if you don't need it)
    unsigned gpu[3] = {0, 0, 0};             // your VAO / VBO / EBO ids
    bool skipped = false;                    // true = the terrain has no basin/channel here (carve stamps not applied): nothing drawn
    int tris = 0;                            // set at build time, valid even if you free `mesh` after uploading
    int triangles() const { return tris; }
};

struct StreamParams {
    float buildRadius = 1800.0f;             // features closer than this are resident
    float evictRadius = 2600.0f;             // ... and are freed beyond this (hysteresis, no thrashing)
    float drawRadius = 1500.0f;              // drawn only inside this
    float nearLod = 500.0f, lodHysteresis = 120.0f;
    int   budgetLookups = 12000;             // terrain-height lookups per frame (a trickle row costs 10, a pool 100-2600)
};

struct FrameStats { int skipped = 0, lookups = 0, built = 0, evicted = 0, resident = 0, residentTris = 0, drawn = 0, drawnTris = 0; };

template <class TM, class Net>
class Streamer {
public:
    std::function<void(Feature&)> onUpload;   // create VAO/VBO/EBO from f.mesh (location 0..4, see wet_water.vert)
    std::function<void(Feature&)> onRelease;  // delete them
    StreamParams sp;
    std::vector<Feature> features;

    void init(const Net& net) {
        features.clear();
        for (size_t i = 0; i < net.pools().size(); ++i) {
            const auto& p = net.pools()[i]; if (p.radiusM <= 0.0f) continue;
            Feature f; f.isPool = true; f.index = (int)i; const float r = p.radiusM + 4.0f;
            f.minX = p.center.x - r; f.maxX = p.center.x + r; f.minZ = p.center.y - r; f.maxZ = p.center.y + r; features.push_back(std::move(f));
        }
        for (size_t i = 0; i < net.trickles().size(); ++i) {
            const auto& t = net.trickles()[i]; if (t.points.empty() || t.wetLengthM <= 0.0f) continue;
            Feature f; f.isPool = false; f.index = (int)i; f.minX = f.minZ = 1e30f; f.maxX = f.maxZ = -1e30f;
            auto grow = [&](glm::vec2 p) { f.minX = std::min(f.minX, p.x); f.maxX = std::max(f.maxX, p.x); f.minZ = std::min(f.minZ, p.y); f.maxZ = std::max(f.maxZ, p.y); };
            grow(t.spring); for (const auto& p : t.points) grow(p);
            const float pad = t.widthM + 6.0f; f.minX -= pad; f.minZ -= pad; f.maxX += pad; f.maxZ += pad; features.push_back(std::move(f));
        }
    }

    // call once per frame, BEFORE drawing
    FrameStats update(TM* tm, const Net& net, float camX, float camZ) {
        FrameStats st;
        struct Job { int f, lod; float d; };
        std::vector<Job> jobs;
        for (int i = 0; i < (int)features.size(); ++i) {
            Feature& f = features[i]; const float d = dist(f, camX, camZ);
            if (f.builtLod >= 0 && d > sp.evictRadius) { if (onRelease) onRelease(f); f.mesh = Mesh(); f.tris = 0; f.builtLod = -1; ++st.evicted; continue; }
            if (d > sp.buildRadius) continue;
            int want = d < sp.nearLod ? 0 : 1;
            if (f.builtLod >= 0 && f.builtLod != want) {                      // hysteresis: only switch when clearly past the border
                if (want == 0 && d > sp.nearLod - sp.lodHysteresis) want = f.builtLod;
                if (want == 1 && d < sp.nearLod + sp.lodHysteresis) want = f.builtLod;
            }
            if (f.builtLod != want) jobs.push_back({i, want, d});
        }
        std::sort(jobs.begin(), jobs.end(), [](const Job& a, const Job& b) { return a.d < b.d; });
        for (const Job& j : jobs) {
            Feature& f = features[j.f];
            const Params prm = j.lod == 0 ? nearParams() : farParams();
            const int cost = f.isPool ? poolCost(net.pools()[f.index], prm) : trickleCost(net.trickles()[f.index], prm);
            if (st.lookups > 0 && st.lookups + cost > sp.budgetLookups) break;     // budget spent; always allow at least one job
            if (onRelease && f.builtLod >= 0) onRelease(f);
            f.mesh = Mesh();
            const bool ok = f.isPool ? buildPool(tm, net.pools()[f.index], prm, f.mesh) : buildTrickle(tm, net.trickles()[f.index], prm, f.mesh);
            if (!ok) f.mesh = Mesh();
            f.builtLod = j.lod; f.tris = (int)(f.mesh.idx.size() / 3); f.skipped = !ok; if (ok && onUpload) onUpload(f);
            st.lookups += cost; ++st.built;
        }
        for (const auto& f : features) if (f.builtLod >= 0) { ++st.resident; st.residentTris += f.triangles(); if (f.skipped) ++st.skipped; }
        return st;
    }

    // fills `out` with the features to draw this frame: inside drawRadius and accepted by your frustum test
    template <class FrustumTest>
    void collectDraw(float camX, float camZ, FrustumTest visible, std::vector<const Feature*>& out, FrameStats* st = nullptr) const {
        out.clear();
        for (const auto& f : features) {
            if (f.builtLod < 0 || f.tris == 0) continue;
            if (dist(f, camX, camZ) > sp.drawRadius) continue;
            if (!visible(f.minX, f.minZ, f.maxX, f.maxZ)) continue;
            out.push_back(&f); if (st) { ++st->drawn; st->drawnTris += f.triangles(); }
        }
    }

private:
    static float dist(const Feature& f, float x, float z) {
        const float dx = std::max(std::max(f.minX - x, 0.0f), x - f.maxX), dz = std::max(std::max(f.minZ - z, 0.0f), z - f.maxZ);
        return std::sqrt(dx * dx + dz * dz);
    }
};

}  // namespace wetwater
