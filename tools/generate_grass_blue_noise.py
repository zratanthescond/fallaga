#!/usr/bin/env python3
"""Bake deterministic Poisson-disk candidates for the runtime vegetation field.

Uses SciPy's QMC PoissonDisk sampler. The C++ renderer tiles these candidates,
then applies current terrain, road, habitat, and shared-water masks at runtime.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

import numpy as np
from scipy.stats import qmc


DEFAULT_OUTPUT = Path("assets/terrain/grass_blue_noise.vscatter")
DEFAULT_TILE_SIZE = 64.0
DEFAULT_MIN_SPACING = 0.52
DEFAULT_SEEDS = (3407, 7919, 104729, 130363)


def bake(output: Path, tile_size: float, min_spacing: float,
         seeds: tuple[int, ...]) -> None:
    if tile_size <= 0.0 or min_spacing <= 0.0 or min_spacing >= tile_size:
        raise ValueError("tile size and minimum spacing must be positive, with spacing < tile size")
    if not seeds or len(seeds) > 16:
        raise ValueError("provide between 1 and 16 deterministic seeds")

    output.parent.mkdir(parents=True, exist_ok=True)
    radius = min_spacing / tile_size
    with output.open("wb") as stream:
        stream.write(struct.pack(
            "<4sIIff", b"GBN1", 1, len(seeds), tile_size, min_spacing))
        for seed in seeds:
            samples = qmc.PoissonDisk(d=2, radius=radius, seed=seed).fill_space()
            samples = np.asarray(samples * tile_size, dtype="<f4")
            samples = samples[np.lexsort((samples[:, 0], samples[:, 1]))]
            stream.write(struct.pack("<I", len(samples)))
            samples.tofile(stream)
            print(f"seed={seed}: {len(samples):,} points")
    print(f"wrote {output} ({output.stat().st_size:,} bytes)")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--tile-size", type=float, default=DEFAULT_TILE_SIZE)
    parser.add_argument("--min-spacing", type=float, default=DEFAULT_MIN_SPACING)
    parser.add_argument("--seeds", type=int, nargs="+", default=DEFAULT_SEEDS)
    args = parser.parse_args()
    bake(args.output, args.tile_size, args.min_spacing, tuple(args.seeds))


if __name__ == "__main__":
    main()
