"""
Terrain Texture Generator v2
=============================
Generates TWO 4-channel splatmaps from a heightmap, giving 8 texture layers.
Tuned for a Mediterranean/coastal terrain with high slope variation.

  Splatmap1 (RGBA): Sand | Grass | Rock(cliff) | Gravel(dirt)
  Splatmap2 (RGBA): Rock030 | SandyGravel | Rubble(GrayRocks) | DirtGrass(transition)
"""

import cv2
import numpy as np
import os
import time


def load_heightmap(path):
    print(f"Loading heightmap from {path}...")
    img = cv2.imread(path, cv2.IMREAD_UNCHANGED)
    if img is None:
        raise FileNotFoundError(f"Heightmap not found at {path}")
    if len(img.shape) == 3:
        img = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    if img.dtype == np.uint16:
        hm = img.astype(np.float32) / 65535.0
    else:
        hm = img.astype(np.float32) / 255.0
    return hm


def generate_noise(shape, scale=100.0, octaves=4, seed=None):
    rng = np.random.RandomState(seed)
    noise = np.zeros(shape, dtype=np.float32)
    for i in range(octaves):
        s = max(int(shape[0] / (scale * (2 ** i))), 2)
        small_noise = rng.rand(s, s).astype(np.float32)
        upscaled = cv2.resize(small_noise, (shape[1], shape[0]), interpolation=cv2.INTER_CUBIC)
        noise += upscaled * (0.5 ** i)
    noise = (noise - noise.min()) / (noise.max() - noise.min() + 1e-8)
    return noise


def calculate_slope(heightmap, z_scale=150.0):
    print("Calculating slope...")
    dzdx = cv2.Sobel(heightmap, cv2.CV_32F, 1, 0, ksize=3) / 8.0
    dzdy = cv2.Sobel(heightmap, cv2.CV_32F, 0, 1, ksize=3) / 8.0
    slope_mag = np.sqrt(dzdx ** 2 + dzdy ** 2) * z_scale
    slope_angle = np.arctan(slope_mag)
    slope_norm = slope_angle / (np.pi / 2.0)
    return slope_norm


def compute_ocean_and_beach(heightmap, water_level=0.03):
    print("Computing ocean and beach masks...")
    hm_u8 = (heightmap * 255).astype(np.uint8)
    thresh = int(water_level * 255)

    mask = np.zeros((heightmap.shape[0] + 2, heightmap.shape[1] + 2), np.uint8)
    flags = 4 | (255 << 8) | cv2.FLOODFILL_FIXED_RANGE
    corners = [(0, 0), (heightmap.shape[1] - 1, 0), (0, heightmap.shape[0] - 1),
               (heightmap.shape[1] - 1, heightmap.shape[0] - 1)]
    for cx, cy in corners:
        if hm_u8[cy, cx] <= thresh and mask[cy + 1, cx + 1] == 0:
            cv2.floodFill(hm_u8, mask, (cx, cy), 255,
                          loDiff=thresh, upDiff=thresh, flags=flags)

    ocean_mask = (mask[1:-1, 1:-1] > 0).astype(np.float32)

    kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (25, 25))
    dilated = cv2.dilate(ocean_mask, kernel, iterations=3)
    shoreline = np.clip(dilated - ocean_mask, 0, 1)
    beach_mask = cv2.GaussianBlur(shoreline, (21, 21), 0)

    return ocean_mask, beach_mask


