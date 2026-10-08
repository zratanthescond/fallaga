#!/usr/bin/env python3
"""Generate the four-channel terrain material mask aligned to heightmap.png.

Dependencies: Pillow, NumPy, and SciPy. R=sand, G=rock, B=wadi gravel,
A=dry plain. Re-run this script whenever the heightmap or wadi mask changes.
"""
from pathlib import Path
import re

import numpy as np
from PIL import Image
from scipy import ndimage


ROOT = Path(__file__).resolve().parents[1]
HEIGHTMAP = ROOT / "assets/terrain/heightmap.png"
WADI_MASK = ROOT / "assets/terrain/heightmap_wadis.png"
OUTPUT = ROOT / "assets/terrain/terrain_splat.png"
WATER_LEVEL_HEADER = ROOT / "src/WaterLevel.h"
MIN_HEIGHT = -70.0
MAX_HEIGHT = 87.8
WORLD_WIDTH_METERS = 27876.0


def smoothstep(edge0: float, edge1: float, values: np.ndarray) -> np.ndarray:
    t = np.clip((values - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def connected_ocean(underwater: np.ndarray) -> np.ndarray:
    labels, _ = ndimage.label(underwater)
    edge_labels = np.unique(np.concatenate((labels[0], labels[-1], labels[:, 0], labels[:, -1])))
    edge_labels = edge_labels[edge_labels != 0]
    if edge_labels.size == 0:
        return underwater
    return np.isin(labels, edge_labels)


def main() -> None:
    header = WATER_LEVEL_HEADER.read_text(encoding="utf-8")
    water_level_match = re.search(r"kBaseWaterLevel\s*=\s*([-+0-9.eEfF]+)", header)
    if not water_level_match:
        raise RuntimeError(f"Could not read base water level from {WATER_LEVEL_HEADER}")
    water_level = float(water_level_match.group(1).rstrip("fF"))

    raw = np.asarray(Image.open(HEIGHTMAP), dtype=np.uint16)
    h, w = raw.shape
    elevation = MIN_HEIGHT + raw.astype(np.float32) * ((MAX_HEIGHT - MIN_HEIGHT) / 65535.0)
    spacing_x = WORLD_WIDTH_METERS / max(1, w - 1)
    spacing_z = WORLD_WIDTH_METERS / max(1, h - 1)
    dz, dx = np.gradient(elevation, spacing_z, spacing_x)
    slope_degrees = np.degrees(np.arctan(np.hypot(dx, dz)))

    underwater = elevation <= water_level
    ocean = connected_ocean(underwater)
    shore_distance = ndimage.distance_transform_edt(~ocean, sampling=(spacing_z, spacing_x))
    land = ~underwater

    # Low ground within roughly 350 m of ocean becomes sand. The elevation ramp
    # prevents inland valleys and disconnected basins from turning into beaches.
    above_water = elevation - water_level
    sand = (1.0 - smoothstep(0.3, 8.0, above_water)) * (1.0 - smoothstep(80.0, 350.0, shore_distance))
    sand *= land

    # The rock mask follows slopes above 8 degrees and adds sparse high ridges.
    rock = np.maximum(smoothstep(7.0, 11.0, slope_degrees),
                      0.30 * smoothstep(78.0, 87.0, elevation))
    rock *= land

    with Image.open(WADI_MASK) as source:
        source = source.convert("L")
        source_w, source_h = source.size
        # The source is black in channels. BOX filtering measures coverage while
        # retaining sub-pixel channels when it is reduced to the DEM resolution.
        channel_pixels = source.point(lambda value: 255 if value < 128 else 0)
        channel_coverage = np.asarray(
            channel_pixels.resize((w, h), Image.Resampling.BOX), dtype=np.float32
        ) / 255.0
    wadi = smoothstep(0.015, 0.22, channel_coverage) * land

    weights = np.stack((sand, rock, wadi), axis=-1)
    total = np.maximum(1.0, weights.sum(axis=-1, keepdims=True))
    weights /= total
    dry_plain = np.maximum(0.0, 1.0 - weights.sum(axis=-1, keepdims=True))
    rgba = np.concatenate((weights, dry_plain), axis=-1)
    output = np.clip(np.rint(rgba * 255.0), 0, 255).astype(np.uint8)
    Image.fromarray(output, mode="RGBA").save(OUTPUT, optimize=True)

    land_count = max(1, int(land.sum()))
    strongly_sandy = int((output[..., 0] >= 128).sum()) / land_count * 100.0
    rocky = int((output[..., 1] >= 128).sum()) / land_count * 100.0
    wadi_pixels = int((output[..., 2] >= 64).sum()) / land_count * 100.0
    print(f"Wrote {OUTPUT} ({w}x{h}, {source_w}x{source_h} wadi source)")
    print(f"Land coverage: sand >= 0.5: {strongly_sandy:.2f}%, rock >= 0.5: {rocky:.2f}%, wadi >= 0.25: {wadi_pixels:.2f}%")


if __name__ == "__main__":
    main()
