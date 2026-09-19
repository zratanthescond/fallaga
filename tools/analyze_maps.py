import os
from PIL import Image

paths = [
    "C:/Users/honco/.gemini/antigravity/brain/4818eef2-736b-4c3e-a2e9-c593fcae1abd/media__1774857401150.png",
    "C:/Users/honco/.gemini/antigravity/brain/4818eef2-736b-4c3e-a2e9-c593fcae1abd/media__1774863252979.jpg",
    "C:/Users/honco/.gemini/antigravity/brain/4818eef2-736b-4c3e-a2e9-c593fcae1abd/media__1774871582988.jpg"
]

for p in paths:
    if os.path.exists(p):
        i = Image.open(p)
        print("IMG:", os.path.basename(p), "Size:", i.size, "Mode:", i.mode)
        # Check if it has a lot of grayscale (white and black)
        i.thumbnail((32, 32))
        colors = i.getcolors(1024)
        if i.mode == 'RGB':
            bw_count = sum(count for count, (r, g, b) in (colors or []) if abs(r-g) < 10 and abs(g-b) < 10)
            total = sum(count for count, _ in (colors or []))
            print("   BW ratio:", bw_count / max(total, 1))
    else:
        print("Missing:", p)
