"""
GenerateHeightmap.py – DEM Topography + Historical Coast/Port Cutout
===================================================================
Combines real 59.9m max DEM terrain with our historical CSV coastlines!
"""

import math
import pathlib
import struct
import zlib
from PIL import Image

SCRIPT_DIR = pathlib.Path(__file__).parent
IN_PATH    = SCRIPT_DIR.parent / "assets" / "terrain" / "heightmap_dem.png"
OUT_PATH   = SCRIPT_DIR.parent / "assets" / "terrain" / "heightmap.png"

SIZE   = 512
MIN_X, MAX_X = -1250.0, 1250.0
MIN_Z, MAX_Z = -2500.0, 2500.0
MAX_H  = 59.9

# ─────────────────────────────────────────────────────────────
# HELPERS
# ─────────────────────────────────────────────────────────────

def clamp(v, lo, hi):
    return max(lo, min(hi, v))

def smoothstep(lo, hi, x):
    t = clamp((x - lo) / (hi - lo + 1e-9), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)

def dist2d(x, z, cx, cz):
    return math.sqrt((x - cx)**2 + (z - cz)**2)

def line_dist(px, pz, x1, z1, x2, z2):
    dx, dz = x2 - x1, z2 - z1
    if dx == 0 and dz == 0:
        return dist2d(px, pz, x1, z1)
    t = max(0.0, min(1.0, ((px - x1)*dx + (pz - z1)*dz) / (dx*dx + dz*dz)))
    return dist2d(px, pz, x1 + t*dx, z1 + t*dz)

def point_in_polygon(x, z, poly):
    inside = False
    n = len(poly)
    j = n - 1
    for i in range(n):
        xi, zi = poly[i]
        xj, zj = poly[j]
        if ((zi > z) != (zj > z)) and (x < (xj - xi) * (z - zi) / (zj - zi + 1e-9) + xi):
            inside = not inside
        j = i
    return inside

# ─────────────────────────────────────────────────────────────
# COASTLINE & PORT (FROM HISTORICAL MAPS)
# ─────────────────────────────────────────────────────────────

_COAST_RAW = [
    (-2500.0, -159.03),
    (-2260.0,  -35.78),
    (-1900.0,  174.81),
    (-1650.0,  334.72),
    (-1460.0,  480.48),
    (-1255.0,  521.82),
    ( -950.0,  637.81),
    ( -640.0,  944.58),
    ( -420.0,  732.16),
    ( -214.0, 1098.00),
    ( -115.0, 1134.42),
    (  115.0, 1053.22),
    (  410.0,  725.46),
    (  660.0,  610.07),
    (  855.0,  683.71),
    ( 1150.0,  778.40),
    ( 1370.0,  856.35),
    ( 1510.0,  920.67),
    ( 2000.0, 1080.00),
    ( 2500.0, 1245.18),
]

def coast_x(z):
    if z <= _COAST_RAW[0][0]: return _COAST_RAW[0][1]
    if z >= _COAST_RAW[-1][0]: return _COAST_RAW[-1][1]
    lo, hi = 0, len(_COAST_RAW) - 1
    while lo < hi - 1:
        mid = (lo + hi) // 2
        if _COAST_RAW[mid][0] <= z: lo = mid
        else: hi = mid
    z0, x0 = _COAST_RAW[lo]
    z1, x1 = _COAST_RAW[hi]
    t = (z - z0) / (z1 - z0)
    return x0 + t * (x1 - x0)

PORT_QUAYS = [
    # 1. WESTERN MAIN QUAY (The large solid land on the left)
    [
        (650, 200), (800, 200), 
        (800, -420), (650, -420), 
        (650, 200)
    ],

    # 2. CENTRAL PIER (The small "island" dock in the middle of the basin)
    [
        (880, 50), (940, 50), 
        (940, -10), (880, -10), 
        (880, 50)
    ],

    # 3. NORTH-EAST BREAKWATER (Angled top-right section)
    [
        (1050, 200), (1200, 200), 
        (1200, 50), (1100, 0), 
        (1050, 80), (1050, 200)
    ],

    # 4. SOUTH-EAST BREAKWATER (Angled bottom-right section)
    [
        (1100, -150), (1200, -220), 
        (1200, -420), (950, -420), 
        (1050, -300), (1100, -150)
    ],

    # 5. SMALL WESTERN JETTY (The protrusion on the bottom left)
    [
        (780, -250), (830, -300), 
        (810, -350), (780, -320), 
        (780, -250)
    ]
]
PORT_WATER = [
    # Full Rectangular Basin Area (Dredged to sea level, quays built on top)
    [(750, 250), (1100, 250), (1100, -600), (750, -600)]
]

