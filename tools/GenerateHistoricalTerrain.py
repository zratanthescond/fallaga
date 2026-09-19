import os
import re
import math
import shutil
from PIL import Image, ImageDraw, ImageFilter
import numpy as np

# ─────────────────────────────────────────────────────────────
# CONSTANTS & CONFIGURATION (UPDATED FOR 10km² SQUARE TERRAIN)
# ─────────────────────────────────────────────────────────────
MIN_X, MAX_X = -13938.0, 13938.0
MIN_Z, MAX_Z = -13938.0, 13938.0
MIN_H = -40.0
MAX_H = 87.8

TERRAIN_DIR = "c:/Users/honco/OneDrive/Bureau/fallaga/assets/terrain"
HEIGHTMAP_PATH = os.path.join(TERRAIN_DIR, "heightmap.png")
BACKUP_PATH = os.path.join(TERRAIN_DIR, "heightmap_user_base.png")
ROAD_MAP_PATH = os.path.join(TERRAIN_DIR, "roadMap.png")
TEXTURE_MAP_PATH = os.path.join(TERRAIN_DIR, "textureMap.png")
H_PATH = "c:/Users/honco/OneDrive/Bureau/fallaga/src/ProceduralMapData.h"

# ─────────────────────────────────────────────────────────────
# 1. BACKUP & LOAD USER BASE HEIGHTMAP
# ─────────────────────────────────────────────────────────────
if not os.path.exists(BACKUP_PATH):
    print(f"[TerrainGen] Creating backup of original user heightmap to {BACKUP_PATH}...")
    shutil.copyfile(HEIGHTMAP_PATH, BACKUP_PATH)

print(f"[TerrainGen] Loading base heightmap from {BACKUP_PATH}...")
base_img = Image.open(BACKUP_PATH)
W, H = base_img.size
print(f"[TerrainGen] Base heightmap resolution: {W}x{H}, mode: {base_img.mode}")

# Convert 16-bit grayscale image to float32 numpy array in meters [MIN_H, MAX_H]
if base_img.mode == 'I;16':
    heights = MIN_H + (np.array(base_img, dtype=np.float32) / 65535.0) * (MAX_H - MIN_H)
else:
    # Fallback for 8-bit
    heights = MIN_H + (np.array(base_img.convert('L'), dtype=np.float32) / 255.0) * (MAX_H - MIN_H)

# ─────────────────────────────────────────────────────────────
# 2. PARSE STREETS FROM C++ SOURCE
# ─────────────────────────────────────────────────────────────
print(f"[TerrainGen] Parsing streets from {H_PATH}...")
with open(H_PATH, "r", encoding="utf-8") as f:
    content = f.read()

street_pattern = re.compile(r'add\(\s*"([^"]+)"\s*,\s*([\d.]+)f\s*,\s*\{(.*?)\}\s*\);', re.DOTALL)
point_pattern = re.compile(r'\{\s*(-?[\d.]+)\s*,\s*(-?[\d.]+)\s*\}')

streets = []
for match in street_pattern.finditer(content):
    name = match.group(1)
    width = float(match.group(2))
    points_str = match.group(3)
    
    pts = []
    for p_match in point_pattern.finditer(points_str):
        pts.append((float(p_match.group(1)), float(p_match.group(2))))
        
    if len(pts) >= 2:
        streets.append({"name": name, "width": width, "points": pts})

print(f"[TerrainGen] Successfully parsed {len(streets)} streets.")

# ─────────────────────────────────────────────────────────────
# 3. DEFINE RIVERS AND RAILWAYS
# ─────────────────────────────────────────────────────────────
# River 1: Oued Blibene (central-north, winding near Medina)
oued_blibene = [
    (-1581, 1150), (-900, 1080), (-600, 980), (-300, 920), 
    (0, 950), (300, 920), (600, 880), (780, 900)
]

# River 2: Oued El Kharoub (further north, near Khezama)
oued_elkharoub = [
    (-1581, 1450), (-900, 1380), (-500, 1320), (0, 1260), 
    (400, 1220), (700, 1190), (950, 1180)
]

rivers = [
    {"name": "Oued Blibene", "width": 28.0, "points": oued_blibene},
    {"name": "Oued El Kharoub", "width": 22.0, "points": oued_elkharoub}
]

