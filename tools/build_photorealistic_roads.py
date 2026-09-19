import cv2
import numpy as np
import time
from scipy.ndimage import distance_transform_edt

def main():
    t0 = time.time()
    print("[*] Generating photorealistic multi-tier road distance field & category map...")

    rm_raw_path = "assets/terrain/roadMap_user_raw.png"
    rm_raw = cv2.imread(rm_raw_path)
    roads_raw = (rm_raw[:, :, 2] > 50).astype(np.uint8)

    # 1. Harbor basin boundary to guarantee dry land
    r0, r1 = 3700, 4500
    c0, c1 = 4800, 5800
    H, W = r1 - r0, c1 - c0
    port_crop = roads_raw[r0:r1, c0:c1].copy()
    bridge = port_crop.copy()
    cv2.line(bridge, (5595 - c0, 4045 - r0), (5615 - c0, 4100 - r0), 1, thickness=12)
    ff_mask = np.zeros((H + 2, W + 2), dtype=np.uint8)
    filled = bridge.copy()
    cv2.floodFill(filled, ff_mask, (5450 - c0, 4100 - r0), 2)
    user_basin = (filled == 2)
    final_basin_crop = user_basin & (~port_crop.astype(bool))

    # 2. Extract active region
    r_idx, c_idx = np.where(roads_raw > 0)
    rmin, rmax = max(0, r_idx.min() - 20), min(8128, r_idx.max() + 20)
    cmin, cmax = max(0, c_idx.min() - 20), min(8128, c_idx.max() + 20)
    crop = roads_raw[rmin:rmax, cmin:cmax].copy()

    # 3. Morphological skeletonization
    element = cv2.getStructuringElement(cv2.MORPH_CROSS, (3, 3))
    skel = np.zeros_like(crop)
    temp = crop.copy()
    while True:
        eroded = cv2.erode(temp, element)
        opened = cv2.morphologyEx(eroded, cv2.MORPH_OPEN, element)
        subset = eroded - opened
        skel = cv2.bitwise_or(skel, subset)
        temp = eroded.copy()
        if cv2.countNonZero(temp) == 0:
            break

    print(f"    [+] Skeleton points: {np.count_nonzero(skel):,}")

    # 4. Measure width from original brush strokes
    dist_orig = cv2.distanceTransform(crop, cv2.DIST_L2, 5)

    target_hw = np.zeros_like(crop, dtype=np.float32)
    cat_map = np.zeros_like(crop, dtype=np.uint8)

    ys, xs = np.where(skel > 0)
    for y, x in zip(ys, xs):
        r_orig = dist_orig[y, x]
        is_city = (3600 <= rmin + y <= 4500) and (4800 <= cmin + x <= 5600)
        if is_city and r_orig > 6.0:
            target_hw[y, x] = 1.40  # Major Colonial Avenue (~10-12m full width)
            cat_map[y, x] = 255     # Major Colonial Avenue
        elif is_city:
            target_hw[y, x] = 0.95  # Urban Street (~7-8m full width)
            cat_map[y, x] = 180     # Urban Street
        elif r_orig > 8.0:
            target_hw[y, x] = 1.05  # Main Country Highway (~7.5-8.5m full width)
            cat_map[y, x] = 200     # Main Country Highway
        elif r_orig > 3.5:
            target_hw[y, x] = 0.75  # Country Dirt Road (~5.5-6.5m full width)
            cat_map[y, x] = 100     # Country Dirt Road
        else:
            target_hw[y, x] = 0.50  # Rural Cart Track (~3.5-4.5m full width)
            cat_map[y, x] = 50      # Rural Cart Track

    # 5. Exact Euclidean distance transform with nearest indices
    dist_skel, idx = distance_transform_edt(~(skel > 0), return_indices=True)

    prop_hw = target_hw[idx[0], idx[1]]
    prop_cat = cat_map[idx[0], idx[1]]

    # Soft shoulder allowance (0.5 px = ~1.7m soft edge transition)
    total_hw = prop_hw + 0.50
    mask_active = (dist_skel <= total_hw) & (prop_hw > 0)

    # Continuous normalized distance gradient: 1.0 at center, 0.0 at edge
    norm_grad = np.zeros_like(crop, dtype=np.float32)
    norm_grad[mask_active] = np.clip(1.0 - (dist_skel[mask_active] / total_hw[mask_active]), 0.0, 1.0)

    # Smooth sub-pixel Gaussian filtering
    smooth_r = cv2.GaussianBlur(norm_grad, (3, 3), sigmaX=0.6, sigmaY=0.6)
    smooth_r_uint8 = np.clip(smooth_r * 255.0, 0, 255).astype(np.uint8)

    # Road category map in Green channel
    road_cat_channel = np.zeros_like(crop, dtype=np.uint8)
    road_cat_channel[mask_active] = prop_cat[mask_active]
    road_cat_smooth = cv2.GaussianBlur(road_cat_channel, (3, 3), sigmaX=0.6, sigmaY=0.6)

    # 6. Preserve existing wadis in Blue channel
    existing_rm = cv2.imread("assets/terrain/roadMap.png")
    out_roadMap = np.zeros((8129, 8129, 3), dtype=np.uint8)
    if existing_rm is not None and existing_rm.shape[:2] == (8129, 8129):
        out_roadMap[:, :, 0] = existing_rm[:, :, 0] # Blue channel = wadis (OpenCV BGR: channel 0 is Blue!)

    # Insert Red (roads gradient) and Green (road category)
    # In OpenCV BGR:
    # Channel 0: Blue (Wadis)
    # Channel 1: Green (Road Category)
    # Channel 2: Red (Road Distance Gradient)
    out_roadMap[rmin:rmax, cmin:cmax, 2] = smooth_r_uint8
    out_roadMap[rmin:rmax, cmin:cmax, 1] = road_cat_smooth

    # Strictly clear harbor basin
    out_roadMap[r0:r1, c0:c1, 2][final_basin_crop] = 0
    out_roadMap[r0:r1, c0:c1, 1][final_basin_crop] = 0

    print("[*] Saving refined assets/terrain/roadMap.png...")
    cv2.imwrite("assets/terrain/roadMap.png", out_roadMap)
    print(f"[+] Successfully built photorealistic road fields in {time.time()-t0:.2f}s!")

if __name__ == "__main__":
    main()
