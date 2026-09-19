import numpy as np
from PIL import Image
import os
import random

# Load heightmap to sample exact elevations
hm_path = "assets/terrain/heightmap.png"
hm = np.array(Image.open(hm_path))
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

# ==============================================================================
# 1. KIOSQUE A MUSIQUE (SQUARE DE LA MARINE CENTERPIECE)
# ==============================================================================
# Foreground park monument facing the street wall and harbor
kiosk_wx = 4166.0
kiosk_wz = -35.0
kiosk_elev = get_elevation(kiosk_wx, kiosk_wz)
klx, klz = to_local(kiosk_wx, kiosk_wz)
new_landmarks.append(
    f"OBJ_V6 Kiosque_Musique {klx:.2f} {klz:.2f} 0.00 {kiosk_elev:.2f} 1.00 1.00 1.00 25.00 assets/moniment/kiosque_musique.obj NONE NONE 0"
)

# ==============================================================================
# 2. DISTINCT HISTORICAL WATERFRONT BUILDINGS (COURS DE LA MARINE STREET WALL)
# ==============================================================================
# In order along the boulevard from South to North:
# 1. Douane (Neo-Moorish) closest to foreground park
# 2. Annexe des Douanes with red-and-white awning
# 3. Grand Hôtel de Sousse with 3 stories, wrought-iron balcony, and mansard roof
# 4. Cercle Militaire with neoclassical pediment and balustrade
# 5. Immeuble Pichon commercial boulevard block
distinct_buildings = [
    # (Name, CenterWX, CenterWZ, Angle, ModelPath, ScaleX, ScaleY, ScaleZ)
    ("Douane_NeoMoorish",     4158.0,  -6.0, 90.0, "assets/moniment/bldg_douane_neomoorish.obj", 1.0, 1.0, 1.0),
    ("Douane_Annexe_Depots",  4159.0,  20.0, 90.0, "assets/moniment/bldg_douane_annexe.obj",    1.0, 1.0, 1.0),
    ("Grand_Hotel_Sousse",    4158.0,  48.0, 90.0, "assets/moniment/bldg_grand_hotel.obj",      1.0, 1.0, 1.0),
    ("Cercle_Militaire",      4159.0,  77.0, 90.0, "assets/moniment/bldg_cercle_militaire.obj",  1.0, 1.0, 1.0),
    ("Immeuble_Pichon",       4160.0, 101.0, 90.0, "assets/moniment/bldg_immeuble_pichon.obj",   1.0, 1.0, 1.0),
]

for name, bwx, bwz, angle, mpath, sx, sy, sz in distinct_buildings:
    belev = get_elevation(bwx, bwz)
    blx, blz = to_local(bwx, bwz)
    new_landmarks.append(
        f"OBJ_V6 {name} {blx:.2f} {blz:.2f} 0.00 {belev:.2f} {sx:.2f} {sy:.2f} {sz:.2f} {angle:.2f} {mpath} NONE NONE 0"
    )

# ==============================================================================
# 3. SQUARE DE LA MARINE GARDEN CURBING (STONE EDGING)
# ==============================================================================
# Enclosing Square de la Marine from X=[4148, 4182], Z=[-52, -18]
curb_segments = []
# North & South borders (length 34m, step 5.6m)
for cx in np.arange(4150.0, 4182.0, 5.6):
    curb_segments.append((cx, -18.0, 0.0))
    curb_segments.append((cx, -52.0, 0.0))
# West & East borders (length 34m, step 5.6m)
for cz in np.arange(-50.0, -19.0, 5.6):
    curb_segments.append((4148.0, cz, 90.0))
    curb_segments.append((4182.0, cz, 90.0))

for idx, (cwx, cwz, c_ang) in enumerate(curb_segments):
    celev = get_elevation(cwx, cwz)
    clx, clz = to_local(cwx, cwz)
    new_landmarks.append(
        f"OBJ_V6 Park_Curb_{idx} {clx:.2f} {clz:.2f} 0.00 {celev:.2f} 1.00 1.00 1.00 {c_ang:.2f} assets/moniment/park_curbing.obj NONE NONE 0"
    )

# ==============================================================================
# 4. MULTI-TIERED PALM GROVE (SQUARE DE LA MARINE)
# ==============================================================================
# Framed around the Kiosque exactly as depicted in the 1899 postcard
random.seed(1899)
park_palms = [
    # Palms framing the Kiosque (South and West foreground framing)
    (-10.0, -10.0, 1.20), (  9.0, -11.0, 1.15), (-12.0,   0.0, 1.25),
    # Palms flanking towards the Douane (North border)
    (-11.0,  11.0, 1.15), ( 10.0,  10.0, 1.10), ( -3.0,  12.0, 1.05),
    # Palms along eastern promenade border
    ( 12.0,  -2.0, 1.10), ( 12.0,   6.0, 1.05),
    # Foreground decorative lower palms
    ( -8.0, -14.0, 0.85), (  6.0, -15.0, 0.88),
]

