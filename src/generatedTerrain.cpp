// terrain.cpp - Historical Sousse (1881-1956) Terrain & City Generator
// Based on colonial-era maps and sousse1881-1956.com historical data
// Recreates the medina, French colonial quarter, port, and key monuments

#include <vector>
#include <array>
#include <cmath>
#include <string>
#include <map>
#include <algorithm>
#include <cstring>
#include <functional>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// CONFIGURATION
// ============================================================================

// World dimensions in meters (based on map scale: ~2km x 2km area)
static const float WORLD_SIZE_X = 2000.0f;  // East-West
static const float WORLD_SIZE_Z = 2000.0f;  // North-South
static const float MAX_ELEVATION = 60.0f;    // Sousse sits on a coastal hill ~50m

// Grid resolution
static const int GRID_RES = 512;
static const float CELL_SIZE_X = WORLD_SIZE_X / (float)GRID_RES;
static const float CELL_SIZE_Z = WORLD_SIZE_Z / (float)GRID_RES;

// Sea level
static const float SEA_LEVEL = 0.0f;

// Coordinate system: (0,0) is SW corner of the map
// X axis = East, Z axis = North, Y axis = Up
// The Medina center is approximately at (1100, 800) in our coordinate system
// The Mediterranean Sea is to the East and North-East

// ============================================================================
// DATA STRUCTURES
// ============================================================================

struct Vec2 { float x, z; };
struct Vec3 { float x, y, z; };
struct Color { float r, g, b, a; };

enum class TerrainType {
    SEA,
    BEACH,
    PORT_WATER,
    PORT_QUAY,
    ROAD,
    BOULEVARD,
    PLAZA,
    MEDINA_GROUND,
    MEDINA_BUILDING,
    COLONIAL_GROUND,
    COLONIAL_BUILDING,
    RAMPART_WALL,
    MONUMENT,
    GARDEN,
    MILITARY_CAMP,
    CEMETERY,
    RAILWAY,
    STADE,
    OPEN_GROUND,
    CATACOMBS_ENTRANCE
};

struct Building {
    float x, z;           // Center position in world coords
    float width, depth;   // Dimensions
    float height;         // Building height
    float rotation;       // Rotation in radians
    std::string name;
    TerrainType type;
};

struct Street {
    std::vector<Vec2> points;
    float width;
    std::string name;
    bool isBoulevard;
};

struct WallSegment {
    Vec2 start, end;
    float height;
    float thickness;
};

// ============================================================================
// HEIGHTMAP & TERRAIN TYPE MAP
// ============================================================================

static float heightmap[GRID_RES][GRID_RES];
static TerrainType terrainTypeMap[GRID_RES][GRID_RES];
static Color colorMap[GRID_RES][GRID_RES];

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

static float clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

static float smoothstep(float edge0, float edge1, float x) {
    float t = clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static float distance2D(float x1, float z1, float x2, float z2) {
    float dx = x1 - x2;
    float dz = z1 - z2;
    return sqrtf(dx * dx + dz * dz);
}

static float pointToSegmentDist(float px, float pz, float ax, float az, float bx, float bz) {
    float dx = bx - ax, dz = bz - az;
    float len2 = dx * dx + dz * dz;
    if (len2 < 0.001f) return distance2D(px, pz, ax, az);
    float t = clamp(((px - ax) * dx + (pz - az) * dz) / len2, 0.0f, 1.0f);
    float projX = ax + t * dx;
    float projZ = az + t * dz;
    return distance2D(px, pz, projX, projZ);
}

static bool pointInPolygon(float px, float pz, const std::vector<Vec2>& poly) {
    bool inside = false;
    int n = (int)poly.size();
    for (int i = 0, j = n - 1; i < n; j = i++) {
        if (((poly[i].z > pz) != (poly[j].z > pz)) &&
            (px < (poly[j].x - poly[i].x) * (pz - poly[i].z) / (poly[j].z - poly[i].z) + poly[i].x)) {
            inside = !inside;
        }
    }
    return inside;
}

// Simple hash-based noise
static float hashNoise(float x, float z) {
    int ix = (int)floorf(x);
    int iz = (int)floorf(z);
    float fx = x - ix;
    float fz = z - iz;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fz = fz * fz * (3.0f - 2.0f * fz);

    auto hash = [](int x, int z) -> float {
        int n = x * 374761393 + z * 668265263;
        n = (n << 13) ^ n;
        return 1.0f - (float)((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f;
    };

    float v00 = hash(ix, iz);
    float v10 = hash(ix + 1, iz);
    float v01 = hash(ix, iz + 1);
    float v11 = hash(ix + 1, iz + 1);

    float v0 = lerp(v00, v10, fx);
    float v1 = lerp(v01, v11, fx);
    return lerp(v0, v1, fz);
}

static float fbmNoise(float x, float z, int octaves, float persistence) {
    float total = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxVal = 0.0f;
    for (int i = 0; i < octaves; i++) {
        total += hashNoise(x * frequency, z * frequency) * amplitude;
        maxVal += amplitude;
        amplitude *= persistence;
        frequency *= 2.0f;
    }
    return total / maxVal;
}

// ============================================================================
// HISTORICAL SOUSSE LAYOUT DATA
// Based on the colonial-era map and sousse1881-1956.com
// ============================================================================

// --- MEDINA WALLS (Ramparts) ---
// The medina of Sousse is roughly rectangular, enclosed by walls.
// Source: "un carré de 700 mètres par 500 mètres" (Leila Ammar, Al-Sabil 2024)
//   N-S: 700m, E-W: 500m
//   East wall: aligns with Bd. Emile Loubet (~X=1270, the demolished east rampart)
//   West wall: X = 1270 - 500 = ~770
//   South wall: Z = ~580 (Bab el Jedid / Bab el Gharbi area)
//   North wall: Z = 580 + 700 = ~1280 (Bab Jebli / Bab el Khabli)
//   The south end is slightly narrower (tapering toward Kasbah), north is wider.
static std::vector<Vec2> getMedinaWallPolygon() {
    return {};
}

static std::vector<Vec2> getKasbahPolygon() {
    return {};
}

static std::vector<Vec2> getPortPolygon() {
    return {};
}

static std::vector<Vec2> getPortBasinPolygon() {
    return {};
}

static std::vector<Vec2> getMilitaryCampPolygon() {
    return {};
}

static std::vector<Vec2> getCatacombsPolygon() {
    return {};
}

static std::vector<Vec2> getGarePolygon() {
    return {};
}

static std::vector<Vec2> getStadePolygon() {
    return {};
}

static std::vector<Vec2> getSeaPolygon() {
    return {};
}

static std::vector<Building> getHistoricalBuildings() {
    return {};
}

static std::vector<Street> getHistoricalStreets() {
    return {};
}

static std::vector<Landmark> getLandmarks() {
    return {};
}

