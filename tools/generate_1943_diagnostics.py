#!/usr/bin/env python3
"""
generate_1943_diagnostics.py
============================
Generates comprehensive visual verification assets comparing the authentic
1943 Allied 12th Air Force reconnaissance photograph with the newly sculpted
heightmap, road network, texture map, and city entities.
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

def main():
    print("[*] Generating 1943 Photogrammetric Alignment Diagnostics...")

    # Load 1943 warped photo chunk (r: 3700..4500, c: 5000..5800)
    warped_path = "scratch/warped_1943_port_chunk.png"
    if not os.path.exists(warped_path):
        print(f"[!] Missing {warped_path}")
        return
    warped_1943 = cv2.imread(warped_path)

    # Load Heightmap chunk:
    hm_img = Image.open("assets/terrain/heightmap.png")
    hm_u16 = np.array(hm_img, dtype=np.float32)
    h_m = MIN_H + (hm_u16 / 65535.0) * H_RANGE
    hm_chunk = h_m[3700:4500, 5000:5800]

    # Compute Hillshade of sculpted heightmap:
    dx = cv2.Sobel(hm_chunk, cv2.CV_32F, 1, 0, ksize=3)
    dy = cv2.Sobel(hm_chunk, cv2.CV_32F, 0, 1, ksize=3)
    slope = np.arctan(np.sqrt(dx**2 + dy**2))
    aspect = np.arctan2(dy, -dx)
    # Light source: 315 deg azimuth, 45 deg altitude
    az = np.radians(315.0)
    alt = np.radians(45.0)
    shaded = np.sin(alt) * np.cos(slope) + np.cos(alt) * np.sin(slope) * np.cos(az - aspect)
    shaded = np.clip(shaded, 0.0, 1.0)
    hillshade_8u = (shaded * 255).astype(np.uint8)
    hillshade_bgr = cv2.cvtColor(hillshade_8u, cv2.COLOR_GRAY2BGR)

    # Colorize water vs land on hillshade:
    water_mask = (hm_chunk <= WATER_LEVEL)
    hillshade_bgr[water_mask] = (hillshade_bgr[water_mask] * 0.4 + np.array([120, 60, 20]) * 0.6).astype(np.uint8)

    # Load TextureMap chunk:
    tm_img = Image.open("assets/terrain/textureMap.png")
    tm = np.array(tm_img)[3700:4500, 5000:5800]

    # Load RoadMap chunk:
    rm_img = Image.open("assets/terrain/roadMap.png")
    rm = np.array(rm_img)[3700:4500, 5000:5800]

    # Create Terrain Composite (Paving + Roads):
    composite = np.zeros((800, 800, 3), dtype=np.uint8)
    # Sand background:
    composite[:] = [140, 175, 200]
    # Water:
    composite[water_mask] = [120, 60, 20]
    # Cobblestone pavement (Red channel):
    pave = (tm[:, :, 0] > 100) & (~water_mask)
    composite[pave] = [160, 160, 170]
    # Roads (White/gray):
    roads = (rm[:, :, 0] > 100) & (~water_mask)
    composite[roads] = [230, 230, 240]

    # Overlay City Entities:
    with open("assets/saves/city.txt", "r", encoding="utf-8") as f:
        city_lines = f.readlines()

    ANCHOR_X = -530.0
    ANCHOR_Z = -1502.0
    WORLD_MIN_X = -13938.0
    WORLD_MIN_Z = -13938.0
    WORLD_SIZE = 27876.0

    for line in city_lines[1:]:
        if "Rock1" in line or "Stone" in line:
            continue
        parts = line.strip().split()
        if len(parts) >= 4 and parts[0] == "OBJ_V6":
            name = parts[1]
            lx, lz = float(parts[2]), float(parts[3])
            wx = ANCHOR_X + lx
            wz = ANCHOR_Z + lz
            c = int(round((wx - WORLD_MIN_X) / WORLD_SIZE * 8128.0))
            r = int(round((wz - WORLD_MIN_Z) / WORLD_SIZE * 8128.0))
            if 5000 <= c < 5800 and 3700 <= r < 4500:
                px = c - 5000
                py = r - 3700
                if "Kiosque" in name:
                    cv2.circle(composite, (px, py), 7, (0, 0, 255), -1)
                    cv2.circle(composite, (px, py), 9, (255, 255, 255), 2)
                elif "Douane" in name or "Hotel" in name or "Cercle" in name or "Pichon" in name:
                    cv2.rectangle(composite, (px - 5, py - 5), (px + 5, py + 5), (0, 140, 255), -1)
                elif "Palm" in name:
                    cv2.circle(composite, (px, py), 3, (0, 180, 0), -1)

    # Blend 1:1 Overlay (1943 photo with game map):
    alpha = 0.5
    overlay = cv2.addWeighted(warped_1943, 0.5, composite, 0.5, 0)

    # 4-Panel Master Comparison:
    canvas = np.zeros((2 * 420, 2 * 420, 3), dtype=np.uint8)
    panels = [
        ("A. Authentic 1943 Reconnaissance Photo", warped_1943),
        ("B. Sculpted Port Bathymetry & Piers", hillshade_bgr),
        ("C. Cobblestone Paving, Roads & Rail", composite),
        ("D. 1:1 Georeferenced Photogrammetric Match", overlay)
    ]

    for idx, (title, img) in enumerate(panels):
        r_idx = (idx // 2) * 420
        c_idx = (idx % 2) * 420
        resized = cv2.resize(img, (400, 380))
        canvas[r_idx + 30:r_idx + 410, c_idx + 10:c_idx + 410] = resized
        cv2.putText(canvas, title, (c_idx + 15, r_idx + 22), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 255), 1)

    diag_path = "C:/Users/honco/.gemini/antigravity-ide/brain/663803a9-f43e-4877-99ff-741274bfc692/diagnostic_1943_alignment.png"
    cv2.imwrite(diag_path, canvas)
    cv2.imwrite("scratch/diagnostic_1943_alignment.png", canvas)
    print(f"[+] Saved diagnostic visualization to {diag_path}!")

if __name__ == "__main__":
    main()
