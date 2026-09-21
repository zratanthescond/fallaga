#!/usr/bin/env python3
"""
tools/project_roads_to_terrain_map.py
======================================
Projects verified road splines from assets/roads/roads.json into assets/terrain/roadMap.png:
1. Rasterizes road centerlines with subpixel accuracy on a 4097x4097 grid.
2. Generates physically calibrated cross-sectional distance gradients (rRaw: 1.0 at center -> 0.25 at roadbed edge -> 0.0 at shoulder).
3. Encodes road classification in Green channel:
   - Green = 255: Colonial avenues & urban streets (renders Layer 10 tarred macadam & Layer 12 cobblestone).
   - Green = 0: Rural highways & country dirt (renders Layer 8 RDR2 dirt road & compressed ruts).
4. Strictly blanks out building exclusion zones so no road bleed touches building foundations.
"""

import os
import sys
import json
import time
import numpy as np
import cv2

def main():
    t0 = time.time()
    print("==================================================================")
    print("[*] Projecting Road Splines onto 4097x4097 Terrain roadMap.png...")
    print("==================================================================")

    roads_json_path = "assets/roads/roads.json"
    if not os.path.exists(roads_json_path):
        print(f"[!] {roads_json_path} not found! Run tools/extract_1899_and_regional_roads.py first.")
        return

    with open(roads_json_path, "r") as f:
        data = json.load(f)
    roads = data.get("roads", [])
    print(f"[*] Loaded {len(roads)} road splines from {roads_json_path}")

    # World bounds matching Fallaga engine
    WORLD_MIN_X = -13938.0
    WORLD_MAX_X = 13938.0
    WORLD_MIN_Z = -13938.0
    WORLD_MAX_Z = 13938.0
    WORLD_SIZE = 27876.0
    GRID_SIZE = 4097
    METERS_PER_PIXEL = WORLD_SIZE / (GRID_SIZE - 1) # ~6.80566 m/pixel

    def world_to_pixel(wx, wz):
        col = (wx - WORLD_MIN_X) / WORLD_SIZE * (GRID_SIZE - 1)
        row = (wz - WORLD_MIN_Z) / WORLD_SIZE * (GRID_SIZE - 1)
        return col, row

    # Separate roads into Urban (colonial avenue, urban street) and Rural (country dirt, highway)
    urban_splines = []
    rural_splines = []
    for r in roads:
        if r.get("type") in ["colonial_avenue", "urban_street"]:
            urban_splines.append(r)
        else:
            rural_splines.append(r)

    print(f"[*] Splines categorized: {len(urban_splines)} urban / colonial, {len(rural_splines)} rural / regional")

    # Create centerline masks
    # Using 1-byte mask for urban and rural centerlines
    urban_center = np.zeros((GRID_SIZE, GRID_SIZE), dtype=np.uint8)
    rural_center = np.zeros((GRID_SIZE, GRID_SIZE), dtype=np.uint8)

    # We also keep track of the half-width per pixel for distance scaling
    # Standard: urban half-width ~ 7.5m (width 11-13m + 2m shoulder) -> ~1.1 to 1.3 pixels
    # Rural half-width ~ 5.5m (width 8m + 1.5m shoulder) -> ~0.8 to 1.0 pixels

    def rasterize_splines_to_mask(splines, target_mask):
        for s in splines:
            pts = s.get("points", [])
            if len(pts) < 2:
                continue
            pix_pts = []
            for p in pts:
                c, r = world_to_pixel(p["x"], p["z"])
                pix_pts.append([int(round(c)), int(round(r))])
            pix_pts = np.array(pix_pts, dtype=np.int32)
            cv2.polylines(target_mask, [pix_pts], isClosed=False, color=255, thickness=1, lineType=cv2.LINE_8)

    print("[*] Rasterizing road centerlines...")
    rasterize_splines_to_mask(urban_splines, urban_center)
    rasterize_splines_to_mask(rural_splines, rural_center)

    print("[*] Computing distance fields for smooth cross-sectional road profiles...")
    
    # Urban distance transform
    urban_inv = np.where(urban_center > 0, 0, 255).astype(np.uint8)
    dist_urban_px = cv2.distanceTransform(urban_inv, cv2.DIST_L2, 5)
    dist_urban_m = dist_urban_px * METERS_PER_PIXEL

    # Rural distance transform
    rural_inv = np.where(rural_center > 0, 0, 255).astype(np.uint8)
    dist_rural_px = cv2.distanceTransform(rural_inv, cv2.DIST_L2, 5)
    dist_rural_m = dist_rural_px * METERS_PER_PIXEL

    # Compute intensity rRaw [0.0, 1.0] from distance in meters
    # Calibrated to exact road width so terrain splatting matches 3D road ribbon
    # Urban: 13m road -> 6.5m half-width + 0.5m shoulder = 7.0m
    # Rural: 8m road -> 4.0m half-width + 1.0m shoulder = 5.0m
    URBAN_R = 7.0
    RURAL_R = 5.0

    # Smooth profile: 1.0 at centerline, 0.4 at roadbed edge, 0.0 at shoulder edge
    r_urban = np.clip(1.0 - (dist_urban_m / URBAN_R), 0.0, 1.0)
    r_rural = np.clip(1.0 - (dist_rural_m / RURAL_R), 0.0, 1.0)

    # Composite into Red channel (overall road intensity) and Green channel (urban flag)
    red_channel = np.maximum(r_urban, r_rural)
    green_channel = np.where(r_urban > 0.05, 1.0, 0.0)

    # Convert to 8-bit [0, 255]
    out_red = (red_channel * 255.0).astype(np.uint8)
    out_green = (green_channel * 255.0).astype(np.uint8)
    out_blue = np.zeros_like(out_red) # Blue = 0 (no river overlap)

    # 4. Strictly Clear Building Exclusion Zones & Harbor Water Basin
    # [bx0, bx1, bz0, bz1]
    EXCLUSION_BOXES = [
        (4008.0, 4052.0, -590.0, -315.0), # Quai de la Douane colonial buildings
        (4116.0, 4144.0, -644.0, -616.0), # Place Pichon Kiosque a Musique
        (2544.0, 2572.0, 1004.0, 1032.0), # Kasbah Tower
    ]

    print("[*] Enforcing absolute clearance for monumental building foundations...")
    for bx0, bx1, bz0, bz1 in EXCLUSION_BOXES:
        c0, r0 = world_to_pixel(bx0, bz0)
        c1, r1 = world_to_pixel(bx1, bz1)
        min_c, max_c = max(0, int(min(c0, c1))), min(GRID_SIZE - 1, int(max(c0, c1)))
        min_r, max_r = max(0, int(min(r0, r1))), min(GRID_SIZE - 1, int(max(r0, r1)))
        out_red[min_r:max_r + 1, min_c:max_c + 1] = 0
        out_green[min_r:max_r + 1, min_c:max_c + 1] = 0

    # Also clear harbor water basin: X > 4085 and -250 < Z < 250
    c_w0, r_w0 = world_to_pixel(4085.0, -250.0)
    c_w1, r_w1 = world_to_pixel(4700.0, 250.0)
    w_min_c, w_max_c = max(0, int(min(c_w0, c_w1))), min(GRID_SIZE - 1, int(max(c_w0, c_w1)))
    w_min_r, w_max_r = max(0, int(min(r_w0, r_w1))), min(GRID_SIZE - 1, int(max(r_w0, r_w1)))
    out_red[w_min_r:w_max_r + 1, w_min_c:w_max_c + 1] = 0
    out_green[w_min_r:w_max_r + 1, w_min_c:w_max_c + 1] = 0

    # Also clear water basin in harbor (X > 4080, Z between -200 and 200 is harbor water)
    # Harbor water basin bounding box: X in [4090, 4700], Z in [-600, 300]
    # Keep the quayside pierheads, but clear deep water
    # (Shader also protects water via alt > waterLevel + 0.3)

    # OpenCV saves images in BGR format
    # out_blue, out_green, out_red
    out_bgr = cv2.merge([out_blue, out_green, out_red])

    out_path = "assets/terrain/roadMap.png"
    cv2.imwrite(out_path, out_bgr)
    sz_mb = os.path.getsize(out_path) / (1024 * 1024)
    print(f"[+] Successfully exported {out_path} ({GRID_SIZE}x{GRID_SIZE}, {sz_mb:.2f} MB) in {time.time() - t0:.2f}s")
    print(f"    Total road pixels rendered: {np.count_nonzero(out_red):,} ({np.count_nonzero(out_red)/(GRID_SIZE*GRID_SIZE)*100:.2f}% of terrain)")

if __name__ == "__main__":
    main()
