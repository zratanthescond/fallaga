#!/usr/bin/env python3
"""
extract_road_splines.py
=======================
Extracts clean, fully continuous vector road splines from hand-drawn historical road strokes:
1. Direct Zhang-Suen morphological thinning (100% topology-preserving).
2. Contraction of junction pixel clusters into exact junction centroid nodes.
3. Extraction of continuous topological road edges between distinct junctions (eliminates zero-length self-loops).
4. Includes dead-end branches connecting to junctions.
5. Smooths polyline waypoints while strictly pinning junction centroids.
6. Exports structured JSON to assets/roads/roads.json.
"""

import os
import cv2
import numpy as np
import json
import time
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

def extract_topological_network(thin):
    """Extracts continuous graph edges connecting distinct junction centroids."""
    padded = np.pad(thin, 1, mode='constant')
    neighbors = [
        padded[:-2, 1:-1], padded[:-2, 2:], padded[1:-1, 2:],
        padded[2:, 2:], padded[2:, 1:-1], padded[2:, :-2],
        padded[1:-1, :-2], padded[:-2, :-2],
    ]
    transitions = sum(
        ((neighbors[i] == 0) & (neighbors[(i + 1) % 8] > 0)).astype(np.uint8)
        for i in range(8)
    )

    # A raw 8-neighbor count mistakes the staircase pixels on diagonal lines for
    # intersections. Count zero-to-one transitions around the pixel instead:
    # one transition is a dead end and three or more are real branch points.
    junction_mask = ((transitions >= 3) & (thin > 0)).astype(np.uint8)
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

    # Trace edges leaving junction clusters
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
                                        # Only terminate if hitting a DIFFERENT junction, or if edge length >= 8
                                        if end_jid != start_jid or len(path) >= 8:
                                            path.append((end_cy, end_cx))
                                            found_next = True
                                            reached_end = True
                                            break
                                    elif not visited_paths[next_y, next_x]:
                                        visited_paths[next_y, next_x] = True
                                        path.append((float(next_y), float(next_x)))
                                        curr_y, curr_x = next_y, next_x
                                        found_next = True
                                        break
                        if not found_next:
                            break
                    # Reject zero-length self-loops
                    if len(path) >= 3 and path[0] != path[-1]:
                        edges.append(path)

    # Trace edges from dead-ends (degree == 1)
    end_ys, end_xs = np.where((transitions == 1) & (thin > 0))
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
                            found_next = True
                            reached_end = True
                            break
                        elif not visited_paths[next_y, next_x]:
                            visited_paths[next_y, next_x] = True
                            path.append((float(next_y), float(next_x)))
                            curr_y, curr_x = next_y, next_x
                            found_next = True
                            break
            if not found_next:
                break
        if len(path) >= 2:
            edges.append(path)

    return edges

def smooth_and_resample_edge(points, step_meters=4.0, m_per_px=3.4296):
    pts = np.array(points, dtype=np.float32)
    if len(pts) < 2:
        return points

    diffs = np.diff(pts, axis=0)
    dists = np.sqrt((diffs ** 2).sum(axis=1)) * m_per_px
    cum_dist = np.concatenate(([0.0], np.cumsum(dists)))
    total_len = cum_dist[-1]

    if total_len < step_meters:
        return [points[0], points[-1]]

    num_samples = max(2, int(np.round(total_len / step_meters)) + 1)
    target_dists = np.linspace(0.0, total_len, num_samples)

    sampled_y = np.interp(target_dists, cum_dist, pts[:, 0])
    sampled_x = np.interp(target_dists, cum_dist, pts[:, 1])
    resampled = np.column_stack((sampled_y, sampled_x))

    if len(resampled) >= 3:
        sigma = 1.6 if len(resampled) >= 5 else 1.0
        sm = gaussian_filter1d(resampled, sigma=sigma, axis=0, mode='nearest')
        sm[0] = resampled[0]
        sm[-1] = resampled[-1]
        return list(zip(sm[:, 0], sm[:, 1]))
    return list(zip(resampled[:, 0], resampled[:, 1]))

