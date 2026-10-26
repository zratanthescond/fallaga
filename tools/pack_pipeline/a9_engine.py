"""A9 Engine binding (A3, A9, A10, A11, A12, A13 settings sheets).

These agents are engine-bound: the plan targets Unreal Engine 5 (alt. Unity)
while this repo is a custom C++ engine, so the pipeline emits the import /
build settings and the data hand-off rather than driving an editor. Nothing
here mutates the landscape.
"""

from __future__ import annotations

import os

from . import common, contract


def run(run_dir, road_info=None, water_info=None, port_info=None, veg_info=None):
    sheet = {
        "agent": "A9",
        "note": "Custom C++/OpenGL implementation; metres, +Y up, +X east, +Z south.",
        "coordinate_contract": {
            "grid": contract.GRID,
            "metres_per_px": contract.METRES_PER_PX,
            "extent_m": contract.EXTENT_M,
            "height_range_m": contract.HEIGHT_RANGE_M,
            "sea_level_u16": contract.SEA_LEVEL_U16,
            "orientation": "top view: sea on the right/east, port basin ~3 km east "
                           "of the image centre, flat platform just south of the basin.",
        },
        "A3_terrain_loader": {
            "heightmap": "assets/terrain/heightmap.png after deployment; source is "
                         "heightmapAssets/heightmap_2017_16bit.r16",
            "height_decode": "raw / 65535 * 130 - 30 metres",
            "grid": [contract.GRID, contract.GRID],
            "metres_per_pixel": contract.METRES_PER_PX,
            "world_bounds": [contract.WORLD_MIN_X, contract.WORLD_MAX_X,
                              contract.WORLD_MIN_Z, contract.WORLD_MAX_Z],
            "sea_level_y": 0.0,
            "mesh_tiles": {"count": [8, 8], "vertices_per_tile": 253,
                           "shared_border_pixels": 1},
            "height_texture": "GL_R16 normalized, GL_LINEAR, GL_CLAMP_TO_EDGE",
            "cpu_gpu_height_samples": 20,
            "acceptance": "CPU and uploaded GL_R16 samples agree within 0.1m; "
                          "north-up top view has sea to east.",
        },
        "A9_material_lighting": {
            "layers": "layers/terrain_weights_0.png + layers/terrain_weights_1.png (RGBA8)",
            "macro_inputs": ["normal_map_2017.png", "ambient_occlusion.png"],
            "road_surface": "assets/terrain/roads_surface_mask_4096.png, "
                             "registered source raster",
            "rvt": "keep road_surface_mask_4096.png + route decal for close-ups",
            "colour_target": "match albedo_preview_2017.png at distance",
            "lighting": "SW-W directional sun, sky ambient, horizon fog, fixed QA exposure.",
            "ssim_gate": 0.80,
        },
        "A10_road_builder": {
            "source": "registered roads_surface_mask_4096.png centerline graph "
                      "and road_polygons.json",
            "material": {"main": "macadam", "secondary": "macadam",
                         "track": "compacted earth"},
            "do_not": "do not alter landscape height; terrain already carries a "
                      "1.6 m minimum under roads.",
            "note": "The road raster is authoritative; centerlines and width "
                    "profiles are extracted from its road bands.",
        },
        "A11_port_urban": {
            "source": "port_platform_mask.png + sea_mask.png + "
                      "berth_candidates.json + era_rules.json",
            "medina_bounds_terrain_px": [1233, 954, 1306, 1052],
            "building_sources": "complement of road mask inside the urban zone, "
                                "split by block, inset 3 m, fit lots.",
        },
        "A12_water": {
            "source": "water_data.json + shoreline.geojson",
            "ocean": "Z=0, extend beyond east edge; seabed edge -30 m -> add skirt.",
            "basin": "sea-level, calmer waves.",
            "sebkha": "salt crust, no permanent water.",
        },
        "A13_vegetation": {
            "source": "density_olive.png / density_steppe.png / density_dune.png",
            "exclusion": "exclusion_mask.png (authoritative)",
            "spacing_m": {"olive": [12, 25]},
            "seeds": "from global coordinates only (see common.coord_seed)",
        },
    }
    if road_info:
        sheet["handoff"] = sheet.get("handoff", {})
        sheet["handoff"]["road"] = {"edges": road_info.get("edge_count"),
                                    "nodes": road_info.get("node_count"),
                                    "polygons": road_info.get("polygon_count")}
    if water_info:
        sheet.setdefault("handoff", {})["water"] = water_info.get("bathymetry")
    if port_info:
        sheet.setdefault("handoff", {})["port"] = {
            "basin_polygons": port_info.get("basin_polygons"),
            "berths": port_info.get("berths")}
    if veg_info:
        sheet.setdefault("handoff", {})["vegetation"] = veg_info.get("mean_density")

    common.save_json(sheet, os.path.join(run_dir, "settings_sheets.json"))

    txt = _as_text(sheet)
    with open(os.path.join(run_dir, "settings_sheet.txt"), "w", encoding="utf-8") as fh:
        fh.write(txt)

    return {"agent": "A9", "sheets": list(k for k in sheet if k.startswith(("A3", "A9", "A10", "A11", "A12", "A13")))}


def _as_text(sheet):
    lines = ["Sousse c.1930s terrain - engine settings sheet", "=" * 48, ""]
    for k, v in sheet.items():
        if k in ("agent", "note"):
            continue
        lines.append(f"[{k}]")
        lines.append(common.json.dumps(v, indent=2, default=str))
        lines.append("")
    return "\n".join(lines)
