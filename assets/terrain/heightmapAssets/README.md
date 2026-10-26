# Sousse c.1930s — terrain pack (2017x2017)

Assumptions (change in build5.py): 15 m/px in the source map (~18.8 km square, 9.33 m/px in the output), land relief 0-100 m, shallow shelf to -30 m.
Height encoding: stored 0 = -30 m (seabed); sea level = 30/130 = 0.2308 (~15123/65535); land max = 1.0 (+100 m).

## Unreal Engine 5
- Import heightmap_2017_16bit.r16 (raw16, little-endian), 2017x2017. Scale X/Y = 933, Z = 25.4.
- Sea level is about -3500 cm in Landscape space: set Landscape Location Z to +3500 cm.
- splat_A (R olive/cultivated, G limestone, B ochre soil), splat_B (R sand, G steppe, B salt crust).
- sea_mask, sebkha_mask (flat seasonal salt flat), oued_mask_dry_riverbeds, port_platform_mask.

## Your road map (projected unmodified)
The original image is NOT altered: no thinning, recolouring or redrawing. It is only scaled/translated (rotation -0.44 deg) into terrain space.
Placement was found automatically from the port shape: the map's inner port basin was matched to the terrain's port basin by maximizing overlap (Dice 0.955; same solution from all 25 starting guesses).
Result: 1 map pixel = 0.2976 terrain px (about 2.8 m); the map covers x 1096-1404, y 772-1232 of the 2017 grid (about 2.9 km x 4.3 km).
- roads_projected_4096_RGBA.png: whole 4096x4096 terrain frame, original pixels (transparent outside the map).
- roads_projected_8192grid_crop_RGBA.png: same, finer (8192 grid), cropped to the map footprint; origin/size in roads_registration.json.
- roads_registration.json: affine matrix map px -> terrain px (2017 grid).
- albedo_preview_with_roads_2017.png: preview only (the faint original lines are darkened so they are visible).
The terrain itself is not modified under the roads.

## Update: exact port match + crisp roads
- The terrain's port basin is now redrawn to the exact outline of your map's basin (the map is not touched), so the quay, basin and moles line up with the terrain edges.
- roads_vector_terrain_2017px.svg: the map's lines traced as smooth vectors (centrelines of your lines), in terrain px. Resolution-independent, no pixelation.
- roads_vector_raster_16x_crop_RGBA.png (+ .json): the same vectors rendered at 16x the terrain grid, cropped to the roads.
- roads_projected_*: still the original pixels, only scaled/moved (kept for reference).
- The 3D viewer draws the vectors on a full-resolution patch of the port (the base mesh is coarser).

## Update: road surfaces (not just edges) + roads on land
- roads_surface_mask_4096.png: the full road SURFACES of your map (the whole band between each pair of edge lines, plaza and quay areas included), projected into terrain space. Same placement as before; the map is only scaled/moved (rotation -0.44 deg).
- Roads are no longer in the sea: wherever the map's roads ran over water, the terrain is now land (a low causeway, 1.6 m above sea) so every road sits on ground. The port basin, moles and quay edge follow the map outline.
- Viewer: roads are drawn as filled surfaces with the original edge lines on a full-resolution patch of the port area (toggle "Road map").

## Update: faithful road surfaces (no morphology)
The road surfaces now come straight from the faces of your original line art (every white region between the drawn lines is classified as road or not), with each line bundle split at its middle. No thinning, no hole filling, no merging: roads that are separate in your map stay separate.
- roads_bands_mask_4096.png: the road bands (the roads proper).
- roads_quay_islands_mask_4096.png: small closed shapes east of the medina (quay strips, moles, plaza islands), shown as paved quay.
- roads_surface_mask_4096.png: bands + quay islands together (use this one as 'road').
- Medina blocks, the fort and other large closed blocks are NOT road; they stay terrain.
- Smoothing: only the pixel staircase is rounded (0.6 map px); topology is unchanged (4 road components before and after).
- Two very narrow medina alleys (about 5 px wide in your map) were not classified as road.
