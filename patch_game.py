#!/usr/bin/env python3
"""Patch Game.cpp: 2 targeted text replacements."""
import sys, re

FILEPATH = r'c:\Users\honco\OneDrive\Bureau\fallaga\src\Game.cpp'

with open(FILEPATH, 'rb') as f:
    raw = f.read()

# Detect line ending style
crlf = b'\r\n' in raw
le = b'\r\n' if crlf else b'\n'

def L(text):
    """Convert newlines to match the file's line ending style."""
    return text.replace(b'\r\n', b'\n').replace(b'\n', le)

# ── PATCH 1 is already applied (snapPt); skip and check ──────────────────

if b'roadDraftPoints.front()' in raw:
    print("PATCH1 already applied (draft loop snap found).")
else:
    print("PATCH1: NOTE - loop snap not found yet, apply separately if needed.")

# ── PATCH 2: insert closed-loop detection block after shoulderWidth line ──
old2 = L(b"""        ns.type = (RoadType)roadDrawType;
        ns.width = roadDrawWidth;
        ns.shoulderWidth = roadDrawShoulder;
        // Ramer-Douglas-Peucker""")

new2 = L(b"""        ns.type = (RoadType)roadDrawType;
        ns.width = roadDrawWidth;
        ns.shoulderWidth = roadDrawShoulder;

        // -- Closed-loop detection -----------------------------------------
        // When the last draft point coincides with the first (user snapped
        // back to the start), mark this as a closed ring road so the mesh
        // builder emits a seamless loop: no seam kink, no texture squash,
        // no end caps.
        const float loopThresh = std::max(ns.width, 15.0f);
        const glm::vec3& dFront = roadDraftPoints.front();
        const glm::vec3& dBack  = roadDraftPoints.back();
        float dLoop = std::sqrt((dBack.x - dFront.x) * (dBack.x - dFront.x) +
                                (dBack.z - dFront.z) * (dBack.z - dFront.z));
        bool isClosedLoop = (roadDraftPoints.size() >= 4) && (dLoop < loopThresh);
        if (isClosedLoop) {
            // Snap the back point exactly onto the front so the mesh seam is pixel-perfect
            roadDraftPoints.back() = glm::vec3(dFront.x, dBack.y, dFront.z);
            ns.closed = true;
            std::cout << "[Road] Detected closed loop (gap=" << dLoop << "m, thresh=" << loopThresh << "m)\\n";
        }

        // Ramer-Douglas-Peucker""")

if old2 in raw:
    print("PATCH2: found, applying...")
    raw = raw.replace(old2, new2, 1)
else:
    print("PATCH2: NOT FOUND, dumping search bytes...")
    # Find "shoulderWidth = roadDrawShoulder" context for debugging
    idx = raw.find(b'shoulderWidth = roadDrawShoulder')
    if idx >= 0:
        print("  Found shoulderWidth at byte", idx)
        print("  Context:", repr(raw[idx-5:idx+120]))
    sys.exit(1)

# ── PATCH 3: add ", CLOSED LOOP" to commit log line ──────────────────────
old3 = L(b"""        std::cout << "[Road] Committed road id=" << ns.id
                  << " (" << ns.controlPoints.size() << " control points"
                  << (skipped > 0 ? ", auto-simplified " + std::to_string(skipped) + " pts" : "") << ")\\n";""")

new3 = L(b"""        std::cout << "[Road] Committed road id=" << ns.id
                  << " (" << ns.controlPoints.size() << " control points"
                  << (ns.closed ? ", CLOSED LOOP" : "")
                  << (skipped > 0 ? ", auto-simplified " + std::to_string(skipped) + " pts" : "") << ")\\n";""")

if old3 in raw:
    print("PATCH3: found, applying...")
    raw = raw.replace(old3, new3, 1)
else:
    print("PATCH3: NOT FOUND")
    idx = raw.find(b'Committed road id=')
    if idx >= 0:
        print("  Context:", repr(raw[idx:idx+200]))
    sys.exit(1)

with open(FILEPATH, 'wb') as f:
    f.write(raw)

print("All patches applied successfully to Game.cpp")
