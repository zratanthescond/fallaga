#!/usr/bin/env python3
"""
capture_1943_port_view.py
=========================
Captures in-engine perspective screenshots of the aligned 1943 Sousse Port,
waterfront promenade, and colonial monuments.
"""

import os
import subprocess
import time
import shutil

def capture(name, pos, angle, shot_frame=60):
    env = os.environ.copy()
    env["FALLAGA_PLAYER_POS"] = pos
    env["FALLAGA_CAM_ANGLE"] = angle
    env["FALLAGA_SHOT"] = str(shot_frame)
    shot_path = os.path.abspath(f"scratch/{name}.png")
    env["FALLAGA_SHOT_PATH"] = shot_path

    exe_path = os.path.abspath("build/Release/RDR2_Prototype.exe")
    if not os.path.exists(exe_path):
        print(f"[!] Executable not found at {exe_path}")
        return False

    print(f"[*] Capturing '{name}' at pos={pos}, angle={angle}...")
    try:
        res = subprocess.run(
            [exe_path],
            cwd=os.path.abspath("."),
            env=env,
            capture_output=True,
            text=True,
            timeout=35
        )
        print(f"    Finished with exit code {res.returncode}")
        if os.path.exists(shot_path):
            art_dir = r"C:\Users\honco\.gemini\antigravity-ide\brain\663803a9-f43e-4877-99ff-741274bfc692"
            dest = os.path.join(art_dir, f"{name}.png")
            shutil.copyfile(shot_path, dest)
            print(f"    [+] Saved screenshot to {dest}")
            return True
        else:
            print(f"    [!] Screenshot file not found at {shot_path}")
            return False
    except Exception as e:
        print(f"    [!] Error running capture: {e}")
        return False

def main():
    # Shot 1: Looking South down Cours de la Marine from Place Pichon
    # Camera at X=4615, Y=-44.5, Z=-630, looking South (yaw ~175, pitch ~5)
    capture("view_cours_de_la_marine", "4615,-44.5,-630", "0,5,175", shot_frame=60)

    # Shot 2: Elevated view of Port Basin, Commercial Pier, and Breakwaters
    # Camera at X=4500, Y=-20, Z=-650, looking South-East towards the harbor
    capture("view_port_aerial_basin", "4500,-20,-650", "0,25,140", shot_frame=60)

if __name__ == "__main__":
    main()
