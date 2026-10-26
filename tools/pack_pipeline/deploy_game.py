"""deploy_game.py

Stage the native-datum terrain and registered road surface into the game's
live assets (the custom C++ engine, RDR2_Prototype).

Writes assets/terrain/{heightmap.png, terrain_splat.png, heightmap_wadis.png}
after timestamped backups, plus an updated heightmap_meta.txt and a small
sanity report. Refuses to run if A1-style integrity of the pack r16 fails.

CLI
---
    python -m tools.pack_pipeline.deploy_game
    python -m tools.pack_pipeline.deploy_game --dry
"""

from __future__ import annotations

import argparse
import os
import shutil
import sys
from datetime import datetime

import numpy as np
from PIL import Image

from . import common, contract
from tools.carve_wadis import carve_channels

SPLAT_SIZE = 2508
WADI_W = 2017


def _game_to_raw(game_m):
    """game-frame metres -> game raw (0..65535)."""
    raw = ((game_m - contract.GAME_MIN_M) / contract.GAME_RANGE_M) * 65535.0
    return np.clip(np.rint(raw), 0, 65535).astype(np.uint32)


def _game_raw(elev_pack):
    """pack metres -> game raw (0..65535) using the game datum (single shift)."""
    return _game_to_raw(elev_pack + contract.GAME_SEA_M)


def _game_preview(pack_elev_sampled, sea):
    """A 1024 preview of the game frame for a quick visual check."""
    v = ((pack_elev_sampled + contract.GAME_SEA_M) - contract.GAME_MIN_M) / contract.GAME_RANGE_M
    v = np.clip(v, 0, 1)
    rgb = (v[..., None] * np.array([[[60, 120, 200]]], np.float32) +
           (1 - v[..., None]) * np.array([[[225, 200, 150]]], np.float32))
    rgb[sea] = [40, 90, 170]
    return np.clip(rgb, 0, 255).astype(np.uint8)