# ─────────────────────────────────────────────────────────────
# GENERATE
# ─────────────────────────────────────────────────────────────

print("Loading DEM topography base layer...")
base_img = Image.open(IN_PATH).convert('L') # Load 8-bit grayscale
base_px = base_img.load()

print(f"Applying Historical Coast & Port Cutouts...")
pixels = bytearray(SIZE * SIZE)

for row in range(SIZE):
    wz = MAX_Z - (row / (SIZE - 1)) * (MAX_Z - MIN_Z)
    cx = coast_x(wz)
    
    for col in range(SIZE):
        wx = MIN_X + (col / (SIZE - 1)) * (MAX_X - MIN_X)
        sd = cx - wx  # positive = inland, negative = in sea
        
        # Read true DEM land altitude for this coordinate
        dem_val = base_px[col, row]
        h = (dem_val / 255.0) * MAX_H
        
        # 1. COASTLINE FIX
        if sd < 0:
            # We are officially in the sea!
            sea_drop = smoothstep(0, -60, sd)
            h = 1.0 * (1 - sea_drop) + 0.2 * sea_drop  # Gentle beach drop to 0.2m
        else:
            # We are officially on land!
            if sd < 50:
                # Merge original land height into a nice flat beach edge
                cliff = smoothstep(0, 50, sd)
                h = h * cliff + 1.2 * (1 - cliff)
        
        # 2. DREDGE WATER BASIN FIRST
        for w_poly in PORT_WATER:
            if point_in_polygon(wx, wz, w_poly):
                h = min(h, 0.2)

        # 3. BUILD QUAYS ON TOP
        is_quay = False
        quay_dist = 9999
        for quay_poly in PORT_QUAYS:
            if point_in_polygon(wx, wz, quay_poly):
                is_quay = True
                for i in range(len(quay_poly)):
                    p1, p2 = quay_poly[i], quay_poly[(i+1)%len(quay_poly)]
                    quay_dist = min(quay_dist, line_dist(wx, wz, p1[0], p1[1], p2[0], p2[1]))
                
        if is_quay:
            quay_blend = smoothstep(10, 0, quay_dist)
            h = h * (1 - quay_blend) + 1.8 * quay_blend  # Set dock height to 1.8m
            
        # Prevent land dropping below sea level except specifically in water
        in_water = any(point_in_polygon(wx, wz, w_poly) for w_poly in PORT_WATER)
        if sd > 15 and h < 1.0 and not in_water:
            h = max(1.0, h)
            
        pixels[row * SIZE + col] = int(clamp(h / MAX_H * 255.0, 0, 255))

# ─────────────────────────────────────────────────────────────
# WRITE PNG (8-bit grayscale)
# ─────────────────────────────────────────────────────────────

def write_png_gray8(path, buf, w, h_img):
    def chunk(tag, data):
        crc = zlib.crc32(tag + data) & 0xFFFFFFFF
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', crc)
    ihdr = struct.pack('>IIBBBBB', w, h_img, 8, 0, 0, 0, 0)
    raw = b''.join(b'\x00' + bytes(buf[r*w:(r+1)*w]) for r in range(h_img))
    idat = zlib.compress(raw, 9)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', ihdr))
        f.write(chunk(b'IDAT', idat))
        f.write(chunk(b'IEND', b''))

write_png_gray8(OUT_PATH, pixels, SIZE, SIZE)
print(f"DONE → {OUT_PATH} successfully merged DEM with Historical Cutouts!")