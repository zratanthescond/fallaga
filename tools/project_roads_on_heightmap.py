#!/usr/bin/env python3
"""
project_roads_on_heightmap.py
=============================
Projects the updated city roads from assets/terrain/roadMap.png onto assets/terrain/heightmap.png:
1. Creates safe backups of heightmap.png and roadMap.png.
2. Extracts road intensity/opacity from the user's hand-drawn white road strokes.
3. Elevates submerged harbor/port road sections above water level (-48.5m) to solid quay heights (-36.0m to -40.0m).
4. Smooths and levels road beds along the entire road corridor so roads are flat and driveable.
5. Saves the updated 16-bit heightmap.png.
6. Reformats roadMap.png to properly encode the roads in the RED channel with zero river (Blue) or paint (Alpha) interference.
"""

import os
import shutil
import time
from PIL import Image
import numpy as np
import cv2

def main():
    base_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    terrain_dir = os.path.join(base_dir, "assets", "terrain")

    heightmap_path = os.path.join(terrain_dir, "heightmap.png")
    roadmap_path = os.path.join(terrain_dir, "roadMap.png")

    heightmap_backup = os.path.join(terrain_dir, "heightmap_before_roads.png")
    roadmap_backup = os.path.join(terrain_dir, "roadMap_user_raw.png")

    print("[*] Starting Road-to-Heightmap Projection...")

    # 1. Backups
    if not os.path.exists(heightmap_backup):
        print(f"[*] Creating heightmap backup: {heightmap_backup}")
        shutil.copyfile(heightmap_path, heightmap_backup)
    else:
        print(f"[*] Heightmap backup already exists: {heightmap_backup}")

    if not os.path.exists(roadmap_backup):
        print(f"[*] Creating user roadMap backup: {roadmap_backup}")
        shutil.copyfile(roadmap_path, roadmap_backup)
    else:
        print(f"[*] User roadMap backup already exists: {roadmap_backup}")

    # 2. Load roadMap
    print(f"[*] Loading roadMap from: {roadmap_path}")
    rm_img = Image.open(roadmap_path)
    rm_arr = np.array(rm_img)
    print(f"    Dimensions: {rm_arr.shape}, Mode: {rm_img.mode}")

    # The user drew white roads with transparency on transparent/black background
    # Calculate road intensity [0.0, 1.0] from RGB and Alpha
    r_ch = rm_arr[:, :, 0].astype(np.float32) / 255.0
    a_ch = rm_arr[:, :, 3].astype(np.float32) / 255.0 if rm_arr.shape[2] == 4 else np.ones_like(r_ch)
    
    # If the user drew on transparent background (RGB=255, A=alpha) or on black (RGB=stroke, A=255)
    road_intensity = np.maximum(r_ch * a_ch, a_ch * (r_ch > 0.05))
    road_mask = road_intensity > 0.05
    road_count = np.count_nonzero(road_mask)
    print(f"    Detected {road_count:,} road pixels.")

    # 3. Load heightmap
    print(f"[*] Loading heightmap from: {heightmap_path}")
    hm_img = Image.open(heightmap_path)
    hm_arr = np.array(hm_img, dtype=np.float32)
    print(f"    Dimensions: {hm_arr.shape}, Mode: {hm_img.mode}")

    MIN_H = -70.0
    MAX_H = 87.8
    height_range = MAX_H - MIN_H
    heights = MIN_H + (hm_arr / 65535.0) * height_range

    print(f"    Current terrain height range: [{heights.min():.2f}m, {heights.max():.2f}m]")
    print(f"    Current road height range:    [{heights[road_mask].min():.2f}m, {heights[road_mask].max():.2f}m]")

    # 4. Handle Submerged / Coastal Road Sections
    # Water level is -48.5m. Submerged roads (seabed at -70m) must be brought onto solid quays/piers.
    submerged_threshold = -42.0
    submerged_roads = road_mask & (heights < submerged_threshold)
    num_submerged = np.count_nonzero(submerged_roads)
    print(f"[*] Submerged road pixels (< {submerged_threshold}m): {num_submerged:,}")

    heights_pier = heights.copy()
    if num_submerged > 0:
        print("[*] Creating raised pier and quay embankments for coastal/harbor roads...")
        # Dilate submerged road mask to create a supporting quay/pier embankment
        kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (25, 25))
        pier_mask = cv2.dilate(submerged_roads.astype(np.uint8), kernel, iterations=2)
        pier_blend = cv2.GaussianBlur(pier_mask.astype(np.float32), (35, 35), 10.0)

        # Standard quay height: -36.0m (consistent with historical port quays, ~12.5m above water level)
        pier_target_h = -36.0
        heights_pier = np.maximum(heights_pier, heights_pier * (1.0 - pier_blend) + pier_target_h * pier_blend)

    # 5. Road-Bed Leveling & Smoothing
    print("[*] Smoothing and leveling road beds across the terrain...")
    # Gaussian blur the terrain to produce smooth, graded elevation along road paths
    blurred_heights = cv2.GaussianBlur(heights_pier, (45, 45), 12.0)
    
    # Soft road influence mask with smooth falloff to preserve natural adjacent terrain
    road_blend = cv2.GaussianBlur(road_intensity, (21, 21), 5.0)
    road_blend = np.clip(road_blend * 1.2, 0.0, 1.0)

    final_heights = heights_pier * (1.0 - road_blend) + blurred_heights * road_blend

    # Guarantee all road pixels are strictly above water level (-48.5m)
    min_safe_road_height = -40.0
    safe_mask = (road_intensity > 0.05) & (final_heights < min_safe_road_height)
    final_heights = np.where(safe_mask, min_safe_road_height, final_heights)

    # Clamp to valid heightmap range
    final_heights = np.clip(final_heights, MIN_H, MAX_H)

    print(f"    New road height range: [{final_heights[road_mask].min():.2f}m, {final_heights[road_mask].max():.2f}m]")
    print(f"    Roads below water level (-48.5m): {np.count_nonzero(final_heights[road_mask] <= -48.5)}")

    # 6. Save Updated 16-Bit Heightmap
    print(f"[*] Saving updated 16-bit heightmap to: {heightmap_path}")
    final_raw16 = np.round((final_heights - MIN_H) / height_range * 65535.0)
    final_raw16 = np.clip(final_raw16, 0, 65535).astype(np.uint16)
    out_hm_img = Image.fromarray(final_raw16, mode='I;16')
    out_hm_img.save(heightmap_path)
    print("[+] Heightmap saved successfully!")

    # 7. Reformat and Save roadMap.png
    print(f"[*] Formatting roadMap.png with Red channel = Roads, Green/Blue/Alpha = 0...")
    out_rm = np.zeros((rm_arr.shape[0], rm_arr.shape[1], 4), dtype=np.uint8)
    # Channel 0: Red (Roads)
    out_rm[:, :, 0] = np.clip(road_intensity * 255.0, 0, 255).astype(np.uint8)
    # Channel 1: Green (Railways - 0)
    out_rm[:, :, 1] = 0
    # Channel 2: Blue (Rivers - 0, prevents river water rendering on roads)
    out_rm[:, :, 2] = 0
    # Channel 3: Alpha (Paint mask - 0, prevents runtime paint override)
    out_rm[:, :, 3] = 0

    out_rm_img = Image.fromarray(out_rm, mode='RGBA')
    out_rm_img.save(roadmap_path)
    print(f"[+] Formatted roadMap.png saved successfully to: {roadmap_path}")

    print("\n[SUCCESS] Road projection onto heightmap completed!")

if __name__ == "__main__":
    t0 = time.time()
    main()
    print(f"Total time: {time.time() - t0:.2f}s")
