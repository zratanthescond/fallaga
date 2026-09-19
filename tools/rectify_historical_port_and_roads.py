import os
import cv2
import numpy as np
from PIL import Image

Image.MAX_IMAGE_PIXELS = None

print("[*] Starting photogrammetric rectification of Sousse port and roads...")

# Paths
ROADMAP_PATH = "assets/terrain/roadMap.png"
HEIGHTMAP_PATH = "assets/terrain/heightmap.png"
TEXTUREMAP_PATH = "assets/terrain/textureMap.png"

# Load full maps
rm_img = cv2.imread(ROADMAP_PATH, cv2.IMREAD_UNCHANGED) # RGBA or RGB
hm_pil = Image.open(HEIGHTMAP_PATH)
hm_u16 = np.array(hm_pil, dtype=np.float32)
tm_img = cv2.imread(TEXTUREMAP_PATH) # BGR: B=riprap, G=breakwater, R=quay

H_TOTAL, W_TOTAL = 8129, 8129
MIN_H = -70.0
MAX_H =  87.8
H_RANGE = MAX_H - MIN_H
h_m = MIN_H + (hm_u16 / 65535.0) * H_RANGE

# Port region crop
r0, r1 = 3700, 4450
c0, c1 = 4800, 5800
H_CROP, W_CROP = r1 - r0, c1 - c0

print(f"[*] Crop region: rows [{r0}..{r1}], cols [{c0}..{c1}] ({H_CROP}x{W_CROP})")

# Extract crops
rm_crop = rm_img[r0:r1, c0:c1].copy()
hm_crop = h_m[r0:r1, c0:c1].copy()
tm_crop = tm_img[r0:r1, c0:c1].copy()

# =========================================================================
# 1. BUILD AUTHENTIC 1943 RECTIFIED ROAD NETWORK
# =========================================================================
print("[*] Step 1: Building authentic 1943 road network...")

# We preserve all existing roads outside the harbor basin zone:
# That is, roads west of col 5260 (x < 460), roads north of row 3880 (y < 180),
# and roads south of row 4400 (y > 700).
# We replace the harbor roads with the authentic 1943 rectified geometry.

# Get current red channel (roads)
if rm_crop.ndim == 3:
    # cv2 loads as BGR(A). Channel 2 is Red (Roads in shader)
    current_roads = rm_crop[:, :, 2].copy()
else:
    current_roads = rm_crop.copy()

# Create clean canvas for the rectified port & urban roads
new_port_roads = np.zeros((H_CROP, W_CROP), dtype=np.float32)

def draw_spline_road(canvas, pts, width_m, intensity=1.0):
    w_px = max(1.5, width_m / 3.4296)
    # Generate fine spline points
    pts = np.array(pts, dtype=np.float32)
    t = np.linspace(0, 1, len(pts))
    t_fine = np.linspace(0, 1, max(10, int(len(pts) * 20)))
    
    fx = np.poly1d(np.polyfit(t, pts[:, 0], deg=min(3, len(pts)-1)))
    fy = np.poly1d(np.polyfit(t, pts[:, 1], deg=min(3, len(pts)-1)))
    
    # Alternatively, direct polyline interpolation
    fine_pts = []
    for i in range(len(pts) - 1):
        pA, pB = pts[i], pts[i+1]
        dist = np.linalg.norm(pB - pA)
        n_steps = max(2, int(dist * 4))
        for step in range(n_steps):
            frac = step / float(n_steps)
            fine_pts.append(pA * (1.0 - frac) + pB * frac)
    fine_pts.append(pts[-1])
    fine_pts = np.array(fine_pts, dtype=np.int32).reshape((-1, 1, 2))
    
    temp = np.zeros_like(canvas, dtype=np.uint8)
    cv2.polylines(temp, [fine_pts], False, 255, int(round(w_px)), lineType=cv2.LINE_AA)
    # Anti-alias with slight gaussian blur
    temp_f = cv2.GaussianBlur(temp.astype(np.float32) / 255.0, (3, 3), 0.7)
    canvas[:] = np.maximum(canvas, temp_f * intensity)

