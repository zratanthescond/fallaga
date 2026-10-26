#!/usr/bin/env python3
"""rebuild_from_pack_raw.py

Uses only the pack's files to rebuild heightmap, surface, and wadis for the game.

- Height: heightmap_2017_16bit.r16 (LE u16) → pack (-30..100, sea 0 at 15123/65535)
  → game space (-70..87.8, sea at -62.78) → out 2017x2017 I;16
- Apply minimal corrections from masks:
  - oued_mask_dry_riverbeds.png: cut channels slightly (-3m) so they read crisp
  - sebkha_mask.png: near-sea flat (sea+0.5m)
  - NO forced port dredge (quays are dry in pack)
- Surface: build terrain_splat.png RGBA in 2508; build heightmap_wadis.png (2017) from oued mask (black=<128)
- Use splat_A/B and masks as-is (no external assumptions except the pack README)

Writes backups first.
"""

import os, shutil
from datetime import datetime
import numpy as np
from PIL import Image

ASSETS = "assets/terrain"
PACK = os.path.join(ASSETS, "heightmapAssets")

GAME_MIN, GAME_MAX, GAME_SEA = -70.0, 87.8, -62.78  # game

def main():
    # heightmap
    raw = np.fromfile(os.path.join(PACK, "heightmap_2017_16bit.r16"), dtype="<u2")
    size = int(round(np.sqrt(raw.size)))
    pack = raw.reshape(size, size).astype(np.float32)
    elev_pack = -30.0 + pack / 65535.0 * 130.0  # m
    elev_game = elev_pack + GAME_SEA  # pack sea 0 -> game -62.78

    # masks
    oued = np.array(Image.open(os.path.join(PACK, "oued_mask_dry_riverbeds.png")).convert("L")) > 40
    sebkha = np.array(Image.open(os.path.join(PACK, "sebkha_mask.png")).convert("L")) > 40
    sea = np.array(Image.open(os.path.join(PACK, "sea_mask.png")).convert("L")) > 127
    elev_game[oued] = np.maximum(elev_game[oued] - 3.0, GAME_SEA + 0.3)
    elev_game[sebkha] = GAME_SEA + 0.5

    g = np.clip(np.rint((elev_game - GAME_MIN) / (GAME_MAX - GAME_MIN) * 65535.0), 0, 65535).astype(np.uint32)

    # write heightmap
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    for tgt in (os.path.join(ASSETS, "heightmap.png"), os.path.join(ASSETS, "terrain_splat.png"), os.path.join(ASSETS, "heightmap_wadis.png")):
        if os.path.exists(tgt):
            shutil.copyfile(tgt, tgt + f".bak_{stamp}")
            print("bak", tgt)

    Image.fromarray(g.reshape(size, size).astype(np.uint16)).save(os.path.join(ASSETS, "heightmap.png"))
    print("wrote heightmap.png", g.shape)

    # surface
    sandB = np.array(Image.open(os.path.join(PACK, "splat_B_sand_steppe_salt.png")).convert("RGB")).astype(np.float32)
    rockA = np.array(Image.open(os.path.join(PACK, "splat_A_olive_rock_ochre.png")).convert("RGB")).astype(np.float32)
    slope = np.array(Image.open(os.path.join(PACK, "slope_map.png")).convert("L")).astype(np.float32)
    urban = np.array(Image.open(os.path.join(PACK, "urban_mask_medina_R_villenouvelle_G_port_B.png")).convert("RGB"))
    land = ~sea

    # build R G B A (land only, normalized)
    coast = np.clip((3.5 - elev_pack) / 3.0, 0, 1)
    R = np.where(land, np.minimum(1.0, np.maximum(coast, sandB[:, :, 0] / 255.0 * 0.7)), 0.0)
    G = np.where(land, np.minimum(1.0, np.maximum((slope - 90) / 90.0, rockA[:, :, 1] / 255.0)), 0.0)
    B = np.where(land, oued * 1.0, 0.0).astype(np.float32)
    city = (urban[:, :, 0] > 40) | (urban[:, :, 1] > 40) | (urban[:, :, 2] > 40)
    R = np.where(city & land, 0.0, R)
    G = np.where(city & land, 0.0, G)
    B = np.where(city & land, 0.0, B)
    A = np.clip(1.0 - R - G - B, 0.35, 1.0)
    A = np.where(land, A, 1.0)
    tot = np.where(land, R + G + B + A, 1.0)
    R = np.where(land, R / tot, 0.0); G = np.where(land, G / tot, 0.0); B = np.where(land, B / tot, 0.0); A = np.where(land, A / tot, 1.0)

    spl = np.stack([R * 255, G * 255, B * 255, A * 255], axis=2).astype(np.uint8)
    spl2508 = np.array(Image.fromarray(spl).resize((2508, 2508), Image.BILINEAR))
    Image.fromarray(spl2508).save(os.path.join(ASSETS, "terrain_splat.png"))
    print("wrote terrain_splat.png", spl2508.shape)

    wadis = np.where(oued, 0, 255).astype(np.uint8)
    Image.fromarray(wadis, "L").save(os.path.join(ASSETS, "heightmap_wadis.png"))
    print("wrote heightmap_wadis.png", wadis.shape)

if __name__ == "__main__":
    main()