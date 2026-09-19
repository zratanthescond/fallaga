#!/usr/bin/env python3
"""
apply_user_aligned_historical_port.py
======================================
Master Execution Script:
1. Sculpt heightmap.png:
   - Water basin dredged to -53.0m (4.5m depth).
   - Flat dry quayside terrace, Cours de la Marine, and parcels leveled to -46.5m (2m above sea level).
   - Breakwater moles and commercial pier deck sculpted to -46.0m.
   - Clean 65m entrance mouth dredged to -53.0m.
2. Update textureMap.png:
   - Stone pavement (isHarbor = 255) across quays, pier decks, avenue, and Place Pichon.
   - 0 in water basin.
3. Update roadMap.png:
   - Sharp road and rail network.
   - Clear strokes from water basin.
4. Update assets/saves/city.txt:
   - Kiosque a Musique at Place Pichon apex (c=5270.0, r=3880.0 -> WX=4136.1, WZ=-631.1).
   - Square de la Marine stone curbing and framing date palms.
   - 5 distinct colonial buildings centered in user parcels:
     1. Douane Neo-Moorish (c=5258.0, r=3980.0)
     2. Douane Annexe & Depots (c=5256.0, r=4039.0)
     3. Grand Hotel de Sousse (c=5256.0, r=4077.0)
     4. Cercle Militaire (c=5256.0, r=4119.0)
     5. Immeuble Pichon (c=5254.0, r=4165.0)
   - Quayside date palms along c=5290.0.
   - Mooring bollards along c=5298.0.
   - Street lamps along c=5284.0.
   - Preserves all 9,342 environmental rocks.
"""

import os
import sys
import math
import time
import numpy as np
import cv2
from PIL import Image

Image.MAX_IMAGE_PIXELS = None

# Coordinate constants:
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

ANCHOR_X = -530.0
ANCHOR_Z = -1502.0

def cr_to_world(c, r):
    wx = WORLD_MIN_X + (c / 8128.0) * WORLD_SIZE
    wz = WORLD_MIN_Z + (r / 8128.0) * WORLD_SIZE
    return wx, wz

def world_to_cr(wx, wz):
    c = int(round((wx - WORLD_MIN_X) / WORLD_SIZE * 8128.0))
    r = int(round((wz - WORLD_MIN_Z) / WORLD_SIZE * 8128.0))
    return np.clip(c, 0, 8128), np.clip(r, 0, 8128)

def to_local(wx, wz):
    return wx - ANCHOR_X, wz - ANCHOR_Z

def cr_to_local(c, r):
    wx, wz = cr_to_world(c, r)
    return to_local(wx, wz)

