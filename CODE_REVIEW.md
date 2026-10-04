# Code Review — `fallaga` (RDR2-style Sousse prototype)

**Date:** review of working tree at commit `b516cc7` + uncommitted work (branch `recovery/morning-2026-09-29-1110`)
**Scope:** `src/` (74 files, ~21 400 LOC excluding the vendored `miniaudio.h`), `CMakeLists.txt`, all 30+ root-level test/tool files, build + asset pipeline.
**Method:** full read of every non-vendored source file plus the build logs, git history and asset manifests. Review only — **no source file was modified.**

> Verified sound along the way (so the report is honest about what is *not* broken): the CMake source list matches disk exactly (39/39, no orphans, no missing files); brace/syntax structure of every file is well formed; the car's 120 Hz fixed-step accumulator with render interpolation (`Car.cpp:520-550`, `Car.cpp:602-618`) is the correct standard form; Bullet compound-shape destruction order is right; and `stepSimulation` binds to the intended `const float*` heightfield overload.

---

## 0. Executive summary

This is a **feature-rich, genuinely impressive prototype** — a chunked GPU terrain with 15-layer texture arrays, a full geometric road-graph pipeline with junction topology and spline meshing, a Pacejka-style per-wheel vehicle solver on Bullet collision, skinned character/horse animation, ocean reflection/refraction, and a very complete in-game map editor. The rendering quality work and the domain modelling (Sousse plan → world coordinates, road masks, zone classification) are the strongest parts of the codebase.

The engineering problems are structural, not cosmetic, and they cluster into five themes:

1. **Two sources of truth for terrain height, and a dangling pointer between them.** The rendered surface and the physics surface are separate arrays, and the road pipeline caches a raw pointer into a third vector that is reallocated on heightmap reload. This is the single highest-risk area.
2. **No ownership discipline for GPU and heap resources.** There is not one `glDeleteProgram` in the codebase; `~Terrain` and `~TerrainManager` free a fraction of what they allocate (≈160 MB of texture arrays plus FBOs leak per run); the car's `wheelOffset*` members are read before they are written.
3. **A 1000+ line God object.** `Game::render` is 1546 lines, its immediate-mode editor HUD alone is 1004 lines, `Game::update` is 1242 lines, `RoadMeshBuilder::build` is 2060 lines.
4. **Debug scaffolding has become load-bearing.** 30 `FALLAGA_*` environment variables, a 7-step `exit(1)`-calling test state machine inside `Game::update`, `glReadPixels` probes in the render path, and `python src/save_heightmap.py` shelled out from C++.
5. **Nothing is tested where it matters.** 8 CTest targets exist, none run in any CI (there is no CI), and all of them unit-test isolated pure functions — zero coverage of `Game`, `Terrain`, `RoadSystem`, `Character` or `Car`.

### The five findings to fix first

| # | Finding | Location | Why it matters |
|---|---------|----------|----------------|
| 1 | Noclip while driving soft-locks the game's only car forever | `Character.cpp:618-623` + `Car.cpp:515-518,566` | Hard gameplay dead-end; needs a restart |
| 2 | `wheelOffset*` read uninitialized when wheel detection fails | `Car.h:206`, `Car.cpp:194-197` vs `104-166` | UB → NaN physics, car teleports |
| 3 | Dangling `heights` pointer after heightmap hot-reload | `Terrain.cpp:359`, `TerrainManager.cpp:131,270` | Use-after-free, corrupts road mesh |
| 4 | Smooth brush moves the mesh but not the collision surface | `TerrainManager.h:69`, `TerrainManager.cpp:841` | Characters/cars float or sink by the full brush delta |
| 5 | Unbounded index into `liveLandmarks` | `Game.cpp:627-629` | Heap write outside the vector |

---

## 1. CRITICAL — correctness

### C1. Noclip while driving/entering permanently wedges the car
`Character::toggleNoclip` clears its own state but never notifies the car:

```cpp
// Character.cpp:618-623
if (m_isDriving) { m_isDriving = false; m_MountedCar = nullptr; }
m_isEnteringCar = false;
m_isExitingCar = false;
```

`Car::m_Driver` remains set and `state` stays `DRIVEN`, so `Car::update` early-returns on every subsequent frame:

```cpp
// Car.cpp:515-518
if (state == State::DRIVEN && m_Driver && !m_Driver->isDriving()) {
  physicsAccumulator = 0.0f;
  return;
}
```

and remounting is permanently blocked:

```cpp
// Car.cpp:566
if (!c || m_Driver) return;
```

The game has exactly one car (`Game.cpp:101`, constructed `PARKED`), so this is a permanent gameplay dead-end, and `m_Driver` becomes a dangling `Character*` once the character is destroyed. The same wedge is reachable from mid-entry (`m_isEnteringCar` is only cleared locally). **Fix:** route noclip through `Car::dismountCharacter(this)` instead of clearing flags by hand.

### C2. `wheelOffset*` are read before they are written
```cpp
// Car.h:206  — no initializer
glm::vec3 wheelOffsetFL, wheelOffsetFR, wheelOffsetBL, wheelOffsetBR;
```
They are assigned only inside `if (wheelInfos.size() >= 4)` (`Car.cpp:162-165`), but read unconditionally 30 lines later:
```cpp
// Car.cpp:194-197
wheels[0].localPos = wheelOffsetFL;
```
The detection above is heuristic (name tokens `"weel"/"wheel"/"body"`, else the 4 smallest AABB volumes, `Car.cpp:64-101`), and `meshFL = meshBL = -1` when it fails — which also means the `if (meshFL >= 0 && meshBL >= 0)` guard at `Car.cpp:168` cannot save you. Indeterminate positions feed `calculateTireForces`, so the failure mode is NaN forces and a teleporting chassis rather than a clean fallback to a default wheelbase. **Fix:** initialize to `glm::vec3(0.0f)` and fall back explicitly when detection fails.

### C3. Dangling `heights` pointer (use-after-free) in the road pipeline
```cpp
// Terrain.cpp:359
roadHeightSampler.init(mapper, terrainManager->getHeightsData(), gridW, gridH);
```
`HeightSampler` stores the pointer raw (`RoadSystem.h:83 m_heights`, `RoadSystem.cpp:58`), and `TerrainManager::loadHeightmap` reallocates the underlying vector:
```cpp
// TerrainManager.cpp:131
heights.resize(w * h);
```
Reload is triggered automatically every 60 frames whenever `heightmap.png`'s mtime changes (`TerrainManager.cpp:262-270`) — which is exactly the workflow this project uses, since 39 tool scripts rewrite that file. Nothing re-runs `roadHeightSampler.init` afterwards, so every subsequent `RoadSpline::evaluate` / `HeightSampler::sample` / deformation query reads freed memory. **Fix:** pass the vector by reference or store a generation counter and invalidate the sampler on reload.

