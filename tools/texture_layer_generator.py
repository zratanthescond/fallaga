#!/usr/bin/env python3
"""
Terrain Texture Layer Generator
Converts your terrain PNG layers into the proper format for the enhanced texture system

The textureMap.png should encode texture types using color values:
- Dark gray (32, 32, 32):      Tarred Gravel (Roads)
- Dark gray (64, 64, 64):      Gravel/Concrete
- Mid gray (128, 128, 128):    Rocks
- Light gray (192, 192, 192):  Rubble
- Sandy/Beige (255, 200, 100): Sandy Gravel (Beach)
"""

from PIL import Image
import numpy as np
import sys

def create_texture_layer_mask(output_path="assets/terrain/textureMap.png", width=512, height=512):
    """
    Create a sample texture layer mask image showing the encoding format.
    This is just for reference - replace with your actual layer identification PNG.
    """
    img = Image.new('RGB', (width, height), color='white')
    pixels = img.load()
    
    # Define regions for each texture type
    regions = [
        # (x1, y1, x2, y2, color, name)
        (0, 0, width//5, height, (32, 32, 32), "Roads"),
        (width//5, 0, 2*width//5, height, (64, 64, 64), "Gravel"),
        (2*width//5, 0, 3*width//5, height, (128, 128, 128), "Rocks"),
        (3*width//5, 0, 4*width//5, height, (192, 192, 192), "Rubble"),
        (4*width//5, 0, width, height, (255, 200, 100), "Beach"),
    ]
    
    for x1, y1, x2, y2, color, name in regions:
        for x in range(x1, x2):
            for y in range(y1, y2):
                pixels[x, y] = color
    
    img.save(output_path)
    print(f"Created reference texture layer mask: {output_path}")

def analyze_existing_mask(mask_path="assets/terrain/textureMap.png"):
    """
    Analyze an existing texture mask to see the color distribution
    """
    try:
        img = Image.open(mask_path)
        arr = np.array(img)
        
        print(f"\nTexture Mask Analysis: {mask_path}")
        print(f"Dimensions: {arr.shape}")
        
        if len(arr.shape) == 3:
            # Analyze color distribution
            print("\nColor Statistics:")
            unique_colors = np.unique(arr.reshape(-1, arr.shape[2]), axis=0)
            print(f"Unique colors: {len(unique_colors)}")
            print("Most common colors:")
            
            # Sample some pixels to show distribution
            sample = arr.reshape(-1, arr.shape[2])
            counts = {}
            for color in sample:
                key = tuple(color[:3])
                counts[key] = counts.get(key, 0) + 1
            
            for color, count in sorted(counts.items(), key=lambda x: -x[1])[:10]:
                pct = (count / len(sample)) * 100
                print(f"  RGB{color}: {pct:.1f}%")
        else:
            print("Grayscale image - analyze pixel values")
            print(f"Value range: {arr.min()} to {arr.max()}")
            
    except Exception as e:
        print(f"Error analyzing mask: {e}")

if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "analyze":
        mask_path = sys.argv[2] if len(sys.argv) > 2 else "assets/terrain/textureMap.png"
        analyze_existing_mask(mask_path)
    elif len(sys.argv) > 1 and sys.argv[1] == "create":
        create_texture_layer_mask()
    else:
        print(__doc__)
        print("\nUsage:")
        print("  python script.py create              - Create sample texture layer mask")
        print("  python script.py analyze [path]      - Analyze existing texture mask")
