"""common.py

Shared image / json / hashing helpers. Deliberately free of heavy deps beyond
numpy, scipy and Pillow (no scikit-image: it is not installed in this project).
"""

from __future__ import annotations

import hashlib
import json
import os
from datetime import datetime

import numpy as np
from PIL import Image

Image.MAX_IMAGE_PIXELS = None


# --------------------------------------------------------------------------- #
# IO
# --------------------------------------------------------------------------- #
def ensure_dir(path):
    os.makedirs(path, exist_ok=True)
    return path


def load_gray(path):
    return np.array(Image.open(path).convert("L"))


def load_road_mask_2017(path=None):
    """Resample the registered 4096 road-surface canvas onto the terrain grid.

    The supplied canvas already contains the registered terrain-space geometry;
    this only converts its pixel coverage to the native 2017 height grid.
    """
    from . import contract

    path = path or contract.ROAD_SURFACE_MASK
    image = Image.open(path).convert("L")
    expected = (contract.ROAD_MASK_CANVAS, contract.ROAD_MASK_CANVAS)
    if image.size != expected:
        raise ValueError(f"road mask must be {expected[0]}x{expected[1]}, got {image.size}")
    resized = image.resize((contract.GRID, contract.GRID), Image.Resampling.BOX)
    return np.asarray(resized) > 127


def load_rgb(path):
    return np.array(Image.open(path).convert("RGB"))


def load_rgba(path):
    return np.array(Image.open(path).convert("RGBA"))


def save_gray(arr, path):
    Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "L").save(path)


def save_rgb(arr, path):
    Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGB").save(path)


def save_rgba(arr, path):
    Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA").save(path)


def save_u16(arr, path):
    Image.fromarray(np.clip(arr, 0, 65535).astype(np.uint16)).save(path)


def save_json(obj, path):
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(obj, fh, indent=2, default=_json_default)
    return path


def load_json(path):
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def _json_default(o):
    if isinstance(o, (np.integer,)):
        return int(o)
    if isinstance(o, (np.floating,)):
        return float(o)
    if isinstance(o, np.ndarray):
        return o.tolist()
    if isinstance(o, (set, tuple)):
        return list(o)
    raise TypeError(f"not JSON serialisable: {type(o)}")


def read_r16(path, size):
    raw = np.fromfile(path, dtype="<u2")
    if raw.size != size * size:
        raise ValueError(
            f"{path}: expected {size*size} u16 samples, got {raw.size}")
    return raw.reshape(size, size)


# --------------------------------------------------------------------------- #
# Hashing / content addressing
# --------------------------------------------------------------------------- #
def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def content_key(*parts):
    h = hashlib.sha256()
    for p in parts:
        h.update(repr(p).encode("utf-8"))
        h.update(b"\x00")
    return h.hexdigest()[:16]


def file_size(path):
    return os.path.getsize(path) if os.path.exists(path) else -1


def make_run_id():
    return datetime.now().strftime("%Y%m%d-%H%M%S")


# --------------------------------------------------------------------------- #
# Deterministic scatter seed from global pixel coords (plan section 8)
# --------------------------------------------------------------------------- #
def coord_seed(x_cm, y_cm, global_seed):
    """Stable per-point seed from *global* (never tile-local) coordinates."""
    h = hashlib.sha256(f"{int(round(x_cm))}:{int(round(y_cm))}:{global_seed}".encode())
    return int.from_bytes(h.digest()[:4], "little")


# --------------------------------------------------------------------------- #
# Morphology helpers (numpy / scipy only)
# --------------------------------------------------------------------------- #
def skeletonize(mask, max_iter=200):
    """Vectorised Zhang-Suen thinning. mask: 2-D bool/0-1 array."""
    img = (np.asarray(mask) > 0).astype(np.uint8)
    changed = True
    it = 0
    while changed and it < max_iter:
        changed = False
        for step in (0, 1):
            p = np.pad(img, 1)
            p2 = p[:-2, 1:-1]
            p3 = p[:-2, 2:]
            p4 = p[1:-1, 2:]
            p5 = p[2:, 2:]
            p6 = p[2:, 1:-1]
            p7 = p[2:, :-2]
            p8 = p[1:-1, :-2]
            p9 = p[:-2, :-2]
            b = p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9
            a = ((p2 == 0) & (p3 == 1)).astype(np.uint8)
            a += ((p3 == 0) & (p4 == 1))
            a += ((p4 == 0) & (p5 == 1))
            a += ((p5 == 0) & (p6 == 1))
            a += ((p6 == 0) & (p7 == 1))
            a += ((p7 == 0) & (p8 == 1))
            a += ((p8 == 0) & (p9 == 1))
            a += ((p9 == 0) & (p2 == 1))
            if step == 0:
                cond = (p2 * p4 * p6 == 0) & (p4 * p6 * p8 == 0)
            else:
                cond = (p2 * p4 * p8 == 0) & (p2 * p6 * p8 == 0)
            remove = (img == 1) & (b >= 2) & (b <= 6) & (a == 1) & cond
            if remove.any():
                img[remove] = 0
                changed = True
        it += 1
    return img.astype(bool)
