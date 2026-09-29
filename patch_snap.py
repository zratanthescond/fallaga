#!/usr/bin/env python3
"""Patch Game.cpp: add draft-loop snapping to snapPt."""
import sys

FILEPATH = r'c:\Users\honco\OneDrive\Bureau\fallaga\src\Game.cpp'

with open(FILEPATH, 'rb') as f:
    raw = f.read()

crlf = b'\r\n' in raw
le = b'\r\n' if crlf else b'\n'

def L(text):
    return text.replace(b'\r\n', b'\n').replace(b'\n', le)

# ── PATCH 1: snapPt - add draft-loop snapping ──────────────────────────────
old1 = L(b"""                                // Snap onto the nearest existing road ENDPOINT (first/last control point)
                                auto snapPt = [&](const glm::vec3& in) -> glm::vec3 {
                                    if (!roadSnapToRoads) return in;
                                    glm::vec3 out = in;
                                    float bestD = roadSnapTolerance;
                                    for (const auto& seg : rg->segments) {
                                        const auto& cps = seg.controlPoints;
                                        if (cps.empty()) continue;
                                        auto probe = [&](const RoadPoint& cp) {
                                            float dx = out.x - cp.pos.x, dz = out.z - cp.pos.y;
                                            float d = std::sqrt(dx * dx + dz * dz);
                                            if (d < bestD) { bestD = d; out = glm::vec3(cp.pos.x, in.y, cp.pos.y); }
                                        };
                                        probe(cps.front());
                                        if (cps.size() > 1) probe(cps.back());
                                    }
                                    return out;
                                };""")

new1 = L(b"""                                // Snap onto the nearest existing road ENDPOINT (first/last control point)
                                // Also snap to the draft's own FIRST point so the user can close a loop.
                                auto snapPt = [&](const glm::vec3& in) -> glm::vec3 {
                                    if (!roadSnapToRoads) return in;
                                    glm::vec3 out = in;
                                    float bestD = roadSnapTolerance;
                                    // Snap to existing committed road endpoints
                                    for (const auto& seg : rg->segments) {
                                        const auto& cps = seg.controlPoints;
                                        if (cps.empty()) continue;
                                        auto probe = [&](const RoadPoint& cp) {
                                            float dx = out.x - cp.pos.x, dz = out.z - cp.pos.y;
                                            float d = std::sqrt(dx * dx + dz * dz);
                                            if (d < bestD) { bestD = d; out = glm::vec3(cp.pos.x, in.y, cp.pos.y); }
                                        };
                                        probe(cps.front());
                                        if (cps.size() > 1) probe(cps.back());
                                    }
                                    // Snap to the draft's own first point (closes a loop / round road)
                                    if (roadDraftPoints.size() >= 3) {
                                        const glm::vec3& front = roadDraftPoints.front();
                                        float dx = out.x - front.x, dz = out.z - front.z;
                                        float d = std::sqrt(dx * dx + dz * dz);
                                        if (d < bestD) { bestD = d; out = glm::vec3(front.x, in.y, front.z); }
                                    }
                                    return out;
                                };""")

if old1 in raw:
    print("PATCH1: found, applying...")
    raw = raw.replace(old1, new1, 1)
elif new1 in raw:
    print("PATCH1: already applied.")
else:
    print("PATCH1: NOT FOUND - checking nearby text...")
    idx = raw.find(b'roadSnapTolerance')
    if idx >= 0:
        print("  Context around roadSnapTolerance:", repr(raw[max(0,idx-100):idx+200]))
    sys.exit(1)

with open(FILEPATH, 'wb') as f:
    f.write(raw)

print("PATCH1 applied to Game.cpp")
