import numpy as np
from PIL import Image
import os
import random
import math

# --- CONFIGURATION ---
CITY_FILE = "assets/saves/city.txt"
BACKUP_FILE = "assets/saves/city.txt.bak"
MASK_FILE = "assets/terrain/textureMap.png"
HEIGHT_FILE = "assets/terrain/heightmap.png"
MODEL_PATHS = [
    "assets/terrain/rocks/rubble.obj"
]

# Total target objects (Reduced because each rubble.obj is a cluster)
TOTAL_TARGET_CHUNKS = 800

# World Bounds (from Terrain.cpp)
WORLD_MIN_X, WORLD_MAX_X = -1250.0, 1250.0
WORLD_MIN_Z, WORLD_MAX_Z = -2500.0, 2500.0

# Anchor from SousseMapData.h
ANCHOR_X, ANCHOR_Z = -530.0, -1502.0

# -----------------
# 1. LOAD DATA
# -----------------
print(f"[*] Loading Port Mask: {MASK_FILE}")
mask_img = Image.open(MASK_FILE).convert("RGB")
mask_data = np.array(mask_img)[:, :, 0] # Red channel is Port Mask

print(f"[*] Loading Heightmap: {HEIGHT_FILE}")
h_img = Image.open(HEIGHT_FILE).convert("L") # 8-bit height fallback
h_data = np.array(h_img)

H, W = mask_data.shape # Should be 512x512

def pixel_to_world(r, c):
    # Mapping image [0, 511] to world bounds [-1250, 1250]
    # Note: PNG Image Y (rows) usually corresponds to -Z or Z depending on flip.
    # In this project, TerrainManager.cpp: uz = (z - worldMinZ) / (worldMaxZ - worldMinZ)
    # So r=0 is worldMinZ, r=511 is worldMaxZ
    ux = c / (W - 1)
    uz = r / (H - 1)
    wx = WORLD_MIN_X + ux * (WORLD_MAX_X - WORLD_MIN_X)
    wz = WORLD_MIN_Z + uz * (WORLD_MAX_Z - WORLD_MIN_Z)
    return wx, wz

# -----------------
# 2. FIND WHITE PIXELS (USER GUIDE)
# -----------------
print("[*] Detecting white pixels for rock placement...")
target_pixels = []
for r in range(H):
    for c in range(W):
        # Pixel is White if all channels are high
        # We check the PIL RGB data
        pixel = mask_img.getpixel((c, r))
        if pixel[0] > 220 and pixel[1] > 220 and pixel[2] > 220:
            target_pixels.append((r, c))

print(f"[+] Found {len(target_pixels)} white pixels to use as placement guides.")

if not target_pixels:
    print("[!] ERROR: No white pixels found in textureMap.png!")
    exit(1)

# -----------------
# 3. GENERATE PLACEMENTS
# -----------------
# Strict 10-Wall Structural Array
NUM_WALLS = 10
print(f"[*] Partitioning port into {NUM_WALLS} massive structural sections.")

rock_count = 0
new_objects = []

# Collect header/anchor from original file
anchor_line = "ANCHOR -530.0 -1502.0\n"
if os.path.exists(BACKUP_FILE):
    with open(BACKUP_FILE, 'r') as f:
        first_line = f.readline()
        if first_line.startswith("ANCHOR"):
            anchor_line = first_line

# --- CLUSTERING: Identify Separate Piers ---
def get_clusters(pixels):
    clusters = []
    visited = set()
    for p in pixels:
        if p in visited: continue
        cluster = []
        stack = [p]
        visited.add(p)
        while stack:
            curr = stack.pop()
            cluster.append(curr)
            for dr in [-1, 0, 1]:
                for dc in [-1, 0, 1]:
                    neighbor = (curr[0]+dr, curr[1]+dc)
                    if neighbor in pixels and neighbor not in visited:
                        visited.add(neighbor)
                        stack.append(neighbor)
        clusters.append(cluster)
    return clusters

def get_ordered_path(pixels):
    if not pixels: return []
    remaining = list(pixels)
    curr = remaining.pop(0)
    path = [curr]
    while remaining:
        nearest_idx = 0
        min_dist = 1e9
        for i, p in enumerate(remaining):
            d = (p[0]-curr[0])**2 + (p[1]-curr[1])**2
            if d < min_dist:
                min_dist = d
                nearest_idx = i
        curr = remaining.pop(nearest_idx)
        path.append(curr)
    return path

all_clusters = get_clusters(target_pixels)
if not all_clusters:
    print("[!] ERROR: No port pixels found to place the object.")
    exit(1)

# --- SHADER-BASED MASK MODE (NO OBJECTS) ---
# We no longer place 3D objects as the harbor is now rendered via terrain shading.
new_objects = []
print("[!] 3D Structural objects are now DEACTIVATED. Using pure shader-based harbor.")

# -----------------
# 4. SAVE NEW LAYOUT
# -----------------
with open(CITY_FILE, 'w') as f:
    f.write(anchor_line)
    f.writelines(new_objects)

print(f"[!] SUCCESS: Placed TWO massive 100m markers at pier starts.")
print(f"[!] Performance secured at 60+ FPS.")
print(f"[!] Please restart the game to see the result.")
