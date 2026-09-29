#!/usr/bin/env python3
"""Patch RoadSystem.cpp: save/load 'closed' field in JSON."""
import sys

FILEPATH = r'c:\Users\honco\OneDrive\Bureau\fallaga\src\RoadSystem.cpp'

with open(FILEPATH, 'rb') as f:
    raw = f.read()

crlf = b'\r\n' in raw
le = b'\r\n' if crlf else b'\n'

def L(text):
    return text.replace(b'\r\n', b'\n').replace(b'\n', le)

# ── PATCH 1: saveToJSON — emit "closed": true only when set ──────────────
old1 = L(b"""        file << "      \\"shoulder\\": " << seg.shoulderWidth << ",\\n";
        file << "      \\"points\\": [\\n";""")

new1 = L(b"""        file << "      \\"shoulder\\": " << seg.shoulderWidth << ",\\n";
        if (seg.closed) file << "      \\"closed\\": true,\\n";
        file << "      \\"points\\": [\\n";""")

if old1 in raw:
    print("SAVE PATCH: found, applying...")
    raw = raw.replace(old1, new1, 1)
elif new1 in raw:
    print("SAVE PATCH: already applied.")
else:
    print("SAVE PATCH: NOT FOUND")
    idx = raw.find(b'shoulderWidth')
    if idx >= 0:
        print("  Context:", repr(raw[max(0,idx-20):idx+120]))
    sys.exit(1)

# ── PATCH 2: loadFromJSON — parse "closed" key ───────────────────────────
old2 = L(b"""            } else if (key == "points") {""")

new2 = L(b"""            } else if (key == "closed") {
                // parse bool: accept true/1/yes
                skipWS();
                if (pos < content.size()) {
                    if (content[pos] == 't' || content[pos] == '1') seg.closed = true;
                    // skip to next comma/whitespace
                    while (pos < content.size() && content[pos] != ',' && content[pos] != '}') pos++;
                }
            } else if (key == "points") {""")

if old2 in raw:
    print("LOAD PATCH: found, applying...")
    raw = raw.replace(old2, new2, 1)
elif new2 in raw:
    print("LOAD PATCH: already applied.")
else:
    print("LOAD PATCH: NOT FOUND")
    sys.exit(1)

with open(FILEPATH, 'wb') as f:
    f.write(raw)

print("RoadSystem.cpp patched successfully.")