def generate_splatmaps(heightmap, slope, beach_mask, ocean_mask):
    """
    Generate two 4-channel splatmaps for 8 texture layers.
    
    Tuned for this terrain where:
      - median slope ~ 0.50 (very mountainous)
      - ~31% ocean
      - Remaining land needs balanced biome distribution
    
    Splatmap 1 (RGBA):
      R = Sand (beaches)
      G = Grass (flat areas below 50% height)
      B = Cliff Rock (very steep: slope > 0.65)
      A = Gravel/Dirt (moderate slopes 0.3-0.65)
    
    Splatmap 2 (RGBA):
      R = Rock030 (high altitude plateaus)
      G = Sandy Gravel (dry low flat areas)
      B = Gray Rubble Rocks (high steep terrain)
      A = Dirt-Grass Transition (gentle-moderate slopes)
    """
    print("Generating 8-layer splatmaps...")

    h, w = heightmap.shape
    land_mask = 1.0 - ocean_mask

    noise1 = generate_noise((h, w), scale=60.0, octaves=4, seed=42)
    noise2 = generate_noise((h, w), scale=120.0, octaves=3, seed=123)
    noise3 = generate_noise((h, w), scale=30.0, octaves=5, seed=77)

    # Normalized height relative to land only
    land_heights = heightmap[ocean_mask < 0.5]
    h_min = np.percentile(land_heights, 2) if len(land_heights) > 0 else 0.03
    h_max = np.percentile(land_heights, 98) if len(land_heights) > 0 else 1.0
    norm_height = np.clip((heightmap - h_min) / (h_max - h_min + 1e-8), 0, 1)

    # ── Layer 0: Sand (beaches only) ──
    sand = beach_mask.copy()

    # ── Layer 1: Grass (flat to gentle, low-mid altitude) ──
    # slope < 0.35 gets grass; fades out above 55% altitude
    grass = np.clip(1.0 - (slope - 0.20) * 4.0, 0, 1)  # full grass below slope 0.20, zero above 0.45
    grass *= np.clip(1.0 - (norm_height - 0.55) * 4.0, 0, 1)  # fade out at high altitude
    grass *= np.clip((norm_height - 0.01) * 20.0, 0, 1)  # above water line
    grass *= (0.65 + noise1 * 0.35)
    grass *= land_mask

    # ── Layer 2: Cliff Rock (only really steep faces) ──
    cliff_rock = np.clip((slope - 0.65) * 4.0, 0, 1)  # only very steep cliffs
    cliff_rock *= (0.7 + noise2 * 0.3)
    cliff_rock *= land_mask

    # ── Layer 3: Gravel/Dirt (moderate slopes, between grass and cliff) ──
    gravel = np.clip((slope - 0.30) * 3.0, 0, 1) * np.clip(1.0 - (slope - 0.65) * 4.0, 0, 1)
    gravel *= (0.7 + noise2 * 0.3)
    gravel *= land_mask

    # ── Layer 4: Rock030 (high altitude flat rocky ground) ──
    rock030 = np.clip((norm_height - 0.65) * 4.0, 0, 1)
    rock030 *= np.clip(1.0 - (slope - 0.50) * 3.0, 0, 1)  # flatter high areas
    rock030 *= (0.5 + noise3 * 0.5)
    rock030 *= land_mask

    # ── Layer 5: Sandy Gravel (dry low flat areas, desert-like) ──
    sandy_gravel = np.clip(1.0 - norm_height * 3.0, 0, 1)  # low altitude
    sandy_gravel *= np.clip(1.0 - slope * 2.5, 0, 1)  # flat
    sandy_gravel *= np.clip(noise1 * 1.8 - 0.5, 0, 1)  # patchy, not everywhere
    sandy_gravel *= np.clip(1.0 - beach_mask * 3.0, 0, 1)
    sandy_gravel *= land_mask

    # ── Layer 6: Gray Rubble Rocks (mountain tops and exposed high-slope areas) ──
    rubble = np.clip((norm_height - 0.75) * 5.0, 0, 1)
    rubble += np.clip((slope - 0.45) * 2.0, 0, 1) * np.clip((norm_height - 0.45) * 3.0, 0, 1) * 0.5
    rubble = np.clip(rubble, 0, 1)
    rubble *= (0.6 + noise2 * 0.4)
    rubble *= land_mask

    # ── Layer 7: Dirt-Grass Transition (gentle-to-moderate slopes) ──
    dirt_grass = np.clip((slope - 0.10) * 5.0, 0, 1) * np.clip(1.0 - (slope - 0.35) * 5.0, 0, 1)
    dirt_grass *= np.clip(norm_height * 4.0, 0, 1)  # not near water
    dirt_grass *= (0.5 + noise3 * 0.5)
    dirt_grass *= land_mask

    # ── Normalize: all 8 layers sum to 1.0 per pixel ──
    all_layers = np.stack([sand, grass, cliff_rock, gravel,
                           rock030, sandy_gravel, rubble, dirt_grass], axis=-1)

    total = np.sum(all_layers, axis=-1, keepdims=True)
    total = np.maximum(total, 1e-5)
    all_layers = all_layers / total

    # Zero out ocean pixels
    all_layers *= land_mask[..., np.newaxis]

    splat1 = all_layers[:, :, 0:4]
    splat2 = all_layers[:, :, 4:8]

    return splat1, splat2, all_layers