### C4. Rendered terrain and collision terrain diverge
`getHeightAt` reads only the physics array (`TerrainManager.cpp:451 const std::vector<float>* src = &physicsVisualHeights;`), but the smooth brush writes only the visual array unless a flag is set (`TerrainManager.cpp:841`). That flag defaults to `false` (`TerrainManager.h:69`) and **`setVisualSmoothingAffectsPhysics` is never called anywhere in `src/`**. So smoothing visibly reshapes the terrain while `getHeight`/`getGroundSurfaceHeight`, car collision, wall grounding and foliage placement all keep the old height. Secondary: `getHeightAt` bilinearly samples the grid while the mesh is triangle-interpolated (`TerrainChunk.cpp:74-75`), so even in sync they disagree by up to the half-cell diagonal.

### C5. Unbounded index into `liveLandmarks`
```cpp
// Game.cpp:627-629
else if (selectedPlacedIdx != -1 && mouseX >= rpX && ...) {
    auto& landmark = SousseMapData::liveLandmarks[selectedPlacedIdx];
```
Only the lower bound is checked, unlike the guarded render path at `Game.cpp:2591`. The next lines *write* through that reference (`landmark.yOffset = ...`, `Game.cpp:635`), so a stale index is a heap write outside the vector. `selectedPlacedIdx` is a public mutable field (`Game.h:24`) written from several places.

### C6. Out-of-bounds heap write into `bareAt`
```cpp
// RoadSystem.cpp:2549
bareAt[contact.host][contact.hostSample] = 1;   // no bounds check
```
`hostSample` is captured in step 0 (`RoadSystem.cpp:1964`, stored at `1996`), but a later iteration of the same loop re-evaluates that host (`RoadSystem.cpp:1988 RoadSpline::evaluate(seg, sampler, 0.5f)`), which can change its sample count. `bareAt[host]` is sized from the **final** count at `RoadSystem.cpp:2295`. A host that shrinks leaves a stale index → heap corruption. The identical index is bounds-guarded at `RoadSystem.cpp:3432` and `2539-2541`, so the guard is missing in exactly one place.

### C7. Uninitialized `ma_sound` objects are `uninit`'d and deleted
```cpp
// AudioSystem.cpp:33-54
ma_sound* sIdle = new ma_sound;         // indeterminate
if (ma_sound_init_from_file(engine, "...", ..., sIdle) != MA_SUCCESS)
    std::cerr << "[AudioSystem] Failed to load engine_idle.mp3" << std::endl;
```
Load failure is logged and ignored, and `m_initialized` is set true regardless. `cleanup()` then calls `ma_sound_uninit` on objects that were never initialized (`AudioSystem.cpp:91-95`) and `delete`s them. Add `{}` value-initialization and track per-sound success.

### C8. `exit(1)` inside the game update path
```cpp
// Game.cpp:1223, 1235, 1266
} else if (now - stateTimer > 5.0) {
    std::cerr << "[TEST] ERROR: Character failed to start entering the car!\n";
    exit(1);
}
```
A 7-state scripted test harness lives inside `Game::update()` (`Game.cpp:1180-1340`), guarded by `FALLAGA_TEST_MODE`. It terminates the process outright, skipping all cleanup and any save. Test scaffolding does not belong on the production update path.

---

## 2. HIGH — resource management

**H1. Not a single `glDeleteProgram` exists in the codebase.** Every `new Shader(...)` leaks a linked GL program when its owner is destroyed: `Terrain.cpp:340-343` (4 programs), `TerrainManager.cpp:24`, `Character.cpp:28`, `Horse.cpp:43`, `Car.cpp:268`, `Sky.cpp:48`. `Shader` has no destructor and a copy constructor that would double-free the same `ID` if it were ever copied.

**H2. `~TerrainManager` frees one of ~10 allocations** (`TerrainManager.cpp:99-103` deletes only `roadGeometryMask`). Leaked: `heightmapTexID`, `terrainLayerArray` + `terrainNormalArray` (15 slices × 1024² RGBA + mips ≈ **160 MB per run**), `portMask`, `albedoTex`, `splatmap2Tex`, `roadMask`.

**H3. `~Terrain` leaks every water texture and the FBO set** (`Terrain.cpp:575-620`): `waterMapTex`, `flatNormalTex`, `normalMap1/2`, `foamTex`, `mapTexture`, plus `reflFBO`/`refrFBO` and their attachments created in `buildWaterFBOs`. Separately, the wall rebuild path releases `wallVBO`/`wallEBO` without zeroing them (`Terrain.cpp:1105-1110`), so `if (wallIndexCount > 0)` re-`glGen`s them on every `batchesDirty` rebuild.

**H4. `assetCache` is never emptied.** `Terrain.cpp:569-570, 1330, 1438, 1497` insert `new ObjModel(...)` into `std::map<std::string, ObjModel*> assetCache`; no destructor code deletes them.

**H5. `Character::initWeapons` leaks its two allocations** — but only if it were ever called, which it is not (see A5).

**H6. `RoadMeshBuilder::upload()` leaks on an empty rebuild** — it early-returns at `RoadSystem.cpp:3814` leaving the previous VAO/VBO/EBO allocated; `destroy()` is the only path that frees them. `glGen*` failures are unchecked (`3825-3827`).

**H7. `glGenQueries(4, ...)` without `glDeleteQueries`** (`Game.cpp:1452`).

**H8. `buildWaterMesh` binds `GL_ELEMENT_ARRAY_BUFFER` with no VAO bound** (`Terrain.cpp:735-741`) — the EBO attaches to whatever VAO happens to be current.

---

## 3. HIGH — robustness and silent failure

