"""Characterise flatness in assets/terrain/heightmap.png.

Reports where the terrain is unnaturally flat (near-constant elevation over a large
area) and whether those areas sit inside the built-up part of the map. A "flat plate"
in an authored heightmap renders as an expanse of paving, so it is worth knowing
whether one is deliberate (a square, a quay) or an artefact of a generation step.

Usage:
    python tools/analyse_terrain_flatness.py [--grid 64] [--min-cells 25]
"""

import argparse
import json
import math

import numpy as np
from PIL import Image

HEIGHTMAP = "assets/terrain/heightmap.png"
ROADS = "assets/roads/roads_manual.json"
# Must match TerrainManager: the 16-bit heightmap is mapped onto this range, and the
# world spans these bounds. Kept here so the tool is readable in real units.
MIN_HEIGHT, MAX_HEIGHT = -70.0, 87.8
WORLD_MIN_X, WORLD_MAX_X = -13938.0, 13938.0
WORLD_MIN_Z, WORLD_MAX_Z = -13938.0, 13938.0
PLAN_OFFSET_X, PLAN_OFFSET_Z = 3600.0, -701.0


def load_heights():
    im = Image.open(HEIGHTMAP)
    a = np.asarray(im)
    if a.ndim == 3:
        a = a[:, :, 0]
    raw = a.astype(np.float64)
    if raw.max() > 255.0:  # 16-bit
        raw /= 65535.0
    else:
        raw /= 255.0
    return MIN_HEIGHT + raw * (MAX_HEIGHT - MIN_HEIGHT)


def grid_stats(h, grid):
    """Per-cell elevation range, in metres."""
    nz, nx = h.shape
    cz, cx = nz // grid, nx // grid
    ranges = np.zeros((grid, grid))
    means = np.zeros((grid, grid))
    for j in range(grid):
        for i in range(grid):
            block = h[j * cz:(j + 1) * cz, i * cx:(i + 1) * cx]
            if block.size == 0:
                continue
            ranges[j, i] = block.max() - block.min()
            means[j, i] = block.mean()
    return ranges, means


def cell_to_world(grid, j, i, nx, nz):
    u = (i + 0.5) / grid
    v = (j + 0.5) / grid
    x = WORLD_MIN_X + u * (WORLD_MAX_X - WORLD_MIN_X)
    z = WORLD_MIN_Z + v * (WORLD_MAX_Z - WORLD_MIN_Z)
    return x, z


def road_coverage(grid, nx, nz, reach=150.0):
    """Fraction of each cell within `reach` metres of a road, by sampling."""
    try:
        data = json.load(open(ROADS))
    except Exception:
        return None
    segs = []
    for r in data["roads"]:
        pts = [(p["x"] + PLAN_OFFSET_X, p["z"] + PLAN_OFFSET_Z) for p in r["points"]]
        segs.append((pts, max(8.0, r.get("width", 6.0)) * 0.5))
    near = np.zeros((grid, grid), dtype=bool)
    for j in range(grid):
        for i in range(grid):
            x, z = cell_to_world(grid, j, i, nx, nz)
            for pts, hw in segs:
                hit = False
                for k in range(len(pts) - 1):
                    ax, az = pts[k]
                    bx, bz = pts[k + 1]
                    dx, dz = bx - ax, bz - az
                    l2 = dx * dx + dz * dz
                    t = 0.0 if l2 < 1e-9 else max(0.0, min(1.0, ((x - ax) * dx + (z - az) * dz) / l2))
                    qx, qz = ax + t * dx, az + t * dz
                    if math.hypot(x - qx, z - qz) <= reach + hw:
                        hit = True
                        break
                if hit:
                    break
            near[j, i] = hit
    return near


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--grid", type=int, default=64)
    ap.add_argument("--flat-range", type=float, default=1.0,
                    help="a cell is 'flat' when its elevation range is under this")
    args = ap.parse_args()

    h = load_heights()
    nz, nx = h.shape
    cell_m = (WORLD_MAX_X - WORLD_MIN_X) / nx
    print(f"heightmap {nx}x{nz}  ({cell_m:.2f} m per texel, "
          f"{(WORLD_MAX_X - WORLD_MIN_X) / 1000:.1f} km across)")
    print(f"elevation range {h.min():.1f} .. {h.max():.1f} m\n")

    ranges, means = grid_stats(h, args.grid)
    near_road = road_coverage(args.grid, nx, nz)
    flat = ranges < args.flat_range

    print(f"analysis grid {args.grid}x{args.grid} "
          f"({(WORLD_MAX_X - WORLD_MIN_X) / args.grid:.0f} m per cell)")
    print(f"flat cells (range < {args.flat_range} m): {int(flat.sum())} "
          f"of {args.grid * args.grid} ({100.0 * flat.mean():.1f}%)")
    if near_road is not None:
        flat_near = int((flat & near_road).sum())
        print(f"  of which within 150 m of a road: {flat_near}")
    print()

    # Largest contiguous flat clusters, via a simple flood fill.
    seen = np.zeros_like(flat, dtype=bool)
    clusters = []
    for j in range(args.grid):
        for i in range(args.grid):
            if not flat[j, i] or seen[j, i]:
                continue
            stack = [(j, i)]
            seen[j, i] = True
            cells = []
            while stack:
                cj, ci = stack.pop()
                cells.append((cj, ci))
                for dj, di in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nj, ni = cj + dj, ci + di
                    if 0 <= nj < args.grid and 0 <= ni < args.grid \
                            and flat[nj, ni] and not seen[nj, ni]:
                        seen[nj, ni] = True
                        stack.append((nj, ni))
            clusters.append(cells)

    clusters.sort(key=len, reverse=True)
    print("Largest flat clusters:")
    for cells in clusters[:8]:
        cj = sum(c[0] for c in cells) / len(cells)
        ci = sum(c[1] for c in cells) / len(cells)
        x, z = cell_to_world(args.grid, cj, ci, nx, nz)
        elev = means[int(cj), int(ci)]
        span_m = math.sqrt(len(cells)) * (WORLD_MAX_X - WORLD_MIN_X) / args.grid
        roads = ""
        if near_road is not None:
            on = any(near_road[c[0], c[1]] for c in cells)
            roads = " (touches road corridor)" if on else " (no road nearby)"
        print(f"  {len(cells):4d} cells  ~{span_m:6.0f} m across  "
              f"at world ({x:9.0f},{z:9.0f})  mean elev {elev:6.1f} m{roads}")


if __name__ == "__main__":
    main()
