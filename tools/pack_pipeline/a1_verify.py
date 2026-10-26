"""A1 Verifier (gate for everything).

Checks the structural integrity of every pack input and the height/mask
invariants from plan sections 3 and 9 (gates G1, G3, G4, G5). Fail-closed:
`run()` returns passed=False if a hard invariant breaks, and the orchestrator
stops downstream waves.
"""

from __future__ import annotations

import os

import numpy as np
from PIL import Image
from scipy import ndimage

from . import common, contract


def run(run_dir, road_mask_2017=None):
    report = {
        "agent": "A1",
        "grid": config_report(),
        "files": {},
        "invariants": [],
        "errors": [],
        "warnings": [],
    }

    # ---- file integrity (G1) ------------------------------------------------
    expect_r16 = contract.GRID * contract.GRID * 2
    r16_ok = os.path.exists(contract.R16) and common.file_size(contract.R16) == expect_r16
    report["files"]["heightmap_2017_16bit.r16"] = {
        "exists": os.path.exists(contract.R16),
        "bytes": common.file_size(contract.R16),
        "expected_bytes": expect_r16,
        "sha256": common.sha256_file(contract.R16) if os.path.exists(contract.R16) else None,
        "ok": r16_ok,
    }
    _inv(report, "G1_r16_size", r16_ok, f"r16 must be {expect_r16} bytes")

    for name, path in [
        ("heightmap_2017_16bit.png", contract.HM16_PNG),
        ("normal_map_2017.png", contract.NORMAL_MAP),
        ("ambient_occlusion.png", contract.AO_MAP),
        ("slope_map.png", contract.SLOPE_MAP),
        ("splat_A_olive_rock_ochre.png", contract.SPLAT_A),
        ("splat_B_sand_steppe_salt.png", contract.SPLAT_B),
        ("sea_mask.png", contract.SEA_MASK),
        ("sebkha_mask.png", contract.SEBKHA_MASK),
        ("oued_mask_dry_riverbeds.png", contract.OUED_MASK),
        ("urban_mask_medina_R_villenouvelle_G_port_B.png", contract.URBAN_MASK),
    ]:
        if not os.path.exists(path):
            report["files"][name] = {"exists": False, "ok": False}
            report["errors"].append(f"missing pack file: {name}")
            continue
        report["files"][name] = {
            "exists": True,
            "sha256": common.sha256_file(path),
            "bytes": common.file_size(path),
            "ok": True,
        }

    for key, path in contract.MISSING_FROM_PACK.items():
        report["files"][key] = {"exists": os.path.exists(path), "expected_by_plan": True}

    # The supplied road surface raster is the geometric source of truth. The
    # vector SVG is an optional close-up aid, not a gate for the raster workflow.
    required_geometry = ("roads_surface_mask_4096", "roads_registration",
                         "port_platform_mask")
    for key in required_geometry:
        if not os.path.exists(contract.MISSING_FROM_PACK[key]):
            message = f"required revised-plan input is missing: {key}"
            if message not in report["errors"]:
                report["errors"].append(message)

    dimensions = [
        ("heightmap_2017_16bit.png", contract.HM16_PNG, (contract.GRID, contract.GRID)),
        ("normal_map_2017.png", contract.NORMAL_MAP, (contract.GRID, contract.GRID)),
        ("ambient_occlusion.png", contract.AO_MAP, (contract.GRID, contract.GRID)),
        ("slope_map.png", contract.SLOPE_MAP, (contract.GRID, contract.GRID)),
        ("splat_A_olive_rock_ochre.png", contract.SPLAT_A, (contract.GRID, contract.GRID)),
        ("splat_B_sand_steppe_salt.png", contract.SPLAT_B, (contract.GRID, contract.GRID)),
        ("sea_mask.png", contract.SEA_MASK, (contract.GRID, contract.GRID)),
        ("sebkha_mask.png", contract.SEBKHA_MASK, (contract.GRID, contract.GRID)),
        ("oued_mask_dry_riverbeds.png", contract.OUED_MASK, (contract.GRID, contract.GRID)),
        ("urban_mask_medina_R_villenouvelle_G_port_B.png", contract.URBAN_MASK,
         (contract.GRID, contract.GRID)),
        ("port_platform_mask.png", contract.PORT_PLATFORM_MASK, (contract.GRID, contract.GRID)),
        ("roads_surface_mask_4096.png", contract.ROAD_SURFACE_MASK,
         (contract.ROAD_MASK_CANVAS, contract.ROAD_MASK_CANVAS)),
    ]
    for name, path, expected in dimensions:
        if not os.path.exists(path):
            continue
        with Image.open(path) as image:
            actual = image.size
            valid = actual == expected
            report["files"].setdefault(name, {}).update(
                {"dimensions": list(actual), "expected_dimensions": list(expected),
                 "dimensions_ok": valid})
            if not valid:
                report["errors"].append(
                    f"{name} dimensions {actual[0]}x{actual[1]} != "
                    f"{expected[0]}x{expected[1]}")

    if os.path.exists(contract.ROAD_REGISTRATION):
        registration = common.load_json(contract.ROAD_REGISTRATION)
        matrix = np.asarray(registration.get("registration_matrix_map_to_2017px", []),
                            dtype=np.float64)
        expected_matrix = np.asarray(contract.ROAD_MATRIX, dtype=np.float64)
        registration_ok = (matrix.shape == expected_matrix.shape and
                           np.allclose(matrix, expected_matrix, atol=1e-5, rtol=0.0))
        _inv(report, "G1_road_registration", registration_ok,
             "registered affine matches the documented road-map placement" if registration_ok
             else "roads_registration.json affine is missing or differs from the documented placement")
        if not registration_ok:
            report["errors"].append("roads_registration.json affine does not match the plan")

    if report["errors"]:
        report["passed"] = False
        common.save_json(report, os.path.join(run_dir, "verify_report.json"))
        return report

    # ---- load + invariants (G3) --------------------------------------------
    raw = common.read_r16(contract.R16, contract.GRID)
    png_raw = np.asarray(Image.open(contract.HM16_PNG))
    png_matches = (png_raw.shape == raw.shape and np.array_equal(png_raw, raw))
    _inv(report, "G1_png_matches_r16", png_matches,
         "16-bit PNG matches the little-endian R16 source" if png_matches
         else "16-bit PNG samples differ from the little-endian R16 source")
    z = contract.packed_to_elev(raw.astype(np.float64))
    sea = common.load_gray(contract.SEA_MASK) > 127
    land = ~sea

    sea_max = float(z[sea].max()) if sea.any() else 0.0
    _inv(report, "G3_sea_below_0p1m", sea_max <= 0.1,
         f"max decoded z over sea_mask = {sea_max:.4f} m (must be <= 0.1)")

    # ---- six-layer splat sums (G1) -----------------------------------------
    a = common.load_rgb(contract.SPLAT_A).astype(np.int32)
    b = common.load_rgb(contract.SPLAT_B).astype(np.int32)
    total = a[:, :, 0] + a[:, :, 1] + a[:, :, 2] + b[:, :, 0] + b[:, :, 1] + b[:, :, 2]
    bad = np.abs(total - 255) > 3
    bad_frac = float(bad.mean())
    _inv(report, "G1_splat_sum_255", bad_frac <= 0.02,
         f"fraction of pixels whose 6 weights are not 255+/-3 = {bad_frac:.4f}")

    # ---- road placement (G4, G5) from the supplied terrain-space raster -----
    road = (road_mask_2017 if road_mask_2017 is not None
            else common.load_road_mask_2017())
    road_eroded = ndimage.binary_erosion(road, iterations=1)
    on_land = z[road_eroded] >= 1.0 if road_eroded.any() else np.array([])
    frac_land = float(on_land.mean()) if on_land.size else 1.0
    overlap = int((road_eroded & sea).sum())
    _inv(report, "G4_roads_on_land", frac_land >= 0.99,
         f"eroded road px with z>=1.0 m = {frac_land*100:.2f}% (>=99% required)")
    _inv(report, "G5_roads_vs_sea", overlap == 0,
         f"eroded road px overlapping sea = {overlap} (must be 0)")

    report["passed"] = all(i["passed"] for i in report["invariants"])
    common.save_json(report, os.path.join(run_dir, "verify_report.json"))
    return report


def config_report():
    return {
        "grid": contract.GRID,
        "metres_per_px": contract.METRES_PER_PX,
        "extent_m": contract.EXTENT_M,
        "height_range_m": contract.HEIGHT_RANGE_M,
        "seabed_min_m": contract.SEABED_MIN_M,
        "land_max_m": contract.LAND_MAX_M,
        "sea_level_stored": contract.SEA_LEVEL_STORED,
        "sea_level_u16": contract.SEA_LEVEL_U16,
        "road_mask_canvas": contract.ROAD_MASK_CANVAS,
        "road_mask_px_per_terrain_px": contract.ROAD_MASK_PX_PER_TERRAIN_PX,
        "roads_footprint_px": list(contract.ROADS_FOOTPRINT_PX),
    }


def _inv(report, name, passed, detail):
    report["invariants"].append({"gate": name, "passed": bool(passed), "detail": detail})