- **`glDisable(GL_CULL_FACE)` with no matching enable.** `Game.cpp:1664` and `TerrainManager.cpp:345` disable culling; `glEnable(GL_CULL_FACE)` appears **nowhere** in the project. All geometry renders double-sided, roughly doubling fragment work on terrain and roads.
- **Shader compile/link failure is non-fatal.** `Shader::checkCompileErrors` prints and returns; a failed program is still returned and used, and every `setXxx` silently no-ops on location `-1`.
- **Texture load failure is unrecoverable.** `roadRefLoaded = true` is set *before* `stbi_load` (`Game.cpp:1570-1615`), so a failed load leaves the flag set, the stale texture bound, and the UI claiming a loaded file.
- **Unchecked binary reads.** `foliage.bin` / `road_mesh.bin` parsing ignores every `fread` count (`Terrain.cpp:757-769`, `889-901`); a truncated file feeds garbage into `resize()`.
- **Unvalidated JSON geometry.** `RoadSystem.cpp:580-587` range-checks `corner` but not `width`/`shoulder` — a zero or negative width produces inverted, self-crossing ribbons. Duplicate `id`s are never detected, which makes the road-priority comparison false in both directions and produces z-fighting.
- **`loadFromJSON` mutates then reports failure.** `segments = std::move(loaded)` runs before `return !segments.empty();` (`RoadSystem.cpp:689-694`), so a valid file with an empty `roads` array wipes the graph *and* returns false.
- **Sticky, irreversible auto-close persisted to disk.** `seg.closed` latches true when a road merely passes near its own start (`RoadSystem.cpp:844-847`) and is written to JSON at `712`; a later endpoint edit cannot undo it.
- **Gap splicing desynchronises every consumer.** `RoadSystem.cpp:2103-2114` rewrites only the local copy's `splinePoints`; `cumulativeDist`, `tangents`, `rightVecs` and `surfaceBounds` stay stale, and `RoadDeformer`, `RoadTileField`, `Terrain::getRoadAt` and the saved JSON all see the un-spliced road. The rendered ribbon extends up to `kMaxRoadGapSplice = 150 m` past what collision knows about, as a single quad, with a raw DEM sample at the far end outside the grade profile.
- **`connectRoads` unit mismatch**: `t * len2 <= tolerance * tolerance` (`Terrain.cpp:2556-2557`) compares a length against a squared length.
- **Silent truncation**: `render()` drops strokes/contacts past 32 (`RoadSystem.cpp:3880-3890`); crossings past `kMaxContacts = 40000` are discarded (`2341`) with no diagnostic; `kMaxHosts = 8` (`2312`) silently ignores lower-ranked hosts.
- **Unity build hygiene**: `fixWorkingDirectory()` (`main.cpp:78-95`) `_chdir`s up to 5 levels looking for `assets`, and on failure prints `CRITICAL` and **continues anyway** — every asset path is then a silently failing relative literal. Running the exe from the wrong directory produces a game with no terrain rather than an error.
- **No shadows at all.** A grep for `shadow|lightSpaceMatrix|depthMap` over `src/` finds only the ocean's refraction depth sampler. The lighting model is a single directional term.

---

## 4. HIGH — performance

**P1. Frame cost is dominated by unoptimised draw volume.** At the shipped 4097 visual grid there are 32×32 = **1024 chunks × 32 768 triangles**; roughly 20×20 chunks fall inside the 6 km draw distance ≈ **14 M triangles/frame** at uniform density. There is **no LOD and no stitching** — "lod" and "stitch" do not appear as identifiers anywhere in the tree. Distance culling (`TerrainManager.cpp:393-435`) only switches 6000 ↔ 2500 m. Also no frustum culling of terrain chunks (the frustum code exists — `Camera.cpp:334-359` — and is applied to instanced objects only).

**P2. Chunks re-create GL objects on every brush frame.** `TerrainChunk::generateMesh` deletes and re-`glGen`s VAO/VBO/EBO each call (`TerrainChunk.cpp:82-88`), and sculpt/conform/paint call it per frame. It also rebuilds and frees a 544 KB `std::vector<Vertex>` per chunk per frame. `glBufferSubData` in place would remove all of this driver churn.

**P3. Road rebuild runs the whole pipeline per drag frame, twice.** Dragging a control point sets `roadGraphDirty` (`Game.cpp:1046-1050`) → `rebuildRoadMesh()` → `RoadMeshBuilder::build` **twice** (`Terrain.cpp:2362` and `2414`) with `upload()` twice, plus `RoadDeformer::build` over the visual grid and `RoadTileField::build`. `build()` takes `std::vector<RoadSegment>` **by value** (`RoadSystem.h:355`), i.e. a deep copy of five parallel per-segment vectors, twice per frame. A 1 Hz JSON write is layered on top.

**P4. Full-grid work per brush frame.** `updateGPUHeightmapTexture` allocates and normalises the entire 8129² grid (`TerrainManager.cpp:1315-1322`) and runs at the end of every sculpt/conform; `paint()` re-uploads the whole RGBA road mask (`1004-1005`); `erode()` allocates 2–3 `std::vector`s **per droplet** inside the lifetime loop (`1221-1225`), ≈500 k allocations/frame at 1–1.5 k droplets.

**P5. O(n) road queries in the wheel loop.** `Terrain::getGroundSurfaceHeight` falls back to scanning every segment × every edge (`Terrain.cpp:2092-2115`) — the spatial index exists (`2081-2089`) but only helps once built. `CarPhysics` calls `terrain->getRoadAt` and `terrain->getZoneAt` **per wheel per physics step** (`CarPhysics.cpp:163-165`), and `getZoneAt` runs four point-in-polygon tests plus a `getHeight` (`Terrain.cpp:2265-2295`).

**P6. Per-frame allocations and O(n²) scans.**
- `VehicleBody::ensureTerrainTiles` allocates a vector of tiles and sorts it **every frame** (`VehicleBody.cpp:159-176`), plus up to 256 terrain samples, plus a fresh `std::function` per frame (`Car.cpp:506-508`).
- `emitRing` linearly scans **all** branch contacts for every ring of every segment → O(rings × T-joins) per build, twice per drag frame (`RoadSystem.cpp:2939-2949`).
- `Game.cpp:1722` allocates `std::vector<float> gH(33*33)`, issues 1089 `getHeight` calls and 1024 immediate-mode quads **every frame** in road mode, even when nothing changed.
- `Character::render` builds `std::string`s and runs `std::transform(..., ::tolower)` per mesh per frame (`Character.cpp:580-584`).

**P7. `Shader` uniform setters pass `const std::string&` and call `glGetUniformLocation` every time** (`Shader.h:16-27, 97-111`). Every uniform write constructs a `std::string` from a `const char*` literal and performs a GL string lookup. `TerrainManager::render` alone issues ~10 per frame; the editor HUD issues hundreds. Cache locations in a map keyed once.

**P8. Immediate-mode OpenGL is still the primary geometry path for UI and editor overlays** — **184** `glBegin`/`glVertex3f`/`glMatrixMode`/`glRasterPos` call sites, concentrated in `Game.cpp:1691-1897, 1947-1998, 2859-2900`, `Terrain.cpp:1770-1879` and `ObjectModel.cpp:429-666`. This forces `GLFW_OPENGL_COMPAT_PROFILE` (`main.cpp:106`), which caps the engine at OpenGL 3.3 compatibility with no path to core profile.

**P9. The performance numbers in the log cannot be trusted.** `game_output.log` (stale, 2026-09-20) shows a metronomic 16.68–16.71 ms across 36 samples — that is a **vsync lock at 60 FPS**, not a measurement of headroom. It tells you nothing about where the time goes, because a capped frame is spent in `Swp`. The profiler also prints two always-zero fields (`waterStepMs`/`waterUploadMs` are never written, `Game.cpp:144-145, 1369-1370`).

### P1 and P9 corrected by measurement

The claim above that terrain costs ~14 M triangles/frame with **no LOD and no culling** is **half wrong**, and the profiling numbers are misleading. Measured with the built-in GPU timer (`FALLAGA_RENDER_PERF=1`) plus the culling counter:

