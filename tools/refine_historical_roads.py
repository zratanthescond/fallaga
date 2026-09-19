#!/usr/bin/env python3
"""
tools/refine_historical_roads.py
================================
Refines roads to authentic historical dimensions from 1899 map and 1943 aerial photo:
1. Skeletons the user's drawn roads to extract their exact centerlines.
2. Re-draws continuous anti-aliased road ribbons:
   - Major avenues / boulevards (Avenue de la Marine, Loubet, Millet): ~14-16m wide
   - Standard city streets & countryside roads: ~9-11m wide
   - Medina alleyways & minor tracks: ~5-7m wide
3. Applies sub-pixel continuous Gaussian anti-aliasing to eliminate texel stair-stepping.
4. Preserves 100% dry land guarantee (no roads in harbor water).
"""

import os
import time
import cv2
import numpy as np
from PIL import Image

Image.MAX_IMAGE_PIXELS = None

def morph_skeleton(img):
    skeleton = np.zeros(img.shape, np.uint8)
    eroded = img.copy()
    element = cv2.getStructuringElement(cv2.MORPH_CROSS, (3, 3))
    while True:
        open_img = cv2.morphologyEx(eroded, cv2.MORPH_OPEN, element)
        temp = cv2.subtract(eroded, open_img)
        eroded = cv2.erode(eroded, element)
        skeleton = cv2.bitwise_or(skeleton, temp)
        if cv2.countNonZero(eroded) == 0:
            break
    return skeleton

def main():
    t0 = time.time()
    print("[*] Loading user raw roads and harbor basin...")
    rm_raw_path = "assets/terrain/roadMap_user_raw.png"
    rm_raw = cv2.imread(rm_raw_path)
    roads_raw = (rm_raw[:, :, 2] > 50).astype(np.uint8) # Red channel

    # Extract harbor basin to ensure zero roads in water
    r0, r1 = 3700, 4500
    c0, c1 = 4800, 5800
    H, W = r1 - r0, c1 - c0
    port_crop = roads_raw[r0:r1, c0:c1].copy()

    # Bridge mouth for basin flood fill
    bridge = port_crop.copy()
    cv2.line(bridge, (5595 - c0, 4045 - r0), (5615 - c0, 4100 - r0), 1, thickness=12)
    ff_mask = np.zeros((H + 2, W + 2), dtype=np.uint8)
    filled = bridge.copy()
    cv2.floodFill(filled, ff_mask, (5450 - c0, 4100 - r0), 2)
    user_basin = (filled == 2)
    final_basin_crop = user_basin & (~port_crop.astype(bool))

    # Active road bounding box
    r_idx, c_idx = np.where(roads_raw > 0)
    rmin, rmax = max(0, r_idx.min() - 20), min(8128, r_idx.max() + 20)
    cmin, cmax = max(0, c_idx.min() - 20), min(8128, c_idx.max() + 20)
    print(f"    [+] Active road region: rows [{rmin}..{rmax}], cols [{cmin}..{cmax}]")

    crop = roads_raw[rmin:rmax, cmin:cmax].copy()
    dist_orig = cv2.distanceTransform(crop, cv2.DIST_L2, 5)

    print("[*] Extracting road network skeleton...")
    skel = morph_skeleton(crop)
    print(f"    [+] Skeleton points: {np.count_nonzero(skel):,}")

    print("[*] Rebuilding continuous anti-aliased road ribbons with metric widths...")
    ys_s, xs_s = np.where(skel > 0)
    coords_s = set(zip(ys_s, xs_s))
    connected = np.zeros_like(crop, dtype=np.uint8)

    for y, x in zip(ys_s, xs_s):
        r_orig = dist_orig[y, x]
        # Historical hierarchy:
        # r_orig > 7.0: major boulevards & avenues (~14-16m wide -> thickness 4 px)
        # r_orig > 3.5: standard town / country roads (~9-11m wide -> thickness 3 px)
        # else: minor tracks & Medina alleys (~5-7m wide -> thickness 2 px)
        th = 4 if r_orig > 7.0 else (3 if r_orig > 3.5 else 2)
        cv2.circle(connected, (x, y), max(1, th // 2), 255, -1)
        for dy in [-1, 0, 1]:
            for dx in [-1, 0, 1]:
                if (dy != 0 or dx != 0) and (y + dy, x + dx) in coords_s:
                    cv2.line(connected, (x, y), (x + dx, y + dy), 255, thickness=th, lineType=cv2.LINE_AA)

    # Sub-pixel continuous Gaussian anti-aliasing
    print("[*] Applying continuous sub-pixel Gaussian anti-aliasing...")
    smooth_ribbon = cv2.GaussianBlur(connected, (5, 5), sigmaX=0.85, sigmaY=0.85)

    # Reconstruct full roadMap.png
    out_roadMap = np.zeros((8129, 8129, 3), dtype=np.uint8)
    out_roadMap[rmin:rmax, cmin:cmax, 2] = smooth_ribbon # Red channel = roads

    # Strictly clear water basin
    out_roadMap[r0:r1, c0:c1, 2][final_basin_crop] = 0

    print("[*] Saving refined assets/terrain/roadMap.png...")
    cv2.imwrite("assets/terrain/roadMap.png", out_roadMap)
    print(f"[+] Successfully generated anti-aliased historical roads in {time.time()-t0:.2f}s!")

if __name__ == "__main__":
    main()
