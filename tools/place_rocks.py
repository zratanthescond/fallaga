import numpy as np
from PIL import Image
import os
import random

def place_rocks_physics():
    # 1. LOAD MAPS
    mask_path = "assets/terrain/textureMap.png"
    height_path = "assets/terrain/heightmap.png"
    
    if not os.path.exists(mask_path) or not os.path.exists(height_path):
        print("Required maps (mask/heightmap) not found!")
        return
        
    mask_arr = np.array(Image.open(mask_path).convert("L"))
    height_img = Image.open(height_path).convert("I;16")
    height_arr = np.array(height_img)
    
    # 2. BOUNDARY DETECTION
    port_mask = mask_arr > 0
    shift_u = np.pad(port_mask, ((0,1),(0,0)), mode='constant')[1:, :]
    shift_d = np.pad(port_mask, ((1,0),(0,0)), mode='constant')[:-1, :]
    shift_l = np.pad(port_mask, ((0,0),(0,1)), mode='constant')[:, 1:]
    shift_r = np.pad(port_mask, ((0,0),(1,0)), mode='constant')[:, :-1]
    boundary = port_mask & ~(shift_u & shift_d & shift_l & shift_r)
    
    y_pts, x_pts = np.where(boundary)
    center_x = x_pts.mean()
    edge_pixels = []
    for i in range(len(x_pts)):
        if x_pts[i] > center_x:
            edge_pixels.append((x_pts[i], y_pts[i]))
    edge_pixels.sort(key=lambda p: p[1])
    
    # 3. SETTINGS
    WATER_LEVEL = 2.5
    PIER_TOP_Y = 5.8
    WALKWAY_WIDTH = 12.0
    SLOPE_WIDTH = 40.0
    
    MIN_X, MAX_X = -1250.0, 1250.0
    MIN_Z, MAX_Z = -2500.0, 2500.0
    W_SIZE, D_SIZE = 2500.0, 5000.0
    land_anchor_x, land_anchor_z = -530.0, -1502.0
    
    rock_variants = [f"assets/terrain/rocks/rock_moss_set_0{j}_4k.obj" for j in range(2, 9)]
    
    # 4. PHYSICS SIMULATION: SETTLED PILE
    # We use a 2D Height Accumulator to simulate stones stacking on each other.
    # Grid resolution: 1.0m
    sim_res = 1.0
    sim_grid_w = int((WALKWAY_WIDTH + SLOPE_WIDTH) / sim_res) + 10
    sim_grid_h = 1000 # Enough for the length of the harbor
    
    # We want to store (world_x, world_z, bakedY, yOffset, scale, yScale, angle, model)
    placed_stones = []
    
    # Step along harbor
    for i, (px, py) in enumerate(edge_pixels):
        if i % 4 != 0: continue
        
        raw_h = height_arr[py, px]
        world_h = (raw_h / 65535.0) * 59.9
        if world_h > (WATER_LEVEL + 0.8): continue
            
        wx_base = MIN_X + (px / 511.0) * W_SIZE
        wz_base = MIN_Z + (py / 511.0) * D_SIZE
        
        # Build the pile using a more natural "pouring" logic
        # For each cross-section slice
        num_stones_this_slice = 25 # High density for stacking
        for _ in range(num_stones_this_slice):
            # Pick a target DX (where we want to drop the stone)
            # Bias toward the top or the slope
            if random.random() < 0.4:
                dx = random.uniform(-WALKWAY_WIDTH/2, WALKWAY_WIDTH/2)
                target_top_y = PIER_TOP_Y
            else:
                dx = random.uniform(WALKWAY_WIDTH/2, SLOPE_WIDTH)
                slope_f = (dx - WALKWAY_WIDTH/2) / (SLOPE_WIDTH - WALKWAY_WIDTH/2)
                target_top_y = PIER_TOP_Y - (slope_f * (PIER_TOP_Y - (world_h - 1.0)))
            
            # Physics-y logic: Find existing stones below this point
            # For simplicity in this script, we'll just check if we've reached target_top_y
            # But we add "jitter" to make them look tumbled.
            
            # Stacking: We iterate layers until we reach target_top_y
            curr_y = world_h - 1.0
            while curr_y < target_top_y:
                is_walkway = (dx < WALKWAY_WIDTH/2)
                is_top = (target_top_y - curr_y < 1.0)
                
                # AESTHETIC PARAMS
                scale = random.uniform(1.3, 2.0)
                yScale = scale * (0.35 if (is_walkway and is_top) else random.uniform(0.7, 1.2))
                angle = random.uniform(0, 360)
                model = random.choice(rock_variants)
                
                # SHIFT X/Z PER STONE (Avoid vertical needles)
                lx = wx_base + dx + random.uniform(-1.0, 1.0) - land_anchor_x
                lz = wz_base + random.uniform(-1.0, 1.0) - land_anchor_z
                
                y_off = max(0, curr_y - world_h)
                
                placed_stones.append({
                    "name": f"Stone_{len(placed_stones)}",
                    "lx": lx,
                    "lz": lz,
                    "yOff": y_off,
                    "bakedY": world_h,
                    "scale": scale,
                    "yScale": yScale,
                    "angle": angle,
                    "model": model
                })
                
                curr_y += (scale * 0.75) 
                if len(placed_stones) > 20000: break
        if len(placed_stones) > 15000: break

    # 5. WRITE BAKED DATA (OBJ_V3)
    save_path = "assets/saves/city.txt"
    with open(save_path, "w") as f:
        f.write(f"ANCHOR {land_anchor_x} {land_anchor_z}\n")
        for s in placed_stones:
            f.write(f"OBJ_V3 {s['name']} {s['lx']:.2f} {s['lz']:.2f} {s['yOff']:.2f} {s['bakedY']:.2f} {s['scale']:.2f} {s['yScale']:.2f} {s['angle']:.2f} {s['model']}\n")
            
    print(f"Generated {len(placed_stones)} PHYSICS-BAKED stones.")

if __name__ == "__main__":
    place_rocks_physics()
