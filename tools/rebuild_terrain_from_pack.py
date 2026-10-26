#!/usr/bin/env python3
"""rebuild_terrain_from_pack.py

Rebuild the live game heightmap (assets/terrain/heightmap.png) from the
authoritative Sousse c.1930s terrain pack in assets/terrain/heightmapAssets/
(pack v3, 2017x2017, r16 + classification masks).

Rationale
---------
The shipped game heightmap lost precision (8-bit quantised, 256 levels) and its
sea/port/oued masking drifted. The pack is true 16-bit (60k+ levels) and carries
explicit masks for sea, port, dry riverbeds (oued), sebkha salt flat and urban,
so it fixes: staircase banding, the flat raw-0 seabed, the empty port basin and
the soft "inland black lines" rivers.

Mapping (keeps the game's world and vertical datum so nothing else breaks)
-------------------------------------------------------------------------
  packs:  raw 0 = -30 m seabed, sea level = raw 15123 = 0 m, +100 m at 65535
  game :  raw 0 = -70 m,   sea level = raw 3000 = -62.78 m,  +87.8 m at 65535
  game_elev = pack_elev + 62.78        (pack sea 0  -> game sea -62.78)
  out_raw   = (game_elev + 70) / 157.8 * 65535

Carves on top of the pack relief
--------------------------------
  * port mask (urban_mask B)  -> NO artificial dredging: the pack already keeps
                                the quays/esplanade ~2 m above sea (dry); only the
                                handful of below-sea harbour pixels flood from the
                                water sim. Pass --port-depth to force a manned
                                basin depth instead.
  * oued mask                 -> channels cut -{oued_cut} m (default 3 m),
                                 floored just below sea so coastal wadis meet water
  * sebkha mask               -> flattened to sea +0.5 m
  * seabed                    -> clamped to the game min so deep water reads flat

Run with no args to write heightmap.png (after a timestamped backup of the
current file), previews, and an updated heightmap_meta.txt. Pass --dry to only
write previews/stats.
"""

import argparse
import os
import shutil
import sys
from datetime import datetime

import numpy as np
from PIL import Image

ASSETS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "terrain")
PACK = os.path.join(ASSETS, "heightmapAssets")

# Pack datum (README v3): seabed -30 m, sea 0 m, land +100 m.
PACK_MIN, PACK_MAX, PACK_SEA = -30.0, 100.0, 0.0
# Game datum (Terrain.cpp bounds + WaterLevel.h kBaseWaterLevel = -62.78).
GAME_MIN, GAME_MAX, GAME_SEA = -70.0, 87.8, -62.78

GAME_RANGE = GAME_MAX - GAME_MIN


def pack_ele2raw(r):
    return PACK_MIN + r / 65535.0 * (PACK_MAX - PACK_MIN)


def game_elev_to_raw(e):
    return np.clip(np.rint((e - GAME_MIN) / GAME_RANGE * 65535.0), 0, 65535).astype(np.uint32)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry", action="store_true", help="write previews/stats only")
    ap.add_argument("--oued-cut", type=float, default=3.0, help="m cut into oued channels (default 3)")
    ap.add_argument("--port-depth", type=float, default=None,
                    help="if set, dredge the whole urban port mask to this game m; default leaves pack geometry")
    args = ap.parse_args()

    r16 = os.path.join(PACK, "heightmap_2017_16bit.r16")
    raw = np.fromfile(r16, dtype="<u2")
    size = int(round(np.sqrt(raw.size)))
    pack = raw.reshape(size, size).astype(np.float32)
    if pack.shape != (2017, 2017):
        sys.exit(f"unexpected pack size {pack.shape}")

    open_img = lambda n: Image.open(os.path.join(PACK, n)).convert("L")
    sea = np.array(open_img("sea_mask.png")) > 127
    oued = np.array(open_img("oued_mask_dry_riverbeds.png")) > 40
    sebkha = np.array(open_img("sebkha_mask.png")) > 40
    urban = np.array(Image.open(os.path.join(PACK, "urban_mask_medina_R_villenouvelle_G_port_B.png")).convert("RGB"))
    port = urban[:, :, 2] > 40

    elev = pack_ele2raw(pack)                     # pack metres
    game = elev + GAME_SEA                        # game metres (sea -> -62.78)

    # 1. Port harbour: the pack already keeps the quays/esplanade ~2 m above sea.
    #    Only dredge below-sea harbour pockets (so the water sim has a clean bowl
    #    to fill); never lift the dry port district.
    if port.any():
        belowSea = game < GAME_SEA
        if args.port_depth is not None:
            game[port] = args.port_depth
        else:
            game[port & belowSea & (game > GAME_SEA - 6.0)] = GAME_SEA - 1.5

    # 2. Oued dry riverbeds: cut channels into the relief.
    if oued.any():
        game[oued] = np.maximum(game[oued] - args.oued_cut, GAME_SEA + 0.3)

    # 3. Sebkha salt flat: near-sea-level pan.
    if sebkha.any():
        game[sebkha] = GAME_SEA + 0.5

    out = game_elev_to_raw(game)

    out_path = os.path.join(ASSETS, "heightmap.png")
    preview = os.path.join(ASSETS, "heightmap_pack_preview.png")
    seamap = os.path.join(ASSETS, "heightmap_pack_seamap.png")

    if not args.dry:
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        backup = os.path.join(ASSETS, f"heightmap_backup_pre_pack_{stamp}.png")
        shutil.copyfile(out_path, backup)
        print(f"backup written: {backup}")

        img = Image.fromarray(out)
        if img.mode != "I;16":
            img = img.convert("I;16")
        img.save(out_path)
        print(f"wrote {out_path}  {out.shape[0]}x{out.shape[1]}  mode={img.mode}  "
              f"distinct={len(np.unique(out))}  min={out.min()} max={out.max()}")
    else:
        print(f"dry run: NOT writing {out_path}  "
              f"distinct={len(np.unique(out))}  min={out.min()} max={out.max()}")

    print(f"  sea px             {sea.sum():7d}  {sea.mean()*100:5.2f}%")
    print(f"  port px (dredged)  {port.sum():7d}  {port.mean()*100:5.2f}%")
    print(f"  oued px (channel)  {oued.sum():7d}  {oued.mean()*100:5.2f}%")
    print(f"  sebkha px (flat)   {sebkha.sum():7d}  {sebkha.mean()*100:5.2f}%")
    print(f"  below sea level    {(out < 3000).sum():7d}  {(out < 3000).mean()*100:5.2f}%")

    # Previews
    v = (game - GAME_MIN) / GAME_RANGE
    v = np.clip(v, 0, 1)
    vis = (v[..., None] * np.array([[[60, 120, 200]]], dtype=np.float32) +
           (1 - v[..., None]) * np.array([[[220, 200, 150]]], dtype=np.float32))
    vis = np.clip(vis, 0, 255).astype(np.uint8)
    Image.fromarray(vis).resize((1024, 1024), Image.BILINEAR).save(preview)
    Image.fromarray((sea.astype(np.uint8) * 255)).save(seamap)
    print(f"previews: {preview}, {seamap}")


if __name__ == "__main__":
    main()