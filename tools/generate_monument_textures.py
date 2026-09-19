import numpy as np
from PIL import Image, ImageDraw, ImageFilter
import os

os.makedirs("assets/moniment/textures", exist_ok=True)

# ---------------------------------------------------------
# 1. DATE PALM TRUNK BARK (1024x1024)
# Authentic diamond-lozenge overlapping leaf-base scars of Phoenix dactylifera
# ---------------------------------------------------------
def create_palm_bark():
    w, h = 1024, 1024
    base_col = np.array([125, 95, 70], dtype=np.float32)
    y_grid, x_grid = np.mgrid[0:h, 0:w].astype(np.float32)

    period_y = 64.0
    period_x = 128.0

    row = np.floor(y_grid / period_y)
    offset_x = (row % 2) * (period_x * 0.5)
    u = ((x_grid + offset_x) % period_x) / period_x - 0.5
    v = (y_grid % period_y) / period_y - 0.5

    dist = np.abs(u) * 1.6 + np.abs(v) * 2.0
    relief = np.clip(1.0 - dist, 0.0, 1.0)
    shadow = np.clip(-v * 2.5, 0.0, 1.0) * relief
    highlight = np.clip(v * 2.0, 0.0, 1.0) * relief

    fibers = (np.sin(x_grid * 0.8) * np.cos(y_grid * 0.05) + np.sin(x_grid * 1.7)) * 0.15

    r = np.clip(base_col[0] * (0.65 + relief * 0.45 - shadow * 0.40 + highlight * 0.30 + fibers), 20, 240)
    g = np.clip(base_col[1] * (0.65 + relief * 0.45 - shadow * 0.40 + highlight * 0.30 + fibers * 0.8), 15, 210)
    b = np.clip(base_col[2] * (0.65 + relief * 0.45 - shadow * 0.40 + highlight * 0.25 + fibers * 0.6), 10, 180)

    rgb = np.stack([r, g, b], axis=-1).astype(np.uint8)
    img = Image.fromarray(rgb, mode='RGB').filter(ImageFilter.SMOOTH_MORE)
    img.save("assets/moniment/textures/palm_bark.png")
    print("Saved assets/moniment/textures/palm_bark.png")

