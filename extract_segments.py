
import math

def extract_land_segments(obj_path):
    # Load all vertices
    vertices = []
    with open(obj_path, 'r') as f:
        for line in f:
            if line.startswith('v '):
                parts = line.split()
                if len(parts) >= 4:
                    vertices.append((float(parts[1]), float(parts[2]), float(parts[3])))
    
    # Sort by Z
    z_groups = {}
    for x, y, z in vertices:
        z_rounded = round(z / 10.0) * 10.0
        if z_rounded not in z_groups:
            z_groups[z_rounded] = []
        z_groups[z_rounded].append((x, y))
    
    segments_by_z = {}
    for z in sorted(z_groups.keys()):
        # Sort X vertices for this Z
        pts = sorted(z_groups[z], key=lambda p: p[0])
        
        segments = []
        if not pts: continue
        
        current_land_start = None
        is_currently_land = False
        
        for i in range(len(pts)):
            x, y = pts[i]
            is_land = (y > 0.05)
            
            if is_land and not is_currently_land:
                current_land_start = x
                is_currently_land = True
            elif not is_land and is_currently_land:
                # Land ended
                segments.append((current_land_start, x))
                is_currently_land = False
        
        if is_currently_land:
            segments.append((current_land_start, pts[-1][0]))
            
        if segments:
            segments_by_z[z] = segments
            
    return segments_by_z

if __name__ == "__main__":
    path = "assets/terrain/sousseMap.obj"
    data = extract_land_segments(path)
    with open("precise_segments.txt", "w") as f:
        for z in sorted(data.keys()):
            segs_str = " ".join([f"{s[0]:.1f},{s[1]:.1f}" for s in data[z]])
            f.write(f"{z:.1f} : {segs_str}\n")
    print(f"Extracted segments for {len(data)} Z-slices.")
