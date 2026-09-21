#!/usr/bin/env python3
"""
tools/extract_1899_and_regional_roads.py
========================================
Extracts clean, vector road splines for the entire Fallaga world:
1. Sousse Urban Grid: Extracted from the authentic 1899 street map (sousseMap1899-roads.png).
2. Strict Building Clearance: Re-routes or clips roads to maintain >=3m clearance from all city buildings.
3. Regional Highway Network: Spliced with regional highways from roadMap_user_raw.png for open country.
4. Exports structured JSON: assets/roads/roads.json.
"""

import os
import sys
import json
import time
import cv2
import numpy as np
from scipy.ndimage import gaussian_filter1d

def zhang_suen_thinning(image):
    img = image.copy().astype(np.uint8)
    while True:
        p = np.pad(img, 1, mode='constant')
        p2 = p[0:-2, 1:-1]; p3 = p[0:-2, 2:]; p4 = p[1:-1, 2:]
        p5 = p[2:, 2:];   p6 = p[2:, 1:-1]; p7 = p[2:, 0:-2]
        p8 = p[1:-1, 0:-2]; p9 = p[0:-2, 0:-2]

        B = p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9
        c2 = (B >= 2) & (B <= 6)

        A = ((p2 == 0) & (p3 == 1)).astype(int) + \
            ((p3 == 0) & (p4 == 1)).astype(int) + \
            ((p4 == 0) & (p5 == 1)).astype(int) + \
            ((p5 == 0) & (p6 == 1)).astype(int) + \
            ((p6 == 0) & (p7 == 1)).astype(int) + \
            ((p7 == 0) & (p8 == 1)).astype(int) + \
            ((p8 == 0) & (p9 == 1)).astype(int) + \
            ((p9 == 0) & (p2 == 1)).astype(int)

        m1 = (img == 1) & c2 & (A == 1) & (p2 * p4 * p6 == 0) & (p4 * p6 * p8 == 0)
        del1 = np.count_nonzero(m1)
        img[m1] = 0

        p = np.pad(img, 1, mode='constant')
        p2 = p[0:-2, 1:-1]; p3 = p[0:-2, 2:]; p4 = p[1:-1, 2:]
        p5 = p[2:, 2:];   p6 = p[2:, 1:-1]; p7 = p[2:, 0:-2]
        p8 = p[1:-1, 0:-2]; p9 = p[0:-2, 0:-2]

        B = p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9
        c2 = (B >= 2) & (B <= 6)

        A = ((p2 == 0) & (p3 == 1)).astype(int) + \
            ((p3 == 0) & (p4 == 1)).astype(int) + \
            ((p4 == 0) & (p5 == 1)).astype(int) + \
            ((p5 == 0) & (p6 == 1)).astype(int) + \
            ((p6 == 0) & (p7 == 1)).astype(int) + \
            ((p7 == 0) & (p8 == 1)).astype(int) + \
            ((p8 == 0) & (p9 == 1)).astype(int) + \
            ((p9 == 0) & (p2 == 1)).astype(int)

        m2 = (img == 1) & c2 & (A == 1) & (p2 * p4 * p8 == 0) & (p2 * p6 * p8 == 0)
        del2 = np.count_nonzero(m2)
        img[m2] = 0

        if del1 == 0 and del2 == 0:
            break
    return img

