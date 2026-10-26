# Sousse c.1930s Terrain: Parallel Agent Build Plan

Prepared for the parallelized AI agent system. Target: your custom C++ / OpenGL engine; renderer, LOD and streaming are yours to implement. A three.js preview already exists (published viewer) and is a working reference for decoding the data.

## 1. Goal and definition of done

Turn the asset pack into an 18.8 km x 18.8 km coastal terrain of Sousse, Tunisia, in the 1930s (French protectorate era): a gentle Sahel plain, a shallow sea, sandy beaches with a dune belt, olive groves and dry steppe, dry riverbeds (oueds), one flat salt flat (sebkha), the old port with its basin and moles, and the user's road network projected exactly onto it.

Done means all of the following are true:

1. The engine loads the heightmap at the correct scale, with sea level exactly at the water plane (y = 0).
2. Every road surface sits on land. No road is in the sea or in the port basin.
3. The port basin, moles and quay edge match the road map outline.
4. Six ground layers plus a road layer blend correctly, with no visible tile seams.
5. Vegetation, water, port and (optionally) buildings are placed from the supplied masks only.
6. Nothing anachronistic appears (no hotel strip, marina resort, modern roads or towers).
7. All QA gates in section 9 pass, and a rerun produces identical outputs.

## 2. Non-negotiable rules

- The road map is never altered. It may only be moved with the matrix in roads\_registration.json (scale 0.2976, rotation -0.44 deg). No thinning, recolouring, redrawing or re-tracing of its geometry.
- Roads are as wide as the map draws them (about 50 m, stylised). Do not narrow them unless the human approves.
- The terrain was already adapted to the roads (coast reshaped, low causeways at 1.6 m). Agents must not undo this or re-flatten the terrain under roads.
- The agent never edits the source files. Every step writes new, versioned outputs.
- Every stochastic step uses a seed derived from global pixel coordinates, never from tile-local coordinates (prevents seams).

## 3. Asset manifest

All files are in the terrain folder of the output pack. The two original uploads are the heightmap source (1254 x 1254, 16-bit) and the road map (1024 x 1536, RGBA line art).

| File | What it is | Used by |
| --- | --- | --- |
| heightmap\_2017\_16bit.r16 | Raw 16-bit little-endian heightmap, 2017 x 2017 | Terrain loader (A3) |
| heightmap\_2017\_16bit.png | Same data as a 16-bit PNG | Verification, other tools |
| normal\_map\_2017.png | Normals from true gradients (RGB, tangent space, +Z up) | Material |
| ambient\_occlusion.png | Baked AO (8-bit) | Material |
| slope\_map.png | Slope, 0..60 degrees mapped to 0..255 | Vegetation, rules |
| splat\_A\_olive\_rock\_ochre.png | R olive/cultivated, G limestone rock, B ochre soil (weights sum to about 1 with splat\_B) | Layer prep |
| splat\_B\_sand\_steppe\_salt.png | R sand (beach and dunes), G esparto steppe, B salt crust | Layer prep |
| sea\_mask.png | 255 = open sea and port basin | Water, QA |
| sebkha\_mask.png | 255 = flat seasonal salt flat | Water, vegetation exclusion |
| oued\_mask\_dry\_riverbeds.png | 0..255 flow strength of dry riverbeds | Material, scatter |
| port\_platform\_mask.png | 255 = flat quay and port platform (2.5 m) | Port |
| roads\_surface\_mask\_4096.png | Full road surfaces in terrain space, 4096 canvas (255 = road) | Roads, material |
| roads\_vector\_terrain\_2017px.svg | Road edge lines traced as vectors, terrain px | Roads, decals |
| roads\_vector\_raster\_16x\_crop\_RGBA.png + .json | Same edge lines rendered at 16x, cropped; origin and scale in the json | Decals, close-ups |
| roads\_projected\_4096\_RGBA.png, roads\_projected\_8192grid\_crop\_RGBA.png | Original map pixels, only scaled and moved | Reference, comparison |
| roads\_registration.json | Affine matrix: road-map px to terrain px | Any re-projection |
| albedo\_preview\_2017.png, albedo\_preview\_with\_roads\_2017.png, port\_closeup\_with\_roads\_4x.png | Baked visual targets | QA reference |
| terrain\_viewer.html | Published three.js preview | Human review |
| README.md | Short summary of scale and import values | All |

Note: the Python scripts that generated these files (build8.py and helpers) are not in the pack. If reproducibility is required, ask for them to be packaged before Wave 0.

