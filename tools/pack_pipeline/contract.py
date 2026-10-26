"""contract.py

Authoritative units, datum and coordinate contract for the Sousse c.1930s
terrain rebuild pipeline.

All numbers here are taken verbatim from the revised build plan
(assets/terrain/heightmapAssets/Sousse c.1930s Terrain Parallel Agent Build Plan (1).md)
section 4, plus the two project-side conventions the game already uses
(Terrain.cpp: world extent and the historic-plan offset stored in
roads_manual.json). Nothing in this module mutates source data.
"""

from __future__ import annotations

import os

# --------------------------------------------------------------------------- #
# Paths
# --------------------------------------------------------------------------- #
TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(TOOLS_DIR, "..", ".."))
ASSETS = os.path.join(REPO_ROOT, "assets", "terrain")
PACK = os.path.join(ASSETS, "heightmapAssets")
ROADS_DIR = os.path.join(REPO_ROOT, "assets", "roads")
OUTPUT_ROOT = os.path.join(REPO_ROOT, "output", "pack_build")

# --------------------------------------------------------------------------- #
# Pack source files (plan section 3 manifest)
# --------------------------------------------------------------------------- #
R16 = os.path.join(PACK, "heightmap_2017_16bit.r16")
HM16_PNG = os.path.join(PACK, "heightmap_2017_16bit.png")
NORMAL_MAP = os.path.join(PACK, "normal_map_2017.png")
AO_MAP = os.path.join(PACK, "ambient_occlusion.png")
SLOPE_MAP = os.path.join(PACK, "slope_map.png")
SPLAT_A = os.path.join(PACK, "splat_A_olive_rock_ochre.png")
SPLAT_B = os.path.join(PACK, "splat_B_sand_steppe_salt.png")
SEA_MASK = os.path.join(PACK, "sea_mask.png")
SEBKHA_MASK = os.path.join(PACK, "sebkha_mask.png")
OUED_MASK = os.path.join(PACK, "oued_mask_dry_riverbeds.png")
URBAN_MASK = os.path.join(PACK, "urban_mask_medina_R_villenouvelle_G_port_B.png")
ALBEDO_PREVIEW = os.path.join(PACK, "albedo_preview_2017.png")
ROAD_SURFACE_MASK = os.path.join(PACK, "roads_surface_mask_4096.png")
ROAD_BANDS_MASK = os.path.join(PACK, "roads_bands_mask_4096.png")
ROAD_QUAY_ISLANDS_MASK = os.path.join(PACK, "roads_quay_islands_mask_4096.png")
PORT_PLATFORM_MASK = os.path.join(PACK, "port_platform_mask.png")
ROAD_REGISTRATION = os.path.join(PACK, "roads_registration.json")

# Supplemental plan assets, including the optional vector close-up aid. The
# road raster, registration, and port platform are required by the revised flow.
MISSING_FROM_PACK = {
    "port_platform_mask": PORT_PLATFORM_MASK,
    "roads_surface_mask_4096": ROAD_SURFACE_MASK,
    "roads_vector_terrain_2017px": os.path.join(PACK, "roads_vector_terrain_2017px.svg"),
    "roads_registration": ROAD_REGISTRATION,
    "albedo_preview_with_roads_2017": os.path.join(PACK, "albedo_preview_with_roads_2017.png"),
    "port_closeup_with_roads_4x": os.path.join(PACK, "port_closeup_with_roads_4x.png"),
}

# Road geometry fallbacks that DO exist in this project.
ROADS_JSON = os.path.join(ROADS_DIR, "roads.json")           # 1109 auto-traced segments
ROADS_MANUAL_JSON = os.path.join(ROADS_DIR, "roads_manual.json")  # hand drawn (28)