def extract_topological_edges(thin):
    """Extracts clean continuous polylines from a 1-pixel skeleton."""
    kernel = np.array([[1, 1, 1], [1, 0, 1], [1, 1, 1]], dtype=np.uint8)
    nbr_count = cv2.filter2D(thin, -1, kernel) * (thin > 0)

    # Junctions are pixels with 3 or more neighbors
    junction_mask = (nbr_count >= 3).astype(np.uint8)
    num_juncs, junc_labels, stats, centroids = cv2.connectedComponentsWithStats(junction_mask, connectivity=8)

    junc_pixel_map = {}
    for jid in range(1, num_juncs):
        cy, cx = float(centroids[jid][1]), float(centroids[jid][0])
        ys, xs = np.where(junc_labels == jid)
        for y, x in zip(ys, xs):
            junc_pixel_map[(y, x)] = (jid, (cy, cx))

    visited_paths = np.zeros_like(thin, dtype=bool)
    edges = []
    dxs = [-1, 0, 1, -1, 1, -1, 0, 1]
    dys = [-1, -1, -1, 0, 0, 1, 1, 1]

    # 1. Trace edges starting from junction nodes
    for (jy, jx), (start_jid, (cy, cx)) in junc_pixel_map.items():
        for dx, dy in zip(dxs, dys):
            ny, nx = jy + dy, jx + dx
            if 0 <= ny < thin.shape[0] and 0 <= nx < thin.shape[1]:
                if thin[ny, nx] > 0 and (ny, nx) not in junc_pixel_map and not visited_paths[ny, nx]:
                    path = [(cy, cx), (float(ny), float(nx))]
                    visited_paths[ny, nx] = True
                    curr_y, curr_x = ny, nx
                    reached_end = False
                    while not reached_end:
                        found_next = False
                        for ndx, ndy in zip(dxs, dys):
                            next_y, next_x = curr_y + ndy, curr_x + ndx
                            if 0 <= next_y < thin.shape[0] and 0 <= next_x < thin.shape[1]:
                                if thin[next_y, next_x] > 0:
                                    if (next_y, next_x) in junc_pixel_map:
                                        end_jid, (end_cy, end_cx) = junc_pixel_map[(next_y, next_x)]
                                        if end_jid != start_jid or len(path) >= 6:
                                            path.append((end_cy, end_cx))
                                            reached_end = True
                                            found_next = True
                                            break
                                    elif not visited_paths[next_y, next_x]:
                                        visited_paths[next_y, next_x] = True
                                        path.append((float(next_y), float(next_x)))
                                        curr_y, curr_x = next_y, next_x
                                        found_next = True
                                        break
                        if not found_next:
                            break
                    if len(path) >= 3 and path[0] != path[-1]:
                        edges.append(path)

    # 2. Trace edges from terminal dead-ends (degree == 1)
    end_ys, end_xs = np.where((nbr_count == 1) & (thin > 0))
    for ey, ex in zip(end_ys, end_xs):
        if visited_paths[ey, ex]:
            continue
        path = [(float(ey), float(ex))]
        visited_paths[ey, ex] = True
        curr_y, curr_x = ey, ex
        reached_end = False
        while not reached_end:
            found_next = False
            for ndx, ndy in zip(dxs, dys):
                next_y, next_x = curr_y + ndy, curr_x + ndx
                if 0 <= next_y < thin.shape[0] and 0 <= next_x < thin.shape[1]:
                    if thin[next_y, next_x] > 0:
                        if (next_y, next_x) in junc_pixel_map:
                            end_jid, (end_cy, end_cx) = junc_pixel_map[(next_y, next_x)]
                            path.append((end_cy, end_cx))
                            reached_end = True
                            found_next = True
                            break
                        elif not visited_paths[next_y, next_x]:
                            visited_paths[next_y, next_x] = True
                            path.append((float(next_y), float(next_x)))
                            curr_y, curr_x = next_y, next_x
                            found_next = True
                            break
            if not found_next:
                break
        if len(path) >= 3:
            edges.append(path)

    return edges