# Railway lines
railway_main = [
    (-260, -1581), (-240, -1000), (-220, -600), (-200, -300), 
    (-150, 0), (-73, 250), (-50, 500), (0, 800), 
    (50, 1200), (100, 1581)
]
railway_port = [
    (-73, 250), (100, 150), (300, 50), (650, 0), (800, 0)
]

railways = [
    {"name": "Main Railway Line", "width": 8.0, "points": railway_main},
    {"name": "Port Railway Branch", "width": 6.0, "points": railway_port}
]

# Port definition
PORT_QUAYS = [
    [(650, 200), (800, 200), (800, -420), (650, -420), (650, 200)],
    [(880, 50), (940, 50), (940, -10), (880, -10), (880, 50)],
    [(1050, 200), (1200, 200), (1200, 50), (1100, 0), (1050, 80), (1050, 200)],
    [(1100, -150), (1200, -220), (1200, -420), (950, -420), (1050, -300), (1100, -150)],
    [(780, -250), (830, -300), (810, -350), (780, -320), (780, -250)]
]
PORT_WATER = [
    [(750, 250), (1100, 250), (1100, -600), (750, -600)]
]

# ─────────────────────────────────────────────────────────────
# HELPER: COORDINATE CONVERSION
# ─────────────────────────────────────────────────────────────
def world_to_pixel(wx, wz, width, height):
    col = (wx - MIN_X) / (MAX_X - MIN_X) * (width - 1)
    row = (MAX_Z - wz) / (MAX_Z - MIN_Z) * (height - 1)
    return col, row

# ─────────────────────────────────────────────────────────────
# 4. COMPUTE ROAD/RAIL MASKS FOR FLATTENING
# ─────────────────────────────────────────────────────────────
print("[TerrainGen] Drawing road/rail masks...")
road_mask_img = Image.new('L', (W, H), 0)
road_draw = ImageDraw.Draw(road_mask_img)

rail_mask_img = Image.new('L', (W, H), 0)
rail_draw = ImageDraw.Draw(rail_mask_img)

# Draw roads
for street in streets:
    pts_px = [world_to_pixel(p[0], p[1], W, H) for p in street["points"]]
    w_px = street["width"] / (MAX_X - MIN_X) * W
    road_draw.line(pts_px, fill=255, width=max(1, int(round(w_px))))

# Draw railways
for rail in railways:
    pts_px = [world_to_pixel(p[0], p[1], W, H) for p in rail["points"]]
    w_px = rail["width"] / (MAX_X - MIN_X) * W
    rail_draw.line(pts_px, fill=255, width=max(1, int(round(w_px))))

road_mask = np.array(road_mask_img, dtype=np.float32) / 255.0
rail_mask = np.array(rail_mask_img, dtype=np.float32) / 255.0

# ─────────────────────────────────────────────────────────────
# 5. HEIGHTMAP PROCESSING (pixel-faithful + road flattening)
# ─────────────────────────────────────────────────────────────
print("[TerrainGen] Processing heightmap (pixel-faithful with road flattening)...")

# A. Dredge Port Basin & Build Quays (gameplay feature)
for w_poly in PORT_WATER:
    poly_px = [world_to_pixel(p[0], p[1], W, H) for p in w_poly]
    mask_img = Image.new('L', (W, H), 0)
    ImageDraw.Draw(mask_img).polygon(poly_px, fill=255)
    mask_arr = np.array(mask_img, dtype=bool)
    heights[mask_arr] = np.minimum(heights[mask_arr], -43.0)

for q_poly in PORT_QUAYS:
    poly_px = [world_to_pixel(p[0], p[1], W, H) for p in q_poly]
    mask_img = Image.new('L', (W, H), 0)
    ImageDraw.Draw(mask_img).polygon(poly_px, fill=255)
    mask_arr = np.array(mask_img, dtype=bool)
    heights[mask_arr] = -36.0

# B. Flatten roads & railways only (all natural terrain stays pixel-perfect)
def box_blur_2d(arr, r):
    r = int(r)
    if r < 1: return arr.copy()
    res = np.copy(arr)
    cumsum = np.cumsum(np.insert(res, 0, 0, axis=1), axis=1)
    for x in range(res.shape[1]):
        x0 = max(0, x - r)
        x1 = min(res.shape[1] - 1, x + r)
        res[:, x] = (cumsum[:, x1 + 1] - cumsum[:, x0]) / (x1 - x0 + 1)
    cumsum = np.cumsum(np.insert(res, 0, 0, axis=0), axis=0)
    for y in range(res.shape[0]):
        y0 = max(0, y - r)
        y1 = min(res.shape[0] - 1, y + r)
        res[y, :] = (cumsum[y1 + 1, :] - cumsum[y0, :]) / (y1 - y0 + 1)
    return res

