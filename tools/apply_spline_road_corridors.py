#!/usr/bin/env python3
"""
apply_spline_road_corridors.py
==============================
Deforms the 16-bit terrain heightmap along all road splines using real-life civil engineering profiles:
1. Longitudinal Profile: Smooths vertical grades along spline trajectories with Gaussian filtering.
2. Cross-Section Curvature: Curves the roadbed with a parabolic crown (+12cm camber).
3. Embankment & Shoulders: Generates sweeping C2-continuous quintic Hermite S-curves across 25m shoulders.
4. Grid Quantization Suppression: Smooths corridor transitions seamlessly into the natural landscape.
"""

import os
import json
import time
import cv2
import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter1d

def main():
    t0 = time.time()
    print("[*] Starting Civil Engineering Curved Road Corridor Terrain Deformation...")

    roads_file = "assets/roads/roads.json"
    hm_file = "assets/terrain/heightmap.png"

    if not os.path.exists(roads_file) or not os.path.exists(hm_file):
        print("[!] Required assets missing!")
        return

    with open(roads_file, "r") as f:
        roads_data = json.load(f)["roads"]

    print(f"[*] Loaded {len(roads_data)} road splines.")

    hm_img = Image.open(hm_file)
    hm = np.array(hm_img, dtype=np.uint16)
    H, W = hm.shape
    print(f"[*] Heightmap grid: {W}x{H}, mode: {hm_img.mode}")

    WORLD_MIN_X = -13938.0
    WORLD_MAX_X = 13938.0
    WORLD_MIN_Z = -13938.0
    WORLD_MAX_Z = 13938.0
    MIN_HEIGHT = -70.0
    MAX_HEIGHT = 87.8
    HEIGHT_RANGE = MAX_HEIGHT - MIN_HEIGHT
    m_per_px = (WORLD_MAX_X - WORLD_MIN_X) / (W - 1)

    elev = MIN_HEIGHT + (hm.astype(np.float32) / 65535.0) * HEIGHT_RANGE

    def w2g(wx, wz):
        gx = (wx - WORLD_MIN_X) / (WORLD_MAX_X - WORLD_MIN_X) * (W - 1)
        gz = (wz - WORLD_MIN_Z) / (WORLD_MAX_Z - WORLD_MIN_Z) * (H - 1)
        return gx, gz

    PORT_DRY_ELEV = -40.0

    center_elev = np.zeros((H, W), dtype=np.float32)
    center_mask = np.zeros((H, W), dtype=np.uint8)

    print("[*] Rasterizing road centerlines and vertical grade profiles...")
    for spline in roads_data:
        pts = spline["points"]
        if len(pts) < 2:
            continue

        h_spline = []
        g_spline = []
        for p in pts:
            gx, gz = w2g(p["x"], p["z"])
            g_spline.append((gx, gz))
            gx_c = int(np.clip(round(gx), 0, W - 1))
            gz_c = int(np.clip(round(gz), 0, H - 1))
            h_val = elev[gz_c, gx_c]
            # Keep port area dry
            if 3600 <= gz_c <= 4500 and 4800 <= gx_c <= 5600:
                h_val = max(h_val, PORT_DRY_ELEV)
            h_spline.append(h_val)

        # Smooth vertical road grade along trajectory (removes abrupt dips/bumps)
        if len(h_spline) >= 3:
            sigma_h = 1.8 if len(h_spline) >= 5 else 1.0
            h_smooth = gaussian_filter1d(h_spline, sigma=sigma_h, mode="nearest")
            h_smooth[0] = h_spline[0]
            h_smooth[-1] = h_spline[-1]
        else:
            h_smooth = h_spline

        # Draw dense sub-pixel segments into centerline buffers
        for i in range(len(g_spline) - 1):
            x0, z0 = g_spline[i]
            x1, z1 = g_spline[i + 1]
            h0, h1 = h_smooth[i], h_smooth[i + 1]
            length = np.hypot(x1 - x0, z1 - z0)
            steps = max(1, int(np.ceil(length * 2)))
            for s in range(steps + 1):
                t = s / steps
                px = int(np.clip(round(x0 + t * (x1 - x0)), 0, W - 1))
                pz = int(np.clip(round(z0 + t * (z1 - z0)), 0, H - 1))
                ph = h0 + t * (h1 - h0)
                center_mask[pz, px] = 255
                center_elev[pz, px] = ph

    print(f"    [+] Centerlines rasterized ({np.count_nonzero(center_mask):,} seed pixels).")

    print("[*] Computing exact Euclidean distance field and nearest centerline mapping...")
    dist, labels = cv2.distanceTransformWithLabels(
        255 - center_mask, cv2.DIST_L2, 5, labelType=cv2.DIST_LABEL_PIXEL
    )

    pts_y, pts_x = np.where(center_mask > 0)
    center_elev_vals = center_elev[pts_y, pts_x]

    # Civil engineering parameters:
    # R_ROAD: core roadbed half-width (5.0m)
    # R_SHOULDER: sweeping shoulder embankment transition (26.0m)
    R_ROAD = 5.0
    R_SHOULDER = 26.0
    MAX_DIST_PX = R_SHOULDER / m_per_px

    print(f"[*] Applying real-life curved cross-section (camber crown + C2 quintic shoulder blend)...")
    corridor_idx = np.where(dist <= MAX_DIST_PX)
    d_px = dist[corridor_idx]
    d_m = d_px * m_per_px
    h_center = center_elev_vals[labels[corridor_idx] - 1]
    h_nat = elev[corridor_idx]

    # Parabolic crown camber: 12cm convex curvature across roadbed
    camber = 0.12 * np.maximum(0.0, 1.0 - (d_m / R_ROAD) ** 2)
    h_bed = h_center + camber

    # C2 quintic Hermite S-curve for organic shoulder grading: 6t^5 - 15t^4 + 10t^3
    t = np.clip((d_m - R_ROAD) / (R_SHOULDER - R_ROAD), 0.0, 1.0)
    w = t * t * t * (t * (t * 6.0 - 15.0) + 10.0)

    deformed_elev = elev.copy()
    deformed_elev[corridor_idx] = (1.0 - w) * h_bed + w * h_nat

    # Pin centerlines
    deformed_elev[pts_y, pts_x] = center_elev_vals + 0.12

    # Smooth the corridor slightly with a 3x3 Gaussian kernel to avoid any raster discretization
    print("[*] Smoothing corridor boundaries for C2 continuity...")
    corridor_binary = (dist <= MAX_DIST_PX + 1.0).astype(np.float32)
    smoothed_corridor = cv2.GaussianBlur(deformed_elev, (3, 3), 0.8)
    deformed_elev = np.where(corridor_binary > 0, smoothed_corridor, elev)

    # Convert back to uint16
    out_norm = np.clip((deformed_elev - MIN_HEIGHT) / HEIGHT_RANGE, 0.0, 1.0)
    out_uint16 = np.clip(out_norm * 65535.0, 0, 65535).astype(np.uint16)

    out_img = Image.fromarray(out_uint16)
    out_img.save(hm_file)
    print(f"[*] Successfully saved curved 16-bit heightmap to {hm_file} in {time.time() - t0:.2f}s")

if __name__ == "__main__":
    main()