## 4. Units, encoding and coordinate contract

```json
{
  "grid": 2017,
  "metres_per_px": 9.3257,
  "extent_m": 18800,
  "height_range_m": 130,
  "seabed_min_m": -30,
  "land_max_m": 100,
  "sea_level_stored": 0.230769,
  "sea_level_u16": 15123,
  "road_mask_canvas": 4096,
  "road_mask_px_per_terrain_px": 2.03075,
  "roads_footprint_px": [1092, 770, 1406, 1232]
}
```

**Height decoding.** stored value v (0..65535): `z_m = v / 65535 * 130 - 30`. Sea level is z = 0 (u16 about 15123). Seabed shelf reaches -30 m. Land runs from 0 to about 100 m.

**Horizontal.** One terrain pixel is 9.3257 m. Pixel (0,0) is the top-left corner, x grows east, y grows south, north is up. Pixel centres sit on integer indices, so 2017 samples span 2016 intervals (18.8 km).

**Your engine (C++ / OpenGL).** Use metres, +Y up, +X east, +Z south, so a top-down render reads north-up. Sea level is y = 0. Vertex height is `y = (texel16 / 65535 * 130 - 30) * verticalScale`; keep verticalScale = 1 and use any exaggeration only as a debug slider. Horizontal spacing is 9.3257 m, so from the north-west corner x = px \* 9.3257 and z = py \* 9.3257. If your world needs the terrain centred on the origin, apply the same shift to every layer.

**GPU upload.** Read the raw file with fread as little-endian uint16, or the PNG with stbi\_load\_16. Create the texture as GL\_R16 (normalised) with `glPixelStorei(GL_UNPACK_ALIGNMENT, 2)`, GL\_LINEAR filtering and GL\_CLAMP\_TO\_EDGE, and sample it in the vertex shader with `textureLod`. Pixel centres sit on integer indices, so use uv = (px + 0.5) / 2017.

**Normals and slope.** Either compute normals from the heightmap (central differences, 9.3257 m spacing) or decode normal\_map\_2017.png as world = (R \* 2 - 1, B \* 2 - 1, G \* 2 - 1): R points east, G points south, B points up. Slope in degrees = slope\_map value / 255 \* 60.

**Mesh strategy.** 2017 x 2017 is about 4.07 M vertices. Use a geometry clipmap or a CDLOD quadtree over 64 tiles of 253 x 253 vertices (1 px shared border), with skirts or stitching against LOD cracks and per-tile frustum culling. Keep the CPU height sampler in sync with the shader (section 12).

**Road map to terrain.** x\_t = 0.29758 x + 0.002295 y + 1095.82; y\_t = -0.002295 x + 0.29758 y + 775.00 (road-map px to 2017-grid px). Road-mask 4096 px divide by 2.03075 to get terrain px.

**Orientation check (do this first).** In a top-down render the sea is on the right (east), the port basin is about 3 to 3.5 km east of the image centre at roughly the same northing, and the flat grey platform sits just south of the basin. If the image is mirrored or rotated, fix the uv or vertex mapping, not the data.

## 5. Assumptions baked into the pack (human to confirm)

1. The source heightmap is 15 m per pixel (so the map is about 18.8 km wide). If the real extent differs, only the scale values change; no data needs regenerating.
2. Land relief is 0 to 100 m and the shelf is -30 m. This is much flatter than the source heightmap's shape, because Sousse is a coastal plain.
3. The user's flat platform south of the basin is intentional and is kept (it has a straight left edge).
4. Roads are drawn about 50 m wide. They are kept as drawn.
5. The coast near the port was reshaped to follow the roads; road areas that were over water became low causeways.

## 6. Agent topology and waves

Every agent is a stateless worker that reads a task card, writes outputs and a result.json, and never talks to another agent directly. The orchestrator owns the dependency graph.

| Wave | Runs in parallel | Needs |
| --- | --- | --- |
| W0 | A0 Orchestrator, A1 Verifier, A2 Historian | none |
| W1 | A3 Terrain Loader, A4 Layer Prep, A5 Road Geometry, A6 Water Data, A7 Port Geometry, A8 Vegetation Density | A1 passed |
| W2 | A9 Terrain Shader and Lighting, A10 Road Renderer, A11 Port and Urban Placement, A12 Water Renderer, A13 Vegetation Instancing | A3 done, plus the data agents named per card |
| W3 | A14 QA shards (parallel), integration | all of W2 |
| W4 | Packaging, human review | W3 passed |

