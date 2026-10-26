"""declip_fractalize_terrain.py

Phase-1 terrain fix, done as a non-destructive post-process on the authored
16-bit heightmap (there is no runnable master baker: the Copernicus DEM it needs
lives in a scratch/ dir that no longer exists, and heightmap_meta.txt records the
current file as "User-enhanced terrain heightmap").

What it does
------------
1. De-clips saturated plates. The live map has a ~12.8 km region pinned at exactly
   MIN_H (raw 0) and smaller plates at exactly MAX_H (raw 65535). A constant floor is
   what reads as a giant flat plane. The sea floor is lifted and given fractal
   bathymetry instead, so it is no longer a plane.
2. Adds domain-warped fBm detail everywhere, with two warp scales (large to bend the
   big shapes, small to roughen edges), amplitude modulated by:
     * slope  - steep faces get more detail, flats get a few cm to  ~0.4 m,
     * depth  - the sea gets large-scale bathymetry,
     * roads  - detail is suppressed along roads so driving stays smooth.
   Soft everywhere, no new clamps/terraces.

Output defaults to assets/terrain/heightmap_declipped.png (the live heightmap is left
untouched). Pass --promote to overwrite heightmap.png, which first writes a timestamped
backup. Always writes a before/after/diff preview PNG next to the output.

Usage:
    python tools/declip_fractalize_terrain.py
    python tools/declip_fractalize_terrain.py --promote
"""

import argparse
import math
import os
import shutil
import time

import numpy as np
from PIL import Image

MIN_H, MAX_H = -70.0, 87.8
H_RANGE = MAX_H - MIN_H
WORLD_MIN, WORLD_MAX = -13938.0, 13938.0
WATER_LEVEL = -48.5

HEIGHTMAP = "assets/terrain/heightmap.png"
ROADMAP = "assets/terrain/roadMap.png"


# ─────────────────────────────────────────────────────────────────────────────
# I/O
# ─────────────────────────────────────────────────────────────────────────────
def load_height(path):
    im = Image.open(path)
    a = np.asarray(im)
    if a.ndim == 3:
        a = a[:, :, 0]
    raw = a.astype(np.float32)
    if raw.max() > 255.0:
        raw /= 65535.0
    else:
        raw /= 255.0
    return MIN_H + raw * H_RANGE


def save_height(path, h):
    raw = np.clip((h - MIN_H) / H_RANGE, 0.0, 1.0)
    u16 = np.round(raw * 65535.0).astype(np.uint16)
    Image.fromarray(u16).save(path)


def save_preview(path, before, after):
    def to8(h):
        return np.clip((h - MIN_H) / H_RANGE * 255.0, 0, 255).astype(np.uint8)

    diff = np.clip(np.abs(after - before) * 8.0, 0, 255).astype(np.uint8)
    strip = np.concatenate([to8(before), to8(after), diff], axis=1)
    Image.fromarray(strip).save(path)


# ─────────────────────────────────────────────────────────────────────────────
# Value-noise fBm with domain warp (numpy only, no scipy)
# ─────────────────────────────────────────────────────────────────────────────
def _lattice(seed, gy, gx):
    rng = np.random.default_rng(seed)
    return (rng.random((gy, gx), dtype=np.float32) * 2.0 - 1.0)


def _sample(lat, x, y):
    """Bilinear (smoothstep) sample of a lattice at float coords x, y."""
    gy, gx = lat.shape
    x = np.clip(x, 0.0, gx - 1.0001)
    y = np.clip(y, 0.0, gy - 1.0001)
    x0 = x.astype(np.int32)
    y0 = y.astype(np.int32)
    fx = x - x0
    fy = y - y0
    fx = fx * fx * (3.0 - 2.0 * fx)
    fy = fy * fy * (3.0 - 2.0 * fy)
    v00 = lat[y0, x0]
    v10 = lat[y0, x0 + 1]
    v01 = lat[y0 + 1, x0]
    v11 = lat[y0 + 1, x0 + 1]
    top = v00 + (v10 - v00) * fx
    bot = v01 + (v11 - v01) * fx
    return top + (bot - top) * fy


def noise_field(seed, H, W, X, Y, cell_px):
    gy = int(math.ceil(H / cell_px)) + 2
    gx = int(math.ceil(W / cell_px)) + 2
    return _sample(_lattice(seed, gy, gx), X / cell_px, Y / cell_px)


def fbm(X, Y, H, W, base_cell_px, octaves, lacunarity, gain, dx, dy, seed):
    """Domain-warped fBm: samples noise at (X+dx, Y+dy)/cell over `octaves`."""
    total = np.zeros((H, W), np.float32)
    norm = 0.0
    amp = 1.0
    cell = base_cell_px
    wx = X + dx
    wy = Y + dy
    for o in range(octaves):
        gy = int(math.ceil(H / cell)) + 2
        gx = int(math.ceil(W / cell)) + 2
        lat = _lattice(seed + o, gy, gx)
        total += amp * _sample(lat, wx / cell, wy / cell)
        norm += amp
        amp *= gain
        cell /= lacunarity
    return total / norm


def box_blur(a, iters):
    a = a.astype(np.float32).copy()
    for _ in range(iters):
        a = (a + np.roll(a, 1, 0) + np.roll(a, -1, 0)
             + np.roll(a, 1, 1) + np.roll(a, -1, 1)) * 0.2
    return a