def main():
    t0 = time.time()
    print("==================================================================")
    print("[*] STEP 1: Sculpting heightmap.png with authentic user port...")
    print("==================================================================")

    hm_path = "assets/terrain/heightmap.png"
    hm_img = Image.open(hm_path)
    hm_u16 = np.array(hm_img, dtype=np.float32)
    h_m = MIN_H + (hm_u16 / 65535.0) * H_RANGE

    rm_raw_path = "assets/terrain/roadMap_user_raw.png"
    rm_raw = np.array(Image.open(rm_raw_path))
    strokes = (rm_raw[:, :, 0] > 100).astype(np.uint8)

    # 1. Close entrance mouth temporarily to isolate inner basin
    # North Pierhead: (c=5600, r=4040), South Pierhead: (c=5615, r=4085)
    temp_canvas = strokes.copy()
    cv2.line(temp_canvas, (5600, 4040), (5615, 4085), 1, thickness=12)

    # 2. Extract Basin via flood-fill from inside seed:
    ff_mask = np.zeros((HEIGHTMAP_RES + 2, HEIGHTMAP_RES + 2), dtype=np.uint8)
    filled = temp_canvas.copy()
    cv2.floodFill(filled, ff_mask, (5450, 4100), 2)
    basin_mask = (filled == 2)
    print(f"    [+] Extracted harbor basin: {np.sum(basin_mask):,} pixels.")

    # 3. Extract Breakwaters (strokes east of col 5300 enclosing the basin):
    bw_strokes = np.zeros_like(strokes)
    bw_strokes[3880:4400, 5300:5680] = strokes[3880:4400, 5300:5680]
    bw_solid = cv2.dilate(bw_strokes, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (15, 15))) > 0
    bw_solid[basin_mask] = False

    # Clear entrance mouth from breakwater stone:
    mouth_box = np.zeros_like(basin_mask)
    mouth_box[4035:4085, 5580:5650] = True
    bw_solid[mouth_box] = False

    # 4. Quayside terrace, Cours de la Marine Avenue, Parcels, and Place Pichon:
    # Ensure flat urban ground at -46.5m (2.0m above sea level)
    land_urban = np.zeros_like(basin_mask)
    land_urban[3700:4400, 4950:5350] = True
    land_urban[basin_mask] = False
    h_m[land_urban] = np.maximum(h_m[land_urban], -46.5)

    # Level the flat quayside avenue and parcel blocks (cols 5220..5305, rows 3820..4350) to -46.5m
    flat_avenue = np.zeros_like(basin_mask)
    flat_avenue[3820:4350, 5220:5305] = True
    flat_avenue[basin_mask] = False
    h_m[flat_avenue] = -46.5

    # 5. Apply Sculpted Elevations:
    # Dredged harbor basin to -53.0m (4.5m water depth):
    h_m[basin_mask] = -53.0

    # Entrance channel dredged to -53.0m:
    h_m[mouth_box & (h_m > -50.0)] = -53.0

    # Breakwater stone moles at -46.0m (2.5m above water level):
    h_m[bw_solid] = -46.0

    # Open sea bathymetry outside breakwaters:
    open_sea_local = np.zeros_like(basin_mask)
    open_sea_local[3850:4450, 5600:] = True
    open_sea_local[bw_solid] = False
    open_sea_local[land_urban] = False
    h_m[open_sea_local] = np.minimum(h_m[open_sea_local], -51.5)

    # Save heightmap
    val_u16 = np.clip(np.round(((h_m - MIN_H) / H_RANGE) * 65535.0), 0, 65535).astype(np.uint16)
    Image.fromarray(val_u16).save(hm_path)
    print(f"[+] Successfully saved authentic sculpted heightmap to {hm_path}!")

    print("\n==================================================================")
    print("[*] STEP 2: Updating textureMap.png (isHarbor stone paving)...")
    print("==================================================================")
    tm_path = "assets/terrain/textureMap.png"
    if os.path.exists(tm_path):
        tm = np.array(Image.open(tm_path))
    else:
        tm = np.zeros((HEIGHTMAP_RES, HEIGHTMAP_RES, 4), dtype=np.uint8)

    # Activate isHarbor (Red channel = 255) on quays, avenue, moles, and square:
    harbor_pave = bw_solid | flat_avenue
    square_pave = np.zeros_like(basin_mask)
    square_pave[3800:3920, 5230:5315] = True
    harbor_pave |= square_pave
    harbor_pave &= ~basin_mask

    tm[harbor_pave, 0] = 255
    tm[basin_mask, 0] = 0 # No pavement in water
    Image.fromarray(tm).save(tm_path)
    print(f"[+] Successfully updated harbor pavement in {tm_path}!")

    print("\n==================================================================")
    print("[*] STEP 3: Updating roadMap.png (authentic user roads & parcels)...")
    print("==================================================================")
    rm_path = "assets/terrain/roadMap.png"
    if os.path.exists(rm_path):
        out_rm = np.array(Image.open(rm_path))
    else:
        out_rm = np.zeros((HEIGHTMAP_RES, HEIGHTMAP_RES, 4), dtype=np.uint8)

    road_strokes = strokes.copy()
    road_strokes[basin_mask] = 0
    kernel_road = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (3, 3))
    thick_roads = cv2.dilate(road_strokes[3700:4400, 4950:5700], kernel_road)
    out_rm[3700:4400, 4950:5700, 0] = np.maximum(out_rm[3700:4400, 4950:5700, 0], thick_roads * 255)
    Image.fromarray(out_rm).save(rm_path)
    print(f"[+] Successfully updated road network in {rm_path}!")

    print("\n==================================================================")
    print("[*] STEP 4: Placing historical buildings and furnishings in city.txt...")
    print("==================================================================")
    city_path = "assets/saves/city.txt"
    with open(city_path, "r", encoding="utf-8") as f:
        existing_lines = f.readlines()

    anchor_line = existing_lines[0]
    rock_lines = [l for l in existing_lines[1:] if "Rock1" in l or "Stone" in l]

    new_objects = []

    # 4.1 KIOSQUE A MUSIQUE (SQUARE DE LA MARINE CENTERPIECE)
    # Centered at Place Pichon apex: (c=5270.0, r=3880.0)
    k_c, k_r = 5270.0, 3880.0
    kiosk_elev = -46.50
    klx, klz = cr_to_local(k_c, k_r)
    new_objects.append(
        f"OBJ_V6 Kiosque_Musique {klx:.2f} {klz:.2f} 0.00 {kiosk_elev:.2f} 1.00 1.00 1.00 25.00 assets/moniment/kiosque_musique.obj NONE NONE 0"
    )

    # 4.2 SQUARE DE LA MARINE STONE CURBING (Fan/triangular park enclosure)
    # Surrounding the Kiosque with stone curbs
    kwx, kwz = cr_to_world(k_c, k_r)
    for cx in np.arange(kwx - 18.0, kwx + 18.0, 5.6):
        for cz in [kwz - 18.0, kwz + 18.0]:
            clx, clz = to_local(cx, cz)
            new_objects.append(f"OBJ_V6 Park_Curb {clx:.2f} {clz:.2f} 0.00 {kiosk_elev:.2f} 1.00 1.00 1.00 0.00 assets/moniment/park_curbing.obj NONE NONE 0")
    for cz in np.arange(kwz - 18.0, kwz + 18.0, 5.6):
        for cx in [kwx - 18.0, kwx + 18.0]:
            clx, clz = to_local(cx, cz)
            new_objects.append(f"OBJ_V6 Park_Curb {clx:.2f} {clz:.2f} 0.00 {kiosk_elev:.2f} 1.00 1.00 1.00 90.00 assets/moniment/park_curbing.obj NONE NONE 0")

    # 4.3 SQUARE DE LA MARINE DATE PALMS (Clustered grove framing the Kiosque)
    np.random.seed(1899)
    park_palm_offsets = [
        (-10.0, -11.0, 1.25), ( 9.0, -11.0, 1.20), (-12.0,  0.0, 1.30),
        (-10.0,  10.0, 1.20), ( 9.0,   9.0, 1.15), ( -2.0, 12.0, 1.10),
        ( 12.0,  -1.0, 1.15), ( 11.0,  7.0, 1.10), ( -7.0,-13.0, 0.95),
        (  5.0, -14.0, 0.95), (-14.0, -5.0, 1.05), ( 14.0, -6.0, 1.05),
        (-13.0,   6.0, 1.05), (  4.0, 14.0, 1.00), ( -6.0, 14.0, 1.00)
    ]
    for idx, (dx, dz, s_mult) in enumerate(park_palm_offsets):
        pwx = kwx + dx + np.random.uniform(-0.4, 0.4)
        pwz = kwz + dz + np.random.uniform(-0.4, 0.4)
        plx, plz = to_local(pwx, pwz)
        yaw = np.random.uniform(0.0, 360.0)
        s = s_mult * np.random.uniform(0.95, 1.05)
        new_objects.append(f"OBJ_V6 Park_Palm_{idx} {plx:.2f} {plz:.2f} 0.00 {kiosk_elev:.2f} {s:.2f} {s:.2f} {s:.2f} {yaw:.2f} assets/moniment/date_palm.obj NONE NONE 0")

    # 4.4 FIVE COLONIAL BUILDINGS CENTERED INSIDE USER PARCELS
    # Facing East towards the port (+X, yaw = 90.0 deg)
    # Parcel 1: c=5258.0, r=3980.0 -> Douane Neo-Moorish
    # Parcel 2: c=5256.0, r=4039.0 -> Douane Annexe & Depots
    # Parcel 3: c=5256.0, r=4077.0 -> Grand Hotel de Sousse
    # Parcel 4: c=5256.0, r=4119.0 -> Cercle Militaire
    # Parcel 5: c=5254.0, r=4165.0 -> Immeuble Pichon
    distinct_buildings = [
        ("Douane_NeoMoorish",     5258.0, 3980.0, 90.0, "assets/moniment/bldg_douane_neomoorish.obj", 1.0, 1.0, 1.0),
        ("Douane_Annexe_Depots",  5256.0, 4039.0, 90.0, "assets/moniment/bldg_douane_annexe.obj",    1.0, 1.0, 1.0),
        ("Grand_Hotel_Sousse",    5256.0, 4077.0, 90.0, "assets/moniment/bldg_grand_hotel.obj",      1.0, 1.0, 1.0),
        ("Cercle_Militaire",      5256.0, 4119.0, 90.0, "assets/moniment/bldg_cercle_militaire.obj",  1.0, 1.0, 1.0),
        ("Immeuble_Pichon",       5254.0, 4165.0, 90.0, "assets/moniment/bldg_immeuble_pichon.obj",   1.0, 1.0, 1.0),
    ]

    for name, bc, br, angle, mpath, sx, sy, sz in distinct_buildings:
        blx, blz = cr_to_local(bc, br)
        new_objects.append(
            f"OBJ_V6 {name} {blx:.2f} {blz:.2f} 0.00 {-46.50:.2f} {sx:.2f} {sy:.2f} {sz:.2f} {angle:.2f} {mpath} NONE NONE 0"
        )

    # 4.5 QUAYSIDE PALM PROMENADE (along c=5290.0 from r=3890 to r=4210)
    for idx, pr in enumerate(np.arange(3890.0, 4210.0, 7.0)):
        j_c = 5290.0 + np.random.uniform(-0.3, 0.3)
        j_r = pr + np.random.uniform(-0.3, 0.3)
        plx, plz = cr_to_local(j_c, j_r)
        yaw = np.random.uniform(0.0, 360.0)
        s = np.random.uniform(1.10, 1.25)
        new_objects.append(f"OBJ_V6 Quay_Palm_{idx} {plx:.2f} {plz:.2f} 0.00 {-46.50:.2f} {s:.2f} {s:.2f} {s:.2f} {yaw:.2f} assets/moniment/date_palm.obj NONE NONE 0")

    # 4.6 MARITIME MOORING BOLLARDS (along dock coping stones at c=5298.0)
    for idx, br in enumerate(np.arange(3880.0, 4220.0, 5.0)):
        blx, blz = cr_to_local(5298.0, br)
        new_objects.append(f"OBJ_V6 Mooring_Bollard_{idx} {blx:.2f} {blz:.2f} 0.00 {-46.50:.2f} 1.00 1.00 1.00 0.00 assets/moniment/mooring_bollard.obj NONE NONE 0")

    # 4.7 QUAYSIDE CAST-IRON LAMP POSTS (along sidewalk at c=5284.0, every ~70m / 20px)
    for idx, lr in enumerate(np.arange(3895.0, 4210.0, 20.0)):
        llx, llz = cr_to_local(5284.0, lr)
        new_objects.append(f"OBJ_V6 Quay_Lamp_{idx} {llx:.2f} {llz:.2f} 0.00 {-46.50:.2f} 1.00 1.00 1.00 90.00 assets/moniment/quayside_lamp_post.obj NONE NONE 0")

    # Write city.txt:
    all_lines = [anchor_line] + [l.strip() + "\n" for l in new_objects] + rock_lines
    with open(city_path, "w", encoding="utf-8") as f:
        f.writelines(all_lines)

    print(f"[+] Placed {len(new_objects)} historical waterfront objects into {city_path}.")
    print(f"[+] Preserved {len(rock_lines)} environmental rocks.")
    print(f"[+] Total execution time: {time.time()-t0:.2f}s.")

if __name__ == "__main__":
    main()