def draw_circle_feature(canvas, center, radius_m, width_m=None, intensity=1.0):
    r_px = radius_m / 3.4296
    temp = np.zeros_like(canvas, dtype=np.uint8)
    if width_m is None:
        cv2.circle(temp, (int(round(center[0])), int(round(center[1]))), int(round(r_px)), 255, -1, lineType=cv2.LINE_AA)
    else:
        w_px = max(1.5, width_m / 3.4296)
        cv2.circle(temp, (int(round(center[0])), int(round(center[1]))), int(round(r_px)), 255, int(round(w_px)), lineType=cv2.LINE_AA)
    temp_f = cv2.GaussianBlur(temp.astype(np.float32) / 255.0, (3, 3), 0.7)
    canvas[:] = np.maximum(canvas, temp_f * intensity)

# 1. Place Pichon (Farhat Hached) - The grand circular garden roundabout
draw_circle_feature(new_port_roads, (470, 185), radius_m=16.0, width_m=8.0) # outer roadway
draw_circle_feature(new_port_roads, (470, 185), radius_m=4.0) # central monument island

# 2. Quai Ouest / Cours de la Marine (Waterfront Boulevard)
# Runs along the west harbor basin with wide promenade and tree-lined avenue
quai_ouest_pts = [
    [470, 185],
    [472, 240],
    [473, 320],
    [475, 420],
    [478, 520],
    [488, 580],
    [515, 640],
    [550, 680],
    [530, 750]  # connects to southern regional road exit
]
draw_spline_road(new_port_roads, quai_ouest_pts, width_m=12.0)

# 3. Quai Nord (Authentic Commercial Pier & Warehouses)
quai_nord_pts = [
    [472, 240],
    [520, 246],
    [570, 254],
    [625, 268],
    [665, 275]
]
draw_spline_road(new_port_roads, quai_nord_pts, width_m=14.0)
draw_circle_feature(new_port_roads, (665, 275), radius_m=9.0) # rounded pierhead platform

# 4. Jetée Nord (Outer Northern Breakwater Mole)
jetee_nord_pts = [
    [475, 160],
    [560, 162],
    [660, 164],
    [760, 166],
    [835, 168]
]
draw_spline_road(new_port_roads, jetee_nord_pts, width_m=7.0)
draw_circle_feature(new_port_roads, (835, 168), radius_m=5.0) # lighthouse pierhead

# 5. Épi Sud (South Pier - authentic straight concrete pier with rail deck)
epi_sud_pts = [
    [550, 680],
    [585, 665],
    [620, 635],
    [655, 575],
    [678, 510],
    [685, 465]
]
draw_spline_road(new_port_roads, epi_sud_pts, width_m=16.0)
draw_circle_feature(new_port_roads, (685, 465), radius_m=11.0) # turning loop / head

# 6. Avenue Habib Bourguiba / Boulevard René Millet (European Radial Grid)
av_bourguiba_pts = [
    [470, 185],
    [420, 145],
    [360, 100],
    [310, 50],
    [265, 0] # connects to north-west regional road exit
]
draw_spline_road(new_port_roads, av_bourguiba_pts, width_m=10.0)

av_victor_hugo_pts = [
    [470, 185],
    [440, 120],
    [410, 60],
    [390, 0] # connects to northern city road exit
]
draw_spline_road(new_port_roads, av_victor_hugo_pts, width_m=8.0)

corniche_beach_pts = [
    [470, 185],
    [474, 110],
    [475, 50],
    [478, 0] # connects to Boujaafar beach road
]
draw_spline_road(new_port_roads, corniche_beach_pts, width_m=8.0)

# 7. Medina East Rampart Promenade & Cross-Streets
medina_east_pts = [
    [420, 215],
    [416, 290],
    [413, 380],
    [410, 480],
    [406, 560],
    [400, 620]
]
draw_spline_road(new_port_roads, medina_east_pts, width_m=7.0)

# Cross streets connecting Medina gates to Quai Ouest
draw_spline_road(new_port_roads, [[415, 240], [472, 240]], width_m=6.0) # Douane
draw_spline_road(new_port_roads, [[413, 320], [473, 320]], width_m=6.0) # Grand Hotel
draw_spline_road(new_port_roads, [[411, 420], [475, 420]], width_m=6.0) # Cercle Militaire
draw_spline_road(new_port_roads, [[409, 520], [478, 520]], width_m=6.0) # Bab Jedid / Immeuble Pichon

