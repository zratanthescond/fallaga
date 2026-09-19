#!/usr/bin/env python3
"""
tools/build_authentic_historical_heightmap.py
=============================================
Reconstructs the authentic historical heightmap for Sousse (1930s-1943) and its surrounding world.

Strict Principles:
1. Pure Natural World Topography:
   - Uses the clean Copernicus 30m DEM baseline (assets/terrain/heightmap_before_roads.png).
   - Preserves 100% of the natural rolling Sahel hills, valleys, ridges, and wadis.
   - Preserves the natural curved Mediterranean coastline (Plage Boujaafar to north, Monastir to south).
   - Keeps the eastern Mediterranean Sea deep and open (no artificial land barriers or trenches).
2. Authentic Historical Sousse City & Port:
   - Sousse Medina hill slopes gently down to the coast with a natural 2D organic gradient over 650+ meters.
     Zero horizontal stripes, zero rectangular borders, zero artificial sand-dune ramps.
   - Place Pichon & Quai de la Marine terrace sits flat at -46.5m (+2.0m above -48.5m sea level).
   - Dredged harbor basin floor at -53.5m (5.0m draft below sea level) with anti-aliased underwater bulkheads.
   - 1943 USAAF Photogrammetric Piers: Grand Môle Sud (39° NE, 144m wide), Causeway, Jetée Nord,
     and Gare Maritime finger dock leveled solid at -46.5m.
   - Plage Boujaafar: natural gentle sand beach gradient into the bay north of Place Pichon.
3. Zero Network Consumption: 100% local computation using local Python and existing assets.
"""

import os
import sys
import time
import cv2
import numpy as np
from PIL import Image

Image.MAX_IMAGE_PIXELS = None

# Engine Constants
MIN_H = -70.0
MAX_H =  87.8
H_RANGE = MAX_H - MIN_H
WATER_LEVEL = -48.5
QUAY_ALT    = -46.5  # +2.0m above sea level
BASIN_DEPTH = -53.5  # 5.0m water depth below sea level
TOTAL_RES   = 8129

def u16_to_h(arr):
    return MIN_H + (arr.astype(np.float32) / 65535.0) * H_RANGE

def h_to_u16(arr):
    norm = np.clip((arr - MIN_H) / H_RANGE, 0.0, 1.0)
    return np.clip(norm * 65535.0 + 0.5, 0, 65535).astype(np.uint16)

