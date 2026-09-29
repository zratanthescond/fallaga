#!/usr/bin/env python3
"""Patch Terrain.cpp: only merge segments of same type and similar width."""
import sys

FILEPATH = r'c:\Users\honco\OneDrive\Bureau\fallaga\src\Terrain.cpp'

with open(FILEPATH, 'rb') as f:
    raw = f.read()

crlf = b'\r\n' in raw
le = b'\r\n' if crlf else b'\n'

def L(text):
    return text.replace(b'\r\n', b'\n').replace(b'\n', le)

# ── PATCH: add type+width guard before the 4 merge-case ifs ───────────────
old = L(b"""        bool attached = false;
        // A.last -> B.first : extend A with B
        if (distPt(A.controlPoints.back(), B.controlPoints.front()) <= tolerance) {""")

new = L(b"""        // Only merge segments that share the same road type and similar width.
        // Merging roads of different hierarchy (e.g. ColonialAvenue into Medina)
        // would destroy their independent rendering, so skip them.
        if (A.type != B.type || std::fabs(A.width - B.width) >= 0.2f) continue;

        bool attached = false;
        // A.last -> B.first : extend A with B
        if (distPt(A.controlPoints.back(), B.controlPoints.front()) <= tolerance) {""")

if old in raw:
    print("Terrain PATCH: found, applying...")
    raw = raw.replace(old, new, 1)
elif new in raw:
    print("Terrain PATCH: already applied.")
else:
    print("Terrain PATCH: NOT FOUND")
    idx = raw.find(b'bool attached = false;')
    if idx >= 0:
        print("  Context:", repr(raw[max(0,idx-50):idx+150]))
    sys.exit(1)

with open(FILEPATH, 'wb') as f:
    f.write(raw)

print("Terrain.cpp patched successfully.")