# ---------------------------------------------------------
# 2. DATE PALM FEATHER FROND (1024x2048 RGBA)
# ---------------------------------------------------------
def create_palm_frond():
    w, h = 1024, 2048
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    cx = w // 2

    num_leaflets = 75
    y_start = int(h * 0.12)
    y_end = int(h * 0.96)

    leaf_cols = [
        (68, 108, 48, 255),
        (78, 122, 54, 255),
        (58, 96, 42, 255),
        (88, 134, 60, 255),
        (98, 142, 68, 255),
    ]

    for i in range(num_leaflets):
        t = i / float(num_leaflets - 1)
        y = y_start + int(t * (y_end - y_start))
        profile = np.sin(t * np.pi) ** 0.65
        max_len = 360 * profile
        angle_deg = 35 + (1.0 - t) * 35
        rad = np.radians(angle_deg)
        dx = max_len * np.cos(rad)
        dy = max_len * np.sin(rad)
        col = leaf_cols[i % len(leaf_cols)]
        width = int(5 + 7 * (1.0 - t * 0.5))

        p0_l = (cx - 6, y)
        p1_l = (cx - int(dx), y - int(dy))
        draw.line([p0_l, p1_l], fill=col, width=width)

        p0_r = (cx + 6, y)
        p1_r = (cx + int(dx), y - int(dy))
        draw.line([p0_r, p1_r], fill=col, width=width)

    for y in range(y_start, h):
        t = (y - y_start) / float(h - y_start)
        stem_w = int(14 * (1.0 - t * 0.7))
        col_stem = (int(130 + 30 * (1.0 - t)), int(140 + 20 * (1.0 - t)), int(60 + 20 * (1.0 - t)), 255)
        draw.line([(cx - stem_w // 2, y), (cx + stem_w // 2, y)], fill=col_stem, width=1)

    img.save("assets/moniment/textures/palm_frond.png")
    print("Saved assets/moniment/textures/palm_frond.png")

# ---------------------------------------------------------
# 3. KIOSK ZINC / COPPER ROOF (1024x1024)
# ---------------------------------------------------------
def create_kiosk_roof():
    w, h = 1024, 1024
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    base_r, base_g, base_b = 95.0, 125.0, 120.0
    cx, cy = w / 2.0, h / 2.0
    angle = np.arctan2(y - cy, x - cx)
    panel_angle = (angle + np.pi) % (np.pi / 4.0)
    seam = np.exp(-((panel_angle - np.pi / 8.0) ** 2) / 0.001)

    noise1 = np.sin(x * 0.03 + y * 0.02) * np.cos(x * 0.015 - y * 0.025)
    noise2 = np.sin(x * 0.08) * np.sin(y * 0.08)
    patina = noise1 * 0.15 + noise2 * 0.10

    r = np.clip(base_r * (1.0 + patina - seam * 0.35), 20, 240)
    g = np.clip(base_g * (1.0 + patina * 1.2 - seam * 0.30), 20, 240)
    b = np.clip(base_b * (1.0 + patina * 1.1 - seam * 0.25), 20, 240)

    rgb = np.stack([r, g, b], axis=-1).astype(np.uint8)
    Image.fromarray(rgb, mode='RGB').save("assets/moniment/textures/kiosk_roof.png")
    print("Saved assets/moniment/textures/kiosk_roof.png")

# ---------------------------------------------------------
# 4. WROUGHT IRON (512x512)
# ---------------------------------------------------------
def create_wrought_iron():
    w, h = 512, 512
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    grain = (np.sin(x * 0.2) + np.cos(y * 0.2)) * 0.05
    val = np.clip(42.0 + grain * 20.0, 20.0, 65.0).astype(np.uint8)
    val_b = np.clip(val.astype(np.float32) * 1.05, 0, 255).astype(np.uint8)
    rgb = np.stack([val, val, val_b], axis=-1)
    Image.fromarray(rgb, mode='RGB').save("assets/moniment/textures/wrought_iron.png")
    print("Saved assets/moniment/textures/wrought_iron.png")

# ---------------------------------------------------------
# 5. PEACH / TERRACOTTA PLASTER (1024x1024)
# Warm Mediterranean lime wash (Building 2: Annexe Douane)
# ---------------------------------------------------------
def create_peach_plaster():
    w, h = 1024, 1024
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    base_r, base_g, base_b = 215.0, 168.0, 142.0
    mottle1 = np.sin(x * 0.02 + y * 0.015) * 0.08
    mottle2 = np.cos(x * 0.05 - y * 0.04) * 0.05
    grit = (np.sin(x * 0.4) * np.sin(y * 0.4)) * 0.03
    factor = 1.0 + mottle1 + mottle2 + grit

    r = np.clip(base_r * factor, 120, 255)
    g = np.clip(base_g * factor, 90, 235)
    b = np.clip(base_b * factor, 75, 210)
    rgb = np.stack([r, g, b], axis=-1).astype(np.uint8)
    Image.fromarray(rgb, mode='RGB').filter(ImageFilter.SMOOTH).save("assets/moniment/textures/peach_plaster.png")
    print("Saved assets/moniment/textures/peach_plaster.png")

# ---------------------------------------------------------
# 6. STRIPED AWNING CANVAS (512x512)
# Alternating deep crimson and warm cream vintage stripes
# ---------------------------------------------------------
def create_striped_awning():
    w, h = 512, 512
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    stripe_w = 32.0
    stripe_phase = (x % (stripe_w * 2.0)) / (stripe_w * 2.0)
    is_red = stripe_phase < 0.5

    red_col = np.array([178, 42, 38], dtype=np.float32)
    cream_col = np.array([238, 226, 205], dtype=np.float32)

    canvas_weave = (np.sin(x * 0.8) * np.cos(y * 0.8)) * 0.06
    sag_shadow = np.sin(y / h * np.pi) * 0.15 # 3D scalloped shade

    r = np.where(is_red, red_col[0] * (1.0 + canvas_weave - sag_shadow), cream_col[0] * (1.0 + canvas_weave - sag_shadow))
    g = np.where(is_red, red_col[1] * (1.0 + canvas_weave - sag_shadow), cream_col[1] * (1.0 + canvas_weave - sag_shadow))
    b = np.where(is_red, red_col[2] * (1.0 + canvas_weave - sag_shadow), cream_col[2] * (1.0 + canvas_weave - sag_shadow))

    rgb = np.stack([np.clip(r, 0, 255), np.clip(g, 0, 255), np.clip(b, 0, 255)], axis=-1).astype(np.uint8)
    Image.fromarray(rgb, mode='RGB').save("assets/moniment/textures/awning_striped.png")
    print("Saved assets/moniment/textures/awning_striped.png")

# ---------------------------------------------------------
# 7. BUFF ASHLAR STONE (1024x1024)
# Dressed Sahel limestone masonry blocks (Cercle Militaire & Curbing)
# ---------------------------------------------------------
def create_buff_stone():
    w, h = 1024, 1024
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    block_h = 64.0
    block_w = 128.0

    row = np.floor(y / block_h)
    shift_x = (row % 2) * (block_w * 0.5)
    in_block_x = (x + shift_x) % block_w
    in_block_y = y % block_h

    # Mortar joint lines (4px)
    is_mortar = (in_block_x < 4.0) | (in_block_y < 4.0)

    base_r, base_g, base_b = 210.0, 195.0, 168.0
    stone_grain = (np.sin(x * 0.15) * np.cos(y * 0.15) + np.sin(x * 0.03)) * 0.08

    r = np.where(is_mortar, 140.0, base_r * (1.0 + stone_grain))
    g = np.where(is_mortar, 135.0, base_g * (1.0 + stone_grain))
    b = np.where(is_mortar, 125.0, base_b * (1.0 + stone_grain))

    rgb = np.stack([np.clip(r, 0, 255), np.clip(g, 0, 255), np.clip(b, 0, 255)], axis=-1).astype(np.uint8)
    Image.fromarray(rgb, mode='RGB').save("assets/moniment/textures/buff_stone.png")
    print("Saved assets/moniment/textures/buff_stone.png")

if __name__ == "__main__":
    create_palm_bark()
    create_palm_frond()
    create_kiosk_roof()
    create_wrought_iron()
    create_peach_plaster()
    create_striped_awning()
    create_buff_stone()
    print("All waterfront textures generated successfully!")