| scenario | chunks drawn | CPU render | **GPU** |
|---|---|---|---|
| ground level, in the old town | **5 / 81** | 0.49 ms | **88–95 ms** |
| 300 m up, pitched 60° down | **2 / 81** | 0.40 ms | **34–44 ms** |
| 1200 m up, 85° down (most terrain coverage) | **2 / 81** | 0.46 ms | **32–40 ms** |

- **Distance, AABB-vs-frustum and clip-plane culling are all implemented and working** (`TerrainManager.cpp:412-456`). Draw count is small in every view tested, including the most terrain-heavy one.
- **Terrain CPU submission is under 0.5 ms**, so triangle count is not the problem at any tested camera.
- The frame is **GPU-bound at 30–95 ms**, and the worst case is *ground level*, where the least terrain is on screen. So the cost is not terrain area at all. Likely consumers are the model/foliage passes (alpha-blended vegetation — 5 865 trees and 32 746 grass patches in the scene) and the water/reflection pass.

**So the "no LOD / 14 M triangles" item was mis-diagnosed.** LOD would not fix a frame that already submits <0.5 ms of CPU work and 2–5 chunks. The correct next step is per-phase **GPU** timing to attribute the 88–95 ms, not a terrain LOD scheme.

**LOD is genuinely absent**, which matters for visual density at the 2500–6000 m draw distances, but it is not the frame-time fix.


---

## 5. HIGH — architecture and maintainability

**A1. God object, measured.** `Game` owns terrain, roads, water, editor, HUD, input, save/load, a scripted test harness and the camera.

| Function | Lines | Extent |
|---|---|---|
| `Game::render` | 1546 | `Game.cpp:1442-2987` |
| ↳ of which the immediate-mode editor HUD | **1004** | `Game.cpp:1945-2948` |
| `Game::update` | 1242 | `Game.cpp:134-1375` |
| `Game::keyDown` | 420 | `Game.cpp:3202-3621` |
| `RoadMeshBuilder::build` | **2060** | `RoadSystem.cpp:1752-3811` |
| `Terrain::renderOpaque` | 1032 | `Terrain.cpp:856-1888` |
| `Terrain::Terrain` (constructor) | 265 | `Terrain.cpp:308-573` |
| `TerrainManager::conformTerrainToRoadMesh` | 240 | `TerrainManager.cpp:531-770` |
| `Character::update` | 328 | `Character.cpp:128-455` |
| `Car::updateActivePhysics` | 215 | `Car.cpp:355-569` |
| `Car::resolveWheelMeshes` | 182 | `Car.cpp:31-212` |
| `TerrainManager::erode` | 184 | `TerrainManager.cpp:1083-1266` |

The editor HUD is hundreds of hardcoded pixel offsets built from string concatenation per frame (`"Page " + std::to_string(...)` at `Game.cpp:2087, 2689`; `ostringstream` at `2285, 2294, 2342, 2381, 2766, 2804`).

**A2. Three competing sources of truth for world geometry.** Terrain height lives in `heights`, `visualHeights` and `physicsVisualHeights`; the road pipeline, the deformer, the tile field and the CPU collision all read different combinations of them (see C3, C4, H-robustness). Water level has three definitions: `Game.h:36 waterLevelAdjust = -55.0f`, the UI label `"Reset to -55.0m"` (`Game.cpp:2227`) and `WaterLevelConfig::RESET_VALUE`.

**A3. Duplicated logic.** Frustum-plane extraction is copy-pasted (`Terrain.cpp:862-878` ≡ `TerrainManager.cpp:374-389`); bilinear sampling has 5+ hand-rolled copies; the 10-line quad emission at `RoadSystem.cpp:3031-3040` and `3059-3068` is byte-identical; `getDriverOutsideWorldPosition` ≡ `getDriverExitWorldPosition` (`Car.h:143-148`); the horse's terrain-alignment block is copy-pasted (`Horse.cpp:85-116` ≡ `182-213`); the car's initial snap exists twice (`Car.cpp:452-500` vs `317-331`) and only one can ever run; `updatePlayerArcadePhysics` is a pure forwarder that the dispatch reaches twice (`Car.cpp:541-547`).

**A4. Duplicated mutable page state.** `static int assetPage` in `update()` (`Game.cpp:260`) is never modified by the page buttons drawn in `render()`, which use their own `static int assetPage` (`2054`) — clicking "Next Page" repaints a different list than the one that scrolls. Same for textures (`674` vs `2660`). The reset-texture button and the pager also share the same `resetY` hit band (`694-714`), so one click does both.

**A5. Dead subsystems compiled into the binary.**
- **Weapons: entirely unwired.** `Character::initWeapons()` has **no caller**, so `weaponManager`/`weaponShader` are always null and `Character.cpp:382-414, 554-604, 718-722` plus the `RifleAimingIdle`/reload animation setup are unreachable.
- **`VehicleDynamics.cpp` is a second, unused vehicle physics model.** It is compiled (`CMakeLists.txt:46`) but `stepVehicleDynamics` is called only from `vehicle_dynamics_test.cpp`. It carries different Pacejka constants (`VehicleDynamics.cpp:10-19`) from the live solver (`CarPhysics.cpp:26-32`), and the config fields `brakeForceN`, `handbrakeForceN`, `rollingResistance`, `centerOfMassHeightM`, `frontWeightShare` are read **only** by it — i.e. inert knobs in `renault_40cv.json`.
- **`terrain/TerrainSystem.cpp` and `terrain/Heightfield.cpp` are stubs** (`TerrainSystem.cpp:29-37` are `(void)`-parameter no-ops) compiled in via `CMakeLists.txt:55-56`, duplicating heightmap loading that `TerrainManager` already does.
- Also dead: `appendCornerArc` (`RoadSystem.cpp:1699-1732`), `computeParallelFrames` (`44-48`), `pixelSizeX/Z`, `seg.curvature`, `ClusterArm::station`, `widestType` (overwritten at `3234-3238`), `Terrain::initStreets`/`distToSegment`/`printDebugMap`, `Game.cpp:1377-1441` (a fully commented-out previous `render()`), and ~30 dead members (`Car.h:175-176, 194-195, 218, 222-225`; `Character.h:34` `currentYAngle` is uninitialized and never read; `m_AnimYOffsets` is written and never read).

**A6. Debug scaffolding in production paths.** 30 `FALLAGA_*` environment variables steer shipped code, several inside hot loops: `FALLAGA_ROADGAP` gates a full 16 M-float signature walk (`TerrainManager.cpp:1330`), `FALLAGA_PROBE` gates `glReadPixels` scanlines inside `renderOpaque` (`Terrain.cpp:1038-1056`), `FALLAGA_PROBE_NOMESH`, `FALLAGA_EDGEONLY`, `FALLAGA_DEBUGVIEW`, `FALLAGA_CONFORM_SKIRT`, `FALLAGA_TEST_MODE`. `saveTerrain` shells out to `python src/save_heightmap.py` (`TerrainManager.cpp:1074`) rather than writing the image in-process. ~26 `std::cout` writes fire from interactive code paths.

