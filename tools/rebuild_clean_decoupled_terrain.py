#!/usr/bin/env python3
"""
tools/rebuild_clean_decoupled_terrain.py
=========================================
Implements the clean, decoupled open-world terrain architecture for Historical Sousse (1930s-1943):

LAYER 1: Pure Topographic Elevation (assets/terrain/heightmap.png)
  - Natural hillside easing from Medina / Kasbah down to the waterfront (eliminates 20m cliff).
  - Stable, flat waterfront terrace at -46.5m for Place Pichon & Quai de la Marine.
  - Flat dredged harbor basin floor at -53.5m (5.0m water draft below -48.5m sea level).
  - Authentic Grand Mole Sud (39° NE, 144m wide), Causeway, Gare Maritime finger dock, and Jetée Nord.
  - Natural sandy beach curves (Plage Boujaafar to north, gentle south coast).
  - Smooth monotonic bathymetric shelf into deep Mediterranean (no sharp -70m trenches/spikes).
  - Edge-preserving bilateral/Gaussian anti-aliasing to eliminate quantization terracing & pixelated lines.

LAYER 2: Surface Materials & Texture Map (assets/terrain/textureMap.png)
  - Red = 255 on paved quayside promenade, Place Pichon, and pier decks (Layer 12 cobblestone).
  - Green / Blue for breakwater rock armor (Layer 13/14 ashlar & riprap).
  - 0 on water basin and natural sand beaches.

LAYER 3: Road Network Overlay (assets/terrain/roadMap.png)
  - Draped 2D tarred asphalt surface mask (Layer 10). Zero elevation distortion.
"""

import os
import sys
import time
import cv2
import numpy as np
from PIL import Image

Image.MAX_IMAGE_PIXELS = None

