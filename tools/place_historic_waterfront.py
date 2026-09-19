import numpy as np
from PIL import Image
import os
import random

# Load heightmap to sample exact elevations
hm_path = "assets/terrain/heightmap.png"
hm = np.array(Image.open(hm_path))
# h = -70.0 + (val / 65535.0) * 157.8
h_m = -70.0 + (hm.astype(np.float32) / 65535.0) * 157.8

def get_elevation(wx, wz):
    c = int(round((wx + 13938.0) / 27876.0 * 8128.0))
    r = int(round((wz + 13938.0) / 27876.0 * 8128.0))
    c = max(0, min(8128, c))
    r = max(0, min(8128, r))
    return float(h_m[r, c])

ANCHOR_X = -530.0
ANCHOR_Z = -1502.0

def to_local(wx, wz):
    return wx - ANCHOR_X, wz - ANCHOR_Z

city_path = "assets/saves/city.txt"
with open(city_path, "r", encoding="utf-8") as f:
    existing_lines = f.readlines()

anchor_line = existing_lines[0]
rock_lines = [l for l in existing_lines[1:] if "Rock1" in l]

new_landmarks = []

# ---------------------------------------------------------
# 1. KIOSQUE A MUSIQUE (Music Pavilion / Bandstand)
# ---------------------------------------------------------
kiosk_wx = 4135.0
kiosk_wz = -35.0
kiosk_elev = get_elevation(kiosk_wx, kiosk_wz)
lx, lz = to_local(kiosk_wx, kiosk_wz)
new_landmarks.append(
    f"OBJ_V6 Kiosque_Musique {lx:.2f} {lz:.2f} 0.00 {kiosk_elev:.2f} 1.00 1.00 1.00 30.00 assets/moniment/kiosque_musique.obj NONE NONE 0"
)

# ---------------------------------------------------------
# 2. COLONIAL WATERFRONT BUILDINGS (Avenue Street Wall)
# ---------------------------------------------------------
building_positions = [
    (4170.0, -110.0, 90.0, 1.0),
    (4170.0, -78.0,  90.0, 0.98),
    (4170.0, -46.0,  90.0, 1.02),
    (4170.0, -14.0,  90.0, 1.0),
    (4170.0,  18.0,  90.0, 0.96),
]

for idx, (bwx, bwz, angle, scale) in enumerate(building_positions):
    belev = get_elevation(bwx, bwz)
    blx, blz = to_local(bwx, bwz)
    new_landmarks.append(
        f"OBJ_V6 Colonial_Building_{idx} {blx:.2f} {blz:.2f} 0.00 {belev:.2f} {scale:.2f} {scale:.2f} {scale:.2f} {angle:.2f} assets/moniment/colonial_hotel_ville.obj NONE NONE 0"
    )

# ---------------------------------------------------------
# 3. QUAYSIDE PALM PROMENADE (Date Palms along the Waterfront)
# ---------------------------------------------------------
random.seed(42)
quay_wx = 4202.0
for idx, pwz in enumerate(np.arange(-280.0, 260.0, 18.0)):
    # Slight micro-jitter for organic placement
    j_wx = quay_wx + random.uniform(-0.8, 0.8)
    j_wz = pwz + random.uniform(-0.8, 0.8)
    pelev = get_elevation(j_wx, j_wz)
    plx, plz = to_local(j_wx, j_wz)
    yaw = random.uniform(0.0, 360.0)
    sx = random.uniform(0.95, 1.12)
    sy = random.uniform(0.95, 1.15)
    sz = sx
    new_landmarks.append(
        f"OBJ_V6 Quay_Palm_{idx} {plx:.2f} {plz:.2f} 0.00 {pelev:.2f} {sx:.2f} {sy:.2f} {sz:.2f} {yaw:.2f} assets/moniment/date_palm.obj NONE NONE 0"
    )

# ---------------------------------------------------------
# 4. PUBLIC PARK PALM GROVE (Around Kiosk)
# ---------------------------------------------------------
park_palm_offsets = [
    # Ring around the kiosk
    (-14.0, -10.0), (14.0, -12.0), (-12.0, 14.0), (15.0, 12.0),
    (-22.0,  -4.0), (22.0,  -6.0), ( -4.0, 22.0), (  4.0, -22.0),
    (-18.0,  18.0), (18.0,  20.0), (-20.0, -18.0), (18.0, -18.0),
    (-8.0,   26.0), ( 8.0,  26.0), (-26.0,   8.0), (26.0,   8.0),
    # Foreground grove (close to camera)
    (-6.0,  -36.0), ( 8.0, -38.0), (-16.0, -42.0), (16.0, -40.0),
]

for idx, (dx, dz) in enumerate(park_palm_offsets):
    gwx = kiosk_wx + dx + random.uniform(-1.2, 1.2)
    gwz = kiosk_wz + dz + random.uniform(-1.2, 1.2)
    gelev = get_elevation(gwx, gwz)
    glx, glz = to_local(gwx, gwz)
    yaw = random.uniform(0.0, 360.0)
    sx = random.uniform(0.90, 1.25)
    sy = random.uniform(0.92, 1.28)
    sz = sx
    new_landmarks.append(
        f"OBJ_V6 Park_Palm_{idx} {glx:.2f} {glz:.2f} 0.00 {gelev:.2f} {sx:.2f} {sy:.2f} {sz:.2f} {yaw:.2f} assets/moniment/date_palm.obj NONE NONE 0"
    )

# ---------------------------------------------------------
# WRITE UPDATED CITY.TXT
# ---------------------------------------------------------
all_lines = [anchor_line] + [l.strip() + "\n" for l in new_landmarks] + rock_lines
with open(city_path, "w", encoding="utf-8") as f:
    f.writelines(all_lines)

print(f"Successfully placed {len(new_landmarks)} historical landmarks & palm trees in {city_path}!")
print(f"  - 1 Kiosque à Musique")
print(f"  - {len(building_positions)} Colonial Waterfront Buildings")
print(f"  - 30 Quayside Date Palms")
print(f"  - {len(park_palm_offsets)} Park Garden Date Palms")
print(f"  - Total objects in city.txt: {len(all_lines):,}")