**A7. Model/hardcoded coupling.** `Car::resolveWheelMeshes` identifies wheels by substring matching names against `"weel"/"wheel"/"body"` and guesses the model's axis convention from AABB spread (`Car.cpp:64-126`), with a silent radius fallback (`186`). Any asset swap breaks the car silently. Constants are duplicated across files: `CHUNK_SIZE = 129` (`TerrainManager.h:85`) vs `129` in `TerrainChunk`; tile size 64 m derived at `VehicleBody.cpp:130` and redeclared at `224`; world bounds ±13938.0f at `Game.cpp:108` **and** `299`.

---

## 6. MEDIUM

- **No `deltaTime` clamp anywhere.** `Game.cpp:163` feeds the raw frame delta straight into character movement (`Character.cpp:319-328`), horse gravity, sculpt and paint. Alt-tab, a window drag or a debugger breakpoint produces a multi-second step. The car is the only system that bounds catch-up (`CarPhysics.cpp:522-526`).
- **Inconsistent gravity by 10×.** `PhysicsSystem.h:9 GRAVITY = -98.1f` (character/horse) vs `Car.h:215 GRAVITY = -9.81f` and `VehicleBody.cpp:58 setGravity(0,-9.81,0)`. On a horse, gravity is applied **twice per frame** (`Game.cpp:181` and `Character.cpp:166` → `Horse.cpp:83` and `180`).
- **`windowWidth/windowHeight` never updated on resize.** Set once in `Game::setup` (`Game.cpp:126-128`); `main.cpp:629-634` only calls `glViewport`. Every HUD coordinate, UI hit-test and raycast normalisation goes stale after any resize, and picking is offset whenever framebuffer size ≠ window size.
- **Unconditional full-screen blit every frame** (`Game.cpp:1932-1938`), even when water is not visible; if the default FBO is multisampled, `GL_LINEAR` on a multisample read is `GL_INVALID_OPERATION` and the refraction copy silently fails.
- **Main-thread file I/O in the frame loop.** `saveProjRefMap()` (read + backup + write) is called from `update()` (`Game.cpp:175-177`, `3001-3042`), and the 1 Hz road JSON autosave fires during drags.
- **`Level` clamping**: `MAX_VISUAL_RES = 4097` assumes a square heightmap (`TerrainManager.cpp:48-51`); non-square DEMs are stretched. `m_cellSize` still reports the requested value when the `maxResolution` clamp bites (`RoadSystem.cpp:1544-1552`).
- **`upload()` / `glGen*` failure unchecked**; `render()` silently truncates junction data past 32 strokes.
- **`getGroundSurfaceHeight` uses `std::max(terrainY, roadY)`** (`Terrain.cpp:2078`), so a road cut *below* terrain cannot be represented.
- **`HeightSampler` and `RoadDeformer` hold raw pointers into heightfields** (`RoadSystem.h:83`) — the same class of bug as C3.
- **`buildTopology` miscounts**: a segment with both endpoints in one cluster is pushed twice, so `connectedSegments.size() >= 3` reports a false junction (`RoadSystem.cpp:784`); node elevation is hardcoded `0.0f` (`790`).
- **Ride-height logic fragmented across three paths** (`Car.cpp:317-348` PARKED, `543-544` DRIVEN, `546` other) with the parked path bypassing the suspension solver entirely.
- **Horse saddle offset applied and then wiped the next frame** (`Character.cpp:177` vs `Horse.cpp:222`), and it disagrees with `Horse.cpp:232` (0.7 vs 0.3).
- **UI numeric bugs**: `std::to_string((int)seg.width)` truncates 8.5 m to `"8"` (`Game.cpp:2477`); `to_string((int)(seg.cornerRadius*2+0.5)/2)` always prints whole metres (`2528`).
- **Editor mode does not consume WASD**, so the player walks while editing (`Game.cpp:3566-3570`).
- **`glDisable(GL_CULL_FACE)` / GL state mutated without save-restore** in `RoadMeshBuilder::render` (`RoadSystem.cpp:3921-3938`).

---

## 7. LOW

- `VehicleBody::step` calls `world->stepSimulation(dt, 0, dt)` (`VehicleBody.cpp:269`) — `maxSubSteps = 0` disables Bullet's internal sub-stepping, making the collision step frame-rate dependent; the rest of the car uses a fixed 120 Hz accumulator, so the two disagree.
- `Horse.h:32` uses a qualified name inside the class (`glm::vec3 Horse::getHorizontalForward()`); MSVC accepts it (C4596), GCC/Clang reject it.
- Unguarded `camera->getForward()` where sibling call sites null-check (`Character.cpp:267`, `Horse.cpp:133`).
- Solver foot-guns: a ±0.05 rad/s dead zone can lock a wheel (`CarPhysics.cpp:203`); the brake-lock test `T_brake > std::abs(T_drive)` (`225`) can hold a wheel locked with nonzero drive torque; rolling resistance is unconditional (`198`).
- Comment/value drift: `RoadSystem.cpp:1455` says "1 cm depth-safe surface bias" but `Z_BIAS = 0.035f` (`1778`); `3362` says "2 cm below" vs `kRoadBedClearance = 0.04f`; `Game.cpp:628` logs "Speed: 40.0 / 150.0" while the code uses 120/500 (`Character.cpp:301-302`); `Character.cpp:243` says "2.04m" vs −1.40 m. Shader stripe offset `u = 0.085` vs geometry band centre 0.080 (`RoadSystem.h:107` vs `1798-1811`) — a systematic ~5 cm marking offset.
- Near-180° interior corners degenerate: `arcRadius = r/tan(θ/2) → 0` collapses the fillet to a point (`RoadSystem.cpp:929-941`).
- `static ObjModel* sharedCarModel` + `carInstanceCount` refcount has no guard if construction throws (`Car.cpp:27-29, 296-304`); every `Car` builds its own `btDiscreteDynamicsWorld` rather than sharing one.
- `snprintf`/`getenv`/`sscanf` deprecation warnings (C4996) are pervasive; `std::getenv` is used without `<cstdlib>` in `TerrainManager.cpp`.

---

## 8. Build, test and repository health

**8.1 The build currently succeeds; the two build logs on disk are misleading.** `build_errors.txt` (2026-06-15, 204 errors, `fatal error C1003`) and `build_build.log` (2026-10-02, 8 syntax errors in `TerrainManager.cpp`) are both stale — the current `src/Terrain.cpp` and `src/TerrainManager.cpp` parse cleanly (verified brace/structure analysis) and `build/Release/RDR2_Prototype.exe` was produced at 11:07:42, one second after the last source edit. **Recommendation:** delete both logs and stop committing build logs; they cost 70 KB and actively mislead.

**8.2 The build is not reproducible and has no CI.** No `.github/`, no pipeline of any kind. `CMakeLists.txt` requires a hardcoded vcpkg tree at `C:/Users/honco/vcpkg` (the compiler resolved `glm` from `C:\Users\honco\vcpkg\installed\x64-windows\include`), `project()` is still named `RDR2_Prototype`, there is no `install()` target, and only one configuration (`Release`, forced at `CMakeLists.txt:6-8`) is exercised. 51 C4996 warnings are tolerated rather than handled.