def main():
    t0 = time.time()
    print("==================================================================")
    print("[*] STEP 1: Loading clean natural baseline DEM...")
    print("==================================================================")
    base_path = "assets/terrain/heightmap_before_roads.png"
    if not os.path.exists(base_path):
        base_path = "assets/terrain/heightmap_backup_pre_satellite_rebuild.png"
    
    print(f"    Source: {base_path}")
    hm_base_pil = Image.open(base_path)
    hm_base_u16 = np.array(hm_base_pil, dtype=np.uint16)
    h_m = u16_to_h(hm_base_u16)
    print(f"    Loaded baseline elevation range: [{h_m.min():.2f}m .. {h_m.max():.2f}m]")

    print("\n==================================================================")
    print("[*] STEP 2: Defining Authentic 1943 USAAF Harbor & Waterfront Polygons...")
    print("==================================================================")
    cw_poly = np.array([
        [5265, 4338],
        [5340, 4305],
        [5420, 4268],
        [5445, 4288],
        [5365, 4325],
        [5290, 4358],
        [5265, 4368]
    ], dtype=np.int32)

    mole_poly = np.array([
        [5420, 4268], # Inner base
        [5455, 4225],
        [5490, 4180],
        [5525, 4138],
        [5538, 4118], # Inner pierhead corner
        [5548, 4108], # Bullnose apex
        [5562, 4110],
        [5572, 4122], # Outer pierhead corner
        [5560, 4148],
        [5525, 4192],
        [5490, 4238],
        [5455, 4282],
        [5445, 4288], # Outer base
        [5420, 4268]
    ], dtype=np.int32)

    gare_poly = np.array([
        # Gare Maritime finger dock:
        [5510, 3975], # West berth start
        [5510, 4020],
        [5510, 4060],
        [5510, 4080], # West tip corner
        [5520, 4090], # Rounded tip
        [5535, 4090],
        [5542, 4080], # East tip corner
        [5542, 4060],
        [5542, 4010],
        [5542, 3985], # East berth end
        # North breakwater (Jetee Nord):
        [5570, 3990],
        [5620, 4005],
        [5670, 4020], # Breakwater tip
        [5672, 4005],
        [5620, 3985],
        [5560, 3970],
        # North platform & Place Pichon / Terre-Plein du Port:
        [5560, 3910],
        [5520, 3865],
        [5460, 3865],
        [5380, 3865],
        [5320, 3865],
        [5245, 3865], # Place Pichon NW
        [5245, 3945], # Quai Nord base
        [5380, 3958], # Quai Nord wharf
        [5450, 3968],
        [5510, 3975]
    ], dtype=np.int32)

    basin_poly = np.array([
        [5265, 3945], # NW corner
        [5380, 3958],
        [5450, 3968],
        [5510, 3975], # North wharf
        [5510, 4080], # Gare Maritime west berth
        [5520, 4090], # Dock tip
        [5548, 4108], # Entrance channel across to Grand Mole
        [5538, 4118],
        [5455, 4225],
        [5420, 4268], # Grand Mole inner wharf
        [5340, 4305],
        [5265, 4338], # Causeway north edge
        [5265, 3945]  # Quai de la Marine
    ], dtype=np.int32)

    # 1. Piers and Quays Mask
    mask_piers = np.zeros((TOTAL_RES, TOTAL_RES), dtype=np.uint8)
    cv2.fillPoly(mask_piers, [mole_poly], 255)
    cv2.fillPoly(mask_piers, [cw_poly], 255)
    cv2.fillPoly(mask_piers, [gare_poly], 255)
    cv2.rectangle(mask_piers, (5245, 3880), (5268, 4368), 255, -1)
    cv2.rectangle(mask_piers, (5235, 3865), (5275, 3915), 255, -1)

    # 2. Dredged Basin Mask
    mask_basin = np.zeros((TOTAL_RES, TOTAL_RES), dtype=np.uint8)
    cv2.fillPoly(mask_basin, [basin_poly], 255)

    print("\n==================================================================")
    print("[*] STEP 3: Organic 2D Urban Hillside Easing (Medina -> Quayside)...")
    print("==================================================================")
    # Organic 2D smooth blending between the Medina hill and the Quayside terrace.
    # We define a 2D influence field where the Quayside terrace is pinned at QUAY_ALT (-46.5m)
    # and smoothly merges with the Medina terrain using 2D distance-transform easing.
    R0, R1 = 3700, 4500
    C0, C1 = 4950, 5350
    sub_h = h_m[R0:R1, C0:C1].copy()
    sub_piers = mask_piers[R0:R1, C0:C1]

    # Quayside terrace core in sub-coordinates:
    terrace_core = np.zeros_like(sub_h, dtype=np.float32)
    # Quayside strip + Place Pichon in sub-coords
    c_p0, c_p1 = 5245 - C0, 5275 - C0
    r_p0, r_p1 = 3865 - R0, 4370 - R0
    terrace_core[r_p0:r_p1, c_p0:c_p1] = 1.0

    # 2D Gaussian blur with a broad organic kernel (sigma=60, ~200m) to generate a seamless 2D easing field
    weight_2d = cv2.GaussianBlur(terrace_core, (201, 201), 60.0)
    weight_2d = np.clip(weight_2d / max(weight_2d.max(), 1e-5), 0.0, 1.0)
    # Cubic smoothstep
    weight_2d = weight_2d * weight_2d * (3.0 - 2.0 * weight_2d)

    # Target elevation: smooth descent from natural Medina down to QUAY_ALT
    # Where weight_2d > 0, blend natural terrain with QUAY_ALT
    eased_sub = sub_h * (1.0 - weight_2d) + QUAY_ALT * weight_2d
    # Core terrace strictly at QUAY_ALT
    eased_sub[terrace_core > 0.5] = QUAY_ALT
    # Apply back to heightmap
    h_m[R0:R1, C0:C1] = eased_sub
    print("    [+] Organic 2D urban slope created. Zero horizontal bands or artificial ramps.")

    print("\n==================================================================")
    print("[*] STEP 4: Applying Dredged Basin & Solid Pier Decks...")
    print("==================================================================")
    # Anti-aliased sub-pixel pier and basin boundaries
    piers_f = cv2.GaussianBlur(mask_piers.astype(np.float32) / 255.0, (5, 5), 1.2)
    basin_f = cv2.GaussianBlur(mask_basin.astype(np.float32) / 255.0, (5, 5), 1.2)
    basin_water_mask = np.clip(basin_f - piers_f * 1.5, 0.0, 1.0)

    # Apply dredged basin floor (-53.5m, 5.0m draft below -48.5m water)
    h_m = h_m * (1.0 - basin_water_mask) + BASIN_DEPTH * basin_water_mask

    # Apply solid pier decks & quayside terrace (-46.5m, +2.0m above sea level)
    h_m = h_m * (1.0 - piers_f) + QUAY_ALT * piers_f

    print("    [+] Pier decks set to -46.5m. Dredged basin floor set to -53.5m.")

    print("\n==================================================================")
    print("[*] STEP 5: Natural Coastline, Beaches & Open Mediterranean Water...")
    print("==================================================================")
    # 1. Plage Boujaafar (North of Place Pichon: rows 3700 to 3865):
    # Transition Place Pichon terrace smoothly into the natural sandy beach.
    for r in range(3700, 3865):
        t_beach = (3865 - r) / (3865 - 3700)
        c_coast_target = int(5265 * (1.0 - t_beach) + 5320 * t_beach)
        slope_start = c_coast_target - 15
        slope_end = c_coast_target + 15
        slope_t = np.linspace(0.0, 1.0, slope_end - slope_start)
        target_beach = QUAY_ALT * (1.0 - slope_t) + (-50.5) * slope_t
        blend_factor = 1.0 - t_beach * 0.8
        h_m[r, slope_start:slope_end] = (
            h_m[r, slope_start:slope_end] * (1.0 - blend_factor) + 
            target_beach * blend_factor
        )

    # 2. Causeway & South Pier Footing into Natural South Coast (rows 4368 to 4450):
    for r in range(4368, 4450):
        t_south = (r - 4368) / (4450 - 4368)
        c_quay_end = int(5265 * (1.0 - t_south) + 5570 * t_south)
        if c_quay_end < 5570:
            h_m[r, 5245:c_quay_end] = np.maximum(
                h_m[r, 5245:c_quay_end], 
                QUAY_ALT * (1.0 - t_south) + h_m[r, 5245:c_quay_end] * t_south
            )

    # 3. Outer Pier Faces Seaward Armor Slope:
    dist_outside_piers = cv2.distanceTransform(255 - mask_piers, cv2.DIST_L2, 5)
    pier_armor_zone = (dist_outside_piers > 0) & (dist_outside_piers <= 8) & (basin_f < 0.2)
    armor_t = np.clip(dist_outside_piers / 8.0, 0.0, 1.0)
    target_armor_slope = QUAY_ALT * (1.0 - armor_t) + (-52.0) * armor_t
    h_m = np.where(pier_armor_zone & (h_m > target_armor_slope), target_armor_slope, h_m)

    # 4. Open Mediterranean Sea Guarantee:
    # Any open sea east of the piers and natural coastline is guaranteed deep open water (<= -50.5m).
    # Zero artificial sandbars, zero land strips blocking the sea.
    print("  - Guaranteeing deep open water across eastern Mediterranean...")
    for r in range(TOTAL_RES):
        row_piers = np.where(mask_piers[r, :] > 0)[0]
        max_pier_col = np.max(row_piers) if len(row_piers) > 0 else 0
        
        land_cols = np.where(hm_base_u16[r, :] > 0)[0]
        max_natural_col = np.max(land_cols) if len(land_cols) > 0 else 0

        coastal_edge = max(max_pier_col, max_natural_col)
        offshore_col = coastal_edge + 15
        if offshore_col < TOTAL_RES:
            h_m[r, offshore_col:] = np.minimum(h_m[r, offshore_col:], -52.0)

    # Local bilateral filter across the urban transition to eliminate any fine quantization terracing
    print("  - Applying edge-preserving smoothing across urban transition...")
    crop_h = h_m[3600:4600, 5000:5800]
    crop_filtered = cv2.bilateralFilter(crop_h.astype(np.float32), 7, 1.8, 1.8)
    h_m[3600:4600, 5000:5800] = crop_filtered

    # Re-enforce pier deck flat tops and dredged basin floor
    h_m = np.where(piers_f > 0.85, QUAY_ALT, h_m)
    h_m = np.where(basin_water_mask > 0.85, BASIN_DEPTH, h_m)

    # Clip to valid range and save
    h_m = np.clip(h_m, MIN_H, MAX_H)
    hm_u16_out = h_to_u16(h_m)
    Image.fromarray(hm_u16_out).save("assets/terrain/heightmap.png")
    print("  [+] Successfully saved authentic assets/terrain/heightmap.png")

    print("\n==================================================================")
    print("[*] STEP 6: Updating Layer 2 Texture Map (Pavements & Riprap)...")
    print("==================================================================")
    tm_out = np.zeros((TOTAL_RES, TOTAL_RES, 3), dtype=np.uint8)

    mask_pave = np.zeros((TOTAL_RES, TOTAL_RES), dtype=np.uint8)
    cv2.fillPoly(mask_pave, [mole_poly], 255)
    cv2.fillPoly(mask_pave, [cw_poly], 255)
    cv2.fillPoly(mask_pave, [gare_poly], 255)
    cv2.rectangle(mask_pave, (5245, 3880), (5268, 4368), 255, -1)
    cv2.rectangle(mask_pave, (5235, 3865), (5275, 3915), 255, -1)

    outer_mole_line = np.array([
        [5445, 4288], [5455, 4282], [5490, 4238], [5525, 4192],
        [5560, 4148], [5572, 4122], [5562, 4110], [5548, 4108]
    ], dtype=np.int32)
    outer_jetee_line = np.array([
        [5560, 3970], [5620, 3985], [5672, 4005], [5670, 4020], [5620, 4005], [5570, 3990]
    ], dtype=np.int32)

    mask_riprap = np.zeros((TOTAL_RES, TOTAL_RES), dtype=np.uint8)
    cv2.polylines(mask_riprap, [outer_mole_line.reshape((-1, 1, 2))], False, 255, 8, lineType=cv2.LINE_AA)
    cv2.polylines(mask_riprap, [outer_jetee_line.reshape((-1, 1, 2))], False, 255, 6, lineType=cv2.LINE_AA)

    mask_pave = np.clip(mask_pave.astype(np.int32) - mask_riprap.astype(np.int32), 0, 255).astype(np.uint8)

    tm_out[:, :, 2] = cv2.GaussianBlur(mask_pave, (3, 3), 0.8)   # Red = Layer 12 Cobblestone
    tm_out[:, :, 1] = cv2.GaussianBlur(mask_riprap, (3, 3), 0.8) # Green = Layer 13 Ashlar / Riprap

    cv2.imwrite("assets/terrain/textureMap.png", tm_out)
    print("  [+] Successfully saved authentic assets/terrain/textureMap.png")

    print("\n==================================================================")
    print("[*] STEP 7: Updating Layer 3 Road Map (Tarred Asphalt)...")
    print("==================================================================")
    rm_raw_path = "assets/terrain/roadMap_user_raw.png"
    rm_raw = cv2.imread(rm_raw_path, cv2.IMREAD_UNCHANGED)
    if len(rm_raw.shape) >= 3:
        clean_roads = rm_raw[:, :, 0].copy()
    else:
        clean_roads = rm_raw.copy()
    if clean_roads.dtype == np.uint16:
        clean_roads = (clean_roads / 256).astype(np.uint8)

    # Clear roads from the water basin and open sea
    clean_roads[basin_water_mask > 0.3] = 0
    clean_roads[h_m < WATER_LEVEL] = 0

    def draw_road(canvas, pts, w=6):
        arr = np.array(pts, dtype=np.int32).reshape((-1, 1, 2))
        cv2.polylines(canvas, [arr], False, 255, w, lineType=cv2.LINE_AA)

    # Draw authentic 1943 harbor access roads on piers:
    draw_road(clean_roads, [[5265, 3885], [5265, 4368]], w=7)
    draw_road(clean_roads, [[5265, 4350], [5340, 4315], [5432, 4278]], w=6)
    draw_road(clean_roads, [[5432, 4278], [5470, 4232], [5508, 4186], [5545, 4138], [5555, 4118]], w=8)
    draw_road(clean_roads, [[5265, 3885], [5380, 3958], [5526, 3975]], w=6)
    draw_road(clean_roads, [[5526, 3975], [5526, 4080]], w=5)
    draw_road(clean_roads, [[5526, 3975], [5570, 3990], [5670, 4015]], w=4)

    clean_roads_aa = cv2.GaussianBlur(clean_roads.astype(np.float32), (3, 3), 0.5)
    clean_roads_final = np.clip(clean_roads_aa, 0, 255).astype(np.uint8)

    rm_out = np.zeros((TOTAL_RES, TOTAL_RES, 3), dtype=np.uint8)
    rm_out[:, :, 2] = clean_roads_final  # Red channel = Layer 10 Tarred Asphalt

    cv2.imwrite("assets/terrain/roadMap.png", rm_out)
    print("  [+] Successfully saved clean assets/terrain/roadMap.png")
    print(f"\n[+] Entire Authentic Heightmap Generation completed in {time.time() - t0:.2f}s!")

if __name__ == '__main__':
    main()
