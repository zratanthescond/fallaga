from PIL import Image
import numpy as np

img = Image.open("assets/terrain/textureMap.png").convert("L")
arr = np.array(img)
y, x = np.where(arr > 0)
if len(x) > 0:
    print(f"Port range X: {x.min()}..{x.max()}, Z: {y.min()}..{y.max()} (Pixels 0-511)")
    # Map to world: -1250, 2500 (North is row 0)
    # Z world = 2500 - (y/511 * 5000)
    # X world = -1250 + (x/511 * 2500)
    print(f"Center World X: {-1250 + (x.mean()/511 * 2500):.1f}, Z: {2500 - (y.mean()/511 * 5000):.1f}")
else:
    print("Empty mask.")
