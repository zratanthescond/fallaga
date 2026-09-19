import urllib.request
import os
import json

base_dir = r"c:\Users\honco\OneDrive\Bureau\fallaga\assets\terrain"
if not os.path.exists(base_dir):
    os.makedirs(base_dir)

def download_file(url, filename):
    filepath = os.path.join(base_dir, filename)
    if os.path.exists(filepath):
        print(f"File {filename} already exists, skipping.")
        return
    print(f"Downloading {url} to {filename}...")
    req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
    try:
        with urllib.request.urlopen(req) as response, open(filepath, 'wb') as out_file:
            data = response.read()
            out_file.write(data)
        print(f"Downloaded {filename}")
    except Exception as e:
        print(f"Failed to download {filename}: {e}")

# Textures to download:
# 1. coast_sand_01
# 2. aerial_rocks_01

# Coast Sand 01 (Diffuse, Normal)
download_file("https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/coast_sand_01/coast_sand_01_diff_2k.jpg", "coast_sand_01_diff_2k.jpg")
download_file("https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/coast_sand_01/coast_sand_01_nor_gl_2k.jpg", "coast_sand_01_nor_gl_2k.jpg")

# Aerial Rocks 01 (Diffuse, Normal)
download_file("https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/aerial_rocks_01/aerial_rocks_01_diff_2k.jpg", "aerial_rocks_01_diff_2k.jpg")
download_file("https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/aerial_rocks_01/aerial_rocks_01_nor_gl_2k.jpg", "aerial_rocks_01_nor_gl_2k.jpg")

print("Done downloading textures.")
