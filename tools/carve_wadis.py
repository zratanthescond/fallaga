"""carve_wadis.py

Incise the pack's dry riverbeds (oueds) into the live heightmap so they read
as real topographic channels instead of flat painted bands.

The engine keeps the native pack datum (sea at y = 0, -30 m seabed, +100 m
land; see src/WaterLevel.h) and, in registered dry-channel pack mode, trusts
that the heightmap already carries the oued relief (it skips the legacy
wet-water carve stamps). The shipped pack r16 has *no* incision -- the oued
pixels actually sit slightly above their surroundings -- so the wadis render as
colour on a tabletop. This tool cuts the missing relief.

How the cut follows the terrain
-------------------------------
The channel floor is the *heightmap surface minus a depth*, so the bed inherits
the map's own downhill gradient and descends with the land (no floating trench).
Depth scales with the oued flow strength, and banks feather outward so the
cutbanks are smooth. The sea, the sebkha rim and the flat port platform are
never touched, so channels stay dry and the intentional flats stay flat.

Output defaults to assets/terrain/heightmap_declipped.png's sibling naming is
NOT used; by default the carved map is written to
assets/terrain/heightmap_carved.png. Pass --promote to overwrite
assets/terrain/heightmap.png (writes a timestamped backup first).

Usage:
    python tools/carve_wadis.py                 # -> heightmap_carved.png
    python tools/carve_wadis.py --promote       # overwrite the live heightmap
"""

from __future__ import annotations

import argparse
import os
import shutil
import time

import numpy as np
from PIL import Image
from scipy import ndimage

from tools.pack_pipeline import contract

MIN_H = contract.GAME_MIN_M if False else -30.0  # native pack datum
MAX_H = 100.0
SRC_OUED = "assets/terrain/heightmapAssets/oued_mask_dry_riverbeds.png"
SRC_SEA = "assets/terrain/heightmapAssets/sea_mask.png"
SRC_SEBKHA = "assets/terrain/heightmapAssets/sebkha_mask.png"
SRC_PORT = "assets/terrain/port_platform_mask.png"
LIVE = "assets/terrain/heightmap.png"
WADI_MASK = "assets/terrain/heightmap_wadis.png"


def load_height(path):
    im = Image.open(path)
    a = np.asarray(im)
    if a.ndim == 3:
        a = a[:, :, 0]
    raw = a.astype(np.float64)
    if raw.max() > 255.0:
        raw = raw / 65535.0
    else:
        raw = raw / 255.0
    return MIN_H + raw * (MAX_H - MIN_H)


def save_height(path, h):
    raw = np.clip((h - MIN_H) / (MAX_H - MIN_H), 0.0, 1.0)
    Image.fromarray(np.round(raw * 65535.0).astype(np.uint16)).save(path)


def load_gray(path, size):
    if not os.path.exists(path):
        return None
    a = np.asarray(Image.open(path).convert("L"))
    if a.shape != size:
        a = np.asarray(Image.fromarray(a).resize((size[1], size[0]), Image.BILINEAR))
    return a.astype(np.float32)


