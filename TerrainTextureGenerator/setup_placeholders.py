import cv2
import numpy as np
import os

def generate_texture(name, color_bgr):
    size = 256
    # Create base color
    img = np.full((size, size, 3), color_bgr, dtype=np.uint8)
    
    # Add some noise
    noise = np.random.randint(-20, 20, (size, size, 3), dtype=np.int16)
    img = np.clip(img.astype(np.int16) + noise, 0, 255).astype(np.uint8)
    
    # Blur slightly for smoother look
    img = cv2.GaussianBlur(img, (3, 3), 0)
    
    # Add a broader noise pattern for variation
    macro_noise = np.random.randint(-15, 15, (size // 8, size // 8, 3), dtype=np.int16)
    macro_noise = cv2.resize(macro_noise.astype(np.float32), (size, size), interpolation=cv2.INTER_CUBIC)
    img = np.clip(img.astype(np.int16) + macro_noise, 0, 255).astype(np.uint8)

    path = os.path.join("TerrainTextureGenerator", "textures", f"{name}.png")
    cv2.imwrite(path, img)
    print(f"Generated {path}")

if __name__ == "__main__":
    # Colors in BGR format
    textures = {
        "sand": (155, 192, 219),  # Sandy beige #DBC09B
        "rock": (110, 110, 110),  # Gray rock
        "grass": (50, 140, 70),   # Green grass
        "cliff": (80, 80, 90),    # Darker grey/brown cliff
        "dirt": (40, 70, 100)     # Brown dirt #644628
    }
    
    for name, color in textures.items():
        generate_texture(name, color)