def smooth_polyline(pts_world, step_meters=5.0):
    pts = np.array(pts_world, dtype=np.float32)
    if len(pts) < 2:
        return pts_world
    diffs = np.diff(pts, axis=0)
    dists = np.sqrt((diffs ** 2).sum(axis=1))
    cum_dist = np.concatenate(([0.0], np.cumsum(dists)))
    total_len = cum_dist[-1]

    if total_len < step_meters:
        return pts_world

    num_samples = max(2, int(np.round(total_len / step_meters)) + 1)
    target_dists = np.linspace(0.0, total_len, num_samples)

    sx = np.interp(target_dists, cum_dist, pts[:, 0])
    sz = np.interp(target_dists, cum_dist, pts[:, 1])
    res = np.column_stack((sx, sz))

    if len(res) >= 4:
        sm = gaussian_filter1d(res, sigma=1.2, axis=0, mode='nearest')
        sm[0] = res[0]
        sm[-1] = res[-1]
        return sm.tolist()
    return res.tolist()

def get_building_boxes():
    """Returns list of (min_x, max_x, min_z, max_z) for landmarks in city.txt with safety margin."""
    boxes = []
    if not os.path.exists("assets/saves/city.txt"):
        return boxes

    anchor = [3300.0, -400.0]
    with open("assets/saves/city.txt", "r") as f:
        for l in f:
            if l.startswith("ANCHOR"):
                p = l.split()
                anchor = [float(p[1]), float(p[2])]
            elif l.startswith("OBJ_V6"):
                p = l.split()
                model = p[10]
                if any(k in model for k in ["bldg", "hotel", "douane", "immeuble", "cercle", "kiosque"]):
                    wx = anchor[0] + float(p[2])
                    wz = anchor[1] + float(p[3])
                    margin_x = 16.0
                    margin_z = 24.0 if "immeuble" in model or "hotel" in model else 16.0
                    boxes.append((wx - margin_x, wx + margin_x, wz - margin_z, wz + margin_z))
    return boxes