Critical path: A1 > A3 > (A10, A11, A12, A13) > A14. Everything else is off the critical path, so give the critical path the most capacity.

## 7. Agent task cards

### A0 Orchestrator

- Builds the job graph (section 10), assigns ids, tracks hashes, retries failures, stops the run on any red QA gate.
- Writes run\_manifest.json: asset hashes (sha256), config, agent versions, seeds.

### A1 Verifier (gate for everything)

- Check: heightmap is 2017 x 2017, 16-bit, little-endian raw, file size 8,136,578 bytes; all masks 2017 x 2017 8-bit; roads mask 4096 x 4096.
- Pin sha256 of every input in the manifest.
- Invariants: for all pixels in sea\_mask, decoded z <= 0.1 m; for roads (road mask resampled to 2017 and eroded by 1 px) at least 99% have z >= 1.0 m; roads and sea\_mask do not overlap after erosion; splat\_A + splat\_B sum per pixel is 255 +/- 3 across the sum of the six channels.
- Output: verify\_report.json. Fail closed: if any invariant fails, W1 must not start.

### A2 Historian (runs from W0 to W3)

- Research Sousse in the 1930s: medina and its walls, ribat, kasbah, port development from 1899, rail connections, building materials, street furniture, vehicles, ships, vegetation. Cite sources and mark each claim verified or unverified.
- Output era\_rules.json: allowed and forbidden asset lists, colour palettes (whitewashed medina, ochre and terracotta roofs), building heights, typical ship types, plus a blocklist of anachronisms.
- It supplies rules only; it never touches geometry.

### A3 Terrain Loader (GPU resources)

- Load the heightmap into a GL\_R16 texture and build the terrain mesh from it (clipmap or CDLOD, 64 tiles of 253 x 253 vertices with a 1 px shared border). Spacing 9.3257 m; sea level at y = 0.
- Provide a CPU-side height sampler (bilinear, same decode as the shader) for placement, picking and collision. Shader and CPU must agree within 0.05 m.
- Provide debug views: height, slope, normal, weight layers, masks, tile borders and LOD colours.
- Acceptance: sample 20 fixed pixels (including the port corner, the sebkha centre and three coast points) on the CPU and read the same points back from the GPU; both equal the decoded z within 0.1 m. The orientation check passes in a top-down render.

### A4 Layer Prep

- Split into single-channel 8-bit weight maps: olive, rock, ochre, sand, steppe, salt (from splat\_A and splat\_B).
- Create the road layer from roads\_surface\_mask\_4096.png and keep it at 4096 for rendering (GL\_R8, mipmaps, anisotropic filtering). Also write a 2017 version for CPU queries.
- Pack for the GPU: olive, rock, ochre, sand in one RGBA8 texture and steppe, salt, road, spare in a second, so one terrain pass needs two weight fetches. Renormalise so layers sum to 255 per pixel. Do not change shapes.
- Outputs: the packed textures (PNG or KTX2) plus layer\_manifest.json (name, albedo, normal and roughness paths, tiling scale in metres, footstep tag).
- Acceptance: sum = 255 +/- 1; no layer weight above 0 inside the sea mask except sand at the shoreline.

### A5 Road Geometry (data only)

- Input: roads\_surface\_mask\_4096.png. Skeletonise (centrelines) and compute width = 2 x distance transform at each point. Build a graph: nodes (junctions, endpoints), edges (polylines with width profile).
- Simplify each edge (Ramer-Douglas-Peucker, about 1.5 m tolerance) and smooth. Large blobs (plazas, quays, the junction near the port) must be kept as polygons, not skeleton spurs: any component whose max width is above 80 m is exported as a polygon instead.
- Convert to world coordinates: terrain px = mask px / 2.03075; x\_m = px \* 9.3257; z\_m = py \* 9.3257; y from the CPU height sampler (bilinear).
- Outputs: road\_graph.json (nodes, edges, widths, class), road\_polygons.json (plazas, quays).
- Acceptance: re-rasterising the graph (centreline +/- half width) reproduces the original mask with IoU >= 0.95.
- The class (main, secondary, track) comes from the width and from the position relative to the urban zone; it is metadata only.

### A6 Water Data

