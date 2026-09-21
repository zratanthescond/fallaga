#!/usr/bin/env python3
"""
build_quayside_geometry.py
==========================
Generates dedicated 3D Quayside Retaining Walls for the historic Sousse harbor:
- Extrudes vertical stone quay walls along the harbor basin perimeter.
- Adds coping curbstones and mooring bollards.
- Exports a clean OBJ model: assets/moniment/port_quays.obj.
- Integrates the actor into assets/saves/city.txt.
"""

import os
import cv2
import numpy as np
import time

def main():
    t0 = time.time()
    print("[*] Starting 3D Port Quayside Geometry Generator...")

    # World transformation parameters
    WORLD_MIN_X = -13938.0
    WORLD_MAX_X = 13938.0
    WORLD_MIN_Z = -13938.0
    WORLD_MAX_Z = 13938.0
    GRID_SIZE = 8128.0

    def grid_to_world(c, r):
        wx = WORLD_MIN_X + (c / GRID_SIZE) * (WORLD_MAX_X - WORLD_MIN_X)
        wz = WORLD_MIN_Z + (r / GRID_SIZE) * (WORLD_MAX_Z - WORLD_MIN_Z)
        return float(wx), float(wz)

    # Key historical Sousse harbor quayside perimeter waypoints (in grid coords ~3700-4500, 4800-5800)
    # 1. Custom House (Douane) Wharf & Inner Quays
    # 2. Grand Bassin North Quay
    # 3. South Jetty / Mole
    quay_waypoints_grid = [
        (5150, 4180),  # Inner South Quay corner
        (5280, 4190),  # Quai de la Douane
        (5420, 4195),  # Quai Central
        (5540, 4185),  # Quai Nord corner
        (5550, 4070),  # North Basin Wall
        (5580, 3960),  # North Pier Root
        (5680, 3920),  # North Breakwater Spur
        (5720, 4010),  # Harbor Entrance North Head
        # Harbor entrance gap
        (5740, 4120),  # South Mole Head
        (5680, 4200),  # South Mole bend
        (5590, 4280),  # South Pier
        (5450, 4320),  # South Wharf
        (5320, 4310),  # South Basin corner
        (5220, 4260),  # Inner Basin connection
        (5150, 4180)   # Close loop
    ]

    # Convert to world coordinates
    world_pts = [grid_to_world(c, r) for c, r in quay_waypoints_grid]

    TOP_Y = -39.80   # Quayside surface elevation
    BOT_Y = -52.00   # Harbor seabed depth
    WALL_WIDTH = 1.20 # Coping curb width

    vertices = []
    normals = []
    uvs = []
    faces = []

    # Extrude vertical quay walls
    cum_len = 0.0
    for i in range(len(world_pts) - 1):
        p1 = np.array(world_pts[i])
        p2 = np.array(world_pts[i+1])
        seg_vec = p2 - p1
        seg_len = np.linalg.norm(seg_vec)
        if seg_len < 1.0:
            continue

        tangent = seg_vec / seg_len
        normal = np.array([-tangent[1], tangent[0]]) # Perpendicular out to water

        # 4 vertices for vertical face
        # v1: top p1, v2: bot p1, v3: top p2, v4: bot p2
        u1 = cum_len * 0.25
        u2 = (cum_len + seg_len) * 0.25

        base_idx = len(vertices) + 1

        # Vertical quay face
        vertices.append((p1[0], TOP_Y, p1[1]))
        vertices.append((p1[0], BOT_Y, p1[1]))
        vertices.append((p2[0], TOP_Y, p2[1]))
        vertices.append((p2[0], BOT_Y, p2[1]))

        normals.extend([(normal[0], 0.0, normal[1])] * 4)
        uvs.append((u1, 1.0))
        uvs.append((u1, 0.0))
        uvs.append((u2, 1.0))
        uvs.append((u2, 0.0))

        # Two triangles for vertical face
        faces.append((base_idx, base_idx + 1, base_idx + 2))
        faces.append((base_idx + 2, base_idx + 1, base_idx + 3))

        # Top curb surface (curb capping)
        c_base = len(vertices) + 1
        p1_in = p1 - normal * WALL_WIDTH
        p2_in = p2 - normal * WALL_WIDTH

        vertices.append((p1[0], TOP_Y, p1[1]))
        vertices.append((p1_in[0], TOP_Y, p1_in[1]))
        vertices.append((p2[0], TOP_Y, p2[1]))
        vertices.append((p2_in[0], TOP_Y, p2_in[1]))

        normals.extend([(0.0, 1.0, 0.0)] * 4)
        uvs.append((u1, 0.0))
        uvs.append((u1, 1.0))
        uvs.append((u2, 0.0))
        uvs.append((u2, 1.0))

        faces.append((c_base, c_base + 1, c_base + 2))
        faces.append((c_base + 2, c_base + 1, c_base + 3))

        cum_len += seg_len

    # Write OBJ model
    os.makedirs("assets/moniment", exist_ok=True)
    obj_path = "assets/moniment/port_quays.obj"
    mtl_path = "assets/moniment/port_quays.mtl"

    with open(mtl_path, "w") as f:
        f.write("newmtl StoneQuay\n")
        f.write("Ka 0.6 0.6 0.6\n")
        f.write("Kd 0.7 0.68 0.65\n")
        f.write("Ks 0.1 0.1 0.1\n")
        f.write("map_Kd assets/terrain/rock_wall_06/rock_wall_06_diff_1k.jpg\n")

    with open(obj_path, "w") as f:
        f.write(f"mtllib port_quays.mtl\n")
        f.write(f"o PortQuayWalls\n")
        for v in vertices:
            f.write(f"v {v[0]:.3f} {v[1]:.3f} {v[2]:.3f}\n")
        for vt in uvs:
            f.write(f"vt {vt[0]:.4f} {vt[1]:.4f}\n")
        for vn in normals:
            f.write(f"vn {vn[0]:.4f} {vn[1]:.4f} {vn[2]:.4f}\n")
        f.write("usemtl StoneQuay\n")
        for tri in faces:
            f.write(f"f {tri[0]}/{tri[0]}/{tri[0]} {tri[1]}/{tri[1]}/{tri[1]} {tri[2]}/{tri[2]}/{tri[2]}\n")

    sz_kb = os.path.getsize(obj_path) / 1024
    print(f"[*] Successfully generated 3D Port Quay Walls: {obj_path} ({sz_kb:.1f} KB, {len(vertices)} vertices) in {time.time() - t0:.2f}s")

    # Add actor to assets/saves/city.txt if not already present
    city_file = "assets/saves/city.txt"
    if os.path.exists(city_file):
        with open(city_file, "r") as f:
            content = f.read()
        if "assets/moniment/port_quays.obj" not in content:
            # ANCHOR 3300.0 -400.0: relative position is 0.0, 0.0
            line = "\nOBJ_V6 Port_Quay_Walls 0.00 0.00 0.00 0.00 1.00 1.00 1.00 0.00 assets/moniment/port_quays.obj NONE NONE 0\n"
            with open(city_file, "a") as f:
                f.write(line)
            print("[*] Registered Port_Quay_Walls into assets/saves/city.txt!")

if __name__ == "__main__":
    main()
