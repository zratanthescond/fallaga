#!/usr/bin/env python3
"""
sculpt_local_satellite_port_medina.py
======================================
Applies authentic high-resolution satellite terrain (Copernicus GLO-30 DEM)
EXCLUSIVELY to the Sousse Port and Medina chunk:
1. Preserves high-detail base terrain (wadis, rock banks, hills, Sebkha lake)
   across the entire 27.876 km world from heightmap_backup_pre_satellite_rebuild.png.
2. Accurately georeferences the real Copernicus DEM over the Port & Medina chunk
   (rows 3300..4600, cols 4500..5900) to render the authentic Kasbah acropolis
   (~+42m above sea level) and the natural topographic slope towards the sea.
3. Sculpt authentic port quays (-46.5m), breakwaters (-46.0m), and dredged basin (-53.0m).
4. KEEPS THE HARBOR ENTRANCE CHANNEL OPEN (-53.0m) connecting the basin to the Mediterranean.
5. Applies a seamless 100-pixel Hermite smoothstep blend at chunk boundaries.
6. Updates textureMap.png (isHarbor stone paving) and re-bakes city.txt object elevations.
"""

import os
import sys
import math
import time
import shutil
import numpy as np
import cv2
from PIL import Image

Image.MAX_IMAGE_PIXELS = None

# --- WORLD GEOMETRY & CONSTANTS ---
WORLD_MIN_X = -13938.0
WORLD_MAX_X =  13938.0
WORLD_MIN_Z = -13938.0
WORLD_MAX_Z =  13938.0
WORLD_SIZE  =  27876.0
HEIGHTMAP_RES = 8129

MIN_H = -70.0
MAX_H =  87.8
H_RANGE = MAX_H - MIN_H
WATER_LEVEL = -48.5

# Chunk Bounding Box in Heightmap (Pixel Coordinates)
CHUNK_R_MIN = 3300
CHUNK_R_MAX = 4600
CHUNK_C_MIN = 4500
CHUNK_C_MAX = 5900

CHUNK_H = CHUNK_R_MAX - CHUNK_R_MIN
CHUNK_W = CHUNK_C_MAX - CHUNK_C_MIN
BLEND_MARGIN = 100 # pixels