def main():
    t0 = time.time()
    print("[*] Starting Clean Topological Vector Road Spline Extraction...")

    raw_path = "assets/terrain/roadMap_user_raw.png"
    if not os.path.exists(raw_path):
        print(f"[!] Error: {raw_path} not found!")
        return

    rm_raw = cv2.imread(raw_path)
    roads_binary = (rm_raw[:, :, 2] > 50).astype(np.uint8)

    r_idx, c_idx = np.where(roads_binary > 0)
    rmin, rmax = max(0, r_idx.min() - 10), min(8128, r_idx.max() + 10)
    cmin, cmax = max(0, c_idx.min() - 10), min(8128, c_idx.max() + 10)
    crop = roads_binary[rmin:rmax, cmin:cmax]

    dist_orig = cv2.distanceTransform(crop, cv2.DIST_L2, 5)

    print(f"[*] Thinning {np.count_nonzero(crop):,} road stroke pixels directly with Zhang-Suen...")
    thin = zhang_suen_thinning(crop)
    print(f"    [+] 1-pixel skeleton: {np.count_nonzero(thin):,} pixels")

    print("[*] Extracting fully connected topological graph edges...")
    raw_edges = extract_topological_network(thin)
    print(f"    [+] Extracted {len(raw_edges)} continuous road edges (0 self-loops)")

    WORLD_MIN_X = -13938.0
    WORLD_MAX_X = 13938.0
    WORLD_MIN_Z = -13938.0
    WORLD_MAX_Z = 13938.0
    GRID_SIZE = 8128.0
    M_PER_PX = (WORLD_MAX_X - WORLD_MIN_X) / GRID_SIZE

    os.makedirs("assets/roads", exist_ok=True)
    spline_list = []

    for i, path in enumerate(raw_edges):
        smoothed = smooth_and_resample_edge(path, step_meters=4.0, m_per_px=M_PER_PX)
        if len(smoothed) < 2:
            continue

        r_vals = [dist_orig[int(np.clip(pt[0], 0, crop.shape[0]-1)), int(np.clip(pt[1], 0, crop.shape[1]-1))] for pt in path]
        avg_r = np.median(r_vals)

        mid_pt = smoothed[len(smoothed) // 2]
        full_r = rmin + mid_pt[0]
        full_c = cmin + mid_pt[1]
        is_city = (3600 <= full_r <= 4500) and (4800 <= full_c <= 5600)

        if is_city and avg_r > 6.0:
            rtype = "colonial_avenue"
            width = 11.0
            shoulder = 2.0
        elif is_city:
            rtype = "urban_street"
            width = 7.5
            shoulder = 1.5
        elif avg_r > 8.0:
            rtype = "country_highway"
            width = 8.0
            shoulder = 2.0
        elif avg_r > 3.5:
            rtype = "country_dirt"
            width = 6.0
            shoulder = 1.5
        else:
            rtype = "rural_track"
            width = 4.0
            shoulder = 1.0

        world_points = []
        for py, px in smoothed:
            gr = rmin + py
            gc = cmin + px
            wx = WORLD_MIN_X + (gc / GRID_SIZE) * (WORLD_MAX_X - WORLD_MIN_X)
            wz = WORLD_MIN_Z + (gr / GRID_SIZE) * (WORLD_MAX_Z - WORLD_MIN_Z)
            world_points.append({"x": round(float(wx), 2), "z": round(float(wz), 2)})

        spline_list.append({
            "id": i,
            "type": rtype,
            "width": width,
            "shoulder": shoulder,
            "points": world_points
        })

    out_file = "assets/roads/roads.json"
    with open(out_file, "w") as f:
        json.dump({"roads": spline_list}, f, indent=2)

    sz_kb = os.path.getsize(out_file) / 1024
    print(f"[*] Successfully saved {len(spline_list)} connected road splines to {out_file} ({sz_kb:.1f} KB) in {time.time() - t0:.2f}s")

if __name__ == "__main__":
    main()
