#!/usr/bin/env python3
"""
tools/smooth_port_medina_slope.py
==================================
Eliminates the artificial 40-meter vertical cliff between the port quayside (-46.5m)
and the eastern Medina boundary (-7.5m).
Replaces the sheer drop with a realistic, authentic 2.5% grade street slope,
eliminates vertically stretched quay stone textures on the hillside,
preserves the dredged basin at -53.0m, South Pier and breakwaters at -46.0m,
and guarantees ZERO underwater roads.
"""

import os
import sys
import numpy as np
import cv2
from PIL import Image

Image.MAX_IMAGE_PIXELS = None

MIN_H = -70.0
MAX_H =  87.8
H_RANGE = MAX_H - MIN_H
WATER_LEVEL = -48.5

def h_to_u16(h_arr):
    norm = np.clip((h_arr - MIN_H) / H_RANGE, 0.0, 1.0)
    return (norm * 65535.0 + 0.5).astype(np.uint16)

def main():
    base_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    terrain_dir = os.path.join(base_dir, "assets", "terrain")

    hm_path = os.path.join(terrain_dir, "heightmap.png")
    tm_path = os.path.join(terrain_dir, "textureMap.png")
    rm_path = os.path.join(terrain_dir, "roadMap_user_raw.png")

    print("[*] Loading terrain assets...")
    hm_u16 = np.array(Image.open(hm_path), dtype=np.uint16)
    h = MIN_H + (hm_u16.astype(np.float32) / 65535.0) * H_RANGE

    tm_arr = np.array(Image.open(tm_path))
    rm_arr = np.array(Image.open(rm_path))[:, :, 0]
    road_px = (rm_arr > 20)

    print(f"[*] Original terrain elevation range: [{h.min():.2f}m .. {h.max():.2f}m]")
    h_new = h.copy()

    # 1. Protect all water (harbor basin at -53m, entrance channel, open sea)
    basin_mask = (h < WATER_LEVEL)
    print(f"[*] Preserving {np.sum(basin_mask):,} water pixels (< {WATER_LEVEL}m).")

    # 2. Smooth northern quay exit ramp (rows 3780..3860, cols 5180..5300)
    print("[*] Smoothing northern quay ramp (Boujaafar beach connection)...")
    for r in range(3780, 3860):
        t = (r - 3780.0) / (3860.0 - 3780.0)
        st = t * t * (3.0 - 2.0 * t)
        for c in range(5180, 5300):
            if h[r, c] < -48.0:
                continue
            h_new[r, c] = (1.0 - st) * h[3780, c] + st * -46.50

    # 3. Smooth southern quay exit ramp (rows 4380..4440, cols 5180..5300)
    print("[*] Smoothing southern quay ramp (railway & south coastal plain connection)...")
    for r in range(4380, 4440):
        t = (r - 4380.0) / (4440.0 - 4380.0)
        st = t * t * (3.0 - 2.0 * t)
        for c in range(5180, 5300):
            if h[r, c] < -48.0:
                continue
            h_new[r, c] = (1.0 - st) * -46.50 + st * h[4440, c]

    # 4. Fill isolated holes in the flat quayside apron
    print("[*] Enforcing clean, flat quayside dock apron at -46.50m...")
    quay_raw = (h_new <= -46.4) & (h_new > -48.0) & (np.arange(8129) >= 5195)[np.newaxis, :]
    kernel = cv2.getStructuringElement(cv2.MORPH_RECT, (7, 7))
    quay_filled = cv2.morphologyEx(quay_raw.astype(np.uint8), cv2.MORPH_CLOSE, kernel)
    h_new[quay_filled > 0] = -46.50

    # 5. Grade western Medina & street slope into the port
    print("[*] Grading western Medina & street slope into quayside...")
    R_MIN, R_MAX = 3760, 4420
    C_MIN = 4850

    for r in range(R_MIN, R_MAX):
        q_cols = np.where(quay_filled[r, :])[0]
        c_quay = 5205 if len(q_cols) == 0 else q_cols.min()

        # Vertical Hermite window
        if r < 3860:
            v = (r - R_MIN) / (3860.0 - R_MIN)
            wr = v * v * (3.0 - 2.0 * v)
        elif r > 4340:
            v = (R_MAX - r) / (R_MAX - 4340.0)
            wr = v * v * (3.0 - 2.0 * v)
        else:
            wr = 1.0

        c_ave = c_quay - 35  # Habib Bourguiba esplanade (~120m wide gentle slope)
        c_med = c_quay - 250 # Medina interior (~850m gentle slope)

        for c in range(C_MIN, c_quay):
            if c >= c_ave:
                tc = (c - c_ave) / float(c_quay - c_ave)
                stc = tc * tc * (3.0 - 2.0 * tc)
                h_ideal = -43.50 + (-46.50 - (-43.50)) * stc
            elif c >= c_med:
                tc = (c - c_med) / float(c_ave - c_med)
                stc = tc * tc * (3.0 - 2.0 * tc)
                h_ideal = -25.00 + (-43.50 - (-25.00)) * stc
            else:
                tc = (c - C_MIN) / float(c_med - C_MIN)
                stc = tc * tc * (3.0 - 2.0 * tc)
                h_ideal = h[r, C_MIN] + (-25.00 - h[r, C_MIN]) * stc

            h_new[r, c] = (1.0 - wr) * h_new[r, c] + wr * h_ideal

    # 6. Strictly preserve dredged basin and sea water
    h_new[basin_mask] = h[basin_mask]

    # 7. Check Invariants
    underwater = road_px & (h_new < WATER_LEVEL)
    n_underwater = np.sum(underwater)
    print(f"[*] Underwater road check: {n_underwater} pixels.")
    assert n_underwater == 0, f"Error: {n_underwater} road pixels submerged!"

    # Measure slopes in the transition area
    sub_new = h_new[3800:4350, 4850:5250]
    dx = np.diff(sub_new, axis=1)
    dy = np.diff(sub_new, axis=0)
    water_sub = (sub_new < WATER_LEVEL)
    dx_land = np.abs(dx) * (~water_sub[:, :-1])
    dy_land = np.abs(dy) * (~water_sub[:-1, :])
    print(f"[+] Graded slope max dx: {dx_land.max():.2f}m per pixel (grade: {dx_land.max()/3.43*100:.1f}%)")
    print(f"[+] Graded slope max dy: {dy_land.max():.2f}m per pixel (grade: {dy_land.max()/3.43*100:.1f}%)")

    # 8. Clean textureMap.png (remove stretched quay stones from hillside)
    print("[*] Cleaning textureMap.png...")
    tm_new = tm_arr.copy()
    # Confine Red channel (quay stone / ashlar) strictly to flat docks
    # Flat docks: h <= -46.2m, col >= 5200, and not water (h > -48.5m)
    quay_tex_mask = (h_new <= -46.2) & (h_new > WATER_LEVEL) & (np.arange(8129) >= 5200)[np.newaxis, :]
    
    # Anywhere else, zero out Red channel
    tm_new[~quay_tex_mask, 0] = 0
    # On the flat docks, ensure Red channel is active
    tm_new[quay_tex_mask, 0] = 255
    print(f"[+] Texture map: {np.sum(tm_new[:, :, 0] > 100):,} quay cobblestone pixels strictly on flat docks.")

    # 9. Save updated assets
    print("[*] Saving updated heightmap.png...")
    # Make backup of previous heightmap
    backup_hm = os.path.join(terrain_dir, "heightmap_backup_before_slope_smoothing.png")
    if not os.path.exists(backup_hm):
        Image.fromarray(hm_u16).save(backup_hm)
        print(f"    Saved backup to {os.path.basename(backup_hm)}")

    Image.fromarray(h_to_u16(h_new)).save(hm_path)
    print(f"[+] Successfully saved {hm_path}")

    print("[*] Saving updated textureMap.png...")
    Image.fromarray(tm_new).save(tm_path)
    print(f"[+] Successfully saved {tm_path}")

    print("\n[SUCCESS] Port-Medina topographic slope smoothing complete!")

if __name__ == "__main__":
    main()