def main():
    base_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    terrain_dir = os.path.join(base_dir, "assets", "terrain")
    saves_dir   = os.path.join(base_dir, "assets", "saves")
    scratch_dir = os.path.join(base_dir, "scratch")

    copernicus_path = os.path.join(scratch_dir, "copernicus_N35_E010.tif")
    base_hm_path    = os.path.join(terrain_dir, "heightmap_backup_pre_satellite_rebuild.png")
    raw_roadmap_path= os.path.join(terrain_dir, "roadMap_user_raw.png")
    heightmap_path  = os.path.join(terrain_dir, "heightmap.png")
    texturemap_path = os.path.join(terrain_dir, "textureMap.png")
    city_path       = os.path.join(saves_dir, "city.txt")

    print("[*] ==================================================================")
    print("[*] Local Satellite Terrain Sculpting: Sousse Port & Medina Chunk")
    print(f"[*] Chunk Rows: [{CHUNK_R_MIN} .. {CHUNK_R_MAX}], Cols: [{CHUNK_C_MIN} .. {CHUNK_C_MAX}]")
    print(f"[*] Base Terrain: Preserving whole map from {os.path.basename(base_hm_path)}")
    print("[*] ==================================================================")
    t0 = time.time()

    # 1. Verify Copernicus DEM and Base Heightmap
    if not os.path.exists(copernicus_path):
        print(f"[!] Error: Missing Copernicus DEM at {copernicus_path}")
        sys.exit(1)
    if not os.path.exists(base_hm_path):
        print(f"[!] Error: Missing base heightmap at {base_hm_path}")
        sys.exit(1)

    print(f"[*] Loading base heightmap from {base_hm_path}...")
    hm_base_img = Image.open(base_hm_path)
    hm_base_u16 = np.array(hm_base_img, dtype=np.uint16)
    game_h = MIN_H + (hm_base_u16.astype(np.float32) / 65535.0) * H_RANGE
    print(f"    Loaded base terrain {game_h.shape}, elevation range: [{game_h.min():.2f}m .. {game_h.max():.2f}m]")

    # 2. Load Copernicus DEM
    print(f"[*] Loading Copernicus DEM from {copernicus_path}...")
    cop = np.array(Image.open(copernicus_path), dtype=np.float32)
    print(f"    Loaded Copernicus DEM {cop.shape}")

    # 3. Load User Hand-Drawn Roads
    print(f"[*] Loading user road map from {raw_roadmap_path}...")
    rm_user_img = Image.open(raw_roadmap_path)
    rm_user_arr = np.array(rm_user_img)
    r_ch = rm_user_arr[:, :, 0].astype(np.float32)
    a_ch = rm_user_arr[:, :, 3].astype(np.float32) if rm_user_arr.shape[2] == 4 else 255.0
    road_user = np.maximum(r_ch, a_ch * (r_ch > 20))

    # 4. Resample Copernicus DEM onto Port & Medina Chunk
    print("[*] Georeferencing and sampling Copernicus DEM for Port & Medina...")
    r_coords = np.arange(CHUNK_R_MIN, CHUNK_R_MAX, dtype=np.float32)
    c_coords = np.arange(CHUNK_C_MIN, CHUNK_C_MAX, dtype=np.float32)
    R_grid, C_grid = np.meshgrid(r_coords, c_coords, indexing='ij')

    # Metric Calibrated Affine Mapping for Sousse Center:
    lats = -0.00001497 * R_grid + 35.889013
    lons = 0.00001977 * C_grid + 10.537837

    my = (36.0 - lats) * 3600.0
    mx = (lons - 10.0) * 3600.0

    sat_elev_raw = cv2.remap(cop, mx.astype(np.float32), my.astype(np.float32), interpolation=cv2.INTER_CUBIC)
    print(f"    Sampled satellite chunk elevation range: [{sat_elev_raw.min():.2f}m .. {sat_elev_raw.max():.2f}m]")

    # Convert satellite elevation (ASL, 0m sea level) to Game Height (WATER_LEVEL = -48.5m):
    # For land (sat_elev >= 0): game_y = WATER_LEVEL + sat_elev
    # For sea (sat_elev < 0): game_y = WATER_LEVEL - shelf depth
    chunk_sat_h = np.zeros((CHUNK_H, CHUNK_W), dtype=np.float32)
    land_sat = (sat_elev_raw >= 0.0)
    chunk_sat_h[land_sat] = WATER_LEVEL + sat_elev_raw[land_sat]

    # Coastal shelf bathymetry for sea pixels
    sea_depth = np.clip(np.abs(sat_elev_raw[~land_sat]) * 1.5 + 2.0, 1.0, 18.0)
    chunk_sat_h[~land_sat] = WATER_LEVEL - sea_depth

    # 5. Extract and Sculpt Port Infrastructure in Chunk
    print("[*] Sculpting authentic historical Port quays, breakwaters, and open water basin...")
    port_user = road_user[CHUNK_R_MIN:CHUNK_R_MAX, CHUNK_C_MIN:CHUNK_C_MAX].copy()
    port_strokes = (port_user > 25).astype(np.uint8)

    # Local coordinates within the chunk:
    # Offset: c_local = c - CHUNK_C_MIN, r_local = r - CHUNK_R_MIN
    # North Pierhead: (c=5610, r=4044) -> local (1110, 744)
    # South Pierhead: (c=5634, r=4079) -> local (1134, 779)
    # Seed for basin: (c=5500, r=4050) -> local (1000, 750)
    c_seed = 5500 - CHUNK_C_MIN
    r_seed = 4050 - CHUNK_R_MIN

    # Create temporary canvas ONLY for topological flood fill
    temp_ff = port_strokes.copy()
    # Close temporary gate across mouth ONLY on temp canvas
    cv2.line(temp_ff, (5610 - CHUNK_C_MIN, 4044 - CHUNK_R_MIN), 
                      (5634 - CHUNK_C_MIN, 4079 - CHUNK_R_MIN), 1, thickness=5)

    ff_mask = np.zeros((CHUNK_H + 2, CHUNK_W + 2), dtype=np.uint8)
    cv2.floodFill(temp_ff, ff_mask, (c_seed, r_seed), 2)
    basin_mask = (temp_ff == 2)
    print(f"    Extracted harbor water basin: {np.sum(basin_mask):,} pixels.")

    # Breakwater strokes: east of col 5420 (local 920) from port_strokes (NO gate line!)
    bw_strokes = np.zeros_like(port_strokes)
    col_split = 5420 - CHUNK_C_MIN
    bw_strokes[:, col_split:] = port_strokes[:, col_split:]
    bw_solid = cv2.dilate(bw_strokes, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (19, 19))) > 0
    # Basin has strict priority over breakwaters
    bw_solid[basin_mask] = False

    # Quays strokes: waterfront platforms between col 5240 and 5420
    quay_strokes = np.zeros_like(port_strokes)
    c_q_start = 5240 - CHUNK_C_MIN
    c_q_end   = 5420 - CHUNK_C_MIN
    quay_strokes[:, c_q_start:c_q_end] = port_strokes[:, c_q_start:c_q_end]
    quay_solid = cv2.dilate(quay_strokes, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (19, 19))) > 0
    quay_solid[basin_mask] = False
    quay_solid[bw_solid] = False

    # Open Navigable Entrance Channel:
    # 130m wide channel connecting basin to the open sea between row 4040 and row 4080
    r_mouth_start = 4042 - CHUNK_R_MIN
    r_mouth_end   = 4080 - CHUNK_R_MIN
    c_mouth_start = 5580 - CHUNK_C_MIN
    c_mouth_end   = 5750 - CHUNK_C_MIN

    # Cut entrance channel completely clear of breakwater stone
    bw_solid[r_mouth_start:r_mouth_end, c_mouth_start:c_mouth_end] = False

    # Apply Sculpted Elevations:
    # 1. Dredged harbor basin at -53.0m (-4.5m water depth)
    chunk_sat_h[basin_mask] = -53.0

    # 2. Open entrance mouth channel dredged to -53.0m
    mouth_mask = np.zeros((CHUNK_H, CHUNK_W), dtype=bool)
    mouth_mask[r_mouth_start:r_mouth_end, c_mouth_start:c_mouth_end] = True
    chunk_sat_h[mouth_mask] = -53.0

    # 3. Quays level stone wharfs at -46.5m (+2.0m ASL)
    chunk_sat_h[quay_solid] = -46.5

    # 4. Breakwaters cresting at -46.0m (+2.5m ASL)
    chunk_sat_h[bw_solid] = -46.0

    # 5. Guarantee sea pixels outside docks are submerged
    # Open Mediterranean sea is east of the docks (c > 5660 in heightmap)
    c_open_sea = 5660 - CHUNK_C_MIN
    open_sea_mask = np.zeros((CHUNK_H, CHUNK_W), dtype=bool)
    open_sea_mask[:, c_open_sea:] = ~bw_solid[:, c_open_sea:]
    chunk_sat_h[open_sea_mask] = np.minimum(chunk_sat_h[open_sea_mask], WATER_LEVEL - 1.5)

    # 6. Seamless Chunk Boundary Blending into Base Heightmap
    print(f"[*] Computing {BLEND_MARGIN}-pixel smoothstep blend into base heightmap...")
    base_crop = game_h[CHUNK_R_MIN:CHUNK_R_MAX, CHUNK_C_MIN:CHUNK_C_MAX].copy()

    # Distance transform from chunk borders
    border_mask = np.ones((CHUNK_H, CHUNK_W), dtype=np.uint8)
    border_mask[0, :] = 0
    border_mask[-1, :] = 0
    border_mask[:, 0] = 0
    border_mask[:, -1] = 0

    dist_border = cv2.distanceTransform(border_mask, cv2.DIST_L2, 5)
    u_blend = np.clip(dist_border / float(BLEND_MARGIN), 0.0, 1.0)
    # Hermite smoothstep: 3*u^2 - 2*u^3
    blend_w = u_blend * u_blend * (3.0 - 2.0 * u_blend)

    # Blend chunk interior with base terrain
    chunk_blended = (1.0 - blend_w) * base_crop + blend_w * chunk_sat_h

    # Re-enforce sharp dock and basin structures inside chunk
    chunk_blended[basin_mask] = -53.0
    chunk_blended[mouth_mask] = -53.0
    chunk_blended[quay_solid] = -46.5
    chunk_blended[bw_solid]   = -46.0

    # Place blended chunk into game heightmap
    game_h[CHUNK_R_MIN:CHUNK_R_MAX, CHUNK_C_MIN:CHUNK_C_MAX] = chunk_blended
    print("[+] Successfully integrated Port & Medina chunk into master terrain!")

    # 7. Update textureMap.png (isHarbor Stone Pavement)
    print(f"[*] Updating textureMap.png for port stone docks and breakwaters...")
    tex_map = np.zeros((HEIGHTMAP_RES, HEIGHTMAP_RES, 4), dtype=np.uint8)
    dock_global = np.zeros((HEIGHTMAP_RES, HEIGHTMAP_RES), dtype=np.uint8)
    dock_local = (quay_solid | bw_solid).astype(np.uint8) * 255
    dock_global[CHUNK_R_MIN:CHUNK_R_MAX, CHUNK_C_MIN:CHUNK_C_MAX] = dock_local
    tex_map[:, :, 0] = dock_global
    Image.fromarray(tex_map, mode='RGBA').save(texturemap_path)
    print(f"[+] Saved textureMap.png ({np.sum(dock_global > 0):,} stone pixels).")

    # 8. Re-bake city.txt Object Heights
    if os.path.exists(city_path):
        print(f"[*] Re-baking object elevations in {city_path}...")
        with open(city_path, "r", encoding="utf-8") as f:
            lines = f.readlines()

        new_lines = []
        updated_count = 0
        for line in lines:
            parts = line.strip().split()
            if len(parts) >= 6 and parts[0] == "OBJ_V6":
                try:
                    wx = float(parts[2])
                    wz = float(parts[3])
                    c = int(round((wx - WORLD_MIN_X) / WORLD_SIZE * (HEIGHTMAP_RES - 1)))
                    r = int(round((wz - WORLD_MIN_Z) / WORLD_SIZE * (HEIGHTMAP_RES - 1)))
                    c = np.clip(c, 0, HEIGHTMAP_RES - 1)
                    r = np.clip(r, 0, HEIGHTMAP_RES - 1)

                    obj_y = float(game_h[r, c])
                    parts[5] = f"{obj_y:.2f}"
                    new_lines.append(" ".join(parts) + "\n")
                    updated_count += 1
                except Exception:
                    new_lines.append(line)
            else:
                new_lines.append(line)

        with open(city_path, "w", encoding="utf-8") as f:
            f.writelines(new_lines)
        print(f"[+] Re-baked {updated_count:,} object elevations in city.txt.")

    # 9. Save Final 16-bit Heightmap
    print(f"[*] Encoding and saving final 16-bit heightmap to {heightmap_path}...")
    norm_h = np.clip((game_h - MIN_H) / H_RANGE * 65535.0, 0, 65535).astype(np.uint16)
    Image.fromarray(norm_h, mode='I;16').save(heightmap_path)
    print(f"[+] Successfully saved {heightmap_path} ({os.path.getsize(heightmap_path):,} bytes).")

    # 10. Key Location Elevation Verification
    def get_elev(c, r):
        c = np.clip(c, 0, HEIGHTMAP_RES - 1)
        r = np.clip(r, 0, HEIGHTMAP_RES - 1)
        return float(game_h[r, c])

    print("\n[*] ==================================================================")
    print("[*] Elevation Verification (Water Level = -48.50m):")
    print(f"    - Sousse Kasbah (c=4950, r=4200)   : {get_elev(4950, 4200):.2f}m (ASL: {get_elev(4950, 4200) - WATER_LEVEL:+.2f}m) -> Authentic Acropolis Hill!")
    print(f"    - Place Pichon (c=5310, r=3800)    : {get_elev(5310, 3800):.2f}m (ASL: {get_elev(5310, 3800) - WATER_LEVEL:+.2f}m) -> City Center Slope!")
    print(f"    - Port Quays (c=5350, r=4000)      : {get_elev(5350, 4000):.2f}m (ASL: {get_elev(5350, 4000) - WATER_LEVEL:+.2f}m) -> Level Stone Wharfs (+2.0m ASL)!")
    print(f"    - Harbor Water Basin (c=5500, r=4050): {get_elev(5500, 4050):.2f}m (ASL: {get_elev(5500, 4050) - WATER_LEVEL:+.2f}m) -> Dredged Harbor Basin (-4.5m)!")
    print(f"    - Entrance Channel (c=5650, r=4060): {get_elev(5650, 4060):.2f}m (ASL: {get_elev(5650, 4060) - WATER_LEVEL:+.2f}m) -> OPEN WATER CHANNEL (-4.5m)!")
    print(f"    - Breakwater Tip (c=5690, r=3912)  : {get_elev(5690, 3912):.2f}m (ASL: {get_elev(5690, 3912) - WATER_LEVEL:+.2f}m) -> Jetée Nord (+2.5m ASL)!")
    print(f"    - Western Hills (c=2000, r=4000)   : {get_elev(2000, 4000):.2f}m (ASL: {get_elev(2000, 4000) - WATER_LEVEL:+.2f}m) -> Base Relief Preserved!")
    print("[*] ==================================================================\n")

    # 11. Render 2K Hillshade Preview & Port Crop
    print("[*] Rendering previews...")
    preview_res = 2048
    h_prev = cv2.resize(game_h, (preview_res, preview_res), interpolation=cv2.INTER_AREA)

    gx = cv2.Sobel(h_prev, cv2.CV_32F, 1, 0, ksize=3)
    gy = cv2.Sobel(h_prev, cv2.CV_32F, 0, 1, ksize=3)
    sun_az = 315.0 * math.pi / 180.0
    sun_alt = 45.0 * math.pi / 180.0
    lx = math.cos(sun_alt) * math.sin(sun_az)
    ly = -math.cos(sun_alt) * math.cos(sun_az)
    lz = math.sin(sun_alt)
    scale = 0.05
    nx = -gx * scale
    ny = -gy * scale
    nz = np.ones_like(h_prev)
    norm = np.sqrt(nx**2 + ny**2 + nz**2)
    shade = (nx/norm)*lx + (ny/norm)*ly + (nz/norm)*lz
    shade = np.clip((shade + 1.0) / 2.0 * 255.0, 0, 255).astype(np.uint8)

    preview_path = os.path.join(scratch_dir, "local_satellite_port_medina_2k.png")
    cv2.imwrite(preview_path, shade)
    print(f"[+] Saved hillshade preview to {preview_path}.")

    # Crop Port & Medina detailed hillshade:
    crop_h = game_h[CHUNK_R_MIN:CHUNK_R_MAX, CHUNK_C_MIN:CHUNK_C_MAX]
    gx_c = cv2.Sobel(crop_h, cv2.CV_32F, 1, 0, ksize=3)
    gy_c = cv2.Sobel(crop_h, cv2.CV_32F, 0, 1, ksize=3)
    nx_c = -gx_c * scale
    ny_c = -gy_c * scale
    nz_c = np.ones_like(crop_h)
    norm_c = np.sqrt(nx_c**2 + ny_c**2 + nz_c**2)
    shade_c = (nx_c/norm_c)*lx + (ny_c/norm_c)*ly + (nz_c/norm_c)*lz
    shade_c = np.clip((shade_c + 1.0) / 2.0 * 255.0, 0, 255).astype(np.uint8)

    # Color water blue, quays grey, land green/tan:
    vis_crop = cv2.cvtColor(shade_c, cv2.COLOR_GRAY2BGR)
    water_c = (crop_h < WATER_LEVEL)
    vis_crop[water_c, 0] = np.clip(vis_crop[water_c, 0].astype(np.float32) * 1.2, 0, 255).astype(np.uint8)
    vis_crop[water_c, 1] = (vis_crop[water_c, 1].astype(np.float32) * 0.7).astype(np.uint8)
    vis_crop[water_c, 2] = (vis_crop[water_c, 2].astype(np.float32) * 0.4).astype(np.uint8)

    crop_path = os.path.join(scratch_dir, "local_satellite_port_medina_crop.png")
    cv2.imwrite(crop_path, vis_crop)
    print(f"[+] Saved Port & Medina crop preview to {crop_path}.")
    print(f"[+] Total execution time: {time.time()-t0:.2f}s.")

if __name__ == "__main__":
    main()
