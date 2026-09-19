#!/usr/bin/env python3
"""
generate_proposed_alignment_preview.py
=======================================
Generates a side-by-side comparison:
Left: Current Placements (showing the offsets identified in the diagnostic)
Right: Proposed Perfect Placements (centered inside user parcel boxes, Square de la Marine at apex, dock furnishings on dock edge)
"""

import os
import math
import numpy as np
import cv2
from PIL import Image

Image.MAX_IMAGE_PIXELS = None

WORLD_MIN_X = -13938.0
WORLD_SIZE  =  27876.0
HEIGHTMAP_RES = 8129

MIN_H = -70.0
MAX_H =  87.8
H_RANGE = MAX_H - MIN_H
WATER_LEVEL = -48.5

ANCHOR_X = -530.0
ANCHOR_Z = -1502.0

def local_to_cr(lx, lz):
    wx = lx + ANCHOR_X
    wz = lz + ANCHOR_Z
    c = (wx - WORLD_MIN_X) / WORLD_SIZE * 8128.0
    r = (wz - WORLD_MIN_Z) / WORLD_SIZE * 8128.0
    return c, r

def cr_to_world(c, r):
    wx = WORLD_MIN_X + (c / 8128.0) * WORLD_SIZE
    wz = WORLD_MIN_Z + (r / 8128.0) * WORLD_SIZE
    return wx, wz

def cr_to_local(c, r):
    wx, wz = cr_to_world(c, r)
    return wx - ANCHOR_X, wz - ANCHOR_Z

def compute_hillshade(elev_crop, light_az=315.0, light_el=45.0):
    az_rad = np.radians(light_az)
    el_rad = np.radians(light_el)
    dx = cv2.Sobel(elev_crop, cv2.CV_32F, 1, 0, ksize=3) / (8.0 * 3.4296)
    dy = cv2.Sobel(elev_crop, cv2.CV_32F, 0, 1, ksize=3) / (8.0 * 3.4296)
    slope = np.arctan(np.sqrt(dx**2 + dy**2))
    aspect = np.arctan2(dy, -dx)
    shaded = np.sin(el_rad) * np.cos(slope) + np.cos(el_rad) * np.sin(slope) * np.cos(az_rad - aspect)
    return np.clip(shaded, 0.0, 1.0)

