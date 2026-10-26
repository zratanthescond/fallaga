#!/usr/bin/env python3
"""rebuild_surface_pack.py

Regenerate the DEM-aligned surface masks the terrain renderer consumes, from the
authoritative Sousse terrain pack (assets/terrain/heightmapAssets, pack v3).

The game's terrain colour is driven by terrain_splat.png sampled at heightmap UV:
   R = coastal sand weight   (also used CPU-side as the BEACH zone mask)
   G = rock weight
   B = wadi gravel weight
   A = dry plain weight (everything else)
plus heightmap_wadis.png, whose BLACK pixels (<128, min-pooled onto the DEM grid)
mark the incised wadi channels that drive the WADI zone.

Everything lives in the same 2017 UV space as the new heightmap, and every source
image ships at 2017 in identical orientation, so the weights are built 1:1 from
the pack masks and splat maps and upsampled to the classic 2508 splat size.

Mapping from pack classes to renderer weights:
  sand  R <- coastal band (land <3 m above sea) | splat_B.sand | splat_B.salt | sebkha
  rock  G <- slope_map steep | splat_A.rock
  wadi  B <- oued_mask_dry_riverbeds
  dry   A <- remainder (splat_B.steppe, splat_A.olive, urban medina/ville/port)
Sea pixels get no R so the beach zone does not scatter claims underwater.

Writes terrain_splat.png (2508 RGBA), heightmap_wadis.png (2017 L, black=wadi),
and a 1024 preview of the weight map (R sand / G rock / B wadi / A dark).
"""

import argparse
import os
import shutil
from datetime import datetime

import numpy as np
from PIL import Image

ASSETS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "terrain")
PACK = os.path.join(ASSETS, "heightmapAssets")
OUT_SPLAT = os.path.join(ASSETS, "terrain_splat.png")
OUT_WADIS = os.path.join(ASSETS, "heightmap_wadis.png")
SPLAT_W, SPLAT_H = 2508, 2508


def gray(name):
    return np.array(Image.open(os.path.join(PACK, name)).convert("L")).astype(np.float32)


def rgb(name):
    return np.array(Image.open(os.path.join(PACK, name)).convert("RGB")).astype(np.float32)


def up(ch, res=(SPLAT_W, SPLAT_H), interp=Image.BILINEAR):
    return np.array(Image.fromarray(np.clip(ch, 0, 255).astype(np.uint8)).resize(res, interp)).astype(np.float32)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sand-strength", type=float, default=0.70,
                    help="splat_B.sand dunes weight onto R (default 0.70)")
    ap.add_argument("--dry-floor", type=float, default=0.35,
                    help="minimum dry-plain weight on land (default 0.35)")
    args = ap.parse_args()

    hm = np.array(Image.open(os.path.join(PACK, "heightmap_2017_16bit.png"))).astype(np.float32)
    elev = -30.0 + hm / 65535.0 * 130.0                       # pack metres (sea = 0)
    sea = gray("sea_mask.png") > 127
    land = ~sea
    sandB = rgb("splat_B_sand_steppe_salt.png")
    rockA = rgb("splat_A_olive_rock_ochre.png")
    slope = gray("slope_map.png")
    oued = gray("oued_mask_dry_riverbeds.png") > 40
    sebkha = gray("sebkha_mask.png") > 40
    urban = rgb("urban_mask_medina_R_villenouvelle_G_port_B.png")

    # R -> coastal sand: broad fade over the first few metres above sea PLUS the
    # pack's land dune class (splat_B.sand is mostly seabed shelf, so mask it by
    # land) PLUS salt flats / sebkha.
    coast = np.clip((3.5 - elev) / 3.0, 0.0, 1.0)
    dunes = (sandB[:, :, 0] / 255.0) * args.sand_strength
    salt = np.maximum(sandB[:, :, 2] / 255.0, (sebkha * 0.8))
    R = np.where(land, np.minimum(1.0, np.maximum(coast, np.maximum(dunes, salt))), 0.0)

    # G -> rock: steep slopes plus the pack's rock class.
    steep = np.clip((slope - 90.0) / 90.0, 0.0, 1.0)
    G = np.where(land, np.minimum(1.0, np.maximum(steep, rockA[:, :, 1] / 255.0)), 0.0)

    # B -> wadi gravel: the pack's dry riverbed lines.
    B = np.where(land, oued * 1.0, 0.0).astype(np.float32)

    # City zones (medina / ville nouvelle / port) stay neutral dry so the road
    # overlay reads clean; never paint them as beach sand, rock, or wadi.
    city = (urban[:, :, 0] > 40) | (urban[:, :, 1] > 40) | (urban[:, :, 2] > 40)
    R = np.where(city & land, 0.0, R)
    G = np.where(city & land, 0.0, G)
    B = np.where(city & land, 0.0, B)

    # A -> dry plain: remainder on land, neutral in the sea so underwater pixels
    # never score BEACH / WADI. Lose-to-steppe bias: steppe is itself the dry
    # plain class, so the bottom third of the dunes band fades into plain.
    A = np.clip(1.0 - R - G - B, args.dry_floor, 1.0)
    A = np.where(land, A, 1.0)
    total = np.where(land, R + G + B + A, 1.0)
    R = np.where(land, R / total, 0.0)
    G = np.where(land, G / total, 0.0)
    B = np.where(land, B / total, 0.0)
    A = np.where(land, A / total, 1.0)

    splat = np.stack([R, G, B, A], axis=2) * 255.0
    splat2508 = np.stack([up(R), up(G), up(B), up(A)], axis=2) * 255.0

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    for src in (OUT_SPLAT, OUT_WADIS):
        if os.path.exists(src):
            bak = f"{src}.bak_{stamp}"
            shutil.copyfile(src, bak)
            print(f"backup: {bak}")

    Image.fromarray(np.clip(splat2508, 0, 255).astype(np.uint8), "RGBA").save(OUT_SPLAT)
    print(f"wrote {OUT_SPLAT} ({SPLAT_W}x{SPLAT_H})  means R/G/B/A = "
          f"{round(float(R.mean()),2)}/{round(float(G.mean()),2)}/"
          f"{round(float(B.mean()),2)}/{round(float(A.mean()),2)} (2017 basis)")

    # heightmap_wadis.png : black(<128) = incised wadi channel (renderer contract).
    wadis = np.where(oued, 0, 255).astype(np.uint8)
    Image.fromarray(wadis, "L").save(OUT_WADIS)
    print(f"wrote {OUT_WADIS} ({wadis.shape[0]}x{wadis.shape[1]})  wadi px "
          f"{int(oued.sum())}  {(oued.mean()*100):.2f}%")

    # Preview: R=red sand, G=green rock, B=blue wadi, A=dark dry.
    prev = np.stack([
        np.clip(R * 190 + 25, 0, 255),
        np.clip(G * 190 + 25, 0, 255),
        np.clip(B * 190 + 25, 0, 255),
    ], axis=2).astype(np.uint8)
    Image.fromarray(prev).resize((1024, 1024), Image.BILINEAR).save(
        os.path.join(ASSETS, "surface_pack_preview.png"))
    print("preview: assets/terrain/surface_pack_preview.png")


if __name__ == "__main__":
    main()