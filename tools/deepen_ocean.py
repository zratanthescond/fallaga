#!/usr/bin/env python3
"""
Deepen the ocean/wadi basin in heightmap.png without touching land elevations.

The game interprets heightmap.png as a linear map: raw pixel [0..65535] ->
world height [minHeight..maxHeight]. Currently minHeight=-50, so the darkest
pixels (raw=0, ~32% of the map) sit at a perfectly flat -50 floor - the ocean
has no real bathymetry and its "coastline" is a very gentle, shallow shelf
that bleeds far inland along low-gradient terrain.

This script:
  1. Leaves every pixel whose current height is >= WATER_LEVEL (dry land)
     completely unchanged in world-space height.
  2. Stretches every pixel below WATER_LEVEL (the sea/wadi basin) down into
     a deeper range [NEW_MIN_HEIGHT..WATER_LEVEL], and adds mild smooth
     noise (tapering to zero right at the shoreline) so the seafloor has
     real bathymetry instead of one flat plain.
  3. Re-encodes everything back into 16-bit raw values using the NEW
     minHeight/maxHeight, and writes the result.

The caller must update TerrainManager's minHeight parameter (and the ocean
shader's hmMinElev uniform) from -50 to NEW_MIN_HEIGHT to match.
"""

import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter
import argparse
from pathlib import Path
import shutil

OLD_MIN_H = -50.0
OLD_MAX_H = 87.8
NEW_MAX_H = 87.8  # land ceiling stays identical


def deepen(heightmap_path, output_path, new_min_h, water_level, noise_amp, seed):
    print(f"[DeepenOcean] Loading {heightmap_path}")
    img = Image.open(heightmap_path)
    raw = np.array(img).astype(np.float64)
    print(f"[DeepenOcean] Size: {raw.shape}, raw range: {raw.min()}..{raw.max()}")

    old_t = raw / 65535.0
    old_height = OLD_MIN_H + old_t * (OLD_MAX_H - OLD_MIN_H)

    is_sea = old_height < water_level
    print(f"[DeepenOcean] Sea/basin coverage: {100.0 * is_sea.sum() / is_sea.size:.2f}%")

    # frac: 0 at the old absolute floor, 1 right at the shoreline (water_level)
    frac = np.clip((old_height - OLD_MIN_H) / (water_level - OLD_MIN_H), 0.0, 1.0)
    deep_height = new_min_h + frac * (water_level - new_min_h)

    # Smooth bathymetry noise, amplitude tapering to 0 at the shoreline so the
    # land/sea boundary stays exactly where it was (no new flooding introduced).
    rng = np.random.default_rng(seed)
    noise = gaussian_filter(rng.standard_normal(raw.shape), sigma=25.0)
    noise = noise / (np.abs(noise).max() + 1e-6) * noise_amp
    taper = 1.0 - frac  # 1 at the deepest floor, 0 at the shoreline
    deep_height += noise * taper

    new_height = np.where(is_sea, deep_height, old_height)

    new_t = np.clip((new_height - new_min_h) / (NEW_MAX_H - new_min_h), 0.0, 1.0)
    new_raw = np.round(new_t * 65535.0).astype(np.uint16)

    print(f"[DeepenOcean] New height range: {new_height.min():.2f}..{new_height.max():.2f}")
    print(f"[DeepenOcean] Land pixels max height delta from original: "
          f"{np.max(np.abs(new_height[~is_sea] - old_height[~is_sea])):.6f} (should be ~0)")

    out_img = Image.fromarray(new_raw, mode='I;16')
    out_img.save(output_path)
    print(f"[DeepenOcean] Saved: {output_path}")


def main():
    parser = argparse.ArgumentParser(description="Deepen the ocean/wadi basin without touching land")
    parser.add_argument("heightmap", help="Path to heightmap.png")
    parser.add_argument("--new-min-height", type=float, default=-70.0,
                         help="New floor elevation for the deepest sea point (default -70)")
    parser.add_argument("--water-level", type=float, default=-48.5,
                         help="Current game water level / land-sea split (default -48.5)")
    parser.add_argument("--noise-amp", type=float, default=6.0,
                         help="Max bathymetry noise amplitude in world units (default 6.0)")
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("-o", "--output", help="Output path (default: overwrite in place, with backup)")
    args = parser.parse_args()

    hm_path = Path(args.heightmap)
    if not hm_path.exists():
        print(f"ERROR: {hm_path} not found")
        return 1

    if args.output:
        out_path = Path(args.output)
    else:
        backup_path = hm_path.parent / f"{hm_path.stem}_before_deepen.png"
        if not backup_path.exists():
            print(f"[Backup] {backup_path}")
            shutil.copy(hm_path, backup_path)
        out_path = hm_path

    deepen(hm_path, out_path, args.new_min_height, args.water_level, args.noise_amp, args.seed)
    print(f"\nNEXT STEP: update minHeight from {OLD_MIN_H} to {args.new_min_height} in "
          f"src/Terrain.cpp (TerrainManager constructor call + hmMinElev uniform).")
    return 0


if __name__ == "__main__":
    exit(main())
