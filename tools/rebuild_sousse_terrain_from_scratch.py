#!/usr/bin/env python3
"""Build a clean, deterministic terrain for the Sousse 1899 scene.

The terrain supplies landform only. Roads are rendered by RoadSystem from
vector data, so this generator deliberately creates no painted road pixels.
"""

from pathlib import Path

import cv2
import numpy as np


ROOT = Path(__file__).resolve().parents[1]
TERRAIN_DIR = ROOT / "assets" / "terrain"

SIZE = 8129
MASK_SIZE = 2048
WORLD_MIN = -13938.0
WORLD_MAX = 13938.0
MIN_HEIGHT = -70.0
MAX_HEIGHT = 87.8
WATER_LEVEL = -48.5


def smoothstep(edge0, edge1, value):
    t = np.clip((value - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def soft_box(x, z, min_x, max_x, min_z, max_z, feather):
    return (
        smoothstep(min_x - feather, min_x + feather, x)
        * (1.0 - smoothstep(max_x - feather, max_x + feather, x))
        * smoothstep(min_z - feather, min_z + feather, z)
        * (1.0 - smoothstep(max_z - feather, max_z + feather, z))
    )


def normalized_height(height):
    return np.clip(
        np.rint((height - MIN_HEIGHT) / (MAX_HEIGHT - MIN_HEIGHT) * 65535.0),
        0,
        65535,
    ).astype(np.uint16)


def build_heightmap():
    print(f"[terrain] Building {SIZE}x{SIZE} 16-bit heightmap")
    x = np.linspace(WORLD_MIN, WORLD_MAX, SIZE, dtype=np.float32)
    heightmap = np.empty((SIZE, SIZE), dtype=np.uint16)

    for row in range(SIZE):
        z = WORLD_MIN + (WORLD_MAX - WORLD_MIN) * row / (SIZE - 1)

        # Dry inland terrain rises gradually west of the coast with broad,
        # low-amplitude Mediterranean undulations. There is no raster road work.
        inland = smoothstep(4300.0, -10500.0, x)
        height = -47.0 + 45.0 * inland
        height += 2.3 * np.sin(x * 0.00072 + z * 0.00021)
        height += 1.5 * np.sin(x * 0.00163 - z * 0.00049)
        height += 0.8 * np.cos(x * 0.00310 + z * 0.00187)

        # Historical Sousse: a gentle Medina/Kasbah rise west of the port.
        medina = np.exp(-(((x - 3720.0) / 830.0) ** 2 + ((z + 540.0) / 980.0) ** 2))
        kasbah = np.exp(-(((x - 3485.0) / 310.0) ** 2 + ((z + 1320.0) / 350.0) ** 2))
        height += 13.0 * medina + 8.0 * kasbah

        # The historical waterfront is a stable, nearly flat terrace. The soft
        # blend prevents a wall or broad visual scar at its inland edge.
        terrace = soft_box(x, z, 3980.0, 4380.0, -1450.0, 640.0, 180.0)
        height = height * (1.0 - terrace) + (-46.3) * terrace

        # The port is water, enclosed by the raised quay terrace rather than a
        # noisy painted mask. It sits below the runtime sea level.
        basin = soft_box(x, z, 4280.0, 4880.0, -430.0, 680.0, 75.0)
        height = height * (1.0 - basin) + (-53.0) * basin

        # Open Mediterranean east of Sousse, with a quiet curved shoreline.
        coast_x = 4490.0 + 135.0 * np.sin(z * 0.00058) + 55.0 * np.sin(z * 0.0021)
        sea = smoothstep(coast_x - 60.0, coast_x + 90.0, x)
        height = height * (1.0 - sea) + (-53.0) * sea

        # Preserve the basin where the port meets the sea.
        height = height * (1.0 - basin) + (-53.0) * basin
        heightmap[row] = normalized_height(height)

        if row % 1024 == 0:
            print(f"[terrain] {row}/{SIZE} rows")

    path = TERRAIN_DIR / "heightmap.png"
    if not cv2.imwrite(str(path), heightmap):
        raise RuntimeError(f"Could not write {path}")


def to_mask_pixel(world_x, world_z):
    u = (world_x - WORLD_MIN) / (WORLD_MAX - WORLD_MIN)
    v = (world_z - WORLD_MIN) / (WORLD_MAX - WORLD_MIN)
    return int(round(u * (MASK_SIZE - 1))), int(round(v * (MASK_SIZE - 1)))


def build_material_masks():
    # roadMap is intentionally blank: road geometry owns the road appearance.
    # This prevents the old raster road network from reappearing underneath it.
    road_map = np.zeros((MASK_SIZE, MASK_SIZE, 3), dtype=np.uint8)
    cv2.imwrite(str(TERRAIN_DIR / "roadMap.png"), road_map)

    # Red is quay/esplanade, green breakwater, blue riprap. This compact mask is
    # only for harbor materials and matches the new terrain footprint.
    texture_map = np.zeros((MASK_SIZE, MASK_SIZE, 3), dtype=np.uint8)
    quay = np.array([
        to_mask_pixel(3980.0, -1450.0),
        to_mask_pixel(4380.0, -1450.0),
        to_mask_pixel(4380.0, 640.0),
        to_mask_pixel(3980.0, 640.0),
    ], dtype=np.int32)
    cv2.fillPoly(texture_map, [quay], (0, 0, 255))

    north_mole = np.array([
        to_mask_pixel(4380.0, 600.0),
        to_mask_pixel(5200.0, 820.0),
        to_mask_pixel(5240.0, 750.0),
        to_mask_pixel(4420.0, 520.0),
    ], dtype=np.int32)
    south_mole = np.array([
        to_mask_pixel(4360.0, -430.0),
        to_mask_pixel(5080.0, -920.0),
        to_mask_pixel(5140.0, -850.0),
        to_mask_pixel(4410.0, -350.0),
    ], dtype=np.int32)
    cv2.fillPoly(texture_map, [north_mole, south_mole], (0, 255, 0))

    cv2.imwrite(str(TERRAIN_DIR / "textureMap.png"), texture_map)


def write_metadata():
    (TERRAIN_DIR / "heightmap_meta.txt").write_text(
        "size=8129x8129\n"
        f"worldX={WORLD_MIN}..{WORLD_MAX}\n"
        f"worldZ={WORLD_MIN}..{WORLD_MAX}\n"
        f"minHeight={MIN_HEIGHT}m\n"
        f"maxHeight={MAX_HEIGHT}m\n"
        "source=Deterministic clean Sousse terrain rebuild; vector roads only\n",
        encoding="utf-8",
    )


def main():
    TERRAIN_DIR.mkdir(parents=True, exist_ok=True)
    build_heightmap()
    build_material_masks()
    write_metadata()
    print("[terrain] Complete: clean terrain, clean harbor mask, no raster roads")


if __name__ == "__main__":
    main()