# ─────────────────────────────────────────────────────────────────────────────
# Flatness metric (same definition as tools/analyse_terrain_flatness.py)
# ─────────────────────────────────────────────────────────────────────────────
def flat_fraction(h, grid=64, flat_range=1.0):
    nz, nx = h.shape
    cz, cx = nz // grid, nx // grid
    flat = 0
    for j in range(grid):
        for i in range(grid):
            block = h[j * cz:(j + 1) * cz, i * cx:(i + 1) * cx]
            if block.size and (block.max() - block.min()) < flat_range:
                flat += 1
    return flat / (grid * grid)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--in", dest="src", default=HEIGHTMAP)
    ap.add_argument("--out", dest="dst", default="assets/terrain/heightmap_declipped.png")
    ap.add_argument("--promote", action="store_true",
                    help="overwrite the live heightmap (writes a timestamped backup first)")
    ap.add_argument("--seed", type=int, default=1337)
    ap.add_argument("--octaves", type=int, default=7)
    ap.add_argument("--lacunarity", type=float, default=2.0)
    ap.add_argument("--gain", type=float, default=0.5)
    ap.add_argument("--base-wave-m", type=float, default=170.0)
    ap.add_argument("--warp-large-m", type=float, default=230.0)
    ap.add_argument("--warp-small-m", type=float, default=18.0)
    ap.add_argument("--amp-flat", type=float, default=0.4, help="metres of detail on flat land")
    ap.add_argument("--amp-slope", type=float, default=4.0, help="extra metres on steep (30 deg) faces")
    ap.add_argument("--amp-sea", type=float, default=10.0, help="metres of bathymetry at depth")
    ap.add_argument("--ocean-lift", type=float, default=8.0,
                    help="lifts the constant sea floor off MIN_H so it stops clipping flat")
    args = ap.parse_args()

    t0 = time.time()
    h = load_height(args.src)
    H, W = h.shape
    cell_m = (WORLD_MAX - WORLD_MIN) / (W - 1)
    print(f"[*] {args.src}  {W}x{H}  {cell_m:.2f} m/texel  "
          f"elev {h.min():.1f}..{h.max():.1f} m")
    print(f"[*] flat cells before (grid 64, range<1 m): {flat_fraction(h) * 100:.1f}%")

    Y, X = np.meshgrid(np.arange(H, dtype=np.float32),
                       np.arange(W, dtype=np.float32), indexing="ij")

    gy, gx = np.gradient(h, cell_m)
    slope = np.degrees(np.arctan(np.hypot(gx, gy)))
    depth = np.clip((WATER_LEVEL - h) / (WATER_LEVEL - MIN_H), 0.0, 1.0)

    # Road suppression: feather the painted road mask so detail fades out over it.
    if os.path.exists(ROADMAP):
        rm = np.asarray(Image.open(ROADMAP).convert("L")).astype(np.float32)
        if rm.shape != h.shape:
            rm = np.asarray(Image.fromarray(rm.astype(np.uint8)).resize((W, H),
                            Image.Resampling.BILINEAR)).astype(np.float32)
        road = (rm > 16).astype(np.float32)
        suppress = 1.0 - np.clip(box_blur(road, 12) * 3.0, 0.0, 1.0)
        print("[*] road mask loaded; detail suppressed over roads")
    else:
        suppress = np.ones((H, W), np.float32)

    # Two-scale domain warp displacement, in pixels.
    warp_large_px = args.warp_large_m / cell_m
    warp_small_px = args.warp_small_m / cell_m
    dx = (warp_large_px * noise_field(args.seed + 11, H, W, X, Y, args.warp_large_m / cell_m)
          + warp_small_px * noise_field(args.seed + 12, H, W, X, Y, args.warp_small_m / cell_m))
    dy = (warp_large_px * noise_field(args.seed + 13, H, W, X, Y, args.warp_large_m / cell_m)
          + warp_small_px * noise_field(args.seed + 14, H, W, X, Y, args.warp_small_m / cell_m))

    base_cell_px = args.base_wave_m / cell_m
    f = fbm(X, Y, H, W, base_cell_px, args.octaves, args.lacunarity, args.gain,
            dx, dy, args.seed)

    slope_f = np.clip(slope / 30.0, 0.0, 1.0)
    amp = (args.amp_flat + args.amp_slope * slope_f) * (1.0 - depth) + args.amp_sea * depth

    h_new = h + f * amp * suppress + args.ocean_lift * depth
    h_new = np.clip(h_new, MIN_H, MAX_H)

    print(f"[*] detail: fbm rms {f.std():.3f}, amp mean {amp.mean():.1f} m, "
          f"warp +-{np.hypot(dx, dy).mean():.1f} px")
    print(f"[*] flat cells after  (grid 64, range<1 m): {flat_fraction(h_new) * 100:.1f}%")
    print(f"[*] elevation after: {h_new.min():.1f}..{h_new.max():.1f} m  "
          f"({time.time() - t0:.1f}s)")

    os.makedirs("assets/terrain", exist_ok=True)
    save_height(args.dst, h_new)
    preview = os.path.splitext(args.dst)[0] + "_preview.png"
    save_preview(preview, h, h_new)
    print(f"[+] wrote {args.dst}")
    print(f"[+] wrote {preview}  (left=before, middle=after, right=8x diff)")

    if args.promote:
        stamp = time.strftime("%Y%m%d-%H%M%S")
        backup = f"assets/terrain/heightmap_backup_pre_declip_{stamp}.png"
        shutil.copyfile(args.src, backup)
        shutil.copyfile(args.dst, args.src)
        print(f"[+] promoted {args.dst} -> {args.src} (backup: {backup})")


if __name__ == "__main__":
    main()
