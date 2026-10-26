"""A4 Layer Prep.

Split splat_A (R olive / G rock / B ochre) and splat_B (R sand / G steppe /
B salt) into six single-channel weightmaps, derive the seventh road layer from
the A5 road mask, and renormalise so the seven weights sum to 255 per pixel.

Acceptance (plan A4): sum = 255 +/- 1; no ground layer above 0 inside the sea
mask (sand only on the shoreline band). Writes layers/*.png + layer_manifest.json.
"""

from __future__ import annotations

import os

import numpy as np

from . import common, contract

LAYERS = ["olive", "rock", "ochre", "sand", "steppe", "salt", "road"]


def run(run_dir, road_mask_2017, ocean=True):
    layers_dir = common.ensure_dir(os.path.join(run_dir, "layers"))

    a = common.load_rgb(contract.SPLAT_A).astype(np.float32)
    b = common.load_rgb(contract.SPLAT_B).astype(np.float32)
    sea = common.load_gray(contract.SEA_MASK) > 127
    dem = contract.packed_to_elev(
        common.read_r16(contract.R16, contract.GRID).astype(np.float64))

    olive = a[:, :, 0].copy()
    rock = a[:, :, 1].copy()
    ochre = a[:, :, 2].copy()
    sand = b[:, :, 0].copy()
    steppe = b[:, :, 1].copy()
    salt = b[:, :, 2].copy()

    # shoreline sand band: keep a little sand on land just above sea, so the
    # beach reads, but nothing below sea.
    shore = (dem > -0.5) & (dem < 3.0) & (~sea)
    sand = np.where(shore, np.maximum(sand, 180.0), sand)

    road = np.where(np.asarray(road_mask_2017, dtype=bool), 255.0, 0.0)

    if ocean:
        # nothing but the (already land-side) sand band survives on water
        for ch in (olive, rock, ochre, steppe, salt):
            ch[sea] = 0.0
        sand[sea] = 0.0

    ground = [olive, rock, ochre, sand, steppe, salt]
    gsum = ground[0] + ground[1] + ground[2] + ground[3] + ground[4] + ground[5]
    # land pixels with no weight anywhere and no road fall back to dry steppe
    empty = (~sea) & (gsum <= 1e-4) & (road <= 0)
    ground[4] = np.where(empty, 1.0, ground[4])
    gsum = gsum + np.where(empty, 1.0, 0.0)
    # road overrides ground; elsewhere ground is scaled to fill 255 - road
    target = 255.0 - road
    scale = np.where(gsum > 0, target / np.maximum(gsum, 1e-6), 0.0)
    ground = [ch * scale for ch in ground]

    out = {name: ch for name, ch in zip(LAYERS[:6], ground)}
    out["road"] = road
    for name in LAYERS:
        np.clip(out[name], 0, 255, out=out[name])
        common.save_gray(out[name], os.path.join(layers_dir, f"layer_{name}.png"))

    stack = np.stack([out[n] for n in LAYERS], axis=2)
    sums = stack.sum(axis=2)
    on_land_no_road = (~sea) & (road <= 0)
    sum_err = float(np.abs(sums[on_land_no_road] - 255.0).max()) if on_land_no_road.any() else 0.0
    sea_ground_nonzero = int(((stack[:, :, :6] > 0) & sea[:, :, None]).sum())

    manifest = {
        "grid": contract.GRID,
        "layers": [
            {"index": i, "name": n, "image": f"layers/layer_{n}.png", "blend": "weight",
             "physical_material": phys(n), "footstep_tag": foot(n)}
            for i, n in enumerate(LAYERS)
        ],
        "acceptance": {
            "max_sum_error_land": sum_err,
            "sea_ground_nonzero_px": sea_ground_nonzero,
            "sum_ok": bool(sum_err <= 1.0),
        },
    }
    common.save_json(manifest, os.path.join(run_dir, "layer_manifest.json"))

    # Two RGBA8 uploads keep the terrain pass to two weight-map fetches.
    packed0 = np.stack([out[n] for n in ("olive", "rock", "ochre", "sand")], axis=2)
    packed1 = np.stack([out["steppe"], out["salt"], out["road"],
                        np.zeros_like(out["road"])], axis=2)
    common.save_rgba(packed0, os.path.join(layers_dir, "terrain_weights_0.png"))
    common.save_rgba(packed1, os.path.join(layers_dir, "terrain_weights_1.png"))
    # The full-resolution road coverage is used for close-up terrain shading;
    # keep the registered source raster intact alongside the 2017 weight pack.
    common.save_gray(common.load_gray(contract.ROAD_SURFACE_MASK),
                     os.path.join(run_dir, "roads_surface_mask_4096.png"))
    manifest["gpu_packs"] = [
        {"image": "layers/terrain_weights_0.png", "channels": ["olive", "rock", "ochre", "sand"]},
        {"image": "layers/terrain_weights_1.png", "channels": ["steppe", "salt", "road", "spare"]},
    ]
    common.save_json(manifest, os.path.join(run_dir, "layer_manifest.json"))

    means = {n: float(out[n].mean()) for n in LAYERS}
    return {"layers_dir": layers_dir, "manifest": manifest,
            "max_sum_error_land": sum_err, "sea_ground_nonzero_px": sea_ground_nonzero,
            "means": means}


def phys(name):
    return {
        "olive": "PM_Cultivated", "rock": "PM_Limestone", "ochre": "PM_Soil",
        "sand": "PM_Sand", "steppe": "PM_Steppe", "salt": "PM_Salt",
        "road": "PM_Asphalt",
    }[name]


def foot(name):
    return {
        "olive": "grass", "rock": "stone", "ochre": "dirt", "sand": "sand",
        "steppe": "dry_grass", "salt": "crust", "road": "gravel",
    }[name]
