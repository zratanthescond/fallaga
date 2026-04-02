import os
from PIL import Image

src_path = "C:/Users/honco/.gemini/antigravity/brain/4818eef2-736b-4c3e-a2e9-c593fcae1abd/media__1774871582988.jpg"
out_path = "C:/Users/honco/OneDrive/Bureau/fallaga/assets/terrain/heightmap.png"

print("Loading user custom terrain map...")
img = Image.open(src_path)

# Convert to grayscale
bw = img.convert("L")

# Resize to standard heightmap dimensions for our engine (512x512)
bw = bw.resize((512, 512), Image.Resampling.LANCZOS)

# Save overriding our generated map!
bw.save(out_path)
print("Successfully generated new terrain from user's black and white map!")