**8.3 Eight test targets, zero real coverage.** `car_handling_test`, `vehicle_dynamics_test`, `vehicle_body_test`, `vehicle_config_test`, `vehicle_suspension_test`, `vehicle_assists_test`, `road_bed_random_test` are registered with CTest; nothing runs them automatically. They unit-test isolated pure functions with hand-computed expected values (e.g. `vehicle_suspension_test.cpp`, `vehicle_config_test.cpp`) — that is good practice, but **`Game`, `Terrain`, `TerrainManager`, `RoadSystem`, `Character` and `Car` have no test at all**, and those are where every critical finding in this report lives. Four more test files (`roadfix_test.cpp`, `roadjson_test.cpp`, `roadmesh_test.cpp`, `roadtiles_test.cpp`, ~73 KB) are **not in CMakeLists** and were never built; `vehicle_dynamics_test` covers the one vehicle physics path that ships unused.

**8.4 Test behaviour depends on CWD.** `vehicle_config_test.cpp:8` loads `"../assets/cars/renault_40cv.json"` relative to the build directory, and `car_handling_test` needs `src/shaders` reachable. These are fragile by construction.

**8.5 The repository carries a 1.27 GiB pack for 769 tracked files.** No Git LFS. Largest blobs are raw assets: `mycityWall.fbx` (49 MB), `rock_moss_set_02_nor_gl_4k.exr` (29 MB), `MP40.fbx` (28 MB, and a byte-identical `assets/guns/mp40/.fbx`), four ~27 MB normal maps, and several duplicated 12 MB `hen_dirt_sandy_dry_1_nm.png` copies under different `TerrainMainTextures/` subdirectories. `assets/` alone is **1.85 GB across 583 files**. Migrating binaries to LFS (or a separate assets repo) is worth doing before the history grows further.

**8.6 The working tree is mid-refactor and the branch names say so.** `git status` shows 23 modified files including a 6 259-line churn in `RoadSystem.cpp` (3 701 insertions / 2 558 deletions against a 3 939-line file — effectively a rewrite), uncommitted, on branch `recovery/morning-2026-09-29-1110`, with four other branches (`main`, `recovery/pre-restore-2026-09-29`, `terrain-rebuild`, `remotes/origin/main`) and two stash-shaped commits. This much uncommitted high-risk work in the most complex file is the biggest process risk in the repo.

**8.7 `core.fsmonitor` is broken and pollutes every git command.** Every `git` invocation in this review emitted `error: fsmonitor_ipc__send_query: unspecified error on '.git/fsmonitor--daemon.ipc'`. Fix with `git config core.fsmonitor false` (or restart the daemon).

**8.8 The repository root is a scratch pad.** 34 loose scripts sit at the root — five `recovered_Game*.cpp` variants, five `patch_*.py`/`patch_*.ps1` source-rewriting scripts, `extract_*`, `scan_pdf.py`, `generate_mask.py`, four unbuilt road tests, and ~25 generated data dumps (`coast_vertices.txt` 328 KB, `sea_scan.obj` 720 KB, `terrain_layout_guide.obj` 3.5 MB, `test.png`, `game_screenshot.png`, `screenshot_test.png`). `scratch/` holds a further 115 MB across 71 files. `.gitignore` is 17 KB yet still tracks some of these.

**8.9 The asset pipeline is 60 scripts / 11 587 lines of overlapping one-shot Python.** 39 of them read or write `assets/terrain/heightmap.png`, and at least six different scripts (`build_authentic_historical_heightmap.py`, `rebuild_clean_decoupled_terrain.py`, `apply_exact_user_port.py`, `rebuild_satellite_terrain.py`, `sculpt_photogrammetric_heightmap.py`, `rebuild_sousse_terrain_from_scratch.py`) each contain their own heightmap writer. There is no documented "canonical" generation order, so reproducibility depends on remembering which script to run last — which is exactly why `TerrainManager` grew mtime-based hot-reload and why C3 (dangling pointer on reload) is reachable in normal use.