# --------------------------------------------------------------------------- #
# Grid / datum (plan section 4)
# --------------------------------------------------------------------------- #
GRID = 2017
GRID_INTERVALS = GRID - 1                      # 2016
METRES_PER_PX = 9.3257                         # pack space
EXTENT_M = 18800.0
HEIGHT_RANGE_M = 130.0
SEABED_MIN_M = -30.0
LAND_MAX_M = 100.0
SEA_LEVEL_STORED = 0.230769                    # 30/130
SEA_LEVEL_U16 = 15123

ROAD_MASK_CANVAS = 4096
ROAD_MASK_PX_PER_TERRAIN_PX = 2.03075
ROADS_FOOTPRINT_PX = (1092, 770, 1406, 1232)   # x0, y0, x1, y1 in terrain px

# road-map px -> 2017 terrain px affine matrix (plan section 4)
ROAD_MATRIX = (
    (0.2975846517970381, 0.0022952031962425444, 1095.8211137671838),
    (-0.0022952031962425444, 0.2975846517970381, 775.0002406584173),
)

# --------------------------------------------------------------------------- #
# Custom-engine datum and geo-reference. The non-centred origin preserves the
# project's existing Sousse road/player coordinates while applying the pack's
# 9.3257m spacing uniformly to every terrain layer.
# --------------------------------------------------------------------------- #
GAME_MIN_M = -30.0
GAME_MAX_M = 100.0
GAME_SEA_M = 0.0
GAME_RANGE_M = GAME_MAX_M - GAME_MIN_M

WORLD_MIN_X = -8287.7745
WORLD_MIN_Z = -10155.3526
WORLD_MAX_X = WORLD_MIN_X + GRID_INTERVALS * METRES_PER_PX
WORLD_MAX_Z = WORLD_MIN_Z + GRID_INTERVALS * METRES_PER_PX
GAME_M_PER_PX = METRES_PER_PX

# roads_manual.json / roads.json store plan-space coords; world = plan + offset
HISTORIC_PLAN_OFFSET_X = 3600.0
HISTORIC_PLAN_OFFSET_Z = -701.0


# --------------------------------------------------------------------------- #
# Decoding / encoding
# --------------------------------------------------------------------------- #
def packed_to_elev(raw):
    """Decode a pack stored value (0..65535) to metres (sea level = 0)."""
    return raw / 65535.0 * HEIGHT_RANGE_M - 30.0


def elev_to_packed(z_m):
    return (z_m + 30.0) / HEIGHT_RANGE_M * 65535.0


def game_to_elev(game_m):
    """Game metres -> pack metres (shift by the datum difference)."""
    return game_m - GAME_SEA_M


def elev_to_game(z_m):
    return z_m + GAME_SEA_M


# --------------------------------------------------------------------------- #
# Coordinate transforms
# --------------------------------------------------------------------------- #
def plan_to_world(x_plan, y_plan):
    return x_plan + HISTORIC_PLAN_OFFSET_X, y_plan + HISTORIC_PLAN_OFFSET_Z


def world_to_terrain_px(x_world, y_world):
    """Geo-referenced world metres -> 2017 grid pixels (float)."""
    px = (x_world - WORLD_MIN_X) / METRES_PER_PX
    py = (y_world - WORLD_MIN_Z) / METRES_PER_PX
    return px, py


def terrain_px_to_world(px, py):
    x = WORLD_MIN_X + px * METRES_PER_PX
    y = WORLD_MIN_Z + py * METRES_PER_PX
    return x, y


def plan_to_terrain_px(x_plan, y_plan):
    return world_to_terrain_px(*plan_to_world(x_plan, y_plan))


def terrain_px_to_mask_px(px, py):
    return px * ROAD_MASK_PX_PER_TERRAIN_PX, py * ROAD_MASK_PX_PER_TERRAIN_PX


def roadmap_px_to_terrain_px(rx, ry):
    a, b, c = ROAD_MATRIX[0]
    d, e, f = ROAD_MATRIX[1]
    return a * rx + b * ry + c, d * rx + e * ry + f