def main():
    # Zoom area: r in [3820, 4250], c in [5180, 5360]
    zr0, zr1 = 3820, 4250
    zc0, zc1 = 5180, 5360
    scale = 2.0
    
    hm_img = Image.open("assets/terrain/heightmap.png")
    hm_u16 = np.array(hm_img, dtype=np.float32)
    h_m = MIN_H + (hm_u16 / 65535.0) * H_RANGE
    
    rm_raw = cv2.imread("assets/terrain/roadMap_user_raw.png")
    
    raw_1943 = cv2.imread(r"C:\Users\honco\.gemini\antigravity-ide\brain\663803a9-f43e-4877-99ff-741274bfc692\.user_uploaded\media_1789687462297.jpg")
    M = np.array([
        [-0.5265712,   0.74011165, 5423.3364 - zc0],
        [-0.74011165, -0.5265712,  4662.708  - zr0]
    ], dtype=np.float32)
    W_z = zc1 - zc0
    H_z = zr1 - zr0
    warped_zoom = cv2.warpAffine(raw_1943, M, (W_z, H_z), flags=cv2.INTER_CUBIC)
    
    zoom_h = h_m[zr0:zr1, zc0:zc1]
    zoom_rm = rm_raw[zr0:zr1, zc0:zc1]
    zoom_hs = compute_hillshade(zoom_h)
    
    zh_2x = cv2.resize(zoom_hs, (0, 0), fx=scale, fy=scale, interpolation=cv2.INTER_CUBIC)
    zp_2x = cv2.resize(warped_zoom, (0, 0), fx=scale, fy=scale, interpolation=cv2.INTER_CUBIC)
    zrm_2x = cv2.resize(zoom_rm, (0, 0), fx=scale, fy=scale, interpolation=cv2.INTER_NEAREST)
    
    # Base background:
    base_2x = cv2.addWeighted(zp_2x, 0.45, cv2.cvtColor((zh_2x*255).astype(np.uint8), cv2.COLOR_GRAY2BGR), 0.55, 0)
    z_road_mask = (zrm_2x[:, :, 0] > 100)
    base_2x[z_road_mask] = cv2.addWeighted(base_2x[z_road_mask], 0.25, np.full_like(base_2x[z_road_mask], (0, 255, 255)), 0.75, 0)
    
    # Left panel: Current placements
    current_canvas = cv2.imread("scratch/projection_zoom_waterfront_parcels.png")
    
    # Right panel: Proposed perfect placements
    proposed_canvas = base_2x.copy()
    
    # Proposed monuments:
    # 1. Kiosque a Musique at Place Pichon center (c=5270.0, r=3880.0)
    k_c, k_r = (5270.0 - zc0) * scale, (3880.0 - zr0) * scale
    rad_k = int(round((4.5 * scale) / 3.4296))
    pts = []
    for a_deg in range(0, 360, 45):
        a_rad = np.radians(a_deg + 25.0)
        px = int(round(k_c + rad_k * np.cos(a_rad)))
        py = int(round(k_r + rad_k * np.sin(a_rad)))
        pts.append([px, py])
    pts = np.array(pts, np.int32)
    cv2.fillPoly(proposed_canvas, [pts], (255, 215, 0))
    cv2.polylines(proposed_canvas, [pts], True, (0, 0, 0), 2)
    cv2.putText(proposed_canvas, "Kiosque a Musique", (int(round(k_c)) + 15, int(round(k_r)) - 2), cv2.FONT_HERSHEY_SIMPLEX, 0.50, (0, 0, 0), 3)
    cv2.putText(proposed_canvas, "Kiosque a Musique", (int(round(k_c)) + 15, int(round(k_r)) - 2), cv2.FONT_HERSHEY_SIMPLEX, 0.50, (0, 255, 255), 1)
    
    # Triangular park curbing & palms around Kiosque:
    for dx, dr in [(-12, -10), (10, -12), (-14, 2), (12, 4), (-8, 12), (8, 12), (-2, 14), (0, -14)]:
        p_c = k_c + (dx * scale) / 3.4296
        p_r = k_r + (dr * scale) / 3.4296
        rad_p = max(3, int(round((3.5 * scale) / 3.4296)))
        cv2.circle(proposed_canvas, (int(round(p_c)), int(round(p_r))), rad_p, (34, 139, 34), -1)
        cv2.circle(proposed_canvas, (int(round(p_c)), int(round(p_r))), rad_p, (0, 255, 0), 1)
    
    # 5 Proposed Colonial Buildings centered in parcels:
    proposed_bldgs = [
        ("1. Douane (Neo-Moorish)", 5258.0, 3980.0, 29.2, 21.4, (255, 105, 180)),
        ("2. Douane Annexe",        5256.0, 4039.0, 24.6, 20.8, (255, 160, 122)),
        ("3. Grand Hotel Sousse",   5256.0, 4077.0, 32.8, 22.2, (50, 205, 50)),
        ("4. Cercle Militaire",     5256.0, 4119.0, 26.6, 19.0, (30, 144, 255)),
        ("5. Immeuble Pichon",      5254.0, 4165.0, 22.0, 17.6, (218, 112, 214)),
    ]
    
    for name, bc, br, w_m, d_m, col in proposed_bldgs:
        c_px = (bc - zc0) * scale
        r_px = (br - zr0) * scale
        hw = (w_m * 0.5 * scale) / 3.4296
        hd = (d_m * 0.5 * scale) / 3.4296
        
        # Facade facing East (+X, 90 deg yaw): depth along X, width along Z
        box_pts = np.array([
            [int(round(c_px - hd)), int(round(r_px - hw))],
            [int(round(c_px + hd)), int(round(r_px - hw))],
            [int(round(c_px + hd)), int(round(r_px + hw))],
            [int(round(c_px - hd)), int(round(r_px + hw))],
        ], np.int32)
        
        cv2.fillPoly(proposed_canvas, [box_pts], col)
        cv2.polylines(proposed_canvas, [box_pts], True, (0, 0, 0), 2)
        
        cv2.putText(proposed_canvas, name, (int(round(c_px)) - 145, int(round(r_px)) + 4), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (0, 0, 0), 3)
        cv2.putText(proposed_canvas, name, (int(round(c_px)) - 145, int(round(r_px)) + 4), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (255, 255, 255), 1)
    
    # Dock edge at c=5298 (coping stone line):
    dock_c = (5298.0 - zc0) * scale
    for dr in range(3880, 4230, 8):
        d_r = (dr - zr0) * scale
        # Bollards at c=5299
        cv2.rectangle(proposed_canvas, (int(round(dock_c + 1)), int(round(d_r - 1))), (int(round(dock_c + 3)), int(round(d_r + 1))), (0, 140, 255), -1)
    
    # Palms at c=5290
    palm_c = (5290.0 - zc0) * scale
    for dr in range(3890, 4220, 16):
        d_r = (dr - zr0) * scale
        cv2.circle(proposed_canvas, (int(round(palm_c)), int(round(d_r))), 4, (34, 139, 34), -1)
        cv2.circle(proposed_canvas, (int(round(palm_c)), int(round(d_r))), 4, (0, 255, 0), 1)
    
    # Lamps at c=5284
    lamp_c = (5284.0 - zc0) * scale
    for dr in range(3895, 4220, 35):
        d_r = (dr - zr0) * scale
        cv2.circle(proposed_canvas, (int(round(lamp_c)), int(round(d_r))), 3, (0, 255, 255), -1)
    
    cv2.putText(proposed_canvas, "PROPOSED: 100% IN PARCELS & ON DOCK", (15, 30), cv2.FONT_HERSHEY_DUPLEX, 0.55, (0, 255, 0), 2)
    cv2.putText(proposed_canvas, "Buildings in Parcel Centers | Kiosk in Square Apex", (15, 55), cv2.FONT_HERSHEY_SIMPLEX, 0.40, (0, 255, 255), 1)
    
    # Header titles for comparison
    cv2.putText(current_canvas, "CURRENT CITY.TXT PLACEMENT", (15, 30), cv2.FONT_HERSHEY_DUPLEX, 0.55, (0, 200, 255), 2)
    cv2.putText(current_canvas, "Notice: Buildings squished north / bollards in water", (15, 55), cv2.FONT_HERSHEY_SIMPLEX, 0.40, (0, 150, 255), 1)
    
    # Combine side by side:
    divider = np.full((current_canvas.shape[0], 6, 3), (255, 255, 255), dtype=np.uint8)
    comparison = np.hstack([current_canvas, divider, proposed_canvas])
    
    cv2.imwrite("scratch/projection_proposed_comparison.png", comparison)
    print("[+] Saved scratch/projection_proposed_comparison.png (726x860)!")

if __name__ == "__main__":
    main()