- From sea\_mask and the heightmap: shoreline polyline at z = 0, depth map, and the wet-sand band (0 to 14 m from the shoreline).
- Sebkha: polygon from sebkha\_mask, flat at its rim level, no permanent water (salt crust, optional shallow seasonal puddle decal).
- Oueds: extract from oued\_mask as dry channels, no water body. Add gravel and riparian vegetation hints.
- Port basin: polygon from sea\_mask in the port zone (the basin is sea).
- Outputs: water\_data.json, shoreline.geojson, bathymetry statistics.

### A7 Port Geometry

- From port\_platform\_mask and sea\_mask: quay edge polylines, mole polygons, basin polygon, bollard line and berth segments along the quay.
- The original road map has the quay edge, mole outlines and the basin drawn as lines; use roads\_vector\_terrain\_2017px.svg as the geometric authority and the masks as a cross-check (they agree within about 1 px).
- Outputs: port\_geometry.json (polylines in world cm), berth\_candidates.json (8 to 16 ship positions).

### A8 Vegetation Density

- Density maps from the weightmaps: olive groves = olive weight x (slope < 12 degrees) x (not within 25 m of roads); steppe = steppe weight; dune grass = sand weight beyond the beach band.
- Exclusions: sea, sebkha, port platform, road surfaces (dilated 15 m), the medina and urban zone.
- Outputs: density\_olive.png, density\_steppe.png, density\_dune.png (8-bit), exclusion\_mask.png.

### A9 Terrain Shader and Lighting (needs A3, A4)

- Terrain fragment shader: blend the 7 layers from the packed weight textures, with height-based blending, a slope-based rock bias and a macro colour variation texture (low-frequency noise at 200 to 800 m). Use normal\_map and ambient\_occlusion for macro shading. Triplanar only on steep rock; planar elsewhere.
- Detail textures tile in metres (suggest 4 m for ground, 12 m for rock) and fade to the baked albedo\_preview colour at far range, so the distant view matches the reference.
- Colour targets: olive groves dark green-grey, steppe straw-yellow, ochre soil warm red-brown, rock pale limestone, sand pale cream, salt near-white.
- Lighting: one directional sun from the south-west to west (azimuth about 250 degrees, elevation 35 to 45 degrees), hemispheric sky ambient, exponential fog tinted to the horizon, cascaded shadow maps and optional SSAO. Fixed exposure for QA.
- Acceptance: a far-camera screenshot has SSIM >= 0.80 against albedo\_preview\_with\_roads\_2017.png.

### A10 Road Renderer (needs A3, A5, A4)

- Option A (recommended): road ribbon meshes from road\_graph.json, as triangle strips along each centreline with the width from the graph. Drape every vertex on the CPU height sampler plus 0.05 m and use a depth bias to avoid z-fighting; subdivide so no segment exceeds 5 m. Plazas, quays and the junction near the port come from road\_polygons.json (earcut triangulation on a fine grid).
- Option B (fastest): draw the road layer in the terrain shader. Sample roads\_surface\_mask\_4096 by terrain uv (u = px / 2016) and blend macadam over the ground; add roads\_vector\_raster\_16x\_crop\_RGBA for edge lines inside the footprint. No geometry, but no 3D kerbs.
- Materials: macadam for main roads, compacted earth for tracks, pale kerb strips. Blend edges with the road weight so they are not hard.
- Do not alter the terrain height. It already carries a 1.6 m minimum under roads.
- Acceptance: probe 1,000 random points on the graph; the mesh is within 0.3 m of the ground; no vertex below y = 0.5 m.

### A11 Port and Urban Placement (needs A3, A5, A7, A2)

- Port: quay walls along the quay polyline, bollards, cranes and warehouses allowed by era\_rules.json, 8 to 16 ships at berth\_candidates.json (steamships, coasters, fishing and sailing boats).
- Medina: the block cluster west of the basin. Its approximate bounds in terrain px are x 1233 to 1306, y 954 to 1052 (about 680 m x 910 m). This is a first estimate from the road map; A5 should refine it by the block pattern. Dense, low, whitewashed buildings, 1 to 3 floors.
- Ville nouvelle: larger blocks in the north and west of the network; ochre and terracotta roofs, 2 to 4 floors, wider streets (already supplied by the roads).
- Building footprints: take the complement of the road mask inside the urban zone, split by block, inset by 3 m, and fit lot rectangles. No building overlaps a road, the sea or the basin.
- Landmarks (ribat, kasbah, walls) only if A2 verified their positions; otherwise leave placeholders.
- Acceptance: zero overlaps (footprint vs road, sea, basin); building count and density reported per block.

### A12 Water Renderer (needs A3, A6)

