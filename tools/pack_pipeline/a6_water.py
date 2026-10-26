"""A6 Water Data.

From sea_mask + the DEM: shoreline polyline (z = 0), a depth/bathymetry
summary, the wet-sand band (0..14 m inland of the shore), the sebkha polygon
(flat, no permanent water) and the dry oued channels (gravel beds, no water).

Outputs: water_data.json, shoreline.geojson, wet_sand_band.png, oued_lines.json
"""

from __future__ import annotations

import os

import numpy as np
from scipy import ndimage

from . import common, contract

try:
    import cv2
except Exception:  # pragma: no cover
    cv2 = None

WET_SAND_M = 14.0


def _contours_to_polys(mask, min_area_px):
    polys = []
    if cv2 is None:
        return polys
    cnts, _ = cv2.findContours(mask.astype(np.uint8), cv2.RETR_LIST, cv2.CHAIN_APPROX_SIMPLE)
    for c in cnts:
        if cv2.contourArea(c) < min_area_px:
            continue
        approx = cv2.approxPolyDP(c, 2.0, True).reshape(-1, 2)
        polys.append(approx.astype(np.float64))
    return polys


def _to_world(poly_px):
    return [list(contract.terrain_px_to_world(float(x), float(y))) for x, y in poly_px]


def run(run_dir):
    dem = contract.packed_to_elev(
        common.read_r16(contract.R16, contract.GRID).astype(np.float64))
    sea = common.load_gray(contract.SEA_MASK) > 127
    sebkha = common.load_gray(contract.SEBKHA_MASK) > 40
    oued = common.load_gray(contract.OUED_MASK) > 40
    port = common.load_gray(contract.PORT_PLATFORM_MASK) > 127

    # ---- shoreline (sea/land boundary ~ z=0) --------------------------------
    shore = sea & ~ndimage.binary_erosion(sea, iterations=1)
    shore_polys = _contours_to_polys(sea, min_area_px=20000)
    shore_geojson = {
        "type": "FeatureCollection",
        "crs": {"type": "name", "properties": {"name": "game-world-metres"}},
        "features": [
            {"type": "Feature",
             "properties": {"kind": "shoreline", "sea_level_m": 0.0},
             "geometry": {"type": "LineString", "coordinates": _to_world(p)}}
            for p in shore_polys
        ],
    }
    common.save_json(shore_geojson, os.path.join(run_dir, "shoreline.geojson"))

    # ---- bathymetry ---------------------------------------------------------
    depth = np.where(sea, np.maximum(0.0, -dem), 0.0)
    bathy = {
        "sea_px": int(sea.sum()),
        "sea_fraction": float(sea.mean()),
        "max_depth_m": float(depth.max()),
        "mean_depth_m": float(depth[sea].mean()) if sea.any() else 0.0,
        "p95_depth_m": float(np.percentile(depth[sea], 95)) if sea.any() else 0.0,
    }

    # ---- wet-sand band (metres from shore, on land) -------------------------
    m_per_px = contract.GAME_M_PER_PX
    dist_px = ndimage.distance_transform_edt(~shore)
    wet_band = (~sea) & (dist_px * m_per_px <= WET_SAND_M) & (dem >= -0.5)
    common.save_gray((wet_band * 255).astype(np.uint8),
                     os.path.join(run_dir, "wet_sand_band.png"))

    # ---- sebkha -------------------------------------------------------------
    sebkha_polys = _contours_to_polys(sebkha, min_area_px=5000)
    common.save_json({
        "flat_level_m": 0.5,
        "permanent_water": False,
        "polygons_world_m": [_to_world(p) for p in sebkha_polys],
    }, os.path.join(run_dir, "sebkha_geometry.json"))

    # ---- oueds (dry channels) ----------------------------------------------
    oued_lines = []
    if oued.any():
        skel = common.skeletonize(oued, max_iter=60)
        oued_lines = _skeleton_to_polylines(skel)
    common.save_json({
        "dry": True,
        "channels": [{"terrain_px": [[float(x), float(y)] for x, y in ln],
                      "world_m": _to_world(ln)} for ln in oued_lines],
    }, os.path.join(run_dir, "oued_lines.json"))

    # ---- port basin (harbour water within reach of the port platform) --------
    reach = max(1, int(round(300.0 / m_per_px)))
    basin = sea & ndimage.binary_dilation(port, iterations=reach)
    basin_polys = _contours_to_polys(basin, min_area_px=200) if basin.any() else []

    water = {
        "agent": "A6",
        "grid": contract.GRID,
        "wet_sand_width_m": WET_SAND_M,
        "bathymetry": bathy,
        "shoreline_segments": len(shore_polys),
        "sebkha_polygons": len(sebkha_polys),
        "oued_channels": len(oued_lines),
        "port_basin_polygons": len(basin_polys),
        "port_basin_world_m": [_to_world(p) for p in basin_polys],
        "note": "seabed edge drops to %.0f m; add a sea skirt/distant plane." % bathy["max_depth_m"],
    }
    common.save_json(water, os.path.join(run_dir, "water_data.json"))
    return {"water_data": water, "shoreline_geojson": shore_geojson,
            "wet_sand_px": int(wet_band.sum()), "oued_channels": len(oued_lines),
            "basin_polygons": len(basin_polys)}


def _skeleton_to_polylines(skel):
    """Very small skeleton->polyline tracer (endpoints + walks)."""
    ys, xs = np.where(skel)
    if len(xs) == 0:
        return []
    pts = set(zip(xs.tolist(), ys.tolist()))
    nbrs = [(-1, -1), (0, -1), (1, -1), (-1, 0), (1, 0), (-1, 1), (0, 1), (1, 1)]
    degree = {p: sum((p[0] + dx, p[1] + dy) in pts for dx, dy in nbrs) for p in pts}
    ends = [p for p in pts if degree[p] == 1]
    starts = ends if ends else [next(iter(pts))]
    visited_edges = set()
    lines = []
    for s in starts:
        cur, prev = s, None
        line = [cur]
        while True:
            nxt = None
            for dx, dy in nbrs:
                q = (cur[0] + dx, cur[1] + dy)
                if q in pts and q != prev and (cur, q) not in visited_edges:
                    nxt = q
                    break
            if nxt is None:
                break
            visited_edges.add((cur, nxt))
            visited_edges.add((nxt, cur))
            line.append(nxt)
            prev, cur = cur, nxt
            if degree.get(cur, 0) != 2 and cur != s:
                if degree.get(cur, 0) == 1:
                    break
                # junction: start a new line later; stop this one
                break
        if len(line) >= 2:
            arr = np.array(line, float)
            lines.append(arr)
    return lines
