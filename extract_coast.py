
import math

def extract_coastline(obj_path):
    points = []
    with open(obj_path, 'r') as f:
        for line in f:
            if line.startswith('v '):
                parts = line.split()
                if len(parts) >= 4:
                    x = float(parts[1])
                    y = float(parts[2])
                    z = float(parts[3])
                    if y > 0.05: # Land
                        points.append((x, z))
    
    # Group by Z slices
    z_slices = {}
    for x, z in points:
        z_rounded = round(z / 5.0) * 5.0 # 5 meter precision
        if z_rounded not in z_slices:
            z_slices[z_rounded] = []
        z_slices[z_rounded].append(x)
    
    # For each slice, the coastline is the maximum X (Eastern shore)
    coastline = []
    for z in sorted(z_slices.keys()):
        max_x = max(z_slices[z])
        coastline.append((max_x, z))
        
    return coastline

if __name__ == "__main__":
    path = "assets/terrain/sousseMap.obj"
    coast = extract_coastline(path)
    with open("precise_coast.txt", "w") as f:
        for x, z in coast:
            f.write(f"{x:.2f}f, {z:.2f}f,\n")
    print(f"Extracted {len(coast)} points.")
