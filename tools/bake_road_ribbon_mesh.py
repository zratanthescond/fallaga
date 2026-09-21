#!/usr/bin/env python3
"""
bake_road_ribbon_mesh.py
========================
Bakes all connected vector road splines into a seamless 3D road ribbon mesh:
1. Intersections: Extends all incoming road ends into junction centers by half-width so crossings and T-junctions are 100% solid and gap-free.
2. Continuity: Smooths polyline waypoints with Gaussian filtering (eliminates raster stair-stepping).
3. Tangent Stabilization: Filters tangent vectors across vertex rings (eliminates accordion/zigzag wobble).
4. Camber & Conformity: Conforms Left, Crown (+camber), and Right vertices tightly to 16-bit DEM heightmap.
5. Saves binary asset: assets/roads/road_mesh.bin (fast C++ load in <5ms).
"""

import os
import json
import time
import struct
import numpy as np
from PIL import Image
from collections import defaultdict
from scipy.ndimage import gaussian_filter1d

def smooth_spline(pts_np):
    """Removes raster stair-step jitter while strictly preserving endpoints."""
    N = len(pts_np)
    if N < 3:
        return pts_np.copy()
    
    sigma = 1.6 if N >= 5 else 1.0
    sm = gaussian_filter1d(pts_np, sigma=sigma, axis=0, mode='nearest')
    sm[0] = pts_np[0]
    sm[-1] = pts_np[-1]
    return sm

def compute_stable_tangents(pts_np):
    """Computes monotonic, smoothed tangent vectors to prevent normal oscillations."""
    N = len(pts_np)
    tangents = np.zeros_like(pts_np)
    if N == 2:
        d = pts_np[1] - pts_np[0]
        l = np.linalg.norm(d)
        tangents[:] = d / (l if l > 1e-4 else 1.0)
        return tangents

    tangents[0] = pts_np[1] - pts_np[0]
    tangents[-1] = pts_np[-1] - pts_np[-2]
    tangents[1:-1] = (pts_np[2:] - pts_np[:-2]) * 0.5

    sigma = 1.4 if N >= 5 else 0.8
    t_sm = gaussian_filter1d(tangents, sigma=sigma, axis=0, mode='nearest')
    lens = np.linalg.norm(t_sm, axis=1, keepdims=True)
    lens[lens < 1e-4] = 1.0
    return t_sm / lens

