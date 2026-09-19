import numpy as np
from PIL import Image
import os
import random

print("[*] Placing Historical Waterfront Objects at Exact Projected Coordinates...")

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
# Situated in Square de la Marine at the North end of the Cours de la Marine
kiosk_wx = 4280.0
kiosk_wz = -750.0
kiosk_elev = get_elevation(kiosk_wx, kiosk_wz)
klx, klz = to_local(kiosk_wx, kiosk_wz)
new_landmarks.append(
    f"OBJ_V6 Kiosque_Musique {klx:.2f} {klz:.2f} 0.00 {kiosk_elev:.2f} 1.00 1.00 1.00 25.00 assets/moniment/kiosque_musique.obj NONE NONE 0"
)

# ==============================================================================
# 2. FIVE DISTINCT HISTORICAL BUILDINGS ALONG COURS DE LA MARINE
# ==============================================================================
# Positioned in order from North to South along the west side of Cours de la Marine
# Facades face East (+X) towards the avenue and harbor basin (angle = 90.0)
distinct_buildings = [
    # (Name, CenterWX, CenterWZ, Angle, ModelPath, ScaleX, ScaleY, ScaleZ)
    ("Douane_NeoMoorish",     4188.0, -610.0, 90.0, "assets/moniment/bldg_douane_neomoorish.obj", 1.0, 1.0, 1.0),
    ("Douane_Annexe_Depots",  4172.0, -475.0, 90.0, "assets/moniment/bldg_douane_annexe.obj",    1.0, 1.0, 1.0),
    ("Grand_Hotel_Sousse",    4155.0, -288.0, 90.0, "assets/moniment/bldg_grand_hotel.obj",      1.0, 1.0, 1.0),
    ("Cercle_Militaire",      4155.0,  -83.0, 90.0, "assets/moniment/bldg_cercle_militaire.obj",  1.0, 1.0, 1.0),
    ("Immeuble_Pichon",       4155.0,  123.0, 90.0, "assets/moniment/bldg_immeuble_pichon.obj",   1.0, 1.0, 1.0),
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
# Enclosing Square de la Marine around (4280.0, -750.0)
curb_segments = []
# North & South borders (length 34m, step 5.6m)
for cx in np.arange(4262.0, 4296.0, 5.6):
    curb_segments.append((cx, -732.0, 0.0))
    curb_segments.append((cx, -768.0, 0.0))
# West & East borders (length 34m, step 5.6m)
for cz in np.arange(-766.0, -733.0, 5.6):
    curb_segments.append((4262.0, cz, 90.0))
    curb_segments.append((4296.0, cz, 90.0))

for idx, (cwx, cwz, c_ang) in enumerate(curb_segments):
    celev = get_elevation(cwx, cwz)
    clx, clz = to_local(cwx, cwz)
    new_landmarks.append(
        f"OBJ_V6 Park_Curb_{idx} {clx:.2f} {clz:.2f} 0.00 {celev:.2f} 1.00 1.00 1.00 {c_ang:.2f} assets/moniment/park_curbing.obj NONE NONE 0"
    )

# ==============================================================================
# 4. MULTI-TIERED PALM GROVE (SQUARE DE LA MARINE)
# ==============================================================================
random.seed(1899)
park_palms = [
    # Palms framing the Kiosque
    (-10.0, -10.0, 1.20), (  9.0, -11.0, 1.15), (-12.0,   0.0, 1.25),
    (-11.0,  11.0, 1.15), ( 10.0,  10.0, 1.10), ( -3.0,  12.0, 1.05),
    ( 12.0,  -2.0, 1.10), ( 12.0,   6.0, 1.05),
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
# 5. QUAYSIDE PALM PROMENADE (ALONG THE WEST DOCK COPING)
# ==============================================================================
quay_wx = 4322.0
for idx, pwz in enumerate(np.arange(-750.0, 350.0, 22.0)):
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
bollard_wx = 4326.5
for idx, bwz in enumerate(np.arange(-750.0, 350.0, 16.0)):
    belev = get_elevation(bollard_wx, bwz)
    blx, blz = to_local(bollard_wx, bwz)
    new_landmarks.append(
        f"OBJ_V6 Mooring_Bollard_{idx} {blx:.2f} {blz:.2f} 0.00 {belev:.2f} 1.00 1.00 1.00 0.00 assets/moniment/mooring_bollard.obj NONE NONE 0"
    )

# ==============================================================================
# 7. HISTORIC QUAYSIDE LAMP POSTS
# ==============================================================================
lamp_posts = [
    (4318.0, -700.0),
    (4318.0, -500.0),
    (4318.0, -300.0),
    (4318.0, -100.0),
    (4318.0,  100.0),
    (4318.0,  300.0),
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

print(f"[+] Placed {len(new_landmarks)} historical objects in {city_path}:")
print(f"  - 1 Kiosque à Musique (Square de la Marine: WX={kiosk_wx}, WZ={kiosk_wz})")
print(f"  - {len(distinct_buildings)} Distinct Buildings along Cours de la Marine")
print(f"  - {len(curb_segments)} Stone Curbing segments")
print(f"  - {len(park_palms)} Square de la Marine Date Palms")
print(f"  - 50 Quayside Promenade Date Palms")
print(f"  - 69 Mooring Bollards")
print(f"  - 6 Quayside Lamp Posts")
print(f"  - Total landmarks in city.txt: {len(all_lines)}")