print("[TerrainGen] Smoothing roads & railways...")
try:
    from scipy.ndimage import gaussian_filter
    road_mask_smooth = gaussian_filter(road_mask, sigma=W*0.003)
    rail_mask_smooth = gaussian_filter(rail_mask, sigma=W*0.003)
    blurred_heights = gaussian_filter(heights, sigma=W*0.015)
except ImportError:
    road_mask_smooth = box_blur_2d(road_mask, W*0.003)
    rail_mask_smooth = box_blur_2d(rail_mask, W*0.003)
    blurred_heights = box_blur_2d(heights, W*0.015)

heights = heights * (1.0 - road_mask_smooth) + blurred_heights * road_mask_smooth
heights = heights * (1.0 - rail_mask_smooth) + blurred_heights * rail_mask_smooth

# No river carving — natural wadis come from the real heightmap
# No noise injection — 100% faithful to source for all non-road terrain

# Ensure heights are clamped to valid range
heights = np.clip(heights, MIN_H, MAX_H)

# Save the final heightmap
print(f"[TerrainGen] Saving final heightmap to {HEIGHTMAP_PATH}...")
final_heights_16 = ((heights - MIN_H) / (MAX_H - MIN_H) * 65535.0)
final_heights_16 = np.clip(final_heights_16, 0.0, 65535.0).astype(np.uint16)
final_heightmap_img = Image.fromarray(final_heights_16, mode='I;16')
final_heightmap_img.save(HEIGHTMAP_PATH)
print("[TerrainGen] Heightmap saved successfully.")

# ─────────────────────────────────────────────────────────────
# 6. GENERATE ROADMAP (RGBA)
# ─────────────────────────────────────────────────────────────
print(f"[TerrainGen] Generating roadMap.png at {ROAD_MAP_PATH}...")
RM_W, RM_H = 2048, 2048
road_map_img = Image.new('RGBA', (RM_W, RM_H), (0, 0, 0, 255))
rm_draw = ImageDraw.Draw(road_map_img)

# Draw roads (Red)
for street in streets:
    pts_px = [world_to_pixel(p[0], p[1], RM_W, RM_H) for p in street["points"]]
    w_px = street["width"] / (MAX_X - MIN_X) * RM_W
    rm_draw.line(pts_px, fill=(255, 0, 0, 255), width=max(1, int(round(w_px))))

# Draw railways (Green)
for rail in railways:
    pts_px = [world_to_pixel(p[0], p[1], RM_W, RM_H) for p in rail["points"]]
    w_px = rail["width"] / (MAX_X - MIN_X) * RM_W
    rm_draw.line(pts_px, fill=(0, 255, 0, 255), width=max(1, int(round(w_px))))

# Draw rivers (Blue)
for river in rivers:
    pts_px = [world_to_pixel(p[0], p[1], RM_W, RM_H) for p in river["points"]]
    w_px = river["width"] / (MAX_X - MIN_X) * RM_W
    rm_draw.line(pts_px, fill=(0, 0, 255, 255), width=max(1, int(round(w_px))))

road_map_img.save(ROAD_MAP_PATH)
print("[TerrainGen] roadMap.png saved successfully.")

# ─────────────────────────────────────────────────────────────
# 7. GENERATE TEXTUREMAP (RGBA)
# ─────────────────────────────────────────────────────────────
print(f"[TerrainGen] Generating textureMap.png at {TEXTURE_MAP_PATH}...")
TM_W, TM_H = 512, 512
texture_map_img = Image.new('RGBA', (TM_W, TM_H), (0, 0, 0, 255))
tm_draw = ImageDraw.Draw(texture_map_img)

for q_poly in PORT_QUAYS:
    poly_px = [world_to_pixel(p[0], p[1], TM_W, TM_H) for p in q_poly]
    tm_draw.polygon(poly_px, fill=(255, 0, 0, 255))

texture_map_img.save(TEXTURE_MAP_PATH)
print("[TerrainGen] textureMap.png saved successfully.")
print("[TerrainGen] All terrain assets generated successfully!")