**8.10 Asset references are broken in places.** MTL files reference 12 textures that do not exist on disk (four `citywallsArray.mtl` PBR maps, `mount.blend1.mtl`'s `ground_grass_3264_4062_Small.jpg`, and seven `rock_moss_set_0*.mtl` files all pointing at `rock_moss_set_02_nor_gl_4k.exr`). `game_output.log` also shows `ERROR: Animation file assets/character/rideIdle.fbx contains no animation data` — the `rideIdle` animation is loaded at `Character.cpp:37` but the file has none.

**8.11 Documentation is scattered and partly stale.** Six overlapping markdown files at the root (`REFACTORING_SUMMARY.md`, `TERRAIN_TEXTURE_README.md`, `TEXTURE_BEFORE_AFTER.md`, `TEXTURE_SYSTEM_ARCHITECTURE.md`, `TEXTURE_SYSTEM_CHANGES.md`, `TEXTURE_SYSTEM_QUICKSTART.md`, `CAR_DRIVING_FIX_PLAN.md`), while `README.md` is 24 bytes of UTF-16LE containing only `# fallaga` (the tooling reads it as binary) and there is no `CLAUDE.md` (`.claude/` holds only `settings.local.json`). `REFACTORING_SUMMARY.md` claims clean compilation and zero duplicate code; both claims are contradicted by the current tree.

---

## 9. Suggested order of work

**Phase 1 — stop the bleeding (correctness, low risk, high payoff)**
1. Fix the noclip soft-lock (C1) — route through `Car::dismountCharacter`.
2. Initialize `wheelOffset*` and add an explicit fallback (C2).
3. Bounds-check `liveLandmarks[selectedPlacedIdx]` (C5) and `bareAt[host][hostSample]` (C6).
4. Decide the smoothing/physics question (C4): either call `setVisualSmoothingAffectsPhysics(true)` where smoothing is applied, or delete the flag and the second array. This is a design decision, not a patch.
5. Invalidate or re-`init` `roadHeightSampler` on heightmap reload (C3).
6. Remove `exit(1)` from `Game::update` and move the scripted harness out of the production path (C8).
7. Clamp `deltaTime` (Section 6, first item) and reconcile the 10× gravity discrepancy.

**Phase 2 — ownership and leaks**
8. Give `Shader` a destructor calling `glDeleteProgram`, and delete it (non-copyable).
9. Complete `~Terrain` and `~TerrainManager` (H2, H3, H4) — verify with a GL debug callback or `glGetIntegerv(GL_TEXTURE_BINDING_2D)` accounting at exit.
10. Value-initialize the `ma_sound` objects (C7) and track per-sound init success.

**Phase 3 — performance, in order of measured return**
11. Add a proper LOD scheme (at minimum, vertex spacing by distance band) plus frustum culling of terrain chunks (P1). This is the only change that addresses the 14 M-triangle frame.
12. Convert chunk regeneration to `glBufferSubData` (P2).
13. Cache terrain heights for the draped reference map and rebuild on dirt only (P6).
14. Stop re-uploading the full heightmap texture and full road mask per brush frame (P4).
15. Cache shader uniform locations (P7).

**Phase 4 — structure**
16. Carve `Game` into `EditorHud`, `RoadEditor`, `InputRouter`, `SaveSystem`; delete the commented-out `render()` (`Game.cpp:1377-1441`).
17. Delete the dead subsystems: the weapon path, `VehicleDynamics`, `terrain/TerrainSystem` + `Heightfield`, the orphan road test files, the root scratch scripts.
18. Add CI that builds and runs `ctest`, and add integration tests for the car enter/exit state machine and road-graph round-trip — the two areas with the most critical findings and zero coverage today.
19. Move `assets/` to Git LFS and commit the in-flight `RoadSystem.cpp` work on a branch with a real description.

---

## Appendix — severity index

| Sev | Count | Representative locations |
|---|---|---|
| CRITICAL | 8 | `Character.cpp:618`, `Car.h:206`, `Terrain.cpp:359`, `TerrainManager.h:69`, `Game.cpp:627`, `RoadSystem.cpp:2549`, `AudioSystem.cpp:33`, `Game.cpp:1223` |
| HIGH | 28 | leaks (`Shader.h:29`, `TerrainManager.cpp:99`, `Terrain.cpp:575`), state (`Game.cpp:1664`), perf (`TerrainChunk.cpp:82`, `RoadSystem.cpp:1752`), structure (`Game.cpp:1442`) |
| MEDIUM | 24 | delta-time, resize, file I/O in update, duplicated page state, dead members |
| LOW | 12 | `VehicleBody.cpp:269`, `Horse.h:32`, magic numbers, comment drift |

*Findings were verified against the source at the cited lines; line numbers refer to the working-tree state at the time of review.*

---

# Resolution status

**Applied in three commits** on branch `recovery/morning-2026-09-29-1110`, each separately revertible. The pre-existing uncommitted work was snapshotted first as `e07d429` so the fixes are cleanly separable.

| Commit | Scope | Diff |
|---|---|---|
| `6116d3a` | Correctness, resource leaks, silent failures | 14 files, +335/−120 |
| `0b74e06` | Heightmap/road-mask texture uploads scoped to the changed region | +64/−24 |
| `b26b400` | Uniform caching, per-frame rebuild work, resize, load flag | +250/−51 |

**Total: 14 files, +649/−195.**

## Fixed

| Severity | Finding | Where |
|---|---|---|
| CRITICAL | Noclip while driving/entering wedged the only car | `Character.cpp` `toggleNoclip` now calls `Car::dismountCharacter` |
| CRITICAL | Uninitialized `wheelOffset*` read by the tire solver | `Car.h` zero-init + `wheelLayoutResolved` flag + synthetic-layout fallback |
| CRITICAL | Dangling `heights` pointer after heightmap hot-reload | `TerrainManager::getHeightRevision()` + `Terrain::refreshHeightDependentCaches()`, called before anything samples terrain |
| CRITICAL | Smooth brush moved the mesh but not the collision surface | physics sync is now unconditional; the dead `visualSmoothingAffectsPhysics` flag and its setter/getter are deleted |
| CRITICAL | Unbounded index into `liveLandmarks` | two-sided bounds check |
| CRITICAL | Out-of-bounds write into `bareAt` | host sample indices re-validated after the endpoint-attach pass, plus a guarded write and a skip in the fallback pad loop |
| CRITICAL | Uninitialized `ma_sound` objects `uninit`'d and deleted | `loadSound` helper nulls failures; `cleanup`, `updateEngine` and the one-shot events null-guard |
| CRITICAL | `exit()` inside `Game::update` | `Game::requestQuit()` + `main()` loop poll; destructors now run |
| HIGH | No `glDeleteProgram` anywhere | `Shader` owns its program (dtor, deleted copy, moving transfer, cache cleared on release) |
| HIGH | `~TerrainManager` leaked the heightmap texture, road mask and two 15-slice arrays | all freed |
| HIGH | `~Terrain` leaked every water texture; wall rebuild leaked two buffers per pass | all freed; `wallVBO`/`wallEBO` zeroed |
| HIGH | `glGenQueries` never deleted | member + `glDeleteQueries` in `~Game` |
| HIGH | `RoadMeshBuilder::upload` leaked buffers on an empty rebuild | `destroy()` on the empty path |
| HIGH | `GL_CULL_FACE` disabled with no matching enable | declared once in `setup_opengl`, redundant mid-frame disable removed |
| HIGH | `roadRefLoaded` set before `stbi_load` | set only after a successful upload; failure clears textures and flag |
| HIGH | Unchecked `road_mesh.bin` / `foliage.bin` reads drove `resize()` | header, count caps and payload verified before use |
| HIGH | JSON `width`/`shoulder` unvalidated; duplicate `id`s | rejected at parse time |
| HIGH | `loadFromJSON` committed then reported failure | commits only after the whole document parses |
| HIGH | `windowWidth`/`windowHeight` stale after resize | `Game::setWindowSize` called from `framebuffer_size_callback` |
| HIGH | No `deltaTime` clamp | clamped to 100 ms |
| PERF | Uniform setters did a GL string lookup per call | per-program location cache |
| PERF | Full native grid re-normalised and re-uploaded per brush frame | `updateGPUHeightmapTextureRect`, scoped to the changed box in sculpt, conform and roadbed |
| PERF | Whole RGBA road mask re-uploaded per paint frame | sub-rectangle upload via `GL_UNPACK_ROW_LENGTH` |
| PERF | Chunk and road VAO/VBO/EBO deleted and re-`glGen`'d per rebuild | created once, refilled with `glBufferData` |
| PERF | 33×33 draped reference grid rebuilt every frame | cached, keyed on both revisions plus the placement inputs |

## Deliberately deferred

| Finding | Why |
|---|---|
| **No LOD (P1, ~14 M triangles/frame)** | Needs a new chunk LOD and stitching design; not a bug fix. Still the single largest performance item. |
| **Road rebuild runs the whole pipeline twice per drag frame (P3)** | Needs a dirty/deferred rebuild architecture in `Game::update`, not a local edit. |
| **`emitRing` O(rings × T-joins) scan (P6)** | Needs a per-segment contact index; touches `RoadMeshBuilder::build`, the highest-risk function in the tree. |
| **Two competing vehicle physics models; `VehicleDynamics` is dead (A5)** | Deleting it or wiring it in is a product decision. The inert JSON knobs (`brakeForceN`, `handbrakeForceN`, `rollingResistance`, `centerOfMassHeightM`, `frontWeightShare`) go with it. |
| **Weapon subsystem entirely unwired (A5)** | Same: delete or wire up. |
| **`terrain/TerrainSystem.cpp` + `Heightfield.cpp` are compiled stubs (A5)** | Same. |
| **God object; 1004-line editor HUD (A1)** | An extraction project, not a fix. |
| **Smoothing vs. mesh bilinear/triangle mismatch** | The large divergence is fixed; the residual sub-cell difference between bilinear `getHeightAt` and the mesh's triangle interpolation remains. Matching them exactly means replicating the chunk triangulation in the query path. |
| **Sticky, persisted auto-close on road segments** | The behavior is documented as intentional in the code; making it reversible needs a design decision about authored vs. derived `closed`. |
| **Duplicated page state, overlapping hit regions (M1)** | UI refactor. |

## Verification performed

- Build: `cmake --build build --config Release` → exit 0, no new warnings. (Note: this environment's MSBuild file tracker emits a spurious `error TRK0002` unless built with `/p:TrackFileAccess=false`.)
- `ctest --test-dir build -C Release` → **7/7 passed**, before and after every batch.
- `FALLAGA_TEST_MODE=1` end-to-end harness → all 13 scripted steps passed, ending `ALL TESTS COMPLETED SUCCESSFULLY!`, exiting **0** through the new graceful-quit path (previously `exit(0)`). This exercises car enter → seat → drive → exit and horse mount → ride → dismount.
- Runtime log confirms the fixed paths are live: `[Car] Wheel Radius: ...` with detection succeeding (fallback dormant), `[Car] Mounted.` / `[Car] Dismounted.`, `[AudioSystem] Audio cleaned up.`, no `truncated`/`implausible`/`FAILED` warnings.
- `FALLAGA_SHOT=240` frame capture, compared against the same capture from the unmodified tree: geometry, terrain texturing, ocean, road ribbon, foliage and the character all render. A pixel-identical diff is not achievable because frame content depends on frame timing; the apparent difference was reproduced on the *unmodified* tree with the same vsync setting, so it is not a regression.

**Caveat:** runtime behaviour is verified by the scripted harness, the log inspection and the captured frame. The drape cache, resize propagation and the road-editor brush paths were not exercised interactively, so they are verified by compilation, unit tests and code inspection rather than by a visual check.

---

# Second pass — road/terrain defects and the road surface

Work driven by in-game screenshots rather than by the audit, in commits `ed08238` → `3310f4f`. Recorded here because several of these were defects the audit missed entirely, and one was a fix I shipped and then reverted.

## Fixed

| Defect | Cause | Fix |
|---|---|---|
| **Terrain lying on the carriageway** | The graded road bed was a ~6 m strip (1.5 m authored shoulder + 4–5 m terrain falloff) while a terrain triangle is **22.25 m**, so triangles bridged over the road at natural height. Compounded by the ribbon adopting only **2 %** of its own ground height, so it disagreed with the bed across the whole width. | Grade the bed a full terrain cell either side (`kRoadBedFlatReach = 34 m`) **and** make the ribbon track it (`kRoadEdgeConform` 0.02 → 1.0). Measured: meanGap 0.196 → 0.082 m, worst 9.75 → 1.39 m, vertices >0.5 m off 7 185 → 14. |
| **Road floating / sinking at the rim** | Nothing guaranteed the asphalt cleared the sampled ground. | Clamp every ribbon vertex to ≥2 cm above the sampled ground (rim, caps, arc fans, junction pads). Measured: vertices with terrain >1 m proud 3 215 → 0. |
| **Junction hole, wedge, spike, notch** | The corridor conform graded against whichever ribbon triangle it sampled first, so where surfaces stack a higher one won and the ground was graded up to it. | Rasterise the shader's own `surfacePriority` decision and grade against that surface. |
| **Roads stopping in mid-air** | 22 of 25 roads have an end more than 25 m from any other end; the saved network is unconnected strokes. | `tools/join_road_ends.py` appends connectors where the end genuinely *points* at its neighbour. 3 joined (aim 0.98–1.00); roads 25 → 28, dead ends 41 → 36. |
| **Black asphalt** | Procedural tarmac at linear ~0.147; the tarred gravel scan already in `assets/` was unused. | Texture the road with `tarred_gravel_diff_2k.jpg` on a new **metric** vertex UV (metres across/along — the marking UV stretches with road width and could not be reused), plus its **normal map** for relief. |

## Reverted — shipped, then measured as a net negative

**Graded verge mesh** (`1cbba4b` → reverted in `3f0df33`). Running a 3.5 m strip horizontally out to the sampled ground produced hard-edged fins and dark polygons wherever ground drops steeply beside the asphalt — 8 044–8 164 quads of them, visible at junctions and along the coast road. Tapering to nothing at segment ends only moved the fins. The ~4–6 cm step it hid was smaller than the artefacts it introduced. The retry requirement is recorded at the removal site: follow a real **cut/fill cross-section**, not a horizontal run.

## Two things that are *not* code defects

- **The T-junction hole** was a genuine geometry defect and is **fixed** — verified at both original sites (4214, −1159) and (4208, −1534).
- **The flat paved expanse around (4110, −300)** is not a flat heightmap: `tools/analyse_terrain_flatness.py` shows the only flat regions are sea level (−70 m) and mountain tops (+87.8 m). It is the terrain shader painting **layer 12 port cobblestone at `WorldPos.xz * 0.22`** (~4.5 m repeat) on a 22.25 m cell — shading detail **5× finer than the geometry**, visible only where the ground is flat and there is no geometric variation. Pre-existing and intentional at close range.

## The remaining ceiling

**22.25 m per terrain texel** (1254×1254 over ±13 938 m) with roads 8–40 m wide. Every remaining road-quality limitation follows from this, and it cannot be tuned away:

1. **Corridor width.** The graded bed has to be a full terrain cell, so ~34 m of levelled ground either side of the carriageway is visible.
2. **No tight edge.** A terrain triangle spans a whole road, so the seam cannot be made crisp without the verge mesh that was reverted — which needs the cut/fill profile above.

The studio answer is a fine heightmap along road corridors over the coarse global map. That is a terrain-data project, not a road-mesh change.

## Tools added

| Tool | Purpose |
|---|---|
| `tools/join_road_ends.py` | Finds and joins dead ends in `roads_manual.json`; `--write` applies with a `.bak_joins` backup, `--loose` widens the aim test. |
| `tools/analyse_terrain_flatness.py` | Reports flat regions in the heightmap in metres, with world coordinates and road proximity. |

Runtime measurement overrides added during this work, all documented at their use sites: `FALLAGA_ROADGAP`, `FALLAGA_ROAD_FLAT`, `FALLAGA_ROAD_EDGE_CONFORM`, `FALLAGA_ROAD_GAIN`, `FALLAGA_ROAD_TINT`, `FALLAGA_ROAD_NORMAL`, `FALLAGA_ROAD_NOTEX`, `FALLAGA_CONFORM_REACH`, `FALLAGA_CONFORM_BERM`.