- Sea: one large quad or clipmap at y = 0 that extends past the terrain edge. Shader: colour from depth (terrain height 0 to -30 m, turquoise to deep blue), Fresnel, scrolling normal maps, a soft shoreline fade using the height texture, and foam where depth < 1.5 m.
- Port basin: the same shader with smaller wave amplitude.
- The seabed at the east edge is -30 m, so add a distant skirt or fog to hide the drop-off.
- Sebkha: salt-crust ground layer only, no water surface (optionally a thin wet-ground darkening where the mask is 255).
- Acceptance: no water fragment above y = 0; no gap between shore and water; no hard seams in the depth fade.

### A13 Vegetation Instancing (needs A3, A8, A5)

- Scatter on the CPU or in a compute shader from the density maps into per-cell instance buffers, drawn with glDrawElementsInstanced (or indirect draws with GPU culling). Olive grove spacing about 12 to 25 m on a jittered grid (confirm with A2); steppe tufts and dune grass as cross-quads or billboards; palms only in urban gardens, and only if A2 allows.
- The exclusion mask is authoritative. Seed from global coordinates (hash of x, z and the global seed), never tile-local coordinates.
- Budget: an instance cap per 1 km cell, 2 to 3 LODs with an impostor at distance, distance fade, wind in the vertex shader. Report instance counts per cell.
- Acceptance: zero instances inside the exclusion mask; none below y = 0.3 m; identical output on rerun.

### A14 QA (parallel shards)

- Shard by region (8 x 8 tiles of the 2017 grid) and by test. See section 9. Produces qa\_report.json plus annotated screenshots.

## 8. Data-parallel strategy

- **Tiling.** 2017 = 8 x 252 + 1. Use 8 x 8 tiles of 253 px with a shared 1 px border (64 jobs) for height-based work. Merge with an owner rule: a pixel belongs to the tile that contains its centre.
- **Roads.** Tile the 4096 mask in 512 px tiles with a 48 px overlap (64 jobs). Skeletonise per tile, then stitch graph edges at tile borders by endpoint matching (tolerance 2 px). Large plazas are handled in a single global pass.
- **Vegetation.** One job per 1 km cell (107 px). Seed = hash of (x\_m, z\_m, global seed).
- **Idempotence.** Every job writes to a content-addressed path (hash of inputs + parameters + code version). A rerun with unchanged inputs does nothing.
- **Concurrency limits.** Renderer-bound agents (A3, A9, A10, A11, A12, A13) run in your engine process or a headless GL context (EGL or an offscreen window) and share it; data agents (A4 to A8) are CPU-only and can scale out. Never run two agents that write the same output; serialise them through a lock owned by A0.

## 9. QA gates

| Gate | Test | Pass |
| --- | --- | --- |
| G1 Integrity | Sizes, bit depth, sha256, sums of splat weights | All match; sums 255 +/- 3 |
| G2 Height | 20 fixed pixels sampled in-engine vs decoded heightmap | Max error <= 0.1 m |
| G3 Sea level | All sea\_mask pixels have z <= 0.1 m; water plane at Z = 0 | 100% |
| G4 Roads on land | Road mask eroded by 1 px, z >= 1.0 m | >= 99% (list the rest) |
| G5 Roads vs sea/basin | Overlap of eroded road mask with sea\_mask | 0 px |
| G6 Road fidelity | Re-rasterised road graph vs roads\_surface\_mask\_4096 | IoU >= 0.95 |
| G7 Port | Basin polygon vs sea\_mask in the port zone; moles inside quay outline | Dice >= 0.97 |
| G8 Slope sanity | Land median and 95th percentile slope | About 2.2 deg and 5.9 deg, within 10% |
| G9 Seams | Height and weight values at tile borders | Identical |
| G10 Visual | Top-down and port close-up screenshots vs the preview PNGs | SSIM >= 0.80 (port >= 0.85) |
| G11 Era | Asset list vs era\_rules blocklist | 0 violations |
| G12 Scatter | Instances in exclusion mask | 0 |
| G13 Performance | Frame time on the reference machine at the main camera | Within the budget set by the human |
| G14 Determinism | Full rerun hash compare | Identical |

A red gate stops downstream waves. A1 and G3/G4/G5 are hard stops.

## 10. Orchestration contract

Example task card (JSON, one per job):