# Merge new rectified roads with existing external roads:
# We preserve all existing roads in the Medina interior (x < 400) and outer regions
# and replace the distorted port area with new_port_roads.
harbor_rect_mask = np.zeros((H_CROP, W_CROP), dtype=np.float32)
# Define smooth transition mask around the port/waterfront area
cv2.rectangle(harbor_rect_mask, (400, 140), (950, 720), 1.0, -1)
harbor_rect_mask = cv2.GaussianBlur(harbor_rect_mask, (31, 31), 10.0)

# Combine: where harbor_rect_mask > 0.5, use new_port_roads; otherwise keep current_roads
blended_roads = (current_roads.astype(np.float32) / 255.0) * (1.0 - harbor_rect_mask) + new_port_roads * harbor_rect_mask
# Also ensure new roads are fully asserted wherever new_port_roads > 0.1
blended_roads = np.maximum(blended_roads, new_port_roads)
final_road_u8 = np.clip(blended_roads * 255.0, 0, 255).astype(np.uint8)

print("[*] Step 1 complete. Road network rectified.")

# =========================================================================
# 2. HARMONIZE TERRAIN HEIGHTMAP (PIER DECKS & DREDGED BASIN)
# =========================================================================
print("[*] Step 2: Harmonizing heightmap with authentic pier and basin geometry...")

# Baseline elevation tokens:
WATER_LEVEL   = -46.5 # Sea level / harbor water
BASIN_FLOOR   = -53.0 # Dredged harbor depth (6.5m draft)
PIER_DECK_ALT = -46.5 # Solid stone pier surface at water level
MOLE_CREST_ALT= -46.0 # Breakwater crest 0.5m above water

# Create pier land masks:
# A. Quai Nord solid polygon
quai_nord_poly = np.array([
    [472, 225],
    [665, 255],
    [675, 275],
    [665, 290],
    [520, 260],
    [472, 255],
], dtype=np.int32)

# B. Jetée Nord solid breakwater mole
jetee_nord_poly = np.array([
    [475, 153],
    [835, 161],
    [842, 168],
    [835, 175],
    [475, 167],
], dtype=np.int32)

# C. Épi Sud solid pier polygon
epi_sud_poly = np.array([
    [535, 680],
    [570, 665],
    [610, 630],
    [645, 570],
    [668, 510],
    [672, 465],
    [695, 465],
    [695, 510],
    [675, 575],
    [645, 640],
    [605, 675],
    [565, 690],
], dtype=np.int32)

# D. Inner dredged basin polygon (water enclosed by quays and piers)
basin_poly = np.array([
    [478, 255],
    [665, 290], # south edge of Quai Nord
    [672, 465], # north edge of Epi Sud
    [668, 510],
    [645, 570],
    [610, 630],
    [570, 665],
    [535, 680],
    [488, 580],
    [478, 520],
    [475, 420],
    [473, 320],
], dtype=np.int32)

# Create masks
mask_quai_nord = np.zeros((H_CROP, W_CROP), dtype=np.float32)
cv2.fillPoly(mask_quai_nord, [quai_nord_poly], 1.0)
mask_quai_nord = cv2.GaussianBlur(mask_quai_nord, (5, 5), 1.0)

mask_jetee_nord = np.zeros((H_CROP, W_CROP), dtype=np.float32)
cv2.fillPoly(mask_jetee_nord, [jetee_nord_poly], 1.0)
cv2.circle(mask_jetee_nord, (835, 168), 7, 1.0, -1)
mask_jetee_nord = cv2.GaussianBlur(mask_jetee_nord, (5, 5), 1.0)

mask_epi_sud = np.zeros((H_CROP, W_CROP), dtype=np.float32)
cv2.fillPoly(mask_epi_sud, [epi_sud_poly], 1.0)
cv2.circle(mask_epi_sud, (685, 465), 13, 1.0, -1)
mask_epi_sud = cv2.GaussianBlur(mask_epi_sud, (5, 5), 1.0)

mask_basin = np.zeros((H_CROP, W_CROP), dtype=np.float32)
cv2.fillPoly(mask_basin, [basin_poly], 1.0)
mask_basin = cv2.GaussianBlur(mask_basin, (9, 9), 2.0)

# Subtract pier decks from basin mask so piers remain solid
solid_piers = np.maximum.reduce([mask_quai_nord, mask_jetee_nord, mask_epi_sud])
pure_basin_water = np.clip(mask_basin - solid_piers * 1.5, 0.0, 1.0)