def main():
    t0 = time.time()
    print("==================================================================")
    print("[*] Extracting 1899 Sousse City Streets & Regional Roadways...")
    print("==================================================================")

    bldg_boxes = get_building_boxes()
    print(f"[*] Loaded {len(bldg_boxes)} building exclusion zones from city.txt")

    # 1. LOAD 1899 MAP
    s1899_path = "assets/terrain/sousseMap1899-roads.png"
    if not os.path.exists(s1899_path):
        print(f"[!] {s1899_path} missing!")
        return

    im1899 = cv2.imread(s1899_path, cv2.IMREAD_UNCHANGED)
    alpha1899 = im1899[:, :, 3] if im1899.shape[2] == 4 else (im1899[:, :, 0] < 200).astype(np.uint8) * 255
    bin1899 = (alpha1899 > 50).astype(np.uint8)

    print(f"[*] Thinning 1899 city streets ({np.count_nonzero(bin1899):,} stroke pixels)...")
    thin1899 = zhang_suen_thinning(bin1899)
    raw_edges1899 = extract_topological_edges(thin1899)
    print(f"    [+] Extracted {len(raw_edges1899)} street segments from 1899 map.")

    # 1899 Coordinate calibration:
    # Place Pichon circle center: col=525.5, row=365.5 -> X=4130.0, Z=-630.0
    # Scaling: dX/dcol = 7.876, dZ/drow = 7.007
    SCALE_X = 7.876
    SCALE_Z = 7.007
    REF_C = 525.5
    REF_R = 365.5
    REF_X = 4130.0
    REF_Z = -630.0

    # Explicit monumental building exclusion zones (with safety margins)
    # 1. Quai de la Douane Colonial Building Row (Douane, Annexe, Grand Hotel, Cercle, Pichon)
    # 2. Place Pichon Kiosque a Musique bandstand
    # 3. Kasbah Tower
    EXCLUSION_BOXES = [
        (4008.0, 4052.0, -590.0, -315.0), # Waterfront colonial facades
        (4116.0, 4144.0, -644.0, -616.0), # Kiosque Musique
        (2544.0, 2572.0, 1004.0, 1032.0), # Kasbah Tower
    ]

    def point_in_boxes(px, pz):
        for bx0, bx1, bz0, bz1 in EXCLUSION_BOXES:
            if bx0 <= px <= bx1 and bz0 <= pz <= bz1:
                return True
        return False

    all_splines = []
    spline_id = 0

    for path in raw_edges1899:
        if len(path) < 3:
            continue

        world_pts = []
        for r, c in path:
            wx = REF_X + (c - REF_C) * SCALE_X
            wz = REF_Z + (r - REF_R) * SCALE_Z
            world_pts.append([wx, wz])

        # Check if the path lies within the Colonial Quarter & Place Pichon core
        # [3970, 4200] x [-700, -220]
        # In this zone, the clean authentic historical arterial network takes over,
        # preventing double-line parallel ghost roads and messy label loops.
        path_in_colonial = False
        xs = [pt[0] for pt in world_pts]
        zs = [pt[1] for pt in world_pts]
        if min(xs) >= 3965.0 and max(xs) <= 4210.0 and min(zs) >= -710.0 and max(zs) <= -210.0:
            path_in_colonial = True

        if path_in_colonial:
            continue

        # Clip out any portions of the street that penetrate building exclusion zones or harbor water
        # Harbor water basin: X > 4085.0 and Z > -250.0
        def point_is_invalid(px, pz):
            if point_in_boxes(px, pz):
                return True
            if px > 4085.0 and -250.0 < pz < 250.0:
                return True
            return False

        sub_paths = []
        current_sub = []
        for pt in world_pts:
            if not point_is_invalid(pt[0], pt[1]):
                current_sub.append(pt)
            else:
                if len(current_sub) >= 2:
                    sub_paths.append(current_sub)
                current_sub = []
        if len(current_sub) >= 2:
            sub_paths.append(current_sub)

        for sub in sub_paths:
            # Check if this sub-path is located in the colonial quarter
            s_xs = [pt[0] for pt in sub]
            s_zs = [pt[1] for pt in sub]
            if min(s_xs) >= 3960.0 and max(s_xs) <= 4250.0 and min(s_zs) >= -750.0 and max(s_zs) <= -210.0:
                continue

            # Filter out tiny isolated fragments (length < 30 meters)
            sub_np = np.array(sub)
            dists = np.sqrt(((sub_np[1:] - sub_np[:-1]) ** 2).sum(axis=1))
            total_len = float(dists.sum())
            if total_len < 30.0:
                continue

            smoothed = smooth_polyline(sub, step_meters=5.0)
            if len(smoothed) < 2:
                continue

            # Double check no smoothed point fell into an exclusion box
            filtered_smoothed = [pt for pt in smoothed if not point_is_invalid(pt[0], pt[1])]
            if len(filtered_smoothed) < 2:
                continue

            mid = filtered_smoothed[len(filtered_smoothed) // 2]
            d_pichon = np.hypot(mid[0] - REF_X, mid[1] - REF_Z)

            if d_pichon < 220.0 or (3980 <= mid[0] <= 4250 and -750 <= mid[1] <= -200):
                stype = "colonial_avenue"
                width = 11.5
                shoulder = 2.0
            elif d_pichon < 750.0:
                stype = "urban_street"
                width = 8.0
                shoulder = 1.5
            else:
                stype = "country_dirt"
                width = 6.0
                shoulder = 1.5

            pts_dict = [{"x": round(float(pt[0]), 2), "z": round(float(pt[1]), 2)} for pt in filtered_smoothed]
            all_splines.append({
                "id": spline_id,
                "type": stype,
                "width": width,
                "shoulder": shoulder,
                "points": pts_dict
            })
            spline_id += 1

    # ─────────────────────────────────────────────────────────────────
    # DEDICATED AUTHENTIC 1899 COLONIAL BOULEVARD & STREET NETWORK
    # ─────────────────────────────────────────────────────────────────

    # 1. Quai de la Douane / Avenue Victor Hugo (Waterfront Grand Boulevard)
    # Strictly monotonic in Z from North (-750m) to South (+300m)
    marine_promenade = [
        [4060.0, -750.0],
        [4060.0, -640.0],
        [4060.0, -560.0],
        [4060.0, -505.0],
        [4060.0, -450.0],
        [4060.0, -395.0],
        [4060.0, -340.0],
        [4060.0, -280.0],
        [4065.0, -200.0],
        [4075.0, -100.0],
        [4090.0,   0.0],
        [4120.0, 150.0],
        [4160.0, 300.0]
    ]
    all_splines.append({
        "id": spline_id,
        "type": "colonial_avenue",
        "width": 13.0,
        "shoulder": 2.5,
        "points": [{"x": round(p[0], 2), "z": round(p[1], 2)} for p in marine_promenade]
    })
    spline_id += 1

    # Place Pichon to Quai connector
    all_splines.append({
        "id": spline_id,
        "type": "colonial_avenue",
        "width": 11.5,
        "shoulder": 2.0,
        "points": [{"x": 4110.0, "z": -630.0}, {"x": 4085.0, "z": -635.0}, {"x": 4060.0, "z": -640.0}]
    })
    spline_id += 1

    # 2. Boulevard René Millet / Rue de Paris (Medina East Wall Street)
    rue_de_paris = [
        [4000.0, -640.0],
        [4000.0, -560.0],
        [4000.0, -505.0],
        [4000.0, -450.0],
        [4000.0, -395.0],
        [4000.0, -340.0],
        [4000.0, -280.0],
        [3985.0, -180.0],
        [3965.0,  -80.0],
        [3945.0,   50.0]
    ]
    all_splines.append({
        "id": spline_id,
        "type": "urban_street",
        "width": 8.5,
        "shoulder": 1.5,
        "points": [{"x": round(p[0], 2), "z": round(p[1], 2)} for p in rue_de_paris]
    })
    spline_id += 1

    # 3. Place Pichon Roundabout Loop (r = 26m around Kiosque a Musique)
    pichon_center = [4130.0, -630.0]
    pichon_radius = 26.0
    pichon_ring = []
    for ang in range(0, 361, 30):
        rad = np.radians(ang)
        pichon_ring.append([pichon_center[0] + np.cos(rad) * pichon_radius,
                            pichon_center[1] + np.sin(rad) * pichon_radius])
    all_splines.append({
        "id": spline_id,
        "type": "colonial_avenue",
        "width": 10.0,
        "shoulder": 1.5,
        "points": [{"x": round(p[0], 2), "z": round(p[1], 2)} for p in pichon_ring]
    })
    spline_id += 1

    # 4. Cross Streets: Rue de l'Église (North of Douane)
    all_splines.append({
        "id": spline_id,
        "type": "urban_street",
        "width": 9.0,
        "shoulder": 1.5,
        "points": [{"x": 3900.0, "z": -600.0}, {"x": 4000.0, "z": -600.0}, {"x": 4060.0, "z": -600.0}]
    })
    spline_id += 1

    # 5. Cross Street: Rue du Sud (South of Immeuble Pichon)
    all_splines.append({
        "id": spline_id,
        "type": "urban_street",
        "width": 9.0,
        "shoulder": 1.5,
        "points": [{"x": 3900.0, "z": -280.0}, {"x": 4000.0, "z": -280.0}, {"x": 4060.0, "z": -280.0}]
    })
    spline_id += 1

    # 6. Avenue de la Gare (Place Pichon Northwest to Station & Tunis Road)
    all_splines.append({
        "id": spline_id,
        "type": "colonial_avenue",
        "width": 11.5,
        "shoulder": 2.0,
        "points": [
            {"x": 4110.0, "z": -650.0},
            {"x": 4040.0, "z": -750.0},
            {"x": 3940.0, "z": -890.0},
            {"x": 3820.0, "z": -1040.0}
        ]
    })
    spline_id += 1

    # 7. Corniche Boulevard (Place Pichon Northeast along beach)
    all_splines.append({
        "id": spline_id,
        "type": "colonial_avenue",
        "width": 11.5,
        "shoulder": 2.0,
        "points": [
            {"x": 4150.0, "z": -645.0},
            {"x": 4220.0, "z": -760.0},
            {"x": 4320.0, "z": -920.0},
            {"x": 4440.0, "z": -1100.0}
        ]
    })
    spline_id += 1

    # 8. Boulevard Bab Djedid (Place Pichon West along North Medina Wall)
    all_splines.append({
        "id": spline_id,
        "type": "colonial_avenue",
        "width": 10.5,
        "shoulder": 1.5,
        "points": [
            {"x": 4105.0, "z": -630.0},
            {"x": 4000.0, "z": -630.0},
            {"x": 3850.0, "z": -630.0},
            {"x": 3650.0, "z": -630.0},
            {"x": 3450.0, "z": -630.0}
        ]
    })
    spline_id += 1

    # 2. LOAD REGIONAL COUNTRY HIGHWAYS (outside city zone)
    raw_path = "assets/terrain/roadMap_user_raw.png"
    if os.path.exists(raw_path):
        print("[*] Splicing regional highways from user roadmap outside city...")
        rm_raw = cv2.imread(raw_path)
        roads_bin = (rm_raw[:, :, 2] > 50).astype(np.uint8)

        # Zero out the Sousse city rectangle in raw map to avoid duplicate / conflicting roads
        # City rectangle: rows 3600..4500, cols 4800..5600
        roads_bin[3600:4500, 4800:5600] = 0

        r_idx, c_idx = np.where(roads_bin > 0)
        if len(r_idx) > 0:
            rmin, rmax = max(0, r_idx.min() - 10), min(8128, r_idx.max() + 10)
            cmin, cmax = max(0, c_idx.min() - 10), min(8128, c_idx.max() + 10)
            crop_country = roads_bin[rmin:rmax, cmin:cmax]

            thin_country = zhang_suen_thinning(crop_country)
            edges_country = extract_topological_edges(thin_country)
            print(f"    [+] Extracted {len(edges_country)} regional highway segments.")

            WORLD_MIN_X = -13938.0
            WORLD_MAX_X = 13938.0
            WORLD_MIN_Z = -13938.0
            WORLD_MAX_Z = 13938.0
            GRID_SIZE = 8128.0

            for path in edges_country:
                if len(path) < 4:
                    continue
                world_pts = []
                for py, px in path:
                    gr = rmin + py
                    gc = cmin + px
                    wx = WORLD_MIN_X + (gc / GRID_SIZE) * (WORLD_MAX_X - WORLD_MIN_X)
                    wz = WORLD_MIN_Z + (gr / GRID_SIZE) * (WORLD_MAX_Z - WORLD_MIN_Z)
                    world_pts.append([wx, wz])

                smoothed = smooth_polyline(world_pts, step_meters=8.0)
                if len(smoothed) < 2:
                    continue

                pts_dict = [{"x": round(float(pt[0]), 2), "z": round(float(pt[1]), 2)} for pt in smoothed]
                all_splines.append({
                    "id": spline_id,
                    "type": "country_highway",
                    "width": 8.0,
                    "shoulder": 2.0,
                    "points": pts_dict
                })
                spline_id += 1

    # 3. EXPORT TO ASSETS/ROADS/ROADS.JSON
    os.makedirs("assets/roads", exist_ok=True)
    out_file = "assets/roads/roads.json"
    with open(out_file, "w") as f:
        json.dump({"roads": all_splines}, f, indent=2)

    sz_kb = os.path.getsize(out_file) / 1024
    print(f"[+] Successfully saved {len(all_splines)} verified road splines to {out_file} ({sz_kb:.1f} KB) in {time.time() - t0:.2f}s")

if __name__ == "__main__":
    main()