def run(run_dir=None, dry=False, oued_cut=3.0, port_depth=None,
        oued_min_depth=5.0, oued_max_depth=20.0, oued_feather_m=26.0,
        oued_gain=2.6, oued_blur_m=15.0):
    if not run_dir:
        sys.exit("--run-dir is required so deployment is tied to a verified A0 build")
    qa_path = os.path.join(run_dir, "qa_report.json")
    if not os.path.exists(qa_path):
        sys.exit("QA report is missing; deployment requires a completed verified build")
    qa = common.load_json(qa_path)
    if qa.get("hard_stop"):
        sys.exit("deployment stopped: a hard QA gate failed; see qa_report.json")

    # --- height -------------------------------------------------------------
    if not os.path.exists(contract.R16) or \
       common.file_size(contract.R16) != contract.GRID * contract.GRID * 2:
        sys.exit("pack r16 missing or wrong size; run A1 first")

    raw = common.read_r16(contract.R16, contract.GRID).astype(np.float32)
    elev_pack = contract.packed_to_elev(raw.astype(np.float64)).astype(np.float32)

    sea = common.load_gray(contract.SEA_MASK) > 127
    oued = common.load_gray(contract.OUED_MASK) > 40
    sebkha = common.load_gray(contract.SEBKHA_MASK) > 40
    urban = common.load_rgb(contract.URBAN_MASK)
    port = urban[:, :, 2] > 40

    # Keep the packed height source unchanged for datum, but cut the dry oued
    # channels: the engine (Terrain.cpp) skips legacy wadi-bed carving in
    # registered dry-channel pack mode and trusts the heightmap to carry the
    # relief, which the raw pack lacks. Channels are incised along the map's own
    # elevation gradient; sea/sebkha/port-platform stay untouched.
    m_per_px = contract.METRES_PER_PX
    protect = sea.copy()
    protect |= sebkha
    if os.path.exists(contract.PORT_PLATFORM_MASK):
        protect |= common.load_gray(contract.PORT_PLATFORM_MASK) > 40
    oued_strength = common.load_gray(contract.OUED_MASK).astype(np.float32)
    elev_carved, _ = carve_channels(
        elev_pack, oued_strength, protect=protect,
        min_depth=oued_min_depth, max_depth=oued_max_depth,
        feather_px=oued_feather_m / m_per_px, gain=oued_gain,
        bank_blur_px=oued_blur_m / m_per_px)
    out_raw = np.clip(np.rint(contract.elev_to_packed(elev_carved)), 0, 65535).astype(np.uint16)

    # --- surface ------------------------------------------------------------
    a = common.load_rgb(contract.SPLAT_A).astype(np.float32)
    b = common.load_rgb(contract.SPLAT_B).astype(np.float32)
    slope = common.load_gray(contract.SLOPE_MAP).astype(np.float32)
    land = ~sea

    coast = np.clip((3.5 - elev_pack) / 3.0, 0, 1)
    dunes = b[:, :, 0] / 255.0 * 0.70
    salt = np.maximum(b[:, :, 2] / 255.0, (sebkha * 0.8))
    R = np.where(land, np.minimum(1.0, np.maximum(np.maximum(coast, dunes), salt)), 0.0)
    steep = np.clip((slope - 90.0) / 90.0, 0, 1)
    G = np.where(land, np.minimum(1.0, np.maximum(steep, a[:, :, 1] / 255.0)), 0.0)
    B = np.where(land, oued * 1.0, 0.0)
    city = (urban[:, :, 0] > 40) | (urban[:, :, 1] > 40) | (urban[:, :, 2] > 40)
    R = np.where(city & land, 0.0, R)
    G = np.where(city & land, 0.0, G)
    B = np.where(city & land, 0.0, B)
    A = np.clip(1.0 - R - G - B, 0.35, 1.0)
    A = np.where(land, A, 1.0)
    tot = np.where(land, R + G + B + A, 1.0)
    R = np.where(land, R / tot, 0.0)
    G = np.where(land, G / tot, 0.0)
    B = np.where(land, B / tot, 0.0)
    A = np.where(land, A / tot, 1.0)

    splat = np.stack([R, G, B, A], axis=2) * 255.0
    splat_rs = np.stack([
        np.array(Image.fromarray(np.clip(c, 0, 255).astype(np.uint8))
                 .resize((SPLAT_SIZE, SPLAT_SIZE), Image.BILINEAR))
        for c in [R, G, B, A]
    ], axis=2) * 255.0

    wadis = np.where(oued, 0, 255).astype(np.uint8)

    # --- write --------------------------------------------------------------
    packed0_path = os.path.join(run_dir, "layers", "terrain_weights_0.png")
    packed1_path = os.path.join(run_dir, "layers", "terrain_weights_1.png")
    if not os.path.exists(packed0_path) or not os.path.exists(packed1_path):
        sys.exit("A4 packed terrain weights are missing; run the build pipeline first")

    if dry:
        print("[deploy] DRY RUN - nothing written")
        print(f"  height distinct={len(np.unique(out_raw))} "
              f"min={out_raw.min()} max={out_raw.max()}")
        print(f"  sea px {int(sea.sum())} ({sea.mean()*100:.2f}%)")
        print(f"  port px {int(port.sum())} oued px {int(oued.sum())} "
              f"sebkha px {int(sebkha.sum())}")
        return {"dry": True}

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    road_surface = common.load_gray(contract.ROAD_SURFACE_MASK)
    road_surface_2017 = common.load_gray(
        os.path.join(run_dir, "road_surface_mask_2017.png"))
    port_platform = common.load_gray(contract.PORT_PLATFORM_MASK)
    targets = {"heightmap.png": out_raw, "terrain_splat.png": splat_rs,
               "heightmap_wadis.png": wadis,
               "roads_surface_mask_4096.png": road_surface,
               "road_surface_mask_2017.png": road_surface_2017,
               "port_platform_mask.png": port_platform}
    # Runtime foliage consumes the same A8 outputs that QA inspects. Stage all
    # four together so the engine can fail closed if any mask is unavailable.
    for name in ("density_olive.png", "density_steppe.png", "density_dune.png",
                 "exclusion_mask.png"):
        source = os.path.join(run_dir, name)
        if not os.path.exists(source):
            sys.exit(f"A8 vegetation output is missing: {source}")
        targets[name] = common.load_gray(source)
    written = {}
    for name, arr in targets.items():
        tgt = os.path.join(contract.ASSETS, name)
        bak = os.path.join(contract.ASSETS, f"{name}.bak_deploy_{stamp}")
        if os.path.exists(tgt):
            shutil.copyfile(tgt, bak)
        if name == "heightmap.png":
            common.save_u16(arr.astype(np.uint16), tgt)
        elif name == "terrain_splat.png":
            common.save_rgba(arr, tgt)
        elif name in ("roads_surface_mask_4096.png", "road_surface_mask_2017.png",
                      "port_platform_mask.png"):
            common.save_gray(arr, tgt)
        else:
            common.save_gray(arr, tgt)
        written[name] = {"backup": os.path.basename(bak) if os.path.exists(bak) else None,
                         "shape": list(arr.shape)}

    for name, source in (("terrain_weights_0.png", packed0_path),
                         ("terrain_weights_1.png", packed1_path)):
        target = os.path.join(contract.ASSETS, name)
        backup = os.path.join(contract.ASSETS, f"{name}.bak_deploy_{stamp}")
        if os.path.exists(target):
            shutil.copyfile(target, backup)
        shutil.copyfile(source, target)
        written[name] = {"backup": os.path.basename(backup) if os.path.exists(backup) else None,
                         "source": os.path.relpath(source, contract.REPO_ROOT)}

    meta = (
        f"size={contract.GRID}x{contract.GRID}\n"
        f"worldX={contract.WORLD_MIN_X:.4f}..{contract.WORLD_MAX_X:.4f}\n"
        f"worldZ={contract.WORLD_MIN_Z:.4f}..{contract.WORLD_MAX_Z:.4f}\n"
        f"minHeight={contract.GAME_MIN_M}m\n"
        f"maxHeight={contract.GAME_MAX_M}m\n"
        f"texelSize={contract.GAME_M_PER_PX:.2f}m\n"
        f"source=heightmapAssets/heightmap_2017_16bit.r16 (unaltered little-endian uint16); "
        f"native pack datum (-30m seabed / 0m sea / +100m land); "
        f"horizontal spacing={contract.METRES_PER_PX}m; dry oued channels incised "
        f"along the elevation gradient ({contract.OUED_MASK.rsplit('/', 1)[-1]}); "
        f"sea/sebkha/port-platform untouched.\n"
    )
    with open(os.path.join(contract.ASSETS, "heightmap_meta.txt"), "w",
              encoding="utf-8") as fh:
        fh.write(meta)
    written["heightmap_meta.txt"] = os.path.join(contract.ASSETS, "heightmap_meta.txt")

    prev = _game_preview(elev_pack[::4, ::4], sea[::4, ::4])
    Image.fromarray(prev).resize((1024, 1024), Image.BILINEAR).save(
        os.path.join(contract.ASSETS, "heightmap_deploy_preview.png"))

    report = {
        "deployed": True,
        "stamp": stamp,
        "files": written,
        "stats": {
            "height_distinct": int(len(np.unique(out_raw))),
            "height_min": int(out_raw.min()),
            "height_max": int(out_raw.max()),
            "sea_level_raw_game": contract.SEA_LEVEL_U16,
            "sea_px": int(sea.sum()),
            "port_px": int(port.sum()),
            "oued_px": int(oued.sum()),
            "sebkha_px": int(sebkha.sum()),
        },
    }
    if run_dir:
        common.save_json(report, os.path.join(run_dir, "deploy_report.json"))
    else:
        common.save_json(report, os.path.join(contract.OUTPUT_ROOT,
                                              f"deploy_{stamp}.json"))
    print(json_dump(report["stats"]))
    return report


def json_dump(obj):
    return common.json.dumps(obj, indent=2)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--run-dir", required=True, help="A0 build directory containing passed QA and A4 packed weights")
    ap.add_argument("--dry", action="store_true")
    ap.add_argument("--oued-cut", type=float, default=3.0)
    ap.add_argument("--port-depth", type=float, default=None,
                    help="force a manned basin depth (game m); default keeps "
                         "the pack's dry quays and only dredges below-sea pockets")
    args = ap.parse_args(argv)
    run(run_dir=args.run_dir, dry=args.dry, oued_cut=args.oued_cut, port_depth=args.port_depth)
    return 0


if __name__ == "__main__":
    sys.exit(main())