# Update heightmap in harbor:
# 1. Quai Nord deck at PIER_DECK_ALT
hm_crop = hm_crop * (1.0 - mask_quai_nord) + PIER_DECK_ALT * mask_quai_nord

# 2. Jetée Nord crest at MOLE_CREST_ALT
hm_crop = hm_crop * (1.0 - mask_jetee_nord) + MOLE_CREST_ALT * mask_jetee_nord

# 3. Épi Sud deck at PIER_DECK_ALT
hm_crop = hm_crop * (1.0 - mask_epi_sud) + PIER_DECK_ALT * mask_epi_sud

# 4. Dredge inner basin floor to BASIN_FLOOR
hm_crop = hm_crop * (1.0 - pure_basin_water) + BASIN_FLOOR * pure_basin_water

# 5. Guarantee dry land under ALL road pixels:
# For every pixel where final_road_u8 > 30, elevation MUST be >= -46.5m!
road_submerged = (final_road_u8 > 30) & (hm_crop < WATER_LEVEL)
num_submerged = np.sum(road_submerged)
print(f"[*] Submerged road pixels before elevation guarantee: {num_submerged}")
if num_submerged > 0:
    hm_crop[road_submerged] = WATER_LEVEL # lift onto solid dry deck!

# Smooth any micro-steps on pier surfaces
hm_crop = np.clip(hm_crop, MIN_H, MAX_H)
print("[*] Step 2 complete. Heightmap synchronized with zero submerged road pixels.")

# =========================================================================
# 3. HARMONIZE TEXTUREMAP (SPLATMAPS FOR QUAYS, BREAKWATERS, AND RIPRAP)
# =========================================================================
print("[*] Step 3: Updating textureMap for authentic materials...")

# tm_crop is BGR: B=riprap boulders, G=breakwater cut stone, R=quayside paving
# Clear old harbor textures in the active port box
harbor_box_mask = np.zeros((H_CROP, W_CROP), dtype=np.float32)
cv2.rectangle(harbor_box_mask, (460, 130), (900, 720), 1.0, -1)

# Quayside paving (R channel):
# Quai Ouest promenade + Quai Nord deck + Place Pichon + urban avenues
tm_crop[:, :, 2] = np.clip(
    tm_crop[:, :, 2].astype(np.float32) * (1.0 - harbor_box_mask) +
    (mask_quai_nord * 255.0) +
    (np.clip(new_port_roads * 1.5, 0.0, 1.0) * 220.0),
    0, 255
).astype(np.uint8)

# Breakwater cut stone (G channel):
# Jetée Nord crest + Épi Sud pierhead
tm_crop[:, :, 1] = np.clip(
    tm_crop[:, :, 1].astype(np.float32) * (1.0 - harbor_box_mask) +
    (mask_jetee_nord * 255.0) +
    (mask_epi_sud * 180.0),
    0, 255
).astype(np.uint8)

# Riprap boulders (B channel):
# Outer seawall of Jetée Nord and eastern flank of Épi Sud
outer_riprap_mask = np.zeros((H_CROP, W_CROP), dtype=np.float32)
# Draw outer boundary lines with 3px width
cv2.polylines(outer_riprap_mask, [epi_sud_poly[5:]], False, 1.0, 4, lineType=cv2.LINE_AA)
cv2.polylines(outer_riprap_mask, [jetee_nord_poly[:2]], False, 1.0, 3, lineType=cv2.LINE_AA)
outer_riprap_mask = cv2.GaussianBlur(outer_riprap_mask, (3, 3), 0.8)

tm_crop[:, :, 0] = np.clip(
    tm_crop[:, :, 0].astype(np.float32) * (1.0 - harbor_box_mask) +
    (outer_riprap_mask * 255.0),
    0, 255
).astype(np.uint8)

print("[*] Step 3 complete. TextureMap materials harmonized.")

# =========================================================================
# 4. WRITE BACK TO ASSETS
# =========================================================================
print("[*] Step 4: Writing updated maps to disk...")

# 1. Update roadMap.png
if rm_img.ndim == 3:
    rm_img[r0:r1, c0:c1, 2] = final_road_u8 # Red channel = Roads
else:
    rm_img[r0:r1, c0:c1] = final_road_u8