for idx, (dx, dz, s_mult) in enumerate(park_palms):
    pwx = kiosk_wx + dx + random.uniform(-0.5, 0.5)
    pwz = kiosk_wz + dz + random.uniform(-0.5, 0.5)
    pelev = get_elevation(pwx, pwz)
    plx, plz = to_local(pwx, pwz)
    yaw = random.uniform(0.0, 360.0)
    sx = s_mult * random.uniform(0.95, 1.05)
    sy = s_mult * random.uniform(0.95, 1.08)
    sz = sx
    new_landmarks.append(
        f"OBJ_V6 Park_Palm_{idx} {plx:.2f} {plz:.2f} 0.00 {pelev:.2f} {sx:.2f} {sy:.2f} {sz:.2f} {yaw:.2f} assets/moniment/date_palm.obj NONE NONE 0"
    )

# ==============================================================================
# 5. QUAYSIDE PALM PROMENADE (ALONG THE WATERFRONT STONE DOCK)
# ==============================================================================
quay_wx = 4202.0
for idx, pwz in enumerate(np.arange(-280.0, 260.0, 17.5)):
    j_wx = quay_wx + random.uniform(-0.5, 0.5)
    j_wz = pwz + random.uniform(-0.5, 0.5)
    pelev = get_elevation(j_wx, j_wz)
    plx, plz = to_local(j_wx, j_wz)
    yaw = random.uniform(0.0, 360.0)
    sx = random.uniform(1.05, 1.25)
    sy = random.uniform(1.05, 1.28)
    sz = sx
    new_landmarks.append(
        f"OBJ_V6 Quay_Palm_{idx} {plx:.2f} {plz:.2f} 0.00 {pelev:.2f} {sx:.2f} {sy:.2f} {sz:.2f} {yaw:.2f} assets/moniment/date_palm.obj NONE NONE 0"
    )

# ==============================================================================
# 6. MARITIME MOORING BOLLARDS (ALONG QUAY EDGE COPING)
# ==============================================================================
bollard_wx = 4204.8
for idx, bwz in enumerate(np.arange(-270.0, 250.0, 14.0)):
    belev = get_elevation(bollard_wx, bwz)
    blx, blz = to_local(bollard_wx, bwz)
    new_landmarks.append(
        f"OBJ_V6 Mooring_Bollard_{idx} {blx:.2f} {blz:.2f} 0.00 {belev:.2f} 1.00 1.00 1.00 0.00 assets/moniment/mooring_bollard.obj NONE NONE 0"
    )

# ==============================================================================
# 7. HISTORIC QUAYSIDE LAMP POSTS
# ==============================================================================
lamp_posts = [
    (4196.0, -15.0),
    (4196.0, -55.0),
    (4196.0,  25.0),
    (4196.0, -95.0),
]
for idx, (lwx, lwz) in enumerate(lamp_posts):
    lelev = get_elevation(lwx, lwz)
    llx, llz = to_local(lwx, lwz)
    new_landmarks.append(
        f"OBJ_V6 Quay_Lamp_{idx} {llx:.2f} {llz:.2f} 0.00 {lelev:.2f} 1.00 1.00 1.00 90.00 assets/moniment/quayside_lamp_post.obj NONE NONE 0"
    )

# ==============================================================================
# WRITE TO CITY.TXT
# ==============================================================================
all_lines = [anchor_line] + [l.strip() + "\n" for l in new_landmarks] + rock_lines
with open(city_path, "w", encoding="utf-8") as f:
    f.writelines(all_lines)

print(f"Placed {len(new_landmarks)} historical objects in {city_path}:")
print(f"  - 1 Kiosque à Musique (Square de la Marine)")
print(f"  - {len(distinct_buildings)} Distinct Historical Buildings (Douane, Annexe, Grand Hôtel, Cercle Militaire, Immeuble Pichon)")
print(f"  - {len(curb_segments)} Stone Park Curbing Segments")
print(f"  - {len(park_palms)} Multi-Tiered Park Palms")
print(f"  - 31 Quayside Date Palms")
print(f"  - 38 Mooring Bollards")
print(f"  - {len(lamp_posts)} Quayside Historic Lamp Posts")
print(f"  - Total objects in city.txt: {len(all_lines):,}")
