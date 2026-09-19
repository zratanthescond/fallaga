import sys
import os
import zlib
import struct

try:
    raw_path = "assets/terrain/heightmap.raw"
    if not os.path.exists(raw_path):
        print(f"[Python] Raw file {raw_path} not found.")
        sys.exit(0)
        
    print("[Python] Reading raw heightmap data...")
    with open(raw_path, "rb") as f:
        w_data = f.read(4)
        h_data = f.read(4)
        if len(w_data) < 4 or len(h_data) < 4:
            raise ValueError("Malformed raw file headers")
        w = struct.unpack("i", w_data)[0]
        h = struct.unpack("i", h_data)[0]
        
        raw_pixels = f.read()
        expected_bytes = w * h * 2
        if len(raw_pixels) < expected_bytes:
            raise ValueError(f"Incomplete raw data: got {len(raw_pixels)} bytes, expected {expected_bytes}")
            
    print(f"[Python] Generating 16-bit PNG: {w}x{h}...")
    png = bytearray([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A])
    
    def make_chunk(tag, data):
        length = struct.pack('>I', len(data))
        crc = struct.pack('>I', zlib.crc32(tag + data))
        return length + tag + data + crc

    # IHDR chunk: 16-bit depth, color type 0 (grayscale)
    ihdr_data = struct.pack('>IIBBBBB', w, h, 16, 0, 0, 0, 0)
    png += make_chunk(b'IHDR', ihdr_data)
    
    # Process scanlines row by row
    raw_bytes = bytearray()
    row_bytes_len = w * 2
    for y in range(h):
        raw_bytes.append(0) # Filter type 0 (None)
        row_slice = raw_pixels[y * row_bytes_len : (y + 1) * row_bytes_len]
        
        pixel_count = len(row_slice) // 2
        # Unpack from little-endian raw file and pack as big-endian for PNG
        pixels = struct.unpack(f"<{pixel_count}H", row_slice)
        raw_bytes += struct.pack(f">{pixel_count}H", *pixels)
        
    print("[Python] Compressing image data...")
    idat_data = zlib.compress(raw_bytes, level=3) # Level 3 is fast and highly efficient
    png += make_chunk(b'IDAT', idat_data)
    
    # IEND chunk
    png += make_chunk(b'IEND', b'')
    
    png_path = "assets/terrain/heightmap.png"
    print(f"[Python] Writing {png_path}...")
    with open(png_path, "wb") as f:
        f.write(png)
        
    os.remove(raw_path)
    print("[Python] Successfully saved 16-bit heightmap.png (dependency-free).")
    
except Exception as e:
    print("[Python Error]", e)
    sys.exit(1)
