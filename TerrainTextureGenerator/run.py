import extract_ytd
import traceback

try:
    print("Starting YTD extraction...")
    extract_ytd.process_folder(
        r'C:\Users\honco\Downloads\Realistic Rhodes Soil-8033-1-0-1770160267\Realistic Scarlett Meadows Soil',
        r'c:\Users\honco\OneDrive\Bureau\fallaga\assets\terrain\rhodes_scarlett_textures'
    )
except Exception as e:
    traceback.print_exc()
