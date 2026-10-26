#pragma once
//
// SculptTools.h -- standalone terrain sculpting brush algorithms.
//
// The brush maths used to live inline in TerrainManager::sculpt() and
// TerrainManager::erode(). It is extracted here the same way TerrainRefinement.h
// was: free of OpenGL and free of TerrainManager, operating on a caller-supplied
// height grid. That keeps the tools testable headlessly and reusable outside the
// engine.
//
// A HeightField is a non-owning view. It points at the caller's height vector and
// carries the world<->grid mapping the brushes need; nothing here owns GPU
// resources or touches collision. TerrainManager still owns the physics mirror,
// the heightmap texture upload and the chunk rebuild around these calls.
//
// Editor brush ids (see Game.h / SculptPanel):
//   1 Raise Terrain          2 Lower Terrain       3 Smooth Terrain
//   4 Erode Terrain          5 Angle Slope
//   6 Fit To Curved Road     7 Add Geometry + Smooth   8 Straighten Shoreline
//   9 Flatten                10 Level
// Tools 1-5 and 9-10 are the pure brushes below. Tool 6 is road-mesh specific and
// stays in TerrainManager::conformTerrainToRoadMesh. Tool 7 wraps TerrainRefinement,
// also GL-free, wrapped here for completeness. Tool 8 is handled by
// TerrainManager::straightenShoreline.

#include <vector>
#include "TerrainRefinement.h"

namespace SculptTools {

// Non-owning view of the editable height grid plus the world mapping.
struct HeightField {
    std::vector<float>& heights;
    int w;   // grid samples along X
    int h;   // grid samples along Z
    float worldMinX, worldMaxX, worldMinZ, worldMaxZ;
    float minHeight, maxHeight;
};

// Inclusive visual-grid cell box. Default-constructed means "empty".
struct IntBox {
    int minX = 0, maxX = -1, minZ = 0, maxZ = -1;
    bool empty() const { return maxX < minX || maxZ < minZ; }
};

// Everything a single brush application needs. Angle Slope reads the two angles;
// the other brushes ignore them.
enum class FalloffMode {
    Smooth,      // soft Hermite edge, good general-purpose default
    Linear,      // predictable linear fade
    Spherical,   // rounded, stronger center with a soft edge
    Constant     // hard-edged brush (no edge fade)
};

struct BrushPoint {
    float wx = 0.0f;
    float wz = 0.0f;
};

struct BrushParams {
    float wx = 0.0f, wz = 0.0f;   // world centre
    float radius = 0.0f;          // world metres
    float speed = 1.0f;           // strength
    float dt = 0.0f;              // seconds since last frame
    float directionDeg = 0.0f;    // Angle Slope azimuth
    float gradeDeg = 20.0f;       // Angle Slope grade
    FalloffMode falloff = FalloffMode::Smooth;
    float hardness = 0.0f;        // 0 = fade from center; 0.8 = hard core + soft rim
    // Flatten (mode 9): the world height to pull the ground toward. The caller
    // samples it once when the stroke starts so the pad stays at a fixed level
    // instead of creeping with the brush. Ignored by the other brushes.
    float targetHeight = 0.0f;
};

// Hermite smoothstep on t in [0,1].
float smoothstep01(float t);
// Shared radial falloff. distance/radius are in world units; hardness is [0, 0.95].
float brushFalloff(float distance, float radius, FalloffMode mode, float hardness = 0.0f);
// Stroke sampling helper: returns evenly spaced brush centers, including the endpoint.
// spacingFraction is relative to radius (0.2 is a useful default). Call once per
// pointer segment; callers should avoid applying the first point twice between segments.
std::vector<BrushPoint> sampleStroke(float fromX, float fromZ, float toX, float toZ,
                                     float radius, float spacingFraction = 0.2f);

// world (x,z) -> continuous grid position
void worldToGrid(const HeightField& f, float wx, float wz, float& gx, float& gz);
// The brush footprint radius in grid samples (max of the X/Z scalings).
float gridRadius(const HeightField& f, float radius);
// Clamped grid box covering a brush footprint.
IntBox footprint(const HeightField& f, float wx, float wz, float radius);

// Pure brushes. Each edits f.heights in place and returns the affected box.
IntBox raiseLower(HeightField& f, const BrushParams& p, float dir); // dir +1 raise, -1 lower
IntBox smoothTerrain(HeightField& f, const BrushParams& p);
IntBox angleSlope(HeightField& f, const BrushParams& p);
// Flatten toward p.targetHeight: a level pad at a caller-chosen height.
IntBox flatten(HeightField& f, const BrushParams& p);
// Level toward the falloff-weighted mean height under the brush: removes bumps
// while leaving the area's overall elevation roughly where it was.
IntBox level(HeightField& f, const BrushParams& p);

// Particle hydraulic erosion. `isLocal` spawns droplets inside the brush circle;
// otherwise they spawn across the whole map (global pass). Edits f.heights.
void erode(HeightField& f, int numDroplets, float wx, float wz, float radius, bool isLocal);
// Droplets for one frame of the erode brush: radius*speed*dt*30, at least one.
int dropletCount(float radius, float speed, float dt);

// Dispatch brush ids 1-5 and 9-10. Returns the affected box (empty for unknown ids).
IntBox apply(HeightField& f, int mode, const BrushParams& p);

// ── Tool 7: Add Geometry + Smooth ───────────────────────────────────────────
// Wraps TerrainRefinement, which is GL-free, so these stay headless too.
// addGeometry raises the subdivision level in the footprint and seeds the new
// fine vertices; returns cells gained and writes the visual-cell box. smoothFine
// runs the fine-grid Laplacian smooth, mirrors the result onto the base grid and
// writes the affected box expressed in visual cells (for the caller's chunk
// rebuild). When there is no added geometry smoothFine does nothing; the caller
// decides whether to fall back to the base-grid smooth.
int addGeometry(TerrainRefinement& ref, HeightField& f, const BrushParams& p, int level,
                IntBox& outCellBox);
void smoothFine(TerrainRefinement& ref, HeightField& f, const BrushParams& p,
                IntBox& outCellBox);

} // namespace SculptTools
