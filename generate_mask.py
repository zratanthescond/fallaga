
import math

def generate_mask(obj_path, res_x=256, res_z=256):
    vertices = []
    with open(obj_path, 'r') as f:
        for line in f:
            if line.startswith('v '):
                parts = line.split()
                if len(parts) >= 4:
                    vertices.append((float(parts[1]), float(parts[2]), float(parts[3])))
    
    # Map bounds
    min_x, max_x = -1250.0, 1250.0
    min_z, max_z = -2500.0, 2500.0
    
    mask = [0] * (res_x * res_z)
    
    for x, y, z in vertices:
        if y > 0.05: # Land
            # Normalize to [0, 1]
            nx = (x - min_x) / (max_x - min_x)
            nz = (z - min_z) / (max_z - min_z)
            
            ix = int(nx * (res_x - 1))
            iz = int(nz * (res_z - 1))
            
            if 0 <= ix < res_x and 0 <= iz < res_z:
                mask[iz * res_x + ix] = 1
                
    # Pack into bytes for C++
    packed = []
    current_byte = 0
    bits_filled = 0
    
    for bit in mask:
        if bit:
            current_byte |= (1 << bits_filled)
        bits_filled += 1
        if bits_filled == 8:
            packed.append(current_byte)
            current_byte = 0
            bits_filled = 0
            
    if bits_filled > 0:
        packed.append(current_byte)
        
    return packed

if __name__ == "__main__":
    path = "assets/terrain/sousseMap.obj"
    packed_data = generate_mask(path)
    
    with open("terrain_mask_data.txt", "w") as f:
        f.write("unsigned char terrainMask[] = {\n")
        for i, b in enumerate(packed_data):
            f.write(f"0x{b:02x}, ")
            if (i + 1) % 16 == 0:
                f.write("\n")
        f.write("\n};")
    print(f"Generated mask with {len(packed_data)} bytes.")
