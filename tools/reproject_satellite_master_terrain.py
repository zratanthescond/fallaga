#!/usr/bin/env python3
"""
reproject_satellite_master_terrain.py
=====================================
Reprojects the Copernicus GLO-30 satellite DEM directly to generate Fallaga's
8129x8129 16-bit heightmap matching the user's hand-drawn road map 1:1.

Zero road distortion, zero affine warping of the user's road drawing.
The satellite DEM is sampled at the calibrated geographic bounding box:
  Lat: [35.732394°N .. 35.915596°N]
  Lon: [10.503971°E .. 10.710706°E]
"""

import os
import sys
import math
import time
import shutil
import json
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

# Calibrated Geographic Bounding Box for 1:1 Road Map Alignment
MIN_LAT = 35.732394
MAX_LAT = 35.915596
MIN_LON = 10.503971
MAX_LON = 10.710706

def main():
    base_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    terrain_dir = os.path.join(base_dir, "assets", "terrain")
    saves_dir   = os.path.join(base_dir, "assets", "saves")
    scratch_dir = os.path.join(base_dir, "scratch")

    copernicus_path  = os.path.join(scratch_dir, "copernicus_N35_E010.tif")
    raw_roadmap_path = os.path.join(terrain_dir, "roadMap_user_raw.png")
    heightmap_path   = os.path.join(terrain_dir, "heightmap.png")
    roadmap_path     = os.path.join(terrain_dir, "roadMap.png")
    texturemap_path  = os.path.join(terrain_dir, "textureMap.png")
    city_path        = os.path.join(saves_dir, "city.txt")

    print("[*] ==================================================================")
    print("[*] Reprojecting Satellite DEM to Match User Road Map (1:1 Direct Fit)")
    print(f"[*] Resolution: {HEIGHTMAP_RES}x{HEIGHTMAP_RES} 16-bit")
    print(f"[*] Bounding Box: Lat [{MIN_LAT:.6f}°N .. {MAX_LAT:.6f}°N]")
    print(f"[*]               Lon [{MIN_LON:.6f}°E .. {MAX_LON:.6f}°E]")
    print("[*] ==================================================================")
    t0 = time.time()

    # 1. Load Copernicus GeoTIFF DEM
    if not os.path.exists(copernicus_path):
        print(f"[!] Error: Copernicus DEM missing at {copernicus_path}")
        sys.exit(1)

    print(f"[*] Loading Copernicus GLO-30 DEM from {copernicus_path}...")
    cop_img = Image.open(copernicus_path)
    cop = np.array(cop_img, dtype=np.float32)
    print(f"    Loaded Copernicus DEM {cop.shape}, elevation range: [{cop.min():.2f}m, {cop.max():.2f}m]")

    # 2. Resample Copernicus DEM into User Map Coordinate Frame:
    # Row 0 = North (MAX_LAT), Row 8128 = South (MIN_LAT)
    # Col 0 = West  (MIN_LON), Col 8128 = East  (MAX_LON)
    print("[*] Resampling Copernicus DEM to match user road map orientation...")
    t_sample = time.time()
    lats = np.linspace(MAX_LAT, MIN_LAT, HEIGHTMAP_RES, dtype=np.float32)
    lons = np.linspace(MIN_LON, MAX_LON, HEIGHTMAP_RES, dtype=np.float32)

    glon, glat = np.meshgrid(lons, lats)
    mx = (glon - 10.0) * 3600.0
    my = (36.0 - glat) * 3600.0

    elev_raw = cv2.remap(cop, mx.astype(np.float32), my.astype(np.float32), interpolation=cv2.INTER_CUBIC)
    print(f"    Resampled in {time.time()-t_sample:.2f}s. Raw elevation range: [{elev_raw.min():.2f}m, {elev_raw.max():.2f}m]")

    # 3. Anti-Aliasing Reconstruction Filter
    print("[*] Applying anti-aliasing reconstruction filter (sigma=1.5 px)...")
    elev_aa = cv2.GaussianBlur(elev_raw, (0, 0), sigmaX=1.5, sigmaY=1.5)

    # 4. Load User Hand-Drawn Roads (1:1 DIRECT MATCH, ZERO WARPING)
    print("[*] Loading user's hand-drawn road map 1:1 from roadMap_user_raw.png...")
    rm_user_img = Image.open(raw_roadmap_path)
    rm_user_arr = np.array(rm_user_img)
    r_ch = rm_user_arr[:, :, 0].astype(np.float32)
    a_ch = rm_user_arr[:, :, 3].astype(np.float32) if rm_user_arr.shape[2] == 4 else 255.0
    road_user = np.maximum(r_ch, a_ch * (r_ch > 20))
    print(f"    1:1 user road pixels: {np.count_nonzero(road_user > 20):,}")

    # 5. Mediterranean Sea & Shoreline Extraction
    print("[*] Extracting authentic Mediterranean coastline and inland water basins...")
    is_sea_raw = (elev_aa <= 0.0).astype(np.float32)
    is_sea_smooth = cv2.GaussianBlur(is_sea_raw, (0, 0), sigmaX=3.0, sigmaY=3.0)
    sea_candidate = (is_sea_smooth > 0.5)

    # Connected components from East boundary (open Mediterranean)
    num_lbl, labels, stats, centroids = cv2.connectedComponentsWithStats(sea_candidate.astype(np.uint8))
    east_lbls = set(labels[:, -1])
    east_lbls.discard(0)

    clean_sea = np.zeros((HEIGHTMAP_RES, HEIGHTMAP_RES), dtype=bool)
    for lbl in east_lbls:
        if stats[lbl, cv2.CC_STAT_AREA] > 5000:
            clean_sea |= (labels == lbl)

    # Inland lake basins (Sebkhas)
    lake_mask = np.zeros((HEIGHTMAP_RES, HEIGHTMAP_RES), dtype=bool)
    for lbl in range(1, num_lbl):
        if lbl not in east_lbls and stats[lbl, cv2.CC_STAT_AREA] > 2000:
            lake_mask |= (labels == lbl)

    # Protect urban and beach road corridor from DEM 0m noise
    urban_roads = (road_user > 20)
    urban_land = cv2.dilate(urban_roads.astype(np.uint8), cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (35, 35))) > 0
    clean_sea[urban_land] = False

    water_mask = clean_sea | lake_mask
    land_mask = ~water_mask
    print(f"    Mediterranean Sea: {100.0 * np.sum(clean_sea) / clean_sea.size:.1f}%")
    print(f"    Total Land: {100.0 * np.sum(land_mask) / land_mask.size:.1f}%")

    # 6. Multi-Scale Fluvial River & Wadi Root Detection (Top-Hat Morphology)
    print("[*] Detecting all rivers, wadis, and tributary roots from satellite geomorphology...")
    t_val = time.time()
    v_fine = cv2.morphologyEx(elev_aa, cv2.MORPH_CLOSE, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (7, 7))) - elev_aa
    v_mid  = cv2.morphologyEx(elev_aa, cv2.MORPH_CLOSE, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (19, 19))) - elev_aa
    v_wide = cv2.morphologyEx(elev_aa, cv2.MORPH_CLOSE, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (45, 45))) - elev_aa
    v_deep = cv2.morphologyEx(elev_aa, cv2.MORPH_CLOSE, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (91, 91))) - elev_aa

    valleys = np.maximum(0.0, v_fine * 0.8 + v_mid * 1.0 + v_wide * 0.8 + v_deep * 0.5) * land_mask

    dist_to_water = cv2.distanceTransform(land_mask.astype(np.uint8), cv2.DIST_L2, 5)
    dist_water_m = dist_to_water * (WORLD_SIZE / (HEIGHTMAP_RES - 1))
    valley_taper = np.clip(dist_water_m / 80.0, 0.0, 1.0)

    # Sculpt natural fluvial channels directly into elevation
    land_elev = np.maximum(0.1, elev_aa - valleys * 0.75 * valley_taper)
    print(f"    River network sculpted in {time.time()-t_val:.2f}s.")

    # 7. Coastal Rise for Flat Lowlands
    coastal_rise = np.clip(dist_water_m / 350.0, 0.0, 1.0) * 3.5
    land_elev = np.maximum(land_elev, coastal_rise)

    # 8. Slope-Modulated Sedimentary Strata & Micro-Relief
    print("[*] Synthesizing slope-modulated sedimentary strata and micro-relief...")
    gx = cv2.Sobel(land_elev, cv2.CV_32F, 1, 0, ksize=3)
    gy = cv2.Sobel(land_elev, cv2.CV_32F, 0, 1, ksize=3)
    slope_mag = np.sqrt(gx**2 + gy**2)

    slope_weight = np.clip((slope_mag - 0.03) / 0.10, 0.0, 1.0)

    np.random.seed(1337)
    noise_med = cv2.GaussianBlur(np.random.randn(HEIGHTMAP_RES // 4, HEIGHTMAP_RES // 4).astype(np.float32), (0, 0), sigmaX=2.0, sigmaY=2.0)
    noise_med = cv2.resize(noise_med, (HEIGHTMAP_RES, HEIGHTMAP_RES), interpolation=cv2.INTER_CUBIC)

    noise_fine = cv2.GaussianBlur(np.random.randn(HEIGHTMAP_RES // 2, HEIGHTMAP_RES // 2).astype(np.float32), (0, 0), sigmaX=1.2, sigmaY=1.2)
    noise_fine = cv2.resize(noise_fine, (HEIGHTMAP_RES, HEIGHTMAP_RES), interpolation=cv2.INTER_CUBIC)

    layer_thickness = 5.5
    strata_phase = (land_elev / layer_thickness) * 2.0 * math.pi + noise_med * 1.5
    strata = np.sin(strata_phase) * 0.7 + np.sin(strata_phase * 2.0) * 0.3
    strata_steps = np.sign(strata) * (np.abs(strata) ** 0.75)
    rills = noise_med * 0.55 + noise_fine * 0.45

    road_dist = cv2.distanceTransform((road_user < 20).astype(np.uint8), cv2.DIST_L2, 3)
    road_m = road_dist * (WORLD_SIZE / (HEIGHTMAP_RES - 1))
    road_smooth_weight = np.clip(road_m / 25.0, 0.0, 1.0)

    micro_detail = (strata_steps * 1.1 + rills * 0.7) * slope_weight * road_smooth_weight * valley_taper
    land_elev = np.maximum(0.1, land_elev + micro_detail)

    # 9. Smooth Beach Transition
    t_beach = np.clip(dist_water_m / 50.0, 0.0, 1.0)
    smooth_beach = t_beach * t_beach * (3.0 - 2.0 * t_beach)
    land_elev = np.maximum(0.1, land_elev * smooth_beach)

    # 10. Offshore Bathymetry
    print("[*] Generating smooth offshore bathymetric seabed...")
    dist_sea = cv2.distanceTransform(clean_sea.astype(np.uint8), cv2.DIST_L2, 5)
    dist_sea_m = dist_sea * (WORLD_SIZE / (HEIGHTMAP_RES - 1))

    max_shelf_depth = 21.5 # -48.5m - (-70.0m)
    u_sea = np.clip(dist_sea_m / 50.0, 0.0, 1.0)
    w_shelf = u_sea * u_sea * (3.0 - 2.0 * u_sea)
    d_near = 1.8 * (u_sea ** 1.3)
    d_shelf = np.clip(0.35 * np.sqrt(np.maximum(0.0, dist_sea_m)) + 0.007 * dist_sea_m, 0.0, max_shelf_depth)
    shelf_depth = (1.0 - w_shelf) * d_near + w_shelf * d_shelf

    game_h = np.where(land_mask, WATER_LEVEL + land_elev, WATER_LEVEL - shelf_depth)

    waterline_band = (dist_water_m < 15.0) | ((dist_sea_m < 15.0) & clean_sea)
    if np.any(waterline_band):
        h_waterline_smooth = cv2.GaussianBlur(game_h, (9, 9), 1.8)
        band_dist = np.where(land_mask, dist_water_m, dist_sea_m)
        band_weight = np.clip(1.0 - band_dist / 15.0, 0.0, 1.0)
        bw = band_weight * band_weight * (3.0 - 2.0 * band_weight) * 0.7
        game_h[waterline_band] = (1.0 - bw[waterline_band]) * game_h[waterline_band] + bw[waterline_band] * h_waterline_smooth[waterline_band]

    # Inland lake basins: flat shallow lakebed at -49.2m
    game_h[lake_mask] = -49.2

    # 11. Sculpt Historical Sousse Port (Pixel-Perfect Topological Flood-Fill from 1:1 User Drawing)
    print("[*] Sculpting authentic historical Sousse Port with 100% pixel-perfect alignment to user drawing...")
    port_canvas = np.zeros((HEIGHTMAP_RES, HEIGHTMAP_RES), dtype=np.uint8)
    port_canvas[3400:4600, 5100:5800] = (road_user[3400:4600, 5100:5800] > 25).astype(np.uint8)

    # Close the harbor entrance mouth gate ONLY on temporary ff_canvas for topological flood-fill
    ff_canvas = port_canvas.copy()
    cv2.line(ff_canvas, (5610, 4044), (5634, 4079), 1, thickness=5)

    # Topological flood fill from inside the harbor basin (seed: col 5500, row 4100)
    ff_mask = np.zeros((HEIGHTMAP_RES + 2, HEIGHTMAP_RES + 2), dtype=np.uint8)
    cv2.floodFill(ff_canvas, ff_mask, (5500, 4100), 2)
    basin_mask = (ff_canvas == 2)
    print(f"    Extracted exact harbor water basin: {np.sum(basin_mask):,} pixels.")

    # Solid stone docks & breakwater moles from user drawn pier contours
    # Dilate pier strokes with 21x21 ellipse to form solid stone piers
    dock_strokes = (port_canvas == 1).astype(np.uint8)
    dock_solid = cv2.dilate(dock_strokes, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (21, 21))) > 0

    # Fill internal hollow inside of South breakwater mole (cols 5450..5650, rows 4050..4350)
    sbw_sub = dock_solid[4050:4350, 5450:5650].copy()
    sbw_inv = 1 - sbw_sub.astype(np.uint8)
    n_lbl, lbls, stats, cents = cv2.connectedComponentsWithStats(sbw_inv)
    for l in range(1, n_lbl):
        if stats[l, cv2.CC_STAT_AREA] < 8000:
            sbw_sub[lbls == l] = 1
    dock_solid[4050:4350, 5450:5650] = sbw_sub

    # Dredged water basin has strict priority (zero dock intrusion into water)
    dock_solid[basin_mask] = False

    # Open Navigable Harbor Entrance Mouth Channel:
    # 110m wide channel between North pierhead (row 4044) and South pierhead (row 4079)
    # connecting the harbor basin directly to the open Mediterranean Sea
    r_mouth_start = 4046
    r_mouth_end   = 4078
    c_mouth_start = 5570
    c_mouth_end   = 5750
    mouth_mask = np.zeros((HEIGHTMAP_RES, HEIGHTMAP_RES), dtype=bool)
    mouth_mask[r_mouth_start:r_mouth_end, c_mouth_start:c_mouth_end] = True

    # Clear entrance channel completely of breakwater stone
    dock_solid[mouth_mask] = False
    print(f"    Extracted solid stone quays and breakwater moles: {np.sum(dock_solid):,} pixels.")

    # Combined port and urban land for coastline enforcement
    port_land = dock_solid | (urban_land & ~basin_mask & ~mouth_mask)

    # Strict Coastline Enforcement across Sousse (rows 3400 to 4600)
    # Eliminates DEM 0m noise holes, sand donut mounds, and offshore islands
    for r in range(3400, 4600):
        cols_land = np.where(port_land[r, :])[0]
        if len(cols_land) > 0:
            c_max = cols_land.max()
            clean_sea[r, c_max+1:] = True
            clean_sea[r, :c_max+1] = basin_mask[r, :c_max+1] | mouth_mask[r, :c_max+1]

    # Morphological closing removes isolated 1-pixel DEM speckles in the open Mediterranean
    clean_sea = cv2.morphologyEx(clean_sea.astype(np.uint8), cv2.MORPH_CLOSE, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (11, 11))) > 0

    # Breakwater crests (moles extending into Mediterranean east of col 5450)
    cols_grid = np.tile(np.arange(HEIGHTMAP_RES, dtype=np.int32), (HEIGHTMAP_RES, 1))
    bw_crest = dock_solid & (cols_grid >= 5450)
    quay_stone = dock_solid & ~bw_crest

    # Full Port & Quayside Waterfront District Leveling:
    # Level all parcel blocks and land between the Medina eastern boulevard (col ~5095)
    # and the harbor waterfront to -46.5m, eliminating all isolated green mesa/island remnants.
    west_nodes = [
        (3650, 5115),
        (3750, 5100),
        (3900, 5095),
        (4050, 5095),
        (4150, 5090),
        (4250, 5080),
        (4350, 5075)
    ]
    r_arr = np.array([pt[0] for pt in west_nodes], dtype=np.float32)
    c_arr = np.array([pt[1] for pt in west_nodes], dtype=np.float32)
    all_port_rows = np.arange(3650, 4351)
    c_west_interp = np.interp(all_port_rows, r_arr, c_arr)

    port_district = np.zeros((HEIGHTMAP_RES, HEIGHTMAP_RES), dtype=bool)
    for r, cw in zip(all_port_rows, c_west_interp):
        cw_int = int(round(cw))
        port_district[r, cw_int:5550] = True

    # District land excludes the dredged basin, breakwaters, and open Mediterranean sea
    district_land = port_district & ~basin_mask & ~mouth_mask & ~clean_sea & (cols_grid < 5550)

    # Combine district land with stone quays
    full_quays = quay_stone | district_land

    # Sculpt elevations:
    # Ensure water/land classification reflects port structures
    clean_sea[dock_solid | district_land] = False
    clean_sea[basin_mask | mouth_mask] = True
    water_mask = clean_sea | lake_mask
    land_mask = ~water_mask

    # Re-apply seabed bathymetry for all clean sea pixels (strictly eliminates all sand donut mounds & islands)
    dist_sea = cv2.distanceTransform(clean_sea.astype(np.uint8), cv2.DIST_L2, 5)
    dist_sea_m = dist_sea * (WORLD_SIZE / (HEIGHTMAP_RES - 1))
    u_sea = np.clip(dist_sea_m / 50.0, 0.0, 1.0)
    w_shelf = u_sea * u_sea * (3.0 - 2.0 * u_sea)
    d_near = 1.8 * (u_sea ** 1.3)
    d_shelf = np.clip(0.35 * np.sqrt(np.maximum(0.0, dist_sea_m)) + 0.007 * dist_sea_m, 0.0, max_shelf_depth)
    shelf_depth = (1.0 - w_shelf) * d_near + w_shelf * d_shelf

    sea_open = clean_sea & ~basin_mask & ~mouth_mask & ~dock_solid & ~district_land
    game_h[sea_open] = WATER_LEVEL - np.maximum(0.8, shelf_depth[sea_open])
    game_h[basin_mask | mouth_mask] = -53.0
    game_h[full_quays] = -46.5
    game_h[bw_crest] = -46.0

    # Smooth Hermite Ramp at the Medina Western Boundary:
    # Blends terrain over a 35-pixel wide zone from the Medina hill down to -46.5m at the port boundary
    ramp_width = 35
    for r, cw in zip(all_port_rows, c_west_interp):
        cw_int = int(round(cw))
        c_start = cw_int - ramp_width
        h_hill = game_h[r, c_start]
        for c in range(c_start, cw_int):
            u = float(c - c_start) / float(ramp_width)
            w = u * u * (3.0 - 2.0 * u)
            game_h[r, c] = (1.0 - w) * h_hill + w * (-46.5)

    # Smooth the ramp zone slightly with Gaussian blur to ensure seamless contours
    ramp_crop = game_h[3640:4360, 5030:5120]
    ramp_smooth = cv2.GaussianBlur(ramp_crop, (11, 11), 2.0)
    game_h[3640:4360, 5030:5120] = ramp_smooth

    # Smooth harbor perimeter transition into surrounding terrain (land-side only, never into sea)
    harbor_total = dock_solid | district_land | basin_mask
    harbor_dilated = cv2.dilate(harbor_total.astype(np.uint8), cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (15, 15))) > 0
    harbor_border = harbor_dilated & ~harbor_total & land_mask
    if np.any(harbor_border):
        h_harbor_smooth = cv2.GaussianBlur(game_h, (15, 15), 3.0)
        game_h[harbor_border] = 0.5 * game_h[harbor_border] + 0.5 * h_harbor_smooth[harbor_border]

    # Absolute guarantee: all sea pixels (outside stone docks and district) must be submerged
    game_h[clean_sea & ~dock_solid & ~district_land] = np.minimum(game_h[clean_sea & ~dock_solid & ~district_land], WATER_LEVEL - 0.5)

    # Save textureMap.png (portMask) - Multi-zone historic harbor texturing
    # Red channel: Port Quays, Wharfs & Esplanade (Cobblestone & Ashlar Dock Walls)
    # Green channel: Breakwater Moles & Pierheads (Ashlar Stone Masonry Blocks)
    # Blue channel: Outer Breakwater Riprap Armor (Coastal Armor Boulders in Sea)
    print(f"[*] Generating textureMap.png (portMask) to {texturemap_path}...")
    bw_all = cv2.dilate(bw_crest.astype(np.uint8), cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (35, 35))) > 0
    bw_all[mouth_mask] = False
    bw_riprap = bw_all & ~bw_crest

    quays_all = cv2.dilate(full_quays.astype(np.uint8), cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (9, 9))) > 0
    quays_all[mouth_mask] = False
    full_quays_expanded = quays_all & ~bw_crest & ~bw_riprap

    tex_map = np.zeros((HEIGHTMAP_RES, HEIGHTMAP_RES, 4), dtype=np.uint8)
    tex_map[:, :, 0] = full_quays_expanded.astype(np.uint8) * 255
    tex_map[:, :, 1] = bw_crest.astype(np.uint8) * 255
    tex_map[:, :, 2] = bw_riprap.astype(np.uint8) * 255
    tex_map[:, :, 3] = 255
    Image.fromarray(tex_map, mode='RGBA').save(texturemap_path)
    print(f"    Port masks exported: {np.sum(full_quays_expanded):,} quayside pixels, {np.sum(bw_crest):,} breakwater pixels, {np.sum(bw_riprap):,} riprap pixels.")

    # 12. Combine Road Network & Grade 3D Roadbed Camber (Authentic Vehicle Scale)
    print("[*] Generating authentic historical road network & 3D roadbed camber...")
    raw_mask = (road_user > 30).astype(np.uint8)
    ys_r, xs_r = np.where(raw_mask > 0)
    pad_r = 15
    ymin_r, ymax_r = max(0, ys_r.min() - pad_r), min(raw_mask.shape[0], ys_r.max() + pad_r)
    xmin_r, xmax_r = max(0, xs_r.min() - pad_r), min(raw_mask.shape[1], xs_r.max() + pad_r)
    crop_mask = raw_mask[ymin_r:ymax_r, xmin_r:xmax_r].copy()

    # Original stroke radius to scale road hierarchy (avenues vs side paths)
    dist_orig_crop = cv2.distanceTransform(crop_mask, cv2.DIST_L2, 5)

    # Zhang-Suen thinning preserves 100% topological connectivity
    def zhang_suen_thin(img):
        im = img.copy().astype(np.uint8)
        while True:
            p = np.pad(im, 1, mode='constant')
            P2, P3, P4 = p[:-2, 1:-1], p[:-2, 2:], p[1:-1, 2:]
            P5, P6, P7 = p[2:, 2:], p[2:, 1:-1], p[2:, :-2]
            P8, P9 = p[1:-1, :-2], p[:-2, :-2]
            B = P2 + P3 + P4 + P5 + P6 + P7 + P8 + P9
            A = ((P2 == 0) & (P3 == 1)).astype(np.uint8) + \
                ((P3 == 0) & (P4 == 1)).astype(np.uint8) + \
                ((P4 == 0) & (P5 == 1)).astype(np.uint8) + \
                ((P5 == 0) & (P6 == 1)).astype(np.uint8) + \
                ((P6 == 0) & (P7 == 1)).astype(np.uint8) + \
                ((P7 == 0) & (P8 == 1)).astype(np.uint8) + \
                ((P8 == 0) & (P9 == 1)).astype(np.uint8) + \
                ((P9 == 0) & (P2 == 1)).astype(np.uint8)
            C1 = (im == 1) & (B >= 2) & (B <= 6) & (A == 1) & ((P2 * P4 * P6) == 0) & ((P4 * P6 * P8) == 0)
            im[C1] = 0

            p = np.pad(im, 1, mode='constant')
            P2, P3, P4 = p[:-2, 1:-1], p[:-2, 2:], p[1:-1, 2:]
            P5, P6, P7 = p[2:, 2:], p[2:, 1:-1], p[2:, :-2]
            P8, P9 = p[1:-1, :-2], p[:-2, :-2]
            B = P2 + P3 + P4 + P5 + P6 + P7 + P8 + P9
            A = ((P2 == 0) & (P3 == 1)).astype(np.uint8) + \
                ((P3 == 0) & (P4 == 1)).astype(np.uint8) + \
                ((P4 == 0) & (P5 == 1)).astype(np.uint8) + \
                ((P5 == 0) & (P6 == 1)).astype(np.uint8) + \
                ((P6 == 0) & (P7 == 1)).astype(np.uint8) + \
                ((P7 == 0) & (P8 == 1)).astype(np.uint8) + \
                ((P8 == 0) & (P9 == 1)).astype(np.uint8) + \
                ((P9 == 0) & (P2 == 1)).astype(np.uint8)
            C2 = (im == 1) & (B >= 2) & (B <= 6) & (A == 1) & ((P2 * P4 * P8) == 0) & ((P2 * P6 * P8) == 0)
            im[C2] = 0
            if not np.any(C1) and not np.any(C2):
                break
        return im

    thinned_crop = zhang_suen_thin(crop_mask)
    ys_s, xs_s = np.where(thinned_crop > 0)
    coords_s = set(zip(ys_s, xs_s))

    # Connect 8-connected skeleton neighbors with continuous anti-aliased line segments
    # Eliminates bead-necklace scalloping and discrete pixel stair-stepping!
    # Road hierarchy widths (1899 historical scale matching car & wagon ruts):
    # - Main colonial avenues & coastal boulevards: thickness 4 (~12-14m wide)
    # - Standard countryside & town roads: thickness 3 (~8-10m wide)
    # - Minor Medina alleys & tracks: thickness 2 (~6-7m wide)
    connected_ribbon = np.zeros_like(crop_mask, dtype=np.uint8)
    for y, x in zip(ys_s, xs_s):
        r_orig = dist_orig_crop[y, x]
        th = 4 if r_orig > 7.0 else (3 if r_orig > 4.0 else 2)
        cv2.circle(connected_ribbon, (x, y), max(1, th // 2), 255, -1)
        for dy in [-1, 0, 1]:
            for dx in [-1, 0, 1]:
                if (dy != 0 or dx != 0) and (y + dy, x + dx) in coords_s:
                    cv2.line(connected_ribbon, (x, y), (x + dx, y + dy), 255, thickness=th, lineType=cv2.LINE_AA)

    # Sub-pixel continuous Gaussian anti-aliasing for organic distance falloff
    smooth_ribbon = cv2.GaussianBlur(connected_ribbon, (7, 7), sigmaX=1.0, sigmaY=1.0)
    final_road_layer = np.zeros((HEIGHTMAP_RES, HEIGHTMAP_RES), dtype=np.uint8)
    final_road_layer[ymin_r:ymax_r, xmin_r:xmax_r] = smooth_ribbon

    # 3D Physical Roadbed Camber Grading into heightmap
    print("[*] Grading 3D physical roadbed bench with convex camber crown (+15cm)...")
    crop_h = game_h[ymin_r:ymax_r, xmin_r:xmax_r]
    h_long_smooth = cv2.GaussianBlur(crop_h, (15, 15), sigmaX=3.0, sigmaY=3.0)

    basin_crop = basin_mask[ymin_r:ymax_r, xmin_r:xmax_r]
    road_weight = (smooth_ribbon.astype(np.float32) / 255.0) * (~basin_crop)

    # Graded driving bench with convex camber crown (+15cm in center)
    h_bench = h_long_smooth + 0.15 * (road_weight ** 1.5)
    w_road = road_weight * 0.85
    crop_h = (1.0 - w_road) * crop_h + w_road * h_bench
    game_h[ymin_r:ymax_r, xmin_r:xmax_r] = crop_h

    # Railway lines in 1:1 user space (narrow single-track ballast)
    rail_layer = np.zeros((HEIGHTMAP_RES, HEIGHTMAP_RES), dtype=np.uint8)
    rail_pts_main = np.array([
        (5250, 2000), (5250, 2800), (5250, 3400), (5250, 3680),
        (5220, 4200), (5200, 4800), (5180, 5600), (5150, 6500)
    ], dtype=np.int32)
    cv2.polylines(rail_layer, [rail_pts_main], isClosed=False, color=255, thickness=1, lineType=cv2.LINE_AA)

    rail_pts_port = np.array([
        (5250, 3680), (5300, 3750), (5350, 3850), (5400, 3950), (5450, 4000)
    ], dtype=np.int32)
    cv2.polylines(rail_layer, [rail_pts_port], isClosed=False, color=255, thickness=1, lineType=cv2.LINE_AA)

    # Guarantee all active land and quayside roads are safely above water level
    active_roads = (final_road_layer > 20) & ~basin_mask
    game_h[active_roads & (game_h < -47.0)] = -46.5

    # 13. Save roadMap.png: Red=Roads, Green=Railways, Blue=0 (NO synthetic river mask), Alpha=0
    print(f"[*] Saving roadMap.png to {roadmap_path}...")
    out_rm = np.zeros((HEIGHTMAP_RES, HEIGHTMAP_RES, 4), dtype=np.uint8)
    out_rm[:, :, 0] = final_road_layer
    out_rm[:, :, 1] = rail_layer
    out_rm[:, :, 2] = 0 # Zero synthetic river mask (strictly detected from DEM)
    out_rm[:, :, 3] = 0
    Image.fromarray(out_rm, mode='RGBA').save(roadmap_path)

    # 14. Save 16-Bit Heightmap
    game_h = np.clip(game_h, MIN_H, MAX_H)
    print(f"[*] Encoding and saving 16-bit heightmap to {heightmap_path}...")
    norm_16 = np.clip((game_h - MIN_H) / H_RANGE * 65535.0, 0.0, 65535.0).astype(np.uint16)
    Image.fromarray(norm_16).save(heightmap_path)
    print(f"    Saved {heightmap_path} ({os.path.getsize(heightmap_path)/1024/1024:.2f} MB).")

    # 15. Re-bake all objects in city.txt with exact row/col lookup
    print(f"[*] Re-baking object elevations in {city_path}...")
    if os.path.exists(city_path):
        with open(city_path, "r", encoding="utf-8") as f:
            lines = f.readlines()

        updated_count = 0
        new_lines = []
        for line in lines:
            parts = line.strip().split()
            if len(parts) >= 6 and (parts[0] == "OBJ_V6" or parts[0] == "OBJ_V5"):
                try:
                    lx = float(parts[2])
                    lz = float(parts[3])
                    # SousseMapData::toWorld: WORLD_ANCHOR = {-530.0f, -1502.0f}
                    wx = -530.0 + lx
                    wz = -1502.0 + lz

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

    # 16. Key Location Elevations Verification
    def get_elev(c, r):
        c = np.clip(c, 0, HEIGHTMAP_RES - 1)
        r = np.clip(r, 0, HEIGHTMAP_RES - 1)
        return float(game_h[r, c])

    print("\n[*] ==================================================================")
    print("[*] Key Locations Verification (Water Level = -48.50m):")
    print(f"    - Sousse Kasbah (c=4950, r=4200)   : {get_elev(4950, 4200):.2f}m (ASL: {get_elev(4950, 4200) - WATER_LEVEL:+.2f}m) -> Acropolis Hill!")
    print(f"    - Place Pichon (c=5310, r=3800)    : {get_elev(5310, 3800):.2f}m (ASL: {get_elev(5310, 3800) - WATER_LEVEL:+.2f}m) -> City Center Slope!")
    print(f"    - Port Quays (c=5350, r=4000)      : {get_elev(5350, 4000):.2f}m (ASL: {get_elev(5350, 4000) - WATER_LEVEL:+.2f}m) -> Level Stone Wharfs (+2.0m ASL)!")
    print(f"    - Harbor Water Basin (c=5500, r=4050): {get_elev(5500, 4050):.2f}m (ASL: {get_elev(5500, 4050) - WATER_LEVEL:+.2f}m) -> Dredged Harbor Basin (-4.5m)!")
    print(f"    - Breakwater Tip (c=5690, r=3912)  : {get_elev(5690, 3912):.2f}m (ASL: {get_elev(5690, 3912) - WATER_LEVEL:+.2f}m) -> Jetée Nord (+2.5m ASL)!")
    print(f"    - Beach / Corniche (c=5280, r=3350): {get_elev(5280, 3350):.2f}m (ASL: {get_elev(5280, 3350) - WATER_LEVEL:+.2f}m) -> Dry Coastal Beach!")
    print(f"    - Western Hills (c=2000, r=4000)   : {get_elev(2000, 4000):.2f}m (ASL: {get_elev(2000, 4000) - WATER_LEVEL:+.2f}m) -> Rich Strata Ridge!")
    print("[*] ==================================================================\n")

    # 17. Render 2K Hillshade Preview
    print("[*] Rendering 2K hillshade preview...")
    preview_res = 2048
    h_prev = cv2.resize(game_h, (preview_res, preview_res), interpolation=cv2.INTER_AREA)

    gx_p = cv2.Sobel(h_prev, cv2.CV_32F, 1, 0, ksize=3)
    gy_p = cv2.Sobel(h_prev, cv2.CV_32F, 0, 1, ksize=3)
    sun_az = 315.0 * math.pi / 180.0
    sun_alt = 45.0 * math.pi / 180.0
    lx = math.cos(sun_alt) * math.sin(sun_az)
    ly = -math.cos(sun_alt) * math.cos(sun_az)
    lz = math.sin(sun_alt)
    scale = 0.05
    nx = -gx_p * scale
    ny = -gy_p * scale
    nz = np.ones_like(h_prev)
    norm = np.sqrt(nx**2 + ny**2 + nz**2)
    shade = (nx/norm)*lx + (ny/norm)*ly + (nz/norm)*lz
    shade = np.clip((shade + 1.0) / 2.0 * 255.0, 0, 255).astype(np.uint8)

    preview_path = os.path.join(scratch_dir, "master_satellite_reproject_2k.png")
    cv2.imwrite(preview_path, shade)
    print(f"[+] Saved hillshade preview to {preview_path}.")
    print(f"[+] Master satellite reprojection completed successfully in {time.time()-t0:.2f}s.")

if __name__ == "__main__":
    main()
