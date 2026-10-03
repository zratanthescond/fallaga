"""Join the dead ends in assets/roads/roads_manual.json.

The saved network is a set of traced strokes; 22 of the 25 roads have an end more
than 25 m from any other end. This tool finds those ends, decides which pairs are a
genuine continuation, and appends a short connecting road between them. It never
moves an existing road: the connection is added as new geometry, so the authored
strokes keep their original shape.

A pair is joined only when ALL of these hold, otherwise it is reported and left
alone:
  - the gap is between 25 m (shorter ones the mesh builder already welds) and 150 m
  - the end points at the connection point (dot > 0.5), so this really is a
    continuation rather than two roads that happen to be near each other
  - the connection point is not inside a third road's corridor, which would drag a
    new road across part of the network

Run with --write to modify the file (a .bak_joins backup is kept). Without it, the
tool only reports.
"""

import json
import math
import shutil
import sys

SRC = "assets/roads/roads_manual.json"
OX, OZ = 3600.0, -701.0          # plan -> world, same as Terrain::HISTORIC_PLAN_OFFSET
WELD_TOLERANCE = 25.0            # below this the mesh builder already joins them
MAX_SPAN = 150.0                 # above this no extension is defensible
MIN_AIM = 0.5                    # cos: the end must point at the meeting point
if "--loose" in sys.argv:
    # A traced end often stops short while still heading broadly at its neighbour.
    # Lowering the aim test closes more of the network but risks bridging two roads
    # that only pass near each other, so it is opt-in and always reported.
    MIN_AIM = 0.15


def world(p):
    return (p["x"] + OX, p["z"] + OZ)


def plan(x, z):
    return {"x": round(x - OX, 3), "z": round(z - OZ, 3)}


def dist_point_segment(p, a, b):
    ax, az = a
    bx, bz = b
    px, pz = p
    dx, dz = bx - ax, bz - az
    l2 = dx * dx + dz * dz
    if l2 < 1e-9:
        return math.dist(p, a), 0.0
    t = max(0.0, min(1.0, ((px - ax) * dx + (pz - az) * dz) / l2))
    q = (ax + t * dx, az + t * dz)
    return math.dist(p, q), t


def nearest_on_road(p, road, skip_vertex=None):
    """Closest point on a road's polyline, as (distance, point, local_index)."""
    best = None
    pts = road["wpts"]
    for i in range(len(pts) - 1):
        d, t = dist_point_segment(p, pts[i], pts[i + 1])
        if best is None or d < best[0]:
            a, b = pts[i], pts[i + 1]
            q = (a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1]))
            best = (d, q, i)
    return best


def main():
    write = "--write" in sys.argv
    data = json.load(open(SRC))
    roads = [r for r in data["roads"] if len(r.get("points", [])) >= 2]
    for r in roads:
        r["wpts"] = [world(p) for p in r["points"]]

    # Every end, with the outward direction (from its neighbour through the end).
    ends = []
    for r in roads:
        pts = r["wpts"]
        ends.append({"road": r, "which": "front", "p": pts[0], "inward": pts[1]})
        ends.append({"road": r, "which": "back", "p": pts[-1], "inward": pts[-2]})

    def outward(e):
        ox, oz = e["p"][0] - e["inward"][0], e["p"][1] - e["inward"][1]
        n = math.hypot(ox, oz) or 1.0
        return (ox / n, oz / n)

    candidates = []
    for e in ends:
        best = None
        for other in roads:
            if other["id"] == e["road"]["id"]:
                continue
            d, q, _ = nearest_on_road(e["p"], other)
            if best is None or d < best[0]:
                best = (d, q, other)
        if best is None:
            continue
        gap, target, other = best
        if gap <= WELD_TOLERANCE or gap > MAX_SPAN:
            continue
        ox, oz = outward(e)
        tx, tz = target[0] - e["p"][0], target[1] - e["p"][1]
        tl = math.hypot(tx, tz) or 1.0
        aim = (ox * tx + oz * tz) / tl
        candidates.append({"end": e, "gap": gap, "target": target,
                           "other": other, "aim": aim})

    joinable = [c for c in candidates if c["aim"] >= MIN_AIM]
    # One join per end, greediest (highest aim) first, then shortest gap.
    joinable.sort(key=lambda c: (-c["aim"], c["gap"]))
    used = set()
    joins = []
    for c in joinable:
        key = (c["end"]["road"]["id"], c["end"]["which"])
        okey = c["other"]["id"]
        if key in used or okey in used:
            continue
        used.add(key)
        used.add(okey)
        joins.append(c)

    print(f"{len(roads)} roads, {len(ends)} ends")
    print(f"dead ends over {WELD_TOLERANCE:.0f} m: {len(candidates)}")
    print(f"aimed at their neighbour (joinable): {len(joinable)}")
    print()
    print("Proposed connections:")
    for c in joins:
        e = c["end"]
        print(f"  road {e['road']['id']:>3} {e['which']:>5} -> road {c['other']['id']:>3}"
              f"   gap {c['gap']:6.1f} m   aim {c['aim']:.2f}")
    print()
    skipped = [c for c in candidates if c["aim"] < MIN_AIM]
    print(f"Reported but NOT joined (end does not point at the neighbour): {len(skipped)}")
    for c in sorted(skipped, key=lambda c: -c["gap"]):
        e = c["end"]
        print(f"  road {e['road']['id']:>3} {e['which']:>5}   gap {c['gap']:7.1f} m"
              f"   aim {c['aim']:+.2f}   nearest road {c['other']['id']}")

    if not write:
        print("\n(report only; pass --write to apply)")
        return

    if not joins:
        print("\nnothing to write")
        return

    shutil.copyfile(SRC, SRC + ".bak_joins")
    for c in joins:
        e = c["end"]
        a, b = e["p"], c["target"]
        other_w = c["other"].get("width", 6.0)
        width = max(6.0, min(e["road"].get("width", 6.0), other_w))
        data["roads"].append({
            "id": max(r["id"] for r in data["roads"]) + len(joins) + 1,
            "type": e["road"]["type"],
            "width": round(width, 4),
            "shoulder": e["road"].get("shoulder", 1.5),
            "points": [plan(*a), plan(*b)],
        })
    with open(SRC, "w") as f:
        json.dump(data, f, indent=2)
        f.write("\n")
    print(f"\nwrote {len(joins)} connections; backup at {SRC}.bak_joins")


if __name__ == "__main__":
    main()