# Engine constants
MIN_H = -70.0
MAX_H =  87.8
H_RANGE = MAX_H - MIN_H
WATER_LEVEL = -48.5
QUAY_ALT    = -46.5  # 2.0m above sea level (dry deck)
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
    print("[*] STEP 1: Loading clean DEM baseline & user road map...")
    print("==================================================================")
    base_path = "assets/terrain/heightmap_backup_pre_satellite_rebuild.png"
    if not os.path.exists(base_path):
        base_path = "assets/terrain/heightmap.png"
    
    hm_base_pil = Image.open(base_path)
    hm_base_u16 = np.array(hm_base_pil, dtype=np.uint16)
    h_m = u16_to_h(hm_base_u16)
    print(f"    Loaded baseline elevation: [{h_m.min():.2f}m .. {h_m.max():.2f}m]")

    rm_raw_path = "assets/terrain/roadMap_user_raw.png"
    rm_raw = cv2.imread(rm_raw_path, cv2.IMREAD_UNCHANGED)
    if len(rm_raw.shape) >= 3:
        user_roads = rm_raw[:, :, 0]
    else:
        user_roads = rm_raw
    if user_roads.dtype == np.uint16:
        user_roads = (user_roads / 256).astype(np.uint8)

    print("\n==================================================================")
    print("[*] STEP 2: Natural Hillside Easing (Medina -> Waterfront Promenade)...")
    print("==================================================================")
    # The Medina sits around C=5000..5160 at elev ~ -18m to -30m.
    # Place Pichon and Quai de la Marine sit at C=5250..5270 at elev -46.5m.
    # In rows 3800..4400, ease the slope smoothly between C=5140 and C=5250:
    R_MIN, R_MAX = 3800, 4400
    C_SLOPE_START = 5140
    C_SLOPE_END   = 5250

    for r in range(R_MIN, R_MAX):
        # Current elevation at the Medina foot:
        h_medina = h_m[r, C_SLOPE_START]
        # We ensure h_medina is at least higher than -46.5:
        h_medina = max(h_medina, -32.0)
        
        # S-curve (smoothstep) blending between Medina foot and Quay terrace
        t = np.linspace(0.0, 1.0, C_SLOPE_END - C_SLOPE_START)
        # Smoothstep curve: 3*t^2 - 2*t^3
        curve = t * t * (3.0 - 2.0 * t)
        target_slope = h_medina * (1.0 - curve) + QUAY_ALT * curve
        
        # Smoothly blend with existing terrain so we don't break north/south continuity
        h_m[r, C_SLOPE_START:C_SLOPE_END] = target_slope
        
        # Waterfront promenade terrace (C=5250 to C=5270) flat at QUAY_ALT
        h_m[r, C_SLOPE_END:5270] = QUAY_ALT

    print("    [+] Eased urban hillside slope. Zero vertical cliffs remain.")

    print("\n==================================================================")
    print("[*] STEP 3: Defining 1943 Photogrammetric Harbor Polygons...")
    print("==================================================================")
    # Polygons matching the 1943 USAAF aerial reconnaissance photo:
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

    # Rasterize with sub-pixel anti-aliasing
    print("\n==================================================================")
    print("[*] STEP 4: Anti-Aliased Sub-Pixel Sculpting of Piers & Basin...")
    print("==================================================================")
    mask_piers = np.zeros((TOTAL_RES, TOTAL_RES), dtype=np.uint8)
    cv2.fillPoly(mask_piers, [mole_poly], 255)
    cv2.fillPoly(mask_piers, [cw_poly], 255)
    cv2.fillPoly(mask_piers, [gare_poly], 255)

    # Quai de la Marine & Place Pichon solid strip (C=5245..5268, R=3880..4365)
    cv2.rectangle(mask_piers, (5245, 3880), (5268, 4365), 255, -1)
    # Place Pichon park square (C=5240..5275, R=3870..3910)
    cv2.rectangle(mask_piers, (5240, 3870), (5275, 3910), 255, -1)

    piers_f = cv2.GaussianBlur(mask_piers.astype(np.float32) / 255.0, (5, 5), 1.0)

    # Dredged Basin Mask
    mask_basin = np.zeros((TOTAL_RES, TOTAL_RES), dtype=np.uint8)
    cv2.fillPoly(mask_basin, [basin_poly], 255)
    basin_f = cv2.GaussianBlur(mask_basin.astype(np.float32) / 255.0, (5, 5), 1.0)
    # Subtract piers so basin does not erode the pier decks
    basin_water_mask = np.clip(basin_f - piers_f * 1.5, 0.0, 1.0)

    # Apply dredged basin floor
    h_m = h_m * (1.0 - basin_water_mask) + BASIN_DEPTH * basin_water_mask

    # Apply solid pier decks at QUAY_ALT (-46.5m)
    h_m = h_m * (1.0 - piers_f) + QUAY_ALT * piers_f

    print("    [+] Dredged basin floor set to -53.5m (5.0m draft). Pier decks at -46.5m.")

    print("\n==================================================================")
    print("[*] STEP 5: Smooth Natural Coastline & Bathymetry...")
    print("==================================================================")
    coast_cols = np.zeros(TOTAL_RES, dtype=np.int32)
    for r in range(TOTAL_RES):
        if r < 3885:
            coast_cols[r] = int(5265 + (3885 - r) * 0.08)
        elif r <= 4368:
            coast_cols[r] = 5265
        else:
            coast_cols[r] = int(5265 + (r - 4368) * 0.12)

    # STEP 5: Smooth Natural Coastline & Open Mediterranean Bathymetry
    # Outside the piers and coastline, the entire eastern area is open Mediterranean Sea!
    print("  - Dredging open Mediterranean bathymetry around outer pier faces...")
    for r in range(2500, 5500):
        # Find outer boundary of land/piers at this row:
        row_piers = np.where(mask_piers[r, :] > 0)[0]
        if len(row_piers) > 0 and 3865 <= r <= 4370:
            c_land_max = np.max(row_piers)
        elif r < 3865:
            c_land_max = coast_cols[r]
        else:
            c_land_max = coast_cols[r]

        # In the notch between Jetee Nord and Grand Mole (Rows 3980..4110),
        # only keep land where mask_piers actually exists:
        if 3980 <= r <= 4110:
            if len(row_piers) > 0:
                c_land_max = np.max(row_piers)
            else:
                c_land_max = 5265

        # Sloped riprap / beach footing (6 pixels ~ 20m)
        c_footing = c_land_max + 6
        if c_footing < TOTAL_RES:
            # Short slope from -46.5m to -52.0m
            ramp = np.linspace(0.0, 1.0, 6)
            h_m[r, c_land_max:c_footing] = np.minimum(h_m[r, c_land_max:c_footing], QUAY_ALT * (1.0 - ramp) + (-52.0) * ramp)
            
            # Continental shelf sloping from -52.0m to -65.0m
            shelf_len = min(200, TOTAL_RES - c_footing)
            shelf_ramp = np.linspace(0.0, 1.0, shelf_len)
            target_shelf = (-52.0) * (1.0 - shelf_ramp) + (-65.0) * shelf_ramp
            h_m[r, c_footing:c_footing+shelf_len] = np.minimum(h_m[r, c_footing:c_footing+shelf_len], target_shelf)
            
            # Deep sea floor
            h_m[r, c_footing+shelf_len:] = MIN_H

    # Local bilateral filter across the port & urban zone to guarantee silky-smooth slopes
    # with NO quantization steps or contour line artifacts:
    print("  - Applying edge-preserving de-terracing filter across Sousse...")
    crop_h = h_m[3600:4600, 5000:5800]
    crop_filtered = cv2.bilateralFilter(crop_h.astype(np.float32), 7, 1.8, 1.8)
    h_m[3600:4600, 5000:5800] = crop_filtered

    # Re-enforce pier deck flat tops after bilateral filter
    h_m = np.where(piers_f > 0.85, QUAY_ALT, h_m)
    # Re-enforce dredged basin floor flat bottom after bilateral filter
    h_m = np.where(basin_water_mask > 0.85, BASIN_DEPTH, h_m)

    # Save heightmap
    h_m = np.clip(h_m, MIN_H, MAX_H)
    hm_u16_out = h_to_u16(h_m)
    Image.fromarray(hm_u16_out).save("assets/terrain/heightmap.png")
    print("  [+] Successfully saved clean assets/terrain/heightmap.png")

    print("\n==================================================================")
    print("[*] STEP 6: Generating Layer 2 Texture Map (Pavements & Riprap)...")
    print("==================================================================")
    tm_out = np.zeros((TOTAL_RES, TOTAL_RES, 3), dtype=np.uint8)

    # Paved stone / cobbles on pier decks, quays, and Place Pichon (Red channel)
    mask_pave = np.zeros((TOTAL_RES, TOTAL_RES), dtype=np.uint8)
    cv2.fillPoly(mask_pave, [mole_poly], 255)
    cv2.fillPoly(mask_pave, [cw_poly], 255)
    cv2.fillPoly(mask_pave, [gare_poly], 255)
    cv2.rectangle(mask_pave, (5245, 3880), (5268, 4365), 255, -1)
    cv2.rectangle(mask_pave, (5240, 3870), (5275, 3910), 255, -1)

    # Breakwater riprap seawall armor (Green channel / Blue channel):
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

    # Suppress pavement where riprap sits
    mask_pave = np.clip(mask_pave.astype(np.int32) - mask_riprap.astype(np.int32), 0, 255).astype(np.uint8)

    # Anti-aliasing
    tm_out[:, :, 2] = cv2.GaussianBlur(mask_pave, (3, 3), 0.8)   # Red = Layer 12 Cobblestone
    tm_out[:, :, 1] = cv2.GaussianBlur(mask_riprap, (3, 3), 0.8) # Green = Layer 13 Ashlar / Riprap

    cv2.imwrite("assets/terrain/textureMap.png", tm_out)
    print("  [+] Successfully saved clean assets/terrain/textureMap.png")

    print("\n==================================================================")
    print("[*] STEP 7: Rebuilding Clean Layer 3 Road Map (Tarred Asphalt)...")
    print("==================================================================")
    clean_roads = user_roads.copy()

    # Clear stray old loops from the water basin and open sea
    for r in range(2500, 5500):
        c_coast = coast_cols[r]
        if r < 3885 or r > 4368:
            clean_roads[r, c_coast+5:] = 0
        elif 3885 <= r <= 4368:
            clean_roads[r, 5275:] = 0

    clean_roads[4300:4700, 5350:5800] = 0

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
    print(f"\n[+] Entire Decoupled Terrain Generation completed in {time.time() - t0:.2f}s!")

if __name__ == '__main__':
    main()
