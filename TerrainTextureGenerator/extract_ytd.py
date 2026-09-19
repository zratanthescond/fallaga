import os
import struct
import zlib
import texture2ddecoder
import numpy as np
from PIL import Image

def get_sys_size(sys_flags, gfx_flags, decomp_len):
    gfx_size = gfx_flags & 0xFFFF0000
    if gfx_size > 0 and gfx_size < decomp_len:
        return decomp_len - gfx_size
    sys_size = sys_flags & 0xFFFF
    return min(sys_size, decomp_len)

def extract_ytd_file(ytd_path, output_dir, is_hi_file):
    with open(ytd_path, 'rb') as f:
        data = f.read()

    if not data.startswith(b'RSC8'):
        return 0

    magic, version, sys_flags, gfx_flags = struct.unpack('<IIII', data[:16])
    try:
        decomp = zlib.decompress(data[16:], -zlib.MAX_WBITS)
    except Exception:
        return 0

    sys_size = get_sys_size(sys_flags, gfx_flags, len(decomp))
    sys_data = decomp[:sys_size]
    gfx_data = decomp[sys_size:]

    if len(sys_data) < 0x40:
        return 0

    count = struct.unpack_from('<H', sys_data, 0x0028)[0]
    if count == 0 or count > 500:
        count = struct.unpack_from('<H', sys_data, 0x0020)[0]

    extracted_count = 0
    ytd_name = os.path.splitext(os.path.basename(ytd_path))[0]

    for i in range(count):
        ptr_offset = 0x0040 + i * 8
        if ptr_offset + 8 > len(sys_data):
            break
        ptr = struct.unpack_from('<Q', sys_data, ptr_offset)[0]
        off = ptr & 0xFFFF

        if off + 0x40 > len(sys_data):
            continue

        w = struct.unpack_from('<H', sys_data, off + 0x18)[0]
        h = struct.unpack_from('<H', sys_data, off + 0x1a)[0]
        fmt = struct.unpack_from('<I', sys_data, off + 0x1c)[0]

        if w == 0 or h == 0 or w > 8192 or h > 8192:
            continue

        name_ptr = struct.unpack_from('<Q', sys_data, off + 0x28)[0]
        name_off = name_ptr & 0xFFFF
        
        name = ""
        if name_off < len(sys_data):
            name_end = sys_data.find(b'\x00', name_off)
            if name_end != -1:
                name = sys_data[name_off:name_end].decode('ascii', errors='ignore')

        if not name or len(name) < 2:
            name = f"{ytd_name}_tex_{i+1:02d}"

        gfx_ptr = struct.unpack_from('<Q', sys_data, off + 0x38)[0]
        gfx_off = gfx_ptr & 0xFFFFFF

        is_normal_map = ('_nm' in name.lower()) or ('_nor' in name.lower())
        is_albedo = ('_abte' in name.lower()) or ('_diff' in name.lower()) or ('_color' in name.lower())

        if is_hi_file and is_albedo:
            png_check = os.path.join(output_dir, f"{name}.png")
            if os.path.exists(png_check):
                continue

        is_bc1 = (fmt in (0x47010001, 0x48010001)) and not is_normal_map
        block_size = 8 if is_bc1 else 16
        blocks_x = (w + 3) // 4
        blocks_y = (h + 3) // 4
        needed_bytes = blocks_x * blocks_y * block_size

        if gfx_off + needed_bytes > len(gfx_data):
            continue

        pix_data = gfx_data[gfx_off : gfx_off + needed_bytes]
        png_path = os.path.join(output_dir, f"{name}.png")

        try:
            if is_normal_map:
                # Normal maps: BC5 format -> R = X normal, G = Y normal, B = Z normal
                raw_rgba = texture2ddecoder.decode_bc5(pix_data, w, h)
                arr = np.frombuffer(raw_rgba, dtype=np.uint8).reshape((h, w, 4))
                
                # decode_bc5 outputs R=X, G=Y, B=0, A=255
                x_raw = arr[:, :, 2].astype(np.float32)  # R channel = X normal
                y_raw = arr[:, :, 1].astype(np.float32)  # G channel = Y normal
                
                nx = (x_raw - 128.0) / 127.0
                ny = (y_raw - 128.0) / 127.0
                nz = np.sqrt(np.maximum(0.0, 1.0 - (nx * nx + ny * ny)))
                
                norm_img = np.zeros((h, w, 4), dtype=np.uint8)
                norm_img[:, :, 0] = arr[:, :, 2]  # Red = X normal
                norm_img[:, :, 1] = arr[:, :, 1]  # Green = Y normal
                norm_img[:, :, 2] = np.clip(nz * 127.0 + 128.0, 0, 255).astype(np.uint8)  # Blue = Z normal
                norm_img[:, :, 3] = 255
                
                img = Image.fromarray(norm_img, 'RGBA')
                img.save(png_path)
            elif is_bc1:
                # BC1 (DXT1) Albedo textures -> decode_bc1 returns BGRA bytes
                raw_bytes = texture2ddecoder.decode_bc1(pix_data, w, h)
                img = Image.frombytes('RGBA', (w, h), raw_bytes, 'raw', 'BGRA')
                img.save(png_path)
            elif fmt == 0x4d010001:
                # BC3 (DXT5) Albedo/Material textures -> decode_bc3 returns BGRA bytes
                raw_bytes = texture2ddecoder.decode_bc3(pix_data, w, h)
                img = Image.frombytes('RGBA', (w, h), raw_bytes, 'raw', 'BGRA')
                img.save(png_path)
            else:
                try:
                    raw_bytes = texture2ddecoder.decode_bc7(pix_data, w, h)
                except Exception:
                    raw_bytes = texture2ddecoder.decode_bc3(pix_data, w, h)
                img = Image.frombytes('RGBA', (w, h), raw_bytes, 'raw', 'BGRA')
                img.save(png_path)

            extracted_count += 1
        except Exception:
            pass

    return extracted_count

def process_folder(input_folder, output_base):
    print(f"Scanning '{input_folder}' for YTD texture dictionaries...")
    total_files = 0
    total_textures = 0

    ytd_files = []
    for root, dirs, files in os.walk(input_folder):
        for f in files:
            if f.lower().endswith('.ytd'):
                ytd_files.append(os.path.join(root, f))

    ytd_files.sort(key=lambda p: 1 if '+hi' in os.path.basename(p).lower() else 0)

    for ytd_path in ytd_files:
        total_files += 1
        f = os.path.basename(ytd_path)
        is_hi = '+hi' in f.lower()
        root = os.path.dirname(ytd_path)
        rel_path = os.path.relpath(root, input_folder)
        target_dir = os.path.join(output_base, rel_path)
        os.makedirs(target_dir, exist_ok=True)
        
        c = extract_ytd_file(ytd_path, target_dir, is_hi)
        total_textures += c
        print(f"[{total_files:03d}/{len(ytd_files)}] {f:40s} -> Extracted {c} textures")

    print(f"\nExtraction Finished! Processed {total_files} YTD files, Extracted {total_textures} PNG textures to '{output_base}'.")

if __name__ == '__main__':
    src = r'C:\Users\honco\Downloads\Realistic Rhodes Soil-8033-1-0-1770160267\Realistic Scarlett Meadows Soil'
    dst = r'c:\Users\honco\OneDrive\Bureau\fallaga\assets\terrain\rhodes_scarlett_textures'
    process_folder(src, dst)
