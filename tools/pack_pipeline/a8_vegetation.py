"""A8 Vegetation Density.

Density maps (8-bit) from the A4 weightmaps, gated by slope, road buffer and
the exclusions from plan A8: sea, sebkha, port platform, road surfaces
(dilated 15 m) and the medina / ville nouvelle urban zone.

Outputs: density_olive.png, density_steppe.png, density_dune.png,
         exclusion_mask.png, vegetation_data.json
"""

from __future__ import annotations

import os

import numpy as np
from scipy import ndimage

from . import common, contract

ROAD_BUFFER_M = 15.0
OLIVE_MAX_SLOPE_DEG = 12.0
OLIVE_ROAD_CLEAR_M = 25.0
BEACH_BAND_M = 14.0


def run(run_dir, road_mask_2017):
    a = common.load_rgb(contract.SPLAT_A).astype(np.float32)
    b = common.load_rgb(contract.SPLAT_B).astype(np.float32)
    slope = common.load_gray(contract.SLOPE_MAP).astype(np.float32) / 255.0 * 60.0
    sea = common.load_gray(contract.SEA_MASK) > 127
    sebkha = common.load_gray(contract.SEBKHA_MASK) > 40
    urban = common.load_rgb(contract.URBAN_MASK)
    city = (urban[:, :, 0] > 40) | (urban[:, :, 1] > 40) | (urban[:, :, 2] > 40)
    port = common.load_gray(contract.PORT_PLATFORM_MASK) > 127

    m_per_px = contract.GAME_M_PER_PX
    road = np.asarray(road_mask_2017, dtype=bool)
    road_buffer = ndimage.binary_dilation(
        road, iterations=max(1, int(round(ROAD_BUFFER_M / m_per_px))))
    olive_clear = ndimage.binary_dilation(
        road, iterations=max(1, int(round(OLIVE_ROAD_CLEAR_M / m_per_px))))

    exclusion = sea | sebkha | port | city | road_buffer

    # distance from shoreline for the dune band
    shore = sea & ~ndimage.binary_erosion(sea, iterations=1)
    dist_px = ndimage.distance_transform_edt(~shore) * m_per_px

    olive = a[:, :, 0]
    steppe = b[:, :, 1]
    dune = b[:, :, 0]

    den_olive = olive * (slope <= OLIVE_MAX_SLOPE_DEG) * (~olive_clear) * (~exclusion)
    den_steppe = steppe * (~exclusion)
    den_dune = dune * (dist_px > BEACH_BAND_M) * (~exclusion)
    # keep the dune band out of the urban/port core (already in exclusion)

    for name, arr in [("olive", den_olive), ("steppe", den_steppe), ("dune", den_dune)]:
        common.save_gray(arr, os.path.join(run_dir, f"density_{name}.png"))
    common.save_gray((exclusion * 255).astype(np.uint8),
                     os.path.join(run_dir, "exclusion_mask.png"))

    data = {
        "agent": "A8",
        "road_buffer_m": ROAD_BUFFER_M,
        "olive_max_slope_deg": OLIVE_MAX_SLOPE_DEG,
        "olive_road_clear_m": OLIVE_ROAD_CLEAR_M,
        "beach_band_m": BEACH_BAND_M,
        "exclusion_px": int(exclusion.sum()),
        "exclusion_fraction": float(exclusion.mean()),
        "mean_density": {
            "olive": float(den_olive.mean()),
            "steppe": float(den_steppe.mean()),
            "dune": float(den_dune.mean()),
        },
    }
    common.save_json(data, os.path.join(run_dir, "vegetation_data.json"))
    return {"agent": "A8", "exclusion_px": int(exclusion.sum()),
            "mean_density": data["mean_density"],
            "exclusion_frac": data["exclusion_fraction"]}