def main():
    t0 = time.time()
    print("[*] Starting 3D Road Ribbon Mesh Baker with Full Junction Coverage...")

    roads_file = "assets/roads/roads.json"
    hm_file = "assets/terrain/heightmap.png"

    if not os.path.exists(roads_file) or not os.path.exists(hm_file):
        print("[!] Required inputs missing!")
        return

    with open(roads_file, "r") as f:
        roads = json.load(f)["roads"]

    print(f"[*] Loaded {len(roads)} road splines.")

    # 1. Map endpoint coordinates to detect junctions
    coord_map = defaultdict(int)
    for r in roads:
        pts = r['points']
        coord_map[(pts[0]['x'], pts[0]['z'])] += 1
        coord_map[(pts[-1]['x'], pts[-1]['z'])] += 1

    # 2. Extend incoming road ends into junctions by half-width
    extended_count = 0
    for r in roads:
        pts = r['points']
        w = r['width']
        hw = w * 0.5
        if len(pts) < 2:
            continue

        p0 = (pts[0]['x'], pts[0]['z'])
        if coord_map[p0] >= 2:
            dx = pts[0]['x'] - pts[1]['x']
            dz = pts[0]['z'] - pts[1]['z']
            l = np.sqrt(dx * dx + dz * dz)
            if l > 1e-4:
                pts[0]['x'] = round(float(pts[0]['x'] + (dx / l) * hw), 2)
                pts[0]['z'] = round(float(pts[0]['z'] + (dz / l) * hw), 2)
                extended_count += 1

        p1 = (pts[-1]['x'], pts[-1]['z'])
        if coord_map[p1] >= 2:
            dx = pts[-1]['x'] - pts[-2]['x']
            dz = pts[-1]['z'] - pts[-2]['z']
            l = np.sqrt(dx * dx + dz * dz)
            if l > 1e-4:
                pts[-1]['x'] = round(float(pts[-1]['x'] + (dx / l) * hw), 2)
                pts[-1]['z'] = round(float(pts[-1]['z'] + (dz / l) * hw), 2)
                extended_count += 1

    print(f"    [+] Extended {extended_count} road endpoints into junctions by half-width.")

    # 3. Load 16-bit heightmap
    hm_img = Image.open(hm_file)
    hm = np.array(hm_img, dtype=np.uint16)
    H, W = hm.shape

    WORLD_MIN_X = -13938.0
    WORLD_MAX_X = 13938.0
    WORLD_MIN_Z = -13938.0
    WORLD_MAX_Z = 13938.0
    MIN_HEIGHT = -70.0
    MAX_HEIGHT = 87.8
    HEIGHT_RANGE = MAX_HEIGHT - MIN_HEIGHT

    def get_height(wx, wz):
        gx = (wx - WORLD_MIN_X) / (WORLD_MAX_X - WORLD_MIN_X) * (W - 1)
        gz = (wz - WORLD_MIN_Z) / (WORLD_MAX_Z - WORLD_MIN_Z) * (H - 1)
        gx = np.clip(gx, 0.0, W - 1.001)
        gz = np.clip(gz, 0.0, H - 1.001)
        x0, z0 = int(gx), int(gz)
        x1, z1 = min(x0 + 1, W - 1), min(z0 + 1, H - 1)
        tx, tz = gx - x0, gz - z0
        h00 = hm[z0, x0]
        h10 = hm[z0, x1]
        h01 = hm[z1, x0]
        h11 = hm[z1, x1]
        norm_h = (h00 * (1 - tx) * (1 - tz) +
                  h10 * tx * (1 - tz) +
                  h01 * (1 - tx) * tz +
                  h11 * tx * tz) / 65535.0
        return MIN_HEIGHT + norm_h * HEIGHT_RANGE

    type_map = {
        "colonial_avenue": 0.0,
        "urban_street": 1.0,
        "country_highway": 2.0,
        "country_dirt": 3.0,
        "rural_track": 4.0
    }

    vertices = []
    indices = []

    print("[*] Generating 3D cambered ribbon geometry with stabilized tangents...")
    for spline in roads:
        pts = spline["points"]
        if len(pts) < 2:
            continue

        width = spline["width"]
        hw = width * 0.5
        rtype = type_map.get(spline["type"], 3.0)

        raw_pts = np.array([[p["x"], p["z"]] for p in pts], dtype=np.float32)
        sm_pts = smooth_spline(raw_pts)
        tangents = compute_stable_tangents(sm_pts)

        # Cumulative distance for texture mapping
        diffs = np.diff(sm_pts, axis=0)
        dists = np.sqrt((diffs ** 2).sum(axis=1))
        cum_v = np.concatenate(([0.0], np.cumsum(dists)))

        spline_start_idx = len(vertices)

        for i in range(len(sm_pts)):
            wx, wz = sm_pts[i, 0], sm_pts[i, 1]
            tx, tz = tangents[i, 0], tangents[i, 1]

            rx = -tz
            rz = tx

            lx = wx - rx * hw
            lz = wz - rz * hw
            rx_pos = wx + rx * hw
            rz_pos = wz + rz * hw

            Z_BIAS = 0.12
            ly = get_height(lx, lz) + Z_BIAS
            cy = get_height(wx, wz) + Z_BIAS + 0.035
            ry = get_height(rx_pos, rz_pos) + Z_BIAS

            v_coord = cum_v[i] * 0.20

            nx, ny, nz = 0.0, 1.0, 0.0

            vertices.append((lx, ly, lz, nx, ny, nz, 0.0, v_coord, rtype))
            vertices.append((wx, cy, wz, nx, ny, nz, 0.5, v_coord, rtype))
            vertices.append((rx_pos, ry, rz_pos, nx, ny, nz, 1.0, v_coord, rtype))

        num_rings = len(sm_pts)
        for i in range(num_rings - 1):
            r0 = spline_start_idx + i * 3
            r1 = spline_start_idx + (i + 1) * 3

            indices.extend([r0 + 0, r1 + 0, r0 + 1])
            indices.extend([r0 + 1, r1 + 0, r1 + 1])

            indices.extend([r0 + 1, r1 + 1, r0 + 2])
            indices.extend([r0 + 2, r1 + 1, r1 + 2])

    print(f"[*] Generated {len(vertices):,} vertices, {len(indices) // 3:,} triangles.")

    out_bin = "assets/roads/road_mesh.bin"
    with open(out_bin, "wb") as f:
        f.write(b"ROAD")
        f.write(struct.pack("<II", len(vertices), len(indices)))
        for v in vertices:
            f.write(struct.pack("<9f", *v))
        for idx in indices:
            f.write(struct.pack("<I", idx))

    sz_mb = os.path.getsize(out_bin) / (1024 * 1024)
    print(f"[*] Successfully saved binary road mesh to {out_bin} ({sz_mb:.2f} MB) in {time.time() - t0:.2f}s")

if __name__ == "__main__":
    main()
