"""A7 Port Geometry from the supplied platform, basin, and quay masks.

Outputs world-space basin/platform/quay/mole outlines and berth candidates.
"""

from __future__ import annotations

import math
import os

import numpy as np
from scipy import ndimage

from . import common, contract

try:
    import cv2
except Exception:  # pragma: no cover
    cv2 = None

BERTH_SPACING_M = 120.0
BASIN_REACH_M = 300.0  # harbour water considered part of the port basin


def _world(poly):
    return [list(contract.terrain_px_to_world(float(x), float(y))) for x, y in poly]


def run(run_dir):
    port = common.load_gray(contract.PORT_PLATFORM_MASK) > 127
    sea = common.load_gray(contract.SEA_MASK) > 127

    port_land = port & ~sea
    # the pack keeps the quays dry, so the basin is the harbour water within
    # reach of the port platform (open sea is one body, so a reach gate is
    # needed to isolate the port basin).
    reach = max(1, int(round(BASIN_REACH_M / contract.GAME_M_PER_PX)))
    basin = sea & ndimage.binary_dilation(port, iterations=reach)

    quay_mask = port_land & ndimage.binary_dilation(basin, iterations=2) & ~basin

    geoms = {"basin": [], "quay_edge": [], "platform": [], "moles": []}
    if cv2 is not None and basin.any():
        for c in cv2.findContours(basin.astype(np.uint8), cv2.RETR_EXTERNAL,
                                  cv2.CHAIN_APPROX_SIMPLE)[0]:
            if cv2.contourArea(c) >= 300:
                geoms["basin"].append(cv2.approxPolyDP(c, 2.0, True).reshape(-1, 2).astype(float))
        for c in cv2.findContours(port_land.astype(np.uint8), cv2.RETR_EXTERNAL,
                                  cv2.CHAIN_APPROX_SIMPLE)[0]:
            if cv2.contourArea(c) >= 300:
                geoms["platform"].append(
                    cv2.approxPolyDP(c, 2.0, True).reshape(-1, 2).astype(float))
        if os.path.exists(contract.ROAD_QUAY_ISLANDS_MASK):
            islands = common.load_gray(contract.ROAD_QUAY_ISLANDS_MASK) > 127
            islands = cv2.resize(islands.astype(np.uint8),
                                 (contract.GRID, contract.GRID),
                                 interpolation=cv2.INTER_AREA) > 127
            for c in cv2.findContours(islands.astype(np.uint8), cv2.RETR_EXTERNAL,
                                      cv2.CHAIN_APPROX_SIMPLE)[0]:
                if cv2.contourArea(c) >= 10:
                    geoms["moles"].append(
                        cv2.approxPolyDP(c, 1.0, True).reshape(-1, 2).astype(float))

    if quay_mask.any():
        skel = common.skeletonize(quay_mask, max_iter=40)
        ys, xs = np.where(skel)
        quay_pts = np.column_stack([xs, ys]).astype(float)

    # ---- berths: sample the basin rim every ~120 m, face the water ----------
    berths = []
    m_per_px = contract.GAME_M_PER_PX
    if geoms["basin"] and port_land.any():
        dist_to_quay = ndimage.distance_transform_edt(~port_land)
        rim = max(geoms["basin"], key=lambda p: len(p))
        n = len(rim)
        cand = []
        if n >= 4:
            seglen = np.hypot(*(np.roll(rim, -1, axis=0) - rim).T)
            cum = np.concatenate([[0.0], np.cumsum(seglen)])
            total_m = cum[-1] * m_per_px
            n_berth = max(1, int(total_m // BERTH_SPACING_M))
            for k in range(n_berth):
                target = (k + 0.5) * BERTH_SPACING_M / m_per_px
                i = int(np.searchsorted(cum, target) - 1)
                i = min(max(i, 0), n - 1)
                t = (target - cum[i]) / max(seglen[i], 1e-6)
                p = rim[i] + t * (rim[(i + 1) % n] - rim[i])
                # keep only points right at the quay (within ~40 m of land)
                if dist_to_quay[int(np.clip(round(p[1]), 0, contract.GRID - 1)),
                                int(np.clip(round(p[0]), 0, contract.GRID - 1))] > 3:
                    continue
                j = (i + 1) % n
                tangent = rim[j] - rim[i]
                normal = np.array([-tangent[1], tangent[0]])
                normal = normal / (np.hypot(*normal) or 1.0)
                probe = p + normal * 5
                inward = sea[int(np.clip(probe[1], 0, contract.GRID - 1)),
                             int(np.clip(probe[0], 0, contract.GRID - 1))]
                if not inward:
                    normal = -normal
                cand.append((p, math.degrees(math.atan2(normal[1], normal[0]))))
        # plan wants 8-16 berths: subsample the quay candidates evenly
        if len(cand) > 16:
            idx = np.linspace(0, len(cand) - 1, 16).round().astype(int)
            cand = [cand[i] for i in np.unique(idx)]
        for p, heading in cand:
            berths.append({
                "id": len(berths),
                "terrain_px": [float(p[0]), float(p[1])],
                "world_m": list(contract.terrain_px_to_world(float(p[0]), float(p[1]))),
                "heading_deg": round(heading, 2),
            })

    port_geometry = {
        "agent": "A7",
        "platform_px": int(port_land.sum()),
        "basin_px": int(basin.sum()),
        "quay_edge_px": int(quay_mask.sum()),
        "basin_polygons_world_m": [_world(p) for p in geoms["basin"]],
        "quay_edge_world_m": _world(quay_pts) if quay_mask.any() else [],
        "platform_polygons_world_m": [_world(p) for p in geoms["platform"]],
        "mole_polygons_world_m": [_world(p) for p in geoms["moles"]],
    }
    common.save_gray((basin * 255).astype(np.uint8),
                     os.path.join(run_dir, "port_basin_mask_2017.png"))
    common.save_json(port_geometry, os.path.join(run_dir, "port_geometry.json"))
    common.save_json({"spacing_m": BERTH_SPACING_M, "berths": berths},
                     os.path.join(run_dir, "berth_candidates.json"))

    return {"agent": "A7", "basin_polygons": len(geoms["basin"]),
            "platform_polygons": len(geoms["platform"]),
            "berths": len(berths),
            "basin_px": int(basin.sum()), "platform_px": int(port_land.sum())}