def carve_channels(h, oued, protect=None, min_depth=5.0, max_depth=20.0,
                   feather_px=2.5, keep_dry=0.5, strength_min=0.06, gain=2.2,
                   bank_blur_px=1.6):
    """IncisE dry channels into a native-datum heightmap (metres in, metres out).

    The floor is the surface minus a depth that scales with oued flow strength,
    so it inherits the map's downhill gradient. Banks feather outward; protected
    zones (sea, sebkha, port platform) are left untouched and beds never drop
    below `keep_dry`.

    The pack mask is faint (median nonzero ~32/255) and only covers a few
    percent of the map, so `gain` boosts the flow value before it is mapped to
    depth -- otherwise every channel collapses to the shallow floor and reads as
    flat.

    `bank_blur_px` blurs the channel footprint before it is multiplied into the
    depth. That turns the mask's 9.3 m pixel-staircase into a smooth, organic
    cross-section: without it every wall of a 10-20 m deep cut sits exactly on a
    raster contour and renders as a chain of straight lines instead of a curve.
    """
    strength = np.clip(oued / 255.0, 0.0, 1.0)
    channel = strength > strength_min
    if protect is not None:
        channel = channel & ~protect
    width = max(1.0, bank_blur_px)
    core = ndimage.gaussian_filter(strength.astype(np.float32), width)
    feather_px = max(1.0, feather_px)
    dist = ndimage.distance_transform_edt(~channel)
    taper = np.where(channel, 1.0, np.clip(1.0 - dist / feather_px, 0.0, 1.0))
    depth = min_depth + (max_depth - min_depth) * np.clip(core * gain, 0.0, 1.0)
    cut = depth * taper
    cut = ndimage.gaussian_filter(cut.astype(np.float32), 0.6)
    h_new = h - cut
    h_new = np.minimum(h_new, h)
    if protect is not None:
        h_new = np.where(protect, h, h_new)
    h_new = np.where(~(protect if protect is not None else np.zeros_like(channel)),
                     np.maximum(h_new, keep_dry), h_new)
    return np.clip(h_new, MIN_H, MAX_H), channel
    h_new = h - cut
    h_new = np.minimum(h_new, h)
    if protect is not None:
        h_new = np.where(protect, h, h_new)
    h_new = np.where(~(protect if protect is not None else np.zeros_like(channel)),
                     np.maximum(h_new, keep_dry), h_new)
    return np.clip(h_new, MIN_H, MAX_H), channel


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--in", dest="src", default=LIVE)
    ap.add_argument("--out", dest="dst", default="assets/terrain/heightmap_carved.png")
    ap.add_argument("--oued", default=SRC_OUED)
    ap.add_argument("--promote", action="store_true")
    ap.add_argument("--max-depth", type=float, default=14.0,
                    help="metres of incision for the strongest oued (flow=1.0)")
    ap.add_argument("--min-depth", type=float, default=3.5,
                    help="metres of incision for the faintest mapped oued")
    ap.add_argument("--gain", type=float, default=2.2,
                    help="boost applied to the faint pack mask before depth mapping")
    ap.add_argument("--feather-m", type=float, default=24.0,
                    help="bank feather width in metres beyond the mask edge")
    ap.add_argument("--blur-m", type=float, default=15.0,
                    help="gaussian blur of the channel footprint, metres (smooths the raster mask into organic banks)")
    ap.add_argument("--keep-dry", type=float, default=0.5,
                    help="never carve the bed below this elevation (metres, native datum)")
    args = ap.parse_args()

    t0 = time.time()
    h = load_height(args.src)
    H, W = h.shape
    cell_m = 9.3257  # native pack texel (src/WaterLevel.h kMetresPerPixel)

    oued = load_gray(args.oued, (H, W))
    if oued is None:
        raise SystemExit(f"oued mask not found: {args.oued}")
    sea = load_gray(SRC_SEA, (H, W))
    sebkha = load_gray(SRC_SEBKHA, (H, W))
    port = load_gray(SRC_PORT, (H, W))

    strength = np.clip(oued / 255.0, 0.0, 1.0)
    channel = strength > 0.06
    print(f"[*] {args.src}  {W}x{H}  elev {h.min():.1f}..{h.max():.1f} m")
    print(f"[*] oued: {int(channel.sum())} px ({channel.mean()*100:.2f}%), "
          f"strength up to {strength.max():.2f}, gain {args.gain}")

    # Excluded zones: open sea, sebkha salt flat, flat port platform.
    protect = np.zeros((H, W), bool)
    if sea is not None:
        protect |= sea > 127
    if sebkha is not None:
        protect |= sebkha > 40
    if port is not None:
        protect |= port > 40
    carve_zone = channel & ~protect

    h_new, _ = carve_channels(
        h, oued, protect=protect,
        min_depth=args.min_depth, max_depth=args.max_depth,
        feather_px=args.feather_m / cell_m, keep_dry=args.keep_dry,
        gain=args.gain, bank_blur_px=args.blur_m / cell_m)

    lowered = (h - h_new)
    ch = carve_zone
    print(f"[*] carved {int((lowered > 0.01).sum())} px; "
          f"max lowering {lowered.max():.2f} m; mean in-channel "
          f"{lowered[ch].mean():.2f} m")
    print(f"[*] oued-zone median elev {np.median(h[ch]):.2f} -> "
          f"{np.median(h_new[ch]):.2f} m")

    os.makedirs("assets/terrain", exist_ok=True)
    save_height(args.dst, h_new)

    # before | after | diff*8 preview
    def to8(hh):
        return np.clip((hh - MIN_H) / (MAX_H - MIN_H) * 255.0, 0, 255).astype(np.uint8)
    diff = np.clip((h - h_new) * 60.0, 0, 255).astype(np.uint8)
    strip = np.concatenate([to8(h), to8(h_new), diff], axis=1)
    prev = os.path.splitext(args.dst)[0] + "_preview.png"
    Image.fromarray(strip).save(prev)
    print(f"[+] wrote {args.dst}")
    print(f"[+] wrote {prev}  (left=before, mid=after, right=diff x60)")

    if args.promote:
        stamp = time.strftime("%Y%m%d-%H%M%S")
        bak = f"assets/terrain/heightmap.png.bak_carve_{stamp}"
        shutil.copyfile(args.src, bak)
        shutil.copyfile(args.dst, args.src)
        if os.path.exists(WADI_MASK):
            pass
        print(f"[+] promoted {args.dst} -> {args.src} (backup: {bak})")

    print(f"[*] done in {time.time() - t0:.1f}s")


if __name__ == "__main__":
    main()
