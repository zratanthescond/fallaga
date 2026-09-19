import numpy as np
from PIL import Image
import cv2
import os

print("[*] Applying Accurate Port Basin & Quayside Shape to heightmap.png...")

hm_path = "assets/terrain/heightmap.png"
hm_img = Image.open(hm_path)
hm = np.array(hm_img)
H, W = hm.shape

rm_img = Image.open("assets/terrain/roadMap_user_raw.png")
rm = np.array(rm_img.resize((W, H), Image.Resampling.BILINEAR))[:, :, 0]

# 16-bit to meters: -70.0 + (val / 65535.0) * 157.8
h_m = -70.0 + (hm.astype(np.float32) / 65535.0) * 157.8

# Port geometry:
c_quay = 5326  # West Quay coping stone line

# Breakwaters (North & South moles from user drawing)
bw_strokes = (rm > 100) & (np.arange(W)[None, :] >= c_quay) & (np.arange(H)[:, None] >= 3840) & (np.arange(H)[:, None] <= 4320)
kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (9, 9))
bw_solid = cv2.dilate(bw_strokes.astype(np.uint8), kernel) > 0

# Dry land:
# Place Pichon, Square de la Marine, Cours de la Marine avenue, and building blocks:
land_mask = np.zeros((H, W), dtype=bool)
land_mask[3750:4250, 5130:c_quay] = True

# Basin water:
# Inside the port basin bounded by north mole, west quay, and south mole
basin_mask = np.zeros((H, W), dtype=bool)
basin_mask[3865:4250, c_quay:5650] = True
basin_water = basin_mask & ~bw_solid

# Apply elevations:
h_m[land_mask] = -46.5
h_m[bw_solid] = -46.0
h_m[basin_water] = -53.0

# Convert back to 16-bit uint16:
val_u16 = np.clip(np.round(((h_m - (-70.0)) / 157.8) * 65535.0), 0, 65535).astype(np.uint16)

# Save heightmap:
Image.fromarray(val_u16).save(hm_path)
print(f"[+] Saved updated {hm_path}!")

# Generate visual diagnostic overlay:
r1, r2, c1, c2 = 3500, 4500, 4700, 5900
sub_h = h_m[r1:r2, c1:c2]
sub_rm = rm[r1:r2, c1:c2]

vis_h = np.zeros((r2-r1, c2-c1, 3), dtype=np.uint8)
dry_mask = sub_h >= -48.5
water_mask = sub_h < -48.5

norm_land = np.clip((sub_h - (-46.5)) / (40.0 - (-46.5)) * 200.0 + 55.0, 55, 255).astype(np.uint8)
vis_h[dry_mask, 0] = norm_land[dry_mask]
vis_h[dry_mask, 1] = norm_land[dry_mask]
vis_h[dry_mask, 2] = norm_land[dry_mask]
vis_h[water_mask] = [220, 150, 40] # blue water

vis_overlay = vis_h.copy()
road_mask = (sub_rm > 128)
vis_overlay[road_mask] = [0, 255, 255] # yellow roads

out_diag = "scratch/accurate_port_shape_overlay.png"
cv2.imwrite(out_diag, vis_overlay)
print(f"[+] Diagnostic overlay saved to {out_diag}")