cv2.imwrite(ROADMAP_PATH, rm_img)
print(f"[+] Saved {ROADMAP_PATH}")

# 2. Update heightmap.png
h_m[r0:r1, c0:c1] = hm_crop

# Global dry-land guarantee: every painted road pixel on the entire terrain
# must be on solid dry land (>= -46.5m).
road_mask_full = (rm_img[:, :, 2] > 30)
submerged_full = road_mask_full & (h_m < WATER_LEVEL)
num_submerged_full = np.sum(submerged_full)
print(f"[*] Global submerged road pixels before elevation guarantee: {num_submerged_full}")
if num_submerged_full > 0:
    h_m[submerged_full] = WATER_LEVEL

# Verify 0 underwater road pixels
submerged_check = (rm_img[:, :, 2] > 30) & (h_m < WATER_LEVEL)
print(f"[+] Final global underwater road pixels: {np.sum(submerged_check)} (Target: 0)")
assert np.sum(submerged_check) == 0, "Invariant violation: road pixels under water!"

hm_u16_final = np.clip((h_m - MIN_H) / H_RANGE * 65535.0, 0, 65535).astype(np.uint16)
Image.fromarray(hm_u16_final).save(HEIGHTMAP_PATH)
print(f"[+] Saved {HEIGHTMAP_PATH}")

# 3. Update textureMap.png
tm_img[r0:r1, c0:c1] = tm_crop
cv2.imwrite(TEXTUREMAP_PATH, tm_img)
print(f"[+] Saved {TEXTUREMAP_PATH}")

# =========================================================================
# 5. VERIFICATION & DIAGNOSTIC VISUALIZATION
# =========================================================================
print("[*] Step 5: Generating verification diagnostic images...")

# Generate hillshade of the rectified port region
def compute_hillshade(elev, light_az=315.0, light_el=45.0):
    az_rad = np.radians(light_az)
    el_rad = np.radians(light_el)
    dx = cv2.Sobel(elev, cv2.CV_32F, 1, 0, ksize=3) / (8.0 * 3.4296)
    dy = cv2.Sobel(elev, cv2.CV_32F, 0, 1, ksize=3) / (8.0 * 3.4296)
    slope = np.arctan(np.sqrt(dx**2 + dy**2))
    aspect = np.arctan2(dy, -dx)
    shaded = np.sin(el_rad) * np.cos(slope) + np.cos(el_rad) * np.sin(slope) * np.cos(az_rad - aspect)
    return np.clip(shaded, 0.0, 1.0)

hs = compute_hillshade(hm_crop)
hs_u8 = (hs * 255.0).astype(np.uint8)
hs_bgr = cv2.cvtColor(hs_u8, cv2.COLOR_GRAY2BGR)

# Composite: Hillshade + Water Tint + Yellow Roads
comp_vis = hs_bgr.copy()
water_mask_vis = (hm_crop < WATER_LEVEL)
comp_vis[water_mask_vis, 0] = np.clip(comp_vis[water_mask_vis, 0] * 0.4 + 110, 0, 255) # Blue
comp_vis[water_mask_vis, 1] = np.clip(comp_vis[water_mask_vis, 1] * 0.4 + 60, 0, 255)  # Green
comp_vis[water_mask_vis, 2] = np.clip(comp_vis[water_mask_vis, 2] * 0.4 + 20, 0, 255)  # Red

road_overlay = (final_road_u8 > 30)
comp_vis[road_overlay] = cv2.addWeighted(comp_vis[road_overlay], 0.15, np.full_like(comp_vis[road_overlay], (0, 255, 255)), 0.85, 0)

cv2.imwrite("scratch/rectified_port_roads_hillshade_verification.png", comp_vis)

# Also composite directly with the 1943 rectified photo
warped_photo = cv2.imread("scratch/rectified_1943_photo_terrain_space.png")
photo_comp = cv2.addWeighted(warped_photo, 0.55, comp_vis, 0.45, 0)
cv2.imwrite("scratch/rectified_roads_vs_1943_photo_composite.png", photo_comp)

print("[+] Verification images saved:")
print("    - scratch/rectified_port_roads_hillshade_verification.png")
print("    - scratch/rectified_roads_vs_1943_photo_composite.png")
print("[*] All photogrammetric rectification tasks successfully executed!")