```json
{
  "id": "A5-road-geometry-tile-03-04",
  "agent": "A5",
  "needs": ["A1"],
  "inputs": {"roads_mask": "roads_surface_mask_4096.png", "tile": [3, 4], "overlap_px": 48},
  "params": {"rdp_tolerance_m": 1.5, "plaza_width_m": 80, "seed": 1930},
  "outputs": ["road_graph_tile_03_04.json"],
  "gates": ["G6"],
  "retry": 2,
  "timeout_s": 900
}
```

Rules: agents report status (queued, running, passed, failed) and write result.json with metrics; the orchestrator reads only result.json. Failures with a transient cause (timeouts, process or GL context crash) are retried; failures from a gate are not. Log every parameter. Keep a human-readable summary at each wave end.

## 11. Known limitations and risks

- **Scale unknown.** The 15 m per pixel is an assumption. Extent, scale and heights should be confirmed by the human before final packaging.
- **Road width.** The map's roads are stylised and wide (about 50 m). Realistic 1930s streets would be 6 to 25 m. Keeping them as drawn is the safe choice; narrowing needs approval.
- **Surface classification.** Road versus block was decided by band width, so some wide blocks near the port may be treated as plaza, and some small block corners are missing.
- **Terrain changes.** The coast near the port and the road causeways were shaped to the roads. The flat grey platform south of the basin keeps its straight left edge (it is the user's own flat patch). A small pale land spike remains at the north-east tip, near the end of the coast road.
- **Placement certainty.** The road map was placed by matching the port basin shape (overlap 95%+) and was checked visually; places far from the port may drift slightly.
- **Not real survey data.** The terrain detail is procedural (fine noise, light erosion) on top of the user's heightmap, not a surveyed DEM.
- **Resolution.** The terrain grid is 9.33 m. Fine detail (kerbs, cobbles) must come from materials, decals and meshes, not from the heightmap.
- **Engine interface.** The plan defines data contracts and renderer behaviour; the clipmap or LOD scheme, shaders and instancing live in your C++ code. Fix the engine interface (height sampler, layer textures, instance buffers) before W1 so every agent codes against it.

## 12. Appendix: reference snippets

```python
import numpy as np

N = 2017
h = np.fromfile('heightmap_2017_16bit.r16', dtype='<u2').reshape(N, N)
z = h / 65535.0 * 130.0 - 30.0               # metres, sea level = 0

M = np.array([[0.2975846517970381, 0.0022952031962425444, 1095.8211137671838],
              [-0.0022952031962425444, 0.2975846517970381, 775.0002406584173]])

def roadmap_to_terrain(x, y):
    return M @ np.array([x, y, 1.0])         # 2017-grid pixels

def terrain_px_to_world_m(px, py):
    return px * 9.3257, py * 9.3257          # x east, z south, from the north-west corner

def road_mask_px_to_terrain_px(mx, my):
    return mx / 2.03075, my / 2.03075
```

```cpp
// CPU height sampler: must match the vertex shader. h = little-endian uint16, N = 2017.
constexpr int N = 2017;
constexpr float MPP = 9.3257f;                  // metres per terrain pixel
float heightMetres(const uint16_t* h, float px, float py) {
    px = std::clamp(px, 0.0f, N - 1.001f);
    py = std::clamp(py, 0.0f, N - 1.001f);
    int x = (int)px, y = (int)py;
    float fx = px - x, fy = py - y;
    auto v = [&](int xx, int yy) { return h[yy * N + xx] / 65535.0f * 130.0f - 30.0f; };
    return (v(x, y) * (1 - fx) + v(x + 1, y) * fx) * (1 - fy)
         + (v(x, y + 1) * (1 - fx) + v(x + 1, y + 1) * fx) * fy;
}
```

```glsl
// Vertex shader: uHeight is GL_R16 (normalised); uv = (px + 0.5) / 2017.
float y = textureLod(uHeight, uv, 0.0).r * 130.0 - 30.0;
vec3 world = vec3(px * 9.3257, y * uVerticalScale, py * 9.3257);
```

Road skeleton sketch for A5: read roads\_surface\_mask\_4096.png, threshold at 128, compute `skimage.morphology.skeletonize`, take `scipy.ndimage.distance_transform_edt` of the mask for widths, build a graph from skeleton pixels (nodes where the neighbour count is not 2), simplify edges with Ramer-Douglas-Peucker, and export polygons for any component wider than 80 m.

First three actions for a new run: (1) A1 verification report, (2) the orientation check in a top-down render, (3) the 20-pixel height sampling test (G2). Do not start W2 before all three are green.
