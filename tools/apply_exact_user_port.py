#!/usr/bin/env python3
"""
tools/apply_exact_user_port.py
==============================
Applies the user's EXACT hand-drawn port shape and roads onto the authentic world heightmap.
"""

import os
import cv2
import numpy as np
from PIL import Image

Image.MAX_IMAGE_PIXELS = None

MIN_H = -70.0
MAX_H =  87.8
H_RANGE = MAX_H - MIN_H
WATER_LEVEL = -48.5
TERRACE_H = -46.5
PIER_DECK_H = -46.0
DREDGE_H = -53.0
HEIGHTMAP_RES = 8129

def main():
    print("[*] STEP 1: Loading clean natural baseline DEM...")
    base_path = "assets/terrain/heightmap_before_roads.png"
    hm_img = Image.open(base_path)
    hm_full = np.array(hm_img, dtype=np.uint16)
    h_full = MIN_H + (hm_full.astype(np.float32) / 65535.0) * H_RANGE

    r0, r1 = 3700, 4500
    c0, c1 = 4800, 5800
    H, W = r1 - r0, c1 - c0

    crop_h = h_full[r0:r1, c0:c1].copy()

    print("[*] STEP 2: Loading user raw road map...")
    rm_raw_path = "assets/terrain/roadMap_user_raw.png"
    rm_raw = np.array(Image.open(rm_raw_path))
    rc = rm_raw[r0:r1, c0:c1]
    road_strokes = (rc[:, :, 0] > 80)

    print("[*] STEP 3: Extracting exact user basin via flood fill...")
    strokes_crop = road_strokes.copy().astype(np.uint8)
    bridge_strokes = strokes_crop.copy()
    # Bridge entrance mouth between pierheads
    cv2.line(bridge_strokes, (5595 - c0, 4045 - r0), (5615 - c0, 4100 - r0), 1, thickness=12)

    ff_mask = np.zeros((H + 2, W + 2), dtype=np.uint8)
    filled = bridge_strokes.copy()
    seed = (5450 - c0, 4100 - r0)
    cv2.floodFill(filled, ff_mask, seed, 2)
    user_basin = (filled == 2)
    final_basin = user_basin & (~road_strokes)
    print(f"    [+] Final dredged basin: {np.count_nonzero(final_basin):,} pixels.")

    print("[*] STEP 4: Sculpting quayside terrace and solid pier decks...")
    h_out = crop_h.copy()

    # Quayside terrace and Place Pichon
    quayside_terrace = np.zeros((H, W), dtype=bool)
    quayside_terrace[3850 - r0:4250 - r0, 5220 - c0:5295 - c0] = True
    place_pichon = np.zeros((H, W), dtype=bool)
    place_pichon[3820 - r0:3890 - r0, 5220 - c0:5320 - c0] = True
    quayside_terrace |= place_pichon

    north_quay = np.zeros((H, W), dtype=bool)
    north_quay[3850 - r0:3925 - r0, 5280 - c0:5600 - c0] = True
    quayside_terrace |= north_quay

    port_roads = road_strokes & (np.arange(W)[None, :] > (5200 - c0)) & (np.arange(W)[None, :] < (5700 - c0)) & (np.arange(H)[:, None] > (3850 - r0)) & (np.arange(H)[:, None] < (4450 - r0))
    port_road_corridor = cv2.dilate(port_roads.astype(np.uint8), cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (11, 11))) > 0
    quayside_terrace |= port_road_corridor
    quayside_terrace[final_basin] = False

    # South Pier deck (dilated from user road loop on pier)
    spier_roads = road_strokes & (np.arange(W)[None, :] > (5300 - c0)) & (np.arange(H)[:, None] > (4080 - r0)) & (np.arange(H)[:, None] < (4400 - r0))
    spier_deck = cv2.dilate(spier_roads.astype(np.uint8), cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (31, 31))) > 0
    spier_deck &= (~final_basin)

    # North Mole & Jetée Nord
    nmole_roads = road_strokes & (np.arange(W)[None, :] > (5560 - c0)) & (np.arange(H)[:, None] >= (3900 - r0)) & (np.arange(H)[:, None] <= (4060 - r0))
    nmole_deck = cv2.dilate(nmole_roads.astype(np.uint8), cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (19, 19))) > 0
    jetee_roads = road_strokes & (np.arange(W)[None, :] > (5560 - c0)) & (np.arange(H)[:, None] < (3930 - r0))
    jetee_deck = cv2.dilate(jetee_roads.astype(np.uint8), cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (19, 19))) > 0
    north_bw_deck = (nmole_deck | jetee_deck) & (~final_basin)

    # Set elevations
    h_out[quayside_terrace] = TERRACE_H
    h_out[spier_deck] = PIER_DECK_H
    h_out[north_bw_deck] = PIER_DECK_H

    # Smooth urban transition from Medina (C=5020) to Quayside (C=5220) over 700 meters
    for r in range(3800 - r0, 4350 - r0):
        c_start = 5020 - c0
        c_end = 5220 - c0
        h_med = h_out[r, c_start]
        h_med = max(h_med, -32.0)
        t = np.linspace(0.0, 1.0, c_end - c_start)
        curve = t * t * (3.0 - 2.0 * t)
        h_out[r, c_start:c_end] = h_med * (1.0 - curve) + TERRACE_H * curve

    # Sloped outer riprap seawalls on South Pier and Jetée Nord
    dist_spier = cv2.distanceTransform((~spier_deck).astype(np.uint8), cv2.DIST_L2, 5)
    riprap_spier = (dist_spier > 0) & (dist_spier <= 12) & (~final_basin) & (~quayside_terrace)
    t_rip = dist_spier[riprap_spier] / 12.0
    h_out[riprap_spier] = PIER_DECK_H * (1.0 - t_rip) + (-51.0) * t_rip

    dist_north = cv2.distanceTransform((~north_bw_deck).astype(np.uint8), cv2.DIST_L2, 5)
    riprap_north = (dist_north > 0) & (dist_north <= 10) & (~final_basin) & (~quayside_terrace)
    t_rn = dist_north[riprap_north] / 10.0
    h_out[riprap_north] = PIER_DECK_H * (1.0 - t_rn) + (-51.0) * t_rn

    # Southern coastline & coastal road protection
    south_zone = np.zeros((H, W), dtype=bool)
    south_zone[4250 - r0:, :] = True
    south_roads = road_strokes & south_zone & (np.arange(W)[None, :] > (5250 - c0))
    south_dry_land = cv2.dilate(south_roads.astype(np.uint8), cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (21, 21))) > 0
    for r in range(4250 - r0, H):
        road_cols = np.where(south_dry_land[r, :])[0]
        if len(road_cols) > 0:
            max_c = road_cols.max()
            south_dry_land[r, :max_c] = True
    # Strictly do not elevate the basin!
    south_dry_land &= (~final_basin)
    h_out[south_dry_land & south_zone] = np.maximum(h_out[south_dry_land & south_zone], -46.5)

    dist_from_south_land = cv2.distanceTransform((~south_dry_land).astype(np.uint8), cv2.DIST_L2, 5)
    beach_south = south_zone & (dist_from_south_land > 0) & (dist_from_south_land <= 30) & (~spier_deck) & (~riprap_spier) & (~final_basin)
    t_sb = dist_from_south_land[beach_south] / 30.0
    h_out[beach_south] = np.minimum(h_out[beach_south], -46.5 * (1.0 - t_sb) + (-51.5) * t_sb)
    deep_sea_south = south_zone & (dist_from_south_land > 30) & (~spier_deck) & (~riprap_spier) & (~final_basin)
    h_out[deep_sea_south] = np.minimum(h_out[deep_sea_south], -52.0)

    # Northern beach (Plage Boujaafar)
    north_zone = np.zeros((H, W), dtype=bool)
    north_zone[:3850 - r0, :] = True
    north_roads = road_strokes & north_zone & (np.arange(W)[None, :] > (5050 - c0))
    north_dry_land = cv2.dilate(north_roads.astype(np.uint8), cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (15, 15))) > 0
    for r in range(0, 3850 - r0):
        road_cols = np.where(north_dry_land[r, :])[0]
        if len(road_cols) > 0:
            max_c = road_cols.max()
            north_dry_land[r, :max_c] = True
    north_dry_land &= (~final_basin)
    h_out[north_dry_land & north_zone] = np.maximum(h_out[north_dry_land & north_zone], -46.5)

    dist_from_north_land = cv2.distanceTransform((~north_dry_land).astype(np.uint8), cv2.DIST_L2, 5)
    beach_north = north_zone & (dist_from_north_land > 0) & (dist_from_north_land <= 35) & (~north_bw_deck) & (~riprap_north) & (~final_basin)
    t_nb = dist_from_north_land[beach_north] / 35.0
    h_out[beach_north] = np.minimum(h_out[beach_north], -46.5 * (1.0 - t_nb) + (-51.5) * t_nb)
    deep_sea_north = north_zone & (dist_from_north_land > 35) & (~north_bw_deck) & (~riprap_north) & (~final_basin)
    h_out[deep_sea_north] = np.minimum(h_out[deep_sea_north], -52.0)

    # STRICT FINAL BASIN DREDGE (-53.0m)
    h_out[final_basin] = DREDGE_H

    # Entrance fairway connecting basin to open sea:
    # Mouth from basin out to open sea, smoothly transitioning from -53.0m down to natural sea (-70.0m)
    entrance_fairway = np.zeros((H, W), dtype=bool)
    entrance_fairway[4035 - r0:4115 - r0, 5575 - c0:5720 - c0] = True
    entrance_fairway &= (~spier_deck) & (~north_bw_deck)
    for c in range(5575 - c0, 5720 - c0):
        t_mouth = np.clip((c - (5575 - c0)) / float(5720 - 5575), 0.0, 1.0)
        h_fairway = DREDGE_H * (1.0 - t_mouth) + (-70.0) * t_mouth
        for r in range(4035 - r0, 4115 - r0):
            if entrance_fairway[r, c]:
                h_out[r, c] = min(h_out[r, c], h_fairway)

    # Open sea east naturally deepens to baseline (-70m)
    sea_east = (np.arange(W)[None, :] > (5650 - c0)) & (np.arange(H)[:, None] >= (3850 - r0)) & (np.arange(H)[:, None] < (4250 - r0))
    sea_east &= (~spier_deck) & (~riprap_spier) & (~north_bw_deck) & (~riprap_north) & (~final_basin) & (~entrance_fairway)
    h_out[sea_east] = np.minimum(h_out[sea_east], crop_h[sea_east])

    # STRICT GUARANTEE: Every user road stroke is dry land (>= -46.5m)
    h_out[road_strokes] = np.maximum(h_out[road_strokes], TERRACE_H)
    print(f"    [+] Underwater user roads: {np.count_nonzero(h_out[road_strokes] < WATER_LEVEL)} (ZERO!)")

    # Save to heightmap
    crop_u16 = np.clip(np.round(((h_out - MIN_H) / H_RANGE) * 65535.0), 0, 65535).astype(np.uint16)
    h_full[r0:r1, c0:c1] = h_out
    full_u16 = np.clip(np.round(((h_full - MIN_H) / H_RANGE) * 65535.0), 0, 65535).astype(np.uint16)
    Image.fromarray(full_u16).save("assets/terrain/heightmap.png")
    print("    [+] Successfully saved authentic assets/terrain/heightmap.png!")

    print("[*] STEP 5: Updating textureMap.png...")
    tm_full = cv2.imread("assets/terrain/textureMap.png") if os.path.exists("assets/terrain/textureMap.png") else np.zeros((HEIGHTMAP_RES, HEIGHTMAP_RES, 3), dtype=np.uint8)
    tm_crop = np.zeros((H, W, 3), dtype=np.uint8)

    # Cobblestones: quayside terrace, south pier deck, north mole deck
    dry_pave = (quayside_terrace | spier_deck | nmole_deck) & (~final_basin)
    tm_crop[dry_pave, 2] = 255 # Red

    # Ashlar breakwater blocks: Jetée Nord crest
    tm_crop[jetee_deck & (~final_basin), 1] = 255 # Green

    # Armor riprap: outer seawalls
    tm_crop[riprap_spier & (~final_basin), 0] = 255 # Blue
    tm_crop[riprap_north & (~final_basin), 0] = 255 # Blue

    # Basin and fairway strictly zero
    tm_crop[final_basin, :] = 0
    tm_crop[entrance_fairway, :] = 0

    tm_full[r0:r1, c0:c1] = tm_crop
    cv2.imwrite("assets/terrain/textureMap.png", tm_full)
    print("    [+] Successfully saved authentic assets/terrain/textureMap.png!")

    print("[*] STEP 6: Updating roadMap.png with user raw roads...")
    user_roads = rm_raw[:, :, 0] if len(rm_raw.shape) >= 3 else rm_raw
    if user_roads.dtype == np.uint16:
        user_roads = (user_roads / 256).astype(np.uint8)

    clean_roads = user_roads.copy()
    # Clear roads from the water basin and deep sea
    clean_roads[r0:r1, c0:c1][final_basin] = 0
    clean_roads[h_full < WATER_LEVEL] = 0

    rm_out = np.zeros((HEIGHTMAP_RES, HEIGHTMAP_RES, 3), dtype=np.uint8)
    rm_out[:, :, 2] = clean_roads # Red = Layer 10 Tarred Asphalt
    cv2.imwrite("assets/terrain/roadMap.png", rm_out)
    print("    [+] Successfully saved clean assets/terrain/roadMap.png!")

    print("[+] Finished applying exact user port successfully!")

if __name__ == "__main__":
    main()