def save_splatmap(path, splatmap):
    """Save float32 RGBA splatmap as PNG. Channels stored as RGBA in memory,
    but OpenCV writes BGRA so we swap R<->B."""
    splat_u8 = (np.clip(splatmap, 0, 1) * 255).astype(np.uint8)
    bgra = np.zeros_like(splat_u8)
    bgra[:, :, 0] = splat_u8[:, :, 2]  # B <- channel 2
    bgra[:, :, 1] = splat_u8[:, :, 1]  # G <- channel 1
    bgra[:, :, 2] = splat_u8[:, :, 0]  # R <- channel 0
    bgra[:, :, 3] = splat_u8[:, :, 3]  # A <- channel 3
    cv2.imwrite(path, bgra)
    print(f"Saved {path}")


def save_grayscale(path, img):
    cv2.imwrite(path, (np.clip(img, 0, 1) * 255).astype(np.uint8))
    print(f"Saved {path}")


def main():
    start_time = time.time()
    input_dir = "input"
    output_dir = "output"
    os.makedirs(output_dir, exist_ok=True)

    hm = load_heightmap(os.path.join(input_dir, "heightmap.png"))
    print(f"  Heightmap size: {hm.shape}")

    slope = calculate_slope(hm)
    save_grayscale(os.path.join(output_dir, "slope.png"), slope)

    ocean_mask, beach_mask = compute_ocean_and_beach(hm, water_level=0.03)
    save_grayscale(os.path.join(output_dir, "ocean_mask.png"), ocean_mask)
    save_grayscale(os.path.join(output_dir, "beach_mask.png"), beach_mask)

    splat1, splat2, all_layers = generate_splatmaps(hm, slope, beach_mask, ocean_mask)

    save_splatmap(os.path.join(output_dir, "splatmap1.png"), splat1)
    save_splatmap(os.path.join(output_dir, "splatmap2.png"), splat2)

    # Debug masks
    layer_names = ["sand", "red_laterite_soil", "cliff_rock", "gravel",
                   "rock030", "sandy_gravel", "rubble", "dirt_grass_transition"]
    for i, name in enumerate(layer_names):
        save_grayscale(os.path.join(output_dir, f"mask_{name}.png"), all_layers[:, :, i])

    # Coverage stats
    print("\n-- Coverage Statistics --")
    land_pixels = np.sum(ocean_mask < 0.5)
    for i, name in enumerate(layer_names):
        layer = all_layers[:, :, i]
        dominant = np.sum((layer > 0.3) & (ocean_mask < 0.5))
        pct = (dominant / land_pixels * 100) if land_pixels > 0 else 0
        mean_w = np.mean(layer[ocean_mask < 0.5]) * 100 if land_pixels > 0 else 0
        print(f"  {name:25s}: {pct:5.1f}% dominant | {mean_w:5.1f}% mean weight")

    elapsed = time.time() - start_time
    print(f"\nTerrain generation complete in {elapsed:.2f} seconds.")


if __name__ == "__main__":
    main()
