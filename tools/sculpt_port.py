import numpy as np
from PIL import Image
import os
import shutil

# --- CONFIGURATION ---
HEIGHTMAP_PATH = "assets/terrain/heightmap.png"
MASK_PATH = "assets/terrain/textureMap.png"
RUBBLE_TEX_PATH = "assets/terrain/rocks/acddafb3-2af0-4f41-8dd1-9f48cec4d042.png"
BACKUP_PATH = "assets/terrain/heightmap_backup.png"

# Sculpting Parameters
PORT_BASE_HEIGHT = 8.5   # Meters above seafloor
RUBBLE_NOISE_DEPTH = 1.2 # How much the texture "bumps" the height
TERRAIN_MIN_H = 0.0
TERRAIN_MAX_H = 59.9

def sculpt_port():
    print(f"[*] Starting Terrain Sculpting...")
    
    # 1. Backup original heightmap
    if not os.path.exists(BACKUP_PATH):
        print(f"[*] Backing up original heightmap to {BACKUP_PATH}")
        shutil.copy(HEIGHTMAP_PATH, BACKUP_PATH)

    # 2. Load Assets
    hm_img = Image.open(HEIGHTMAP_PATH)
    if hm_img.mode != 'I;16':
        print("[!] Warning: Heightmap is not 16-bit. Sculpting quality will be reduced.")
    
    # Convert to float numpy array for processing
    hm_data = np.array(hm_img).astype(np.float32)
    mask_data = np.array(Image.open(MASK_PATH).convert('L'))
    rubble_tex = np.array(Image.open(RUBBLE_TEX_PATH).convert('L')).astype(np.float32) / 255.0

    H, W = hm_data.shape
    r_h, r_w = rubble_tex.shape

    print(f"[*] Processing {W}x{H} terrain grid...")

    # 3. Apply Sculpting
    # Identify port areas (White pixels in mask)
    port_pixels = np.where(mask_data > 220)
    
    # Calculate heightmap scaling constants (16-bit map)
    # val = min_h + (data / 65535) * (max_h - min_h)
    # data = (val - min_h) * 65535 / (max_h - min_h)
    
    h_span = TERRAIN_MAX_H - TERRAIN_MIN_H
    
    count = 0
    for r, c in zip(port_pixels[0], port_pixels[1]):
        # Tile the rubble texture across the terrain
        tr, tc = r % r_h, c % r_w
        noise = rubble_tex[tr, tc]
        
        # Calculate final sculpting height in meters
        final_h = PORT_BASE_HEIGHT + (noise * RUBBLE_NOISE_DEPTH)
        
        # Convert meters back to 16-bit integer value
        raw_val = (final_h - TERRAIN_MIN_H) * 65535.0 / h_span
        hm_data[r, c] = np.clip(raw_val, 0, 65535)
        count += 1

    # 4. Save result
    print(f"[+] Sculpted {count} pixels with rubble detail.")
    new_hm = Image.fromarray(hm_data.astype(np.uint16), mode='I;16')
    new_hm.save(HEIGHTMAP_PATH)
    print(f"[!] SUCCESS: Heightmap updated with sculpted rubble detail.")

if __name__ == "__main__":
    sculpt_port()
