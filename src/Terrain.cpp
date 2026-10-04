#include "Terrain.h"
#include "PhysicsSystem.h"
#include "WaterSimulation.h"
#include "ProceduralMapData.h"
#include "SousseMapData.h"
#include "TextureEngine.h"
#include <cstdlib>
#include <GL/glew.h>
#ifdef HAS_GLUT
#include <GL/glut.h>
#endif
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stb_image.h>
#include <vector>
// Keep the water below the mapped coastal shelf; connected masking excludes inland pits.
static constexpr float WATER_BASE = -55.0f;
static float waterLevelOffset = 0.0f; // Positive raises terrain (lowers water)
static float waterLevelRuntime = WATER_BASE;  // Real-time editor override

namespace {
constexpr float HISTORIC_PLAN_OFFSET_X = 3600.0f;
constexpr float HISTORIC_PLAN_OFFSET_Z = -701.0f;
constexpr float kCarCollisionCellSize = 64.0f;
constexpr float kRoadSurfaceCellSize = 64.0f;

std::uint64_t carCollisionCellKey(int x, int z) {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
         static_cast<std::uint32_t>(z);
}

std::uint64_t roadSurfaceCellKey(int x, int z) {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
         static_cast<std::uint32_t>(z);
}

bool isInsideHistoricCity(const RoadSegment& segment) {
  for (const RoadPoint& point : segment.controlPoints) {
    if (point.pos.x >= 2450.0f && point.pos.x <= 4850.0f &&
        point.pos.y >= -2100.0f && point.pos.y <= 1550.0f) {
      return true;
    }
  }
  return false;
}

// Four terrain anchors used to validate the projected road network.
// Plan coordinates come from ProceduralMapData.h "Key anchors (verified against
// map)"; their world coordinates are the same points translated into the DEM
// game world by the historic plan offset below.
constexpr glm::vec2 kAnchorPlan[] = {
    {-114.0f, -929.0f},  // Kasbah
    { 530.0f,   71.0f},  // Medina (Place Pichon)
    { 585.0f,  200.0f},  // Port (user-built quay front: bollards plan x 572-585, z 51..387)
    { -73.0f,  247.0f},  // Gare PLM
};
const char* const kAnchorNames[] = {"Kasbah", "Medina (Pichon)", "Port", "Gare PLM"};

// Translate the traced road network from the projected-plan coordinate space
// (X in [-1250,2500], Z in [-2500,2500], see ProceduralMapData.h) into the DEM
// world where the historic city sits.  This is the same plan->world mapping
// the legacy historic-city roads used (HISTORIC_PLAN_OFFSET).
void offsetRoadGraphToWorld(RoadGraph& graph, float dx, float dz) {
  for (auto& seg : graph.segments) {
    for (auto& p : seg.controlPoints) {
      p.pos.x += dx;
      p.pos.y += dz;
    }
  }
}

void verifyRoadAnchors(const RoadGraph& graph) {
  std::cout << "\n[Terrain] ===== ROAD ANCHOR VERIFICATION (DEM world) =====" << std::endl;
  for (int ai = 0; ai < 4; ++ai) {
    const glm::vec2 aPlan = kAnchorPlan[ai];
    const glm::vec2 aWorld = aPlan + glm::vec2(HISTORIC_PLAN_OFFSET_X, HISTORIC_PLAN_OFFSET_Z);

    float best = 1e9f;
    for (const auto& seg : graph.segments) {
      const auto& cps = seg.controlPoints;
      for (size_t i = 0; i + 1 < cps.size(); ++i) {
        const glm::vec2& p0 = cps[i].pos;
        const glm::vec2& p1 = cps[i + 1].pos;
        const glm::vec2 d = p1 - p0;
        const float len2 = glm::dot(d, d);
        float t = (len2 > 1e-8f)
                      ? glm::clamp(glm::dot(aWorld - p0, d) / len2, 0.0f, 1.0f)
                      : 0.0f;
        const glm::vec2 proj = p0 + d * t;
        best = std::min(best, glm::length(aWorld - proj));
      }
    }
    const bool ok = best < 50.0f;
    std::cout << "[Terrain]   " << std::left << std::setw(16) << kAnchorNames[ai]
              << " world(" << std::setw(7) << aWorld.x << ", " << std::setw(7) << aWorld.y
              << ") plan(" << std::setw(7) << aPlan.x << ", " << std::setw(7) << aPlan.y
              << ") -> nearest road " << std::setw(6) << std::setprecision(1)
              << std::fixed << best << " m  " << (ok ? "OK" : "MISS") << std::endl;
  }
  std::cout << "[Terrain] ===================================================\n" << std::endl;
}

const char* roadTypeName(RoadType t) {
  switch (t) {
    case RoadType::ColonialAvenue: return "colonial_avenue";
    case RoadType::UrbanStreet:    return "urban_street";
    case RoadType::CountryHighway: return "country_highway";
    case RoadType::CountryDirt:    return "country_dirt";
    case RoadType::Port:           return "port";
    case RoadType::Medina:         return "medina";
  }
  return "country_dirt";
}

// Print resolved road sizes (width/shoulder per type) so widths are visible
// and auditable against the world meter scale used by the mesh builder.
void reportRoadSizes(const RoadGraph& graph) {
  std::map<RoadType, int> counts;
  std::map<RoadType, float> sumW, sumS;
  for (const auto& seg : graph.segments) {
    counts[seg.type]++;
    sumW[seg.type] += seg.width;
    sumS[seg.type] += seg.shoulderWidth;
  }
  std::cout << "[Terrain] Road sizes (resolved by type):" << std::endl;
  for (const auto& kv : counts) {
    std::cout << "[Terrain]   " << std::left << std::setw(18) << roadTypeName(kv.first)
              << std::setw(4) << kv.second << " segs   width=" << std::setprecision(1)
              << std::fixed << (sumW[kv.first] / kv.second) << " m   shoulder="
              << (sumS[kv.first] / kv.second) << " m" << std::endl;
  }
}

RoadType historicRoadType(const ProceduralMap::MapStreet& street) {
  const bool medinaStreet = street.name.find("Souk") != std::string::npos ||
                            street.name.find("Kasbah") != std::string::npos ||
                            street.name.find("Bab ") != std::string::npos ||
                            street.name.find("El Hadjira") != std::string::npos;
  if (medinaStreet) return RoadType::Medina;
  if (street.name.find("Marine") != std::string::npos ||
      street.name.find("Fallieres") != std::string::npos) return RoadType::Port;
  if (street.width >= 16.0f) return RoadType::ColonialAvenue;
  return RoadType::UrbanStreet;
}

void replaceWithHistoricCityRoads(RoadGraph& graph) {
  graph.segments.erase(
      std::remove_if(graph.segments.begin(), graph.segments.end(), isInsideHistoricCity),
      graph.segments.end());

  uint32_t nextId = 0;
  for (const RoadSegment& segment : graph.segments)
    nextId = std::max(nextId, segment.id + 1);

  for (const ProceduralMap::MapStreet& street : ProceduralMap::getStreets()) {
    RoadSegment segment;
    segment.id = nextId++;
    segment.type = historicRoadType(street);
    segment.width = street.width;
    segment.shoulderWidth = segment.type == RoadType::Medina ? 0.75f : 1.5f;

    for (const glm::vec2& point : street.points) {
      segment.controlPoints.push_back(
          {{point.x + HISTORIC_PLAN_OFFSET_X, point.y + HISTORIC_PLAN_OFFSET_Z}});
    }
    graph.segments.push_back(std::move(segment));
  }
}
} // namespace
// ----------------------------------------------------------------
//  Hash-based value noise â€” NO sine/cosine grids!
// ----------------------------------------------------------------
static float hashf(int x, int y) {
  // Integer hash with good avalanche mixing
  int h = x * 374761393 + y * 668265263;
  h = (h ^ (h >> 13)) * 1274126177;
  h = (h ^ (h >> 16));
  return (float)(h & 0x7FFFFFFF) / (float)0x7FFFFFFF * 2.0f - 1.0f; // [-1,1]
}

static float valueNoise2D(float x, float y) {
  int ix = (int)floorf(x), iy = (int)floorf(y);
  float fx = x - ix, fy = y - iy;
  // Smoothstep
  float ux = fx * fx * (3.0f - 2.0f * fx);
  float uy = fy * fy * (3.0f - 2.0f * fy);
  float v00 = hashf(ix, iy);
  float v10 = hashf(ix + 1, iy);
  float v01 = hashf(ix, iy + 1);
  float v11 = hashf(ix + 1, iy + 1);
  return v00 * (1 - ux) * (1 - uy) + v10 * ux * (1 - uy) + v01 * (1 - ux) * uy +
         v11 * ux * uy;
}

static float fbmNoise(float x, float y, int octaves) {
  float val = 0.0f, amp = 0.5f, freq = 1.0f;
  for (int i = 0; i < octaves; i++) {
    val += amp * valueNoise2D(x * freq, y * freq);
    freq *= 2.13f; // irrational multiplier avoids overtone cancellation
    amp *= 0.5f;
  }
  return val;
}

static unsigned int buildNormalMap(int sz = 512) {
  std::vector<unsigned char> px(sz * sz * 3);
  float scale = 8.0f; // lower = larger waves on the normal map
  float eps = 1.0f / sz;
  for (int y = 0; y < sz; ++y)
    for (int x = 0; x < sz; ++x) {
      float u = (float)x / sz, v = (float)y / sz;
      float dX = fbmNoise((u + eps) * scale, v * scale, 6) -
                 fbmNoise((u - eps) * scale, v * scale, 6);
      float dZ = fbmNoise(u * scale, (v + eps) * scale, 6) -
                 fbmNoise(u * scale, (v - eps) * scale, 6);
      float sc = 1.0f / (2.0f * eps);
      dX *= sc * 0.8f; // strength of normal
      dZ *= sc * 0.8f;
      float len = sqrtf(dX * dX + 1.0f + dZ * dZ);
      int i = (y * sz + x) * 3;
      auto cv = [](float f) -> unsigned char {
        int v = (int)(f * 255.0f);
        return (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v));
      };
      px[i + 0] = cv(0.5f + 0.5f * (-dX / len));
      px[i + 1] = cv(0.5f + 0.5f * (1.0f / len));
      px[i + 2] = cv(0.5f + 0.5f * (-dZ / len));
    }
  unsigned int tid;
  glGenTextures(1, &tid);
  glBindTexture(GL_TEXTURE_2D, tid);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, sz, sz, 0, GL_RGB, GL_UNSIGNED_BYTE,
               px.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glGenerateMipmap(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, 0);
  return tid;
}

static unsigned int buildFoamMap(int sz = 512) {
  std::vector<unsigned char> px(sz * sz);
  float scale = 6.0f;
  for (int y = 0; y < sz; ++y)
    for (int x = 0; x < sz; ++x) {
      float u = (float)x / sz, v = (float)y / sz;
      // High-frequency FBM for organic foam blobs
      float n = fbmNoise(u * scale, v * scale, 5);
      // Boost and clip to get bright sparse blobs
      float foam = powf(std::max(0.0f, n * 1.8f + 0.3f), 2.5f);
      int val = (int)(foam * 255.0f);
      px[y * sz + x] = (unsigned char)(val < 0 ? 0 : (val > 255 ? 255 : val));
    }
  unsigned int tid;
  glGenTextures(1, &tid);
  glBindTexture(GL_TEXTURE_2D, tid);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, sz, sz, 0, GL_RED, GL_UNSIGNED_BYTE,
               px.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glGenerateMipmap(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, 0);
  return tid;
}

static unsigned int loadTexture2D(const char *path) {
  unsigned int textureID;
  glGenTextures(1, &textureID);
  int width, height, nrChannels;
  unsigned char *data = stbi_load(path, &width, &height, &nrChannels, 0);
  if (data) {
    GLenum format =
        (nrChannels == 1) ? GL_RED : (nrChannels == 3 ? GL_RGB : GL_RGBA);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format,
                 GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stbi_image_free(data);
    std::cout << "[Terrain] Loaded Texture: " << path << " (" << width << "x"
              << height << ")" << std::endl;
  } else {
    std::cerr << "[Terrain] FAILED to load texture: " << path << std::endl;
    stbi_image_free(data);
  }
  return textureID;
}

bool Terrain::isFlatMode = false;

Terrain::Terrain() {
  terrainManager = new TerrainManager("assets/terrain/heightmap.png",
                                      "assets/terrain/textureMap.png", -13938.0f,
                                      13938.0f, -13938.0f, 13938.0f, -70.0f, 87.8f);

  // Keep the gameplay surface mask small: the source is 8129x8129, while a
  // 1024-ish nearest-neighbor copy is enough to identify broad wadi channels.
  int wadiW = 0, wadiH = 0, wadiChannels = 0;
  unsigned char* wadiPixels = stbi_load("assets/terrain/heightmap_wadis.png",
                                       &wadiW, &wadiH, &wadiChannels, 1);
  if (wadiPixels && wadiW > 0 && wadiH > 0) {
    const int stride = std::max(1, (std::max(wadiW, wadiH) + 1023) / 1024);
    wadiSurfaceMaskW = (wadiW + stride - 1) / stride;
    wadiSurfaceMaskH = (wadiH + stride - 1) / stride;
    wadiSurfaceMask.resize(static_cast<size_t>(wadiSurfaceMaskW) * wadiSurfaceMaskH);
    for (int y = 0; y < wadiSurfaceMaskH; ++y) {
      const int sourceY = std::min(y * stride, wadiH - 1);
      for (int x = 0; x < wadiSurfaceMaskW; ++x) {
        const int sourceX = std::min(x * stride, wadiW - 1);
        wadiSurfaceMask[static_cast<size_t>(y) * wadiSurfaceMaskW + x] =
            wadiPixels[static_cast<size_t>(sourceY) * wadiW + sourceX];
      }
    }
    stbi_image_free(wadiPixels);
    std::cout << "[Terrain] Loaded wadi surface mask: " << wadiSurfaceMaskW
              << "x" << wadiSurfaceMaskH << std::endl;
  } else {
    if (wadiPixels) stbi_image_free(wadiPixels);
    std::cerr << "[Terrain] Wadi surface mask unavailable; using geographic fallback."
              << std::endl;
  }

  oceanShader = new Shader("src/shaders/ocean.vert", "src/shaders/ocean.frag");
  streetShader =
      new Shader("src/shaders/street.vert", "src/shaders/street.frag");
  modelShader = new Shader("src/shaders/model.vert", "src/shaders/model.frag");

  std::cout << "[Terrain] Initializing Water Mesh..." << std::endl;
  buildWaterMesh();

  // â”€â”€ GEOMETRY-BASED ROAD SYSTEM â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  std::cout << "[Terrain] Initializing Geometry Road System..." << std::endl;
  {
      // 1. Set up height sampler from TerrainManager
      TerrainCoordinateMapper mapper;
      mapper.bounds = { terrainManager->getWorldMinX(), terrainManager->getWorldMaxX(),
                        terrainManager->getWorldMinZ(), terrainManager->getWorldMaxZ(),
                        terrainManager->getMinHeight(), terrainManager->getMaxHeight() };
      mapper.heightmapW = terrainManager->getGridW();
      mapper.heightmapH = terrainManager->getGridH();

      roadHeightSampler.init(mapper, terrainManager->getHeightsData(),
                             terrainManager->getGridW(), terrainManager->getGridH());
      cachedHeightRevision = terrainManager->getHeightRevision();
      roadMapper = mapper;

      // 2. Road graph: the old auto-traced road network is REMOVED â€” roads are
      //    now drawn by hand in the Road Editor. Only manual roads saved to
      //    roads_manual.json are restored at startup (empty slate otherwise).
      roadGraph = new RoadGraph();
      if (roadGraph->loadFromJSON("assets/roads/roads_manual.json")) {
          // The JSON stores plan coordinates; translate into DEM world space
          // (mirrors what saveRoadsToJSON does in reverse).
          offsetRoadGraphToWorld(*roadGraph, HISTORIC_PLAN_OFFSET_X, HISTORIC_PLAN_OFFSET_Z);
          roadGraph->buildTopology();
          std::cout << "[Terrain] Road system: restored " << roadGraph->segments.size()
                    << " manually drawn road segments\n";
      } else {
          // loadFromJSON already explained why on stderr (missing file, or a
          // syntax error with its line and column).
          std::cout << "[Terrain] Road system: no roads loaded from roads_manual.json â€” blank slate, draw roads in Road Editor\n";
      }

      // 4. Build the road mesh from the untouched source terrain profile.
      for (auto& seg : roadGraph->segments) {
          RoadSpline::evaluate(seg, roadHeightSampler, 0.5f);
      }
      roadMeshBuilder = new RoadMeshBuilder();
      // Build a flat support ribbon for the terrain conform pass. The rendered
      // crowned mesh is rebuilt from the resulting heightfield in snapRoads.
      roadMeshBuilder->build(roadGraph->segments, roadHeightSampler, nullptr, 1.5f, false);
      roadMeshBuilder->upload();

      // Cut/fill the terrain heightfield itself under the authored road ribbons.
      roadDeformer = new RoadDeformer();
      // Rasterize road cuts on the same grid as the rendered terrain mesh. A
      // smaller mask left multi-metre ungraded strips at road edges after resampling.
      const int deformW = terrainManager->getVisualGridW();
      const int deformH = terrainManager->getVisualGridH();

      // FALLAGA_ROADGAP: trace the worst terrain-above-road point through the
      // construction phases, so it is unambiguous which stage leaves the ground
      // above the asphalt. Remove once the grading is settled.
      auto probeVisualY = [&](float wx, float wz) -> float {
        const int vw = terrainManager->getVisualGridW();
        const int vh = terrainManager->getVisualGridH();
        const std::vector<float>& H = terrainManager->getVisualHeightsData();
        if (vw < 2 || vh < 2 || H.empty()) return 0.0f;
        const float wx0 = terrainManager->getWorldMinX(), wx1 = terrainManager->getWorldMaxX();
        const float wz0 = terrainManager->getWorldMinZ(), wz1 = terrainManager->getWorldMaxZ();
        const float u = glm::clamp((wx - wx0) / (wx1 - wx0) * (vw - 1), 0.0f, (float)(vw - 1));
        const float v = glm::clamp((wz - wz0) / (wz1 - wz0) * (vh - 1), 0.0f, (float)(vh - 1));
        const int x0 = (int)u, z0 = (int)v;
        const int x1 = std::min(x0 + 1, vw - 1), z1 = std::min(z0 + 1, vh - 1);
        const float tx = u - x0, tz = v - z0;
        return H[(size_t)z0 * vw + x0] * (1 - tx) * (1 - tz) +
               H[(size_t)z0 * vw + x1] * tx * (1 - tz) +
               H[(size_t)z1 * vw + x0] * (1 - tx) * tz +
               H[(size_t)z1 * vw + x1] * tx * tz;
      };
      const bool tracePhases = std::getenv("FALLAGA_ROADGAP") != nullptr;
      if (tracePhases)
        std::cout << "[Phase] afterSupportRibbon probeY=" << probeVisualY(4313.14f, -989.314f) << std::endl;

      roadDeformer->build(roadGraph->segments, roadHeightSampler, roadMapper, deformW, deformH);
      terrainManager->setRoadGeometryMask(roadDeformer->getSurfaceMaskData(), deformW, deformH);
      terrainManager->applyRoadBedHeightfield(roadDeformer->getDeformationData(), deformW, deformH);
      if (tracePhases)
        std::cout << "[Phase] afterRoadBed       probeY=" << probeVisualY(4313.14f, -989.314f) << std::endl;
      // Final support pass follows the built mesh itself so curved widths, end
      // caps, and junction surfaces all have terrain directly beneath them.
      terrainManager->conformTerrainToRoadMesh(0.0f, 0.0f, 0.0f, 1.0f, 1.0f,
          roadMeshBuilder->debugVertices(), roadMeshBuilder->debugIndices(), true, 20.0f);
      if (tracePhases)
        std::cout << "[Phase] afterConform       probeY=" << probeVisualY(4313.14f, -989.314f) << std::endl;
      // Seat the ribbon on the graded heightfield before anything samples it again.
      snapRoadsToRenderedTerrain();
      if (tracePhases)
        std::cout << "[Phase] afterSnap          probeY=" << probeVisualY(4313.14f, -989.314f) << std::endl;

      // FALLAGA_ROADGAP: measure how far the road surface actually sits above the
      // terrain the GPU draws. The road is sampled from the native DEM grid while the
      // rendered terrain comes from the visual grid, so this is the only way to tell a
      // conform that worked (centimetres) from one that silently missed (metres).
      if (std::getenv("FALLAGA_ROADGAP")) {
        const int vw = terrainManager->getVisualGridW();
        const int vh = terrainManager->getVisualGridH();
        const float wMinX = terrainManager->getWorldMinX(), wMaxX = terrainManager->getWorldMaxX();
        const float wMinZ = terrainManager->getWorldMinZ(), wMaxZ = terrainManager->getWorldMaxZ();
        const std::vector<float>& H = terrainManager->getVisualHeightsData();
        const float cellSize = vw > 1 ? (wMaxX - wMinX) / (vw - 1) : 0.0f;
        auto sampleVisual = [&](float wx, float wz) -> float {
          if (vw < 2 || vh < 2 || H.empty()) return 0.0f;
          const float u = glm::clamp((wx - wMinX) / (wMaxX - wMinX) * (vw - 1), 0.0f, (float)(vw - 1));
          const float v = glm::clamp((wz - wMinZ) / (wMaxZ - wMinZ) * (vh - 1), 0.0f, (float)(vh - 1));
          const int x0 = (int)u, z0 = (int)v;
          const int x1 = std::min(x0 + 1, vw - 1), z1 = std::min(z0 + 1, vh - 1);
          const float tx = u - x0, tz = v - z0;
          return H[(size_t)z0 * vw + x0] * (1 - tx) * (1 - tz) +
                 H[(size_t)z0 * vw + x1] * tx * (1 - tz) +
                 H[(size_t)z1 * vw + x0] * (1 - tx) * tz +
                 H[(size_t)z1 * vw + x1] * tx * tz;
        };
        double sum = 0.0, sumAbs = 0.0, maxAbs = 0.0, maxAbove = 0.0;
        int n = 0, above05 = 0, above2 = 0, terrainAbove1 = 0, terrainAbove3 = 0;
        for (const auto& v : roadMeshBuilder->debugVertices()) {
          const float d = v.position.y - sampleVisual(v.position.x, v.position.z);
          sum += d; sumAbs += std::fabs(d); ++n;
          if (std::fabs(d) > maxAbs) maxAbs = std::fabs(d);
          if (d > 0.5f) ++above05;
          if (d > 2.0f) ++above2;
          // Ground still standing proud of the asphalt: high places the tool did not
          // clean. This is the number to drive down, and it is independent of the
          // road-above-terrain figures.
          const float proud = -d;
          if (proud > maxAbove) maxAbove = proud;
          if (proud > 1.0f) ++terrainAbove1;
          if (proud > 3.0f) ++terrainAbove3;
        }
        if (n > 0) {
          // Terrain poking through the ribbon is the failure that matters, and it can
          // only be judged against the real road surface at the probe point. Comparing
          // offset terrain against the height of the nearest road *vertex* is wrong on
          // any sloped or cross-sloped road and invents thousands of false pokes, so
          // walk the triangles incident to each vertex and interpolate properly.
          const auto& rv = roadMeshBuilder->debugVertices();
          const auto& ri = roadMeshBuilder->debugIndices();
          std::vector<std::vector<uint32_t>> incident(rv.size());
          for (uint32_t t = 0; t * 3 + 2 < ri.size(); ++t) {
            for (int k = 0; k < 3; ++k) {
              const uint32_t vi = ri[t * 3 + k];
              if (vi < incident.size()) incident[vi].push_back(t);
            }
          }
          const float kStencil[2] = { 8.0f, 16.0f };
          double worstPoke = 0.0;
          float worstPokeX = 0.0f, worstPokeZ = 0.0f, worstPokeRoadY = 0.0f;
          int probes = 0, poke025 = 0, poke1m = 0;
          int meshProbes = 0, meshPoke025 = 0, meshPoke1m = 0;
          double meshWorstPoke = 0.0;
          float meshWorstX = 0.0f, meshWorstZ = 0.0f;
          float meshWorstRoadY = 0.0f;
          for (size_t vi = 0; vi < rv.size(); ++vi) {
            for (int s = 0; s < 2; ++s) {
              const float o = kStencil[s];
              const float ox[5] = { 0.0f, o, -o, 0.0f, 0.0f };
              const float oz[5] = { 0.0f, 0.0f, 0.0f, o, -o };
              for (int k = 0; k < 5; ++k) {
                const float px = rv[vi].position.x + ox[k];
                const float pz = rv[vi].position.z + oz[k];
                float roadY = -std::numeric_limits<float>::infinity();
                for (uint32_t t : incident[vi]) {
                  const glm::vec3 A = rv[ri[t * 3 + 0]].position;
                  const glm::vec3 B = rv[ri[t * 3 + 1]].position;
                  const glm::vec3 C = rv[ri[t * 3 + 2]].position;
                  const float area = (B.x - A.x) * (C.z - A.z) - (B.z - A.z) * (C.x - A.x);
                  if (std::fabs(area) < 1e-8f) continue;
                  const float wb = ((px - A.x) * (C.z - A.z) - (pz - A.z) * (C.x - A.x)) / area;
                  const float wc = ((B.x - A.x) * (pz - A.z) - (B.z - A.z) * (px - A.x)) / area;
                  const float wa = 1.0f - wb - wc;
                  if (wa < 0.0f || wb < 0.0f || wc < 0.0f) continue;
                  roadY = std::max(roadY, wa * A.y + wb * B.y + wc * C.y);
                }
                if (roadY == -std::numeric_limits<float>::infinity()) continue;
                const float proud = sampleVisual(px, pz) - roadY;
                ++probes;
                // The chunks triangulate each quad along the tl-br diagonal, so the
                // drawn surface is piecewise-LINEAR, not bilinear. Measure against the
                // triangles that share this point's cell: BOTH of them, taking the
                // lower, because a road has to clear the lowest ground drawn under it,
                // not whichever triangle happens to contain the sample.
                {
                  const float u = glm::clamp((px - wMinX) / (wMaxX - wMinX) * (vw - 1),
                                             0.0f, (float)(vw - 1));
                  const float v = glm::clamp((pz - wMinZ) / (wMaxZ - wMinZ) * (vh - 1),
                                             0.0f, (float)(vh - 1));
                  const int i0 = std::min((int)u, vw - 2);
                  const int j0 = std::min((int)v, vh - 2);
                  const float tx = u - i0, tz = v - j0;
                  const float h00 = H[(size_t)j0 * vw + i0];
                  const float h10 = H[(size_t)j0 * vw + i0 + 1];
                  const float h01 = H[(size_t)(j0 + 1) * vw + i0];
                  const float h11 = H[(size_t)(j0 + 1) * vw + i0 + 1];
                  // Chunk indices: tl,bl,tr and tr,bl,br -> both share the tl-br diagonal.
                  const float triTop = h00 + (h01 - h00) * tz + (h10 - h00) * tx;
                  const float triBot = h11 + (h10 - h11) * (1.0f - tz) +
                                       (h01 - h11) * (1.0f - tx);
                  const float meshY = std::min(triTop, triBot);
                  const float meshProud = meshY - roadY;
                  ++meshProbes;
                  if (meshProud > 0.25f) ++meshPoke025;
                  if (meshProud > 1.0f) ++meshPoke1m;
                  if (meshProud > meshWorstPoke) {
                    meshWorstPoke = meshProud;
                    meshWorstX = px; meshWorstZ = pz;
                    meshWorstRoadY = roadY;
                  }
                }
                if (proud > worstPoke) {
                  worstPoke = proud;
                  worstPokeX = px; worstPokeZ = pz; worstPokeRoadY = roadY;
                }
                if (proud > 0.25f) ++poke025;
                if (proud > 1.0f) ++poke1m;
              }
            }
          }
          std::cout << "[RoadGap] cell=" << cellSize << "m verts=" << n
                    << " meanGap=" << (sum / n)
                    << " meanAbs=" << (sumAbs / n)
                    << " maxAbs=" << maxAbs
                    << " above0.5m=" << above05
                    << " above2m=" << above2
                    << " | proud>1m=" << terrainAbove1
                    << " maxProud=" << maxAbove
                    << " || probes=" << probes
                    << " poke>0.25m=" << poke025
                    << " poke>1m=" << poke1m
                    << " worstPoke=" << worstPoke
                    << " at=(" << worstPokeX << "," << worstPokeZ << ")"
                    << " terrainY=" << sampleVisual(worstPokeX, worstPokeZ)
                    << " roadY=" << worstPokeRoadY
                    << " worldX=[" << wMinX << "," << wMaxX << "]"
                    << " worldZ=[" << wMinZ << "," << wMaxZ << "]"
                    << " || MESH-model poke>0.25m=" << meshPoke025
                    << " poke>1m=" << meshPoke1m
                    << " worstPoke=" << meshWorstPoke
                    << " at=(" << meshWorstX << "," << meshWorstZ << ")"
                    << " roadY=" << meshWorstRoadY
                    << " of " << meshProbes
                    << std::endl;
        }
      }


      // 5. Derived 4-direction auto-tiling field (read-only consequence of the
      //    evaluated splines: connectivity queries, shape classification).
      roadTileField = new RoadTileField();
      roadTileField->build(roadGraph->segments, roadMapper);

      std::cout << "[Terrain] Road System ready: "
                << roadGraph->segments.size() << " segments, "
                << roadMeshBuilder->getVertexCount() << " vertices, "
                << roadMeshBuilder->getTriangleCount() << " triangles" << std::endl;
  }
  // Load real water normal maps and foam from assets (fall back to procedural if missing)
  auto loadWaterTex = [](const char* path, unsigned int fallback) -> unsigned int {
    int w, h, ch;
    unsigned char* d = stbi_load(path, &w, &h, &ch, 0);
    if (!d) {
      std::cout << "[Terrain] Water texture not found, using procedural: " << path << std::endl;
      return fallback;
    }
    unsigned int tid;
    glGenTextures(1, &tid);
    glBindTexture(GL_TEXTURE_2D, tid);
    GLenum fmt = (ch == 1) ? GL_RED : (ch == 3 ? GL_RGB : GL_RGBA);
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, d);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(d);
    std::cout << "[Terrain] Loaded water texture: " << path << " (" << w << "x" << h << ")" << std::endl;
    return tid;
  };

  unsigned int proceduralNorm1 = buildNormalMap(512);
  unsigned int proceduralNorm2 = buildNormalMap(512);
  unsigned int proceduralFoam  = buildFoamMap(512);

  flatNormalTex = proceduralNorm1;
  normalMap1    = loadWaterTex("assets/terrain/water/normal1.png", proceduralNorm1);
  normalMap2    = loadWaterTex("assets/terrain/water/normal2.png", proceduralNorm2);
  foamTex       = loadWaterTex("assets/terrain/water/foam.png",    proceduralFoam);

  // Tarred gravel for the road surface. sRGB, because it is colour not data, so the
  // GPU linearises it before the lighting maths. Missing file leaves this at 0 and
  // street.frag falls back to its procedural asphalt.
  roadSurfaceTex = TextureEngine::Instance().loadTexture(
      "assets/terrain/tarred_gravel_2k/textures/tarred_gravel_diff_2k.jpg", true);
  // Normal map stays LINEAR: normals are data, not colour, so it must not go
  // through the sRGB decode. The _gl variant is the OpenGL green-channel convention.
  roadNormalTex = TextureEngine::Instance().loadTexture(
      "assets/terrain/tarred_gravel_2k/textures/tarred_gravel_nor_gl_2k.exr", false);
  if (roadNormalTex == 0) {
    // stb_image cannot read EXR; fall back to the PNG (dx convention, green flipped
    // in the shader would be needed -- acceptable, the relief direction is subtle on
    // a road surface).
    roadNormalTex = TextureEngine::Instance().loadTexture(
        "assets/terrain/tarred_gravel_2k/textures/tarred_gravel_nor_dx_2k.png", false);
  }
  std::cout << "[Terrain] Road surface texture: "
            << (roadSurfaceTex ? "tarred gravel" : "MISSING - procedural fallback")
            << ", normal map: " << (roadNormalTex ? "yes" : "no")
            << std::endl;

  // Initialize dynamic waterMapTex (256x256 single-channel floating-point texture)
  glGenTextures(1, &waterMapTex);
  glBindTexture(GL_TEXTURE_2D, waterMapTex);
  std::vector<float> initiallyDry(WaterSimulation::SIM_SIZE * WaterSimulation::SIM_SIZE, -1000.0f);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, WaterSimulation::SIM_SIZE, WaterSimulation::SIM_SIZE,
               0, GL_RED, GL_FLOAT, initiallyDry.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D, 0);

  cityWallModel = new ObjModel("assets/moniment/mycityWall.fbx");
  rockModel = new ObjModel("assets/Rock1/Rock1.obj");
  assetCache["assets/Rock1/Rock1.obj"] = rockModel;

  std::cout << "[Terrain] Pre-loading Tree and Grass models..." << std::endl;
  assetCache["assets/Tree_02/Tree.obj"] = new ObjModel("assets/Tree_02/Tree.obj");
  assetCache["assets/terrain/grass_medium_01_2k.fbx"] = new ObjModel("assets/terrain/grass_medium_01_2k.fbx");

  std::cout << "[Terrain] Initialization done." << std::endl;
}

Terrain::~Terrain() {
  if (waterVAO)
    glDeleteVertexArrays(1, &waterVAO);
  if (waterVBO)
    glDeleteBuffers(1, &waterVBO);
  if (waterEBO)
    glDeleteBuffers(1, &waterEBO);
  if (streetVAO)
    glDeleteVertexArrays(1, &streetVAO);
  if (streetVBO)
    glDeleteBuffers(1, &streetVBO);
  if (streetEBO)
    glDeleteBuffers(1, &streetEBO);
  if (wallVAO)
    glDeleteVertexArrays(1, &wallVAO);
  if (wallVBO)
    glDeleteBuffers(1, &wallVBO);
  if (wallEBO)
    glDeleteBuffers(1, &wallEBO);

  // Cleanup Dynamic Asset Cache
  for (auto const &[path, model] : assetCache) {
    if (model != rockModel) {
      delete model;
    }
  }
  assetCache.clear();

  delete terrainManager;
  delete terrainShader;
  delete oceanShader;
  delete streetShader;
  delete modelShader;
  delete cityWallModel;
  delete rockModel;

  // Cleanup water FBOs
  if (reflFBO) { glDeleteFramebuffers(1, &reflFBO); glDeleteTextures(1, &reflColorTex); glDeleteRenderbuffers(1, &reflDepthRBO); }
  if (refrFBO) { glDeleteFramebuffers(1, &refrFBO); glDeleteTextures(1, &refrColorTex); glDeleteTextures(1, &refrDepthTex); }

  // Cleanup water textures. flatNormalTex is the procedural fallback that
  // normalMap1 may alias, so it is deleted once and aliases are excluded.
  if (normalMap1 && normalMap1 != flatNormalTex) glDeleteTextures(1, &normalMap1);
  if (normalMap2 && normalMap2 != flatNormalTex) glDeleteTextures(1, &normalMap2);
  if (foamTex    && foamTex    != flatNormalTex) glDeleteTextures(1, &foamTex);
  if (flatNormalTex) glDeleteTextures(1, &flatNormalTex);
  if (waterMapTex)   glDeleteTextures(1, &waterMapTex);
  if (mapTexture)    glDeleteTextures(1, &mapTexture);
  waterMapTex = flatNormalTex = normalMap1 = normalMap2 = foamTex = mapTexture = 0;

  // roadGeometryMask / heightmapTexID / texture arrays are owned by TerrainManager,
  // which is deleted above.

  // Cleanup road system
  delete roadGraph;
  delete roadDeformer;
  delete roadMeshBuilder;
  delete roadTileField;
}

void Terrain::buildWaterFBOs(int width, int height) {
  // Halve resolution for performance â€” reflections don't need full res
  int w = width  / 2;
  int h = height / 2;
  fboW = w; fboH = h;

  // â”€â”€ REFLECTION FBO â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  glGenFramebuffers(1, &reflFBO);
  glBindFramebuffer(GL_FRAMEBUFFER, reflFBO);

  glGenTextures(1, &reflColorTex);
  glBindTexture(GL_TEXTURE_2D, reflColorTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, reflColorTex, 0);

  glGenRenderbuffers(1, &reflDepthRBO);
  glBindRenderbuffer(GL_RENDERBUFFER, reflDepthRBO);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, reflDepthRBO);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    std::cerr << "[Terrain] Reflection FBO incomplete!" << std::endl;

  // â”€â”€ REFRACTION FBO â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  glGenFramebuffers(1, &refrFBO);
  glBindFramebuffer(GL_FRAMEBUFFER, refrFBO);

  glGenTextures(1, &refrColorTex);
  glBindTexture(GL_TEXTURE_2D, refrColorTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, refrColorTex, 0);

  // Attach a DEPTH TEXTURE (not renderbuffer) so we can sample it in the water shader
  glGenTextures(1, &refrDepthTex);
  glBindTexture(GL_TEXTURE_2D, refrDepthTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, refrDepthTex, 0);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    std::cerr << "[Terrain] Refraction FBO incomplete!" << std::endl;

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  fbosReady = true;
  std::cout << "[Terrain] Water FBOs created: " << w << "x" << h << std::endl;
}

void Terrain::initStreets() {}
float Terrain::distToSegment(float px, float pz, float x1, float z1, float x2,
                             float z2) const {
  return 0.0f;
}

struct WaterVertex {
  glm::vec3 pos;
  glm::vec2 uv;
};

void Terrain::buildWaterMesh() {
  std::vector<WaterVertex> verts;
  std::vector<unsigned int> idx;
  int W = 256, H = 256; // Optimized density for high performance Gerstner waves
  float startX = -13938.0f, endX = 13938.0f;
  float startZ = -13938.0f, endZ = 13938.0f;

  for (int i = 0; i < H; ++i) {
    float fz = (float)i / (H - 1);
    float rawZ = startZ + fz * (endZ - startZ);
    float z = rawZ;
    if (i == 0)
      z = -100000.0f;
    else if (i == H - 1)
      z = 100000.0f;
    for (int j = 0; j < W; ++j) {
      float fx = (float)j / (W - 1);
      float rawX = startX + fx * (endX - startX);
      float x = rawX;
      if (j == 0)
        x = -100000.0f;
      else if (j == W - 1)
        x = 100000.0f;

      float u = (rawX - startX) / (endX - startX);
      float v = (rawZ - startZ) / (endZ - startZ);
      verts.push_back({glm::vec3(x, WATER_BASE, z), glm::vec2(u, v)});
    }
  }
  for (int i = 0; i < H - 1; ++i) {
    for (int j = 0; j < W - 1; ++j) {
      int tl = i * W + j, tr = tl + 1, bl = tl + W, br = tl + W + 1;
      idx.push_back(tl);
      idx.push_back(bl);
      idx.push_back(tr);
      idx.push_back(tr);
      idx.push_back(bl);
      idx.push_back(br);
    }
  }
  waterIndexCount = (int)idx.size();
  glGenVertexArrays(1, &waterVAO);
  glGenBuffers(1, &waterVBO);
  glGenBuffers(1, &waterEBO);
  glBindVertexArray(waterVAO);
  glBindBuffer(GL_ARRAY_BUFFER, waterVBO);
  glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(WaterVertex), verts.data(),
               GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, waterEBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int),
               idx.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(WaterVertex), (void *)offsetof(WaterVertex, pos));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(WaterVertex), (void *)offsetof(WaterVertex, uv));
  glBindVertexArray(0);
}

void Terrain::buildStreetMesh() {
  std::string binPath = "assets/roads/road_mesh.bin";
  FILE* f = fopen(binPath.c_str(), "rb");
  if (!f) {
    std::cout << "[Terrain] No binary road mesh found at " << binPath << std::endl;
    return;
  }
  char magic[4];
  if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "ROAD", 4) != 0) {
    fclose(f);
    return;
  }
  uint32_t vCount = 0, iCount = 0;
  // A truncated file must not drive the resizes below: a garbage count turns into
  // a multi-gigabyte allocation, and a short read leaves the buffers half-filled.
  if (fread(&vCount, sizeof(uint32_t), 1, f) != 1 ||
      fread(&iCount, sizeof(uint32_t), 1, f) != 1) {
    std::cerr << "[Terrain] " << binPath << ": truncated header" << std::endl;
    fclose(f);
    return;
  }
  constexpr uint32_t kMaxMeshVertices = 8u * 1024u * 1024u;
  constexpr uint32_t kMaxMeshIndices  = 24u * 1024u * 1024u;
  if (vCount == 0 || iCount == 0) {
    std::cerr << "[Terrain] " << binPath << ": empty road mesh" << std::endl;
    fclose(f);
    return;
  }
  if (vCount > kMaxMeshVertices || iCount > kMaxMeshIndices) {
    std::cerr << "[Terrain] " << binPath << ": implausible counts (v=" << vCount
              << " i=" << iCount << "), ignoring" << std::endl;
    fclose(f);
    return;
  }

  std::vector<float> vData((size_t)vCount * 9);
  std::vector<uint32_t> iData((size_t)iCount);
  if (fread(vData.data(), sizeof(float), vData.size(), f) != vData.size() ||
      fread(iData.data(), sizeof(uint32_t), iData.size(), f) != iData.size()) {
    std::cerr << "[Terrain] " << binPath << ": truncated payload, ignoring"
              << std::endl;
    fclose(f);
    return;
  }
  fclose(f);

  streetVertexCount = (int)vCount;
  streetIndexCount = (int)iCount;

  if (streetVAO == 0) glGenVertexArrays(1, &streetVAO);
  if (streetVBO == 0) glGenBuffers(1, &streetVBO);
  if (streetEBO == 0) glGenBuffers(1, &streetEBO);

  glBindVertexArray(streetVAO);

  glBindBuffer(GL_ARRAY_BUFFER, streetVBO);
  glBufferData(GL_ARRAY_BUFFER, vData.size() * sizeof(float), vData.data(), GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, streetEBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, iData.size() * sizeof(uint32_t), iData.data(), GL_STATIC_DRAW);

  // Layout 0: vec3 aPos
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);

  // Layout 1: vec3 aNormal
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(3 * sizeof(float)));

  // Layout 2: vec2 aUV
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(6 * sizeof(float)));

  // Layout 3: float aType
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(8 * sizeof(float)));

  glBindVertexArray(0);
  std::cout << "[Terrain] Successfully loaded 3D Road Ribbon Mesh: " << vCount 
            << " vertices, " << (iCount / 3) << " triangles." << std::endl;
}

namespace {
bool roadDebugView() {
    static const bool on = (std::getenv("FALLAGA_DEBUGVIEW") != nullptr);
    return on;
}

// FALLAGA_EDGEONLY: draw nothing but the road's own edge lines. The surface, the
// shoulder and the paint are all discarded in the fragment shader, leaving the ribbon
// outline on its own so ribbon deformation (pinch, twist, self-overlap, torn caps) can
// be seen directly instead of being hidden by tarmac shading.
bool roadEdgeOnlyView() {
    static const bool on = (std::getenv("FALLAGA_EDGEONLY") != nullptr);
    return on;
}

// Every uniform the road surface texture, its brightness and its normal map need.
//
// Shared by the spline ribbon and the tile grid, because both draw through the same
// street shader. It is a function rather than duplicated code because getting it wrong
// is silent and total: the tile path originally set only roadSurfaceValid, leaving
// roadSurfaceGain at its default 0, so every tile road rendered pure black.
void setRoadSurfaceUniforms(Shader& shader, unsigned int surfaceTex, unsigned int normalTex,
                            bool noRoadTex) {
    shader.setInt("roadSurfaceTex", 14);
    shader.setInt("roadSurfaceValid", (surfaceTex != 0 && !noRoadTex) ? 1 : 0);
    // Metres per texture repeat. The source is a 2 km gravel scan, so ~6 m keeps the
    // aggregate reading at roughly its real size on screen.
    shader.setFloat("roadSurfaceScale", 6.0f);
    // The scan is authored dark and the albedo is multiplied by ambient+NdotL, so it
    // needs a gain to reach a usable brightness. Overridable from the environment so
    // the look can be dialled without a rebuild:
    //   FALLAGA_ROAD_GAIN=3.0  FALLAGA_ROAD_TINT=1.05,0.98,0.88
    static const float roadGain = [] {
        if (const char* e = std::getenv("FALLAGA_ROAD_GAIN")) {
            const float v = std::strtof(e, nullptr);
            if (v > 0.0f) return v;
        }
        return 3.2f;
    }();
    static const glm::vec3 roadTint = [] {
        if (const char* e = std::getenv("FALLAGA_ROAD_TINT")) {
            float r = 0, g = 0, b = 0;
            if (std::sscanf(e, "%f,%f,%f", &r, &g, &b) == 3)
                return glm::vec3(r, g, b);
        }
        return glm::vec3(1.06f, 1.00f, 0.90f);
    }();
    shader.setFloat("roadSurfaceGain", roadGain);
    shader.setVec3("roadSurfaceTint", roadTint);
    // Gravel relief, on the next unit up. Strength is overridable so the relief can be
    // turned down if it reads as noise at driving height.
    glActiveTexture(GL_TEXTURE15);
    glBindTexture(GL_TEXTURE_2D, normalTex);
    shader.setInt("roadNormalTex", 15);
    shader.setInt("roadNormalValid", (normalTex != 0 && !noRoadTex) ? 1 : 0);
    static const float roadNormalStrength = [] {
        if (const char* e = std::getenv("FALLAGA_ROAD_NORMAL")) {
            const float v = std::strtof(e, nullptr);
            if (v >= 0.0f) return v;
        }
        return 0.85f;
    }();
    shader.setFloat("roadNormalStrength", roadNormalStrength);
}
}  // namespace

void Terrain::renderStreets(const glm::mat4& proj, const glm::mat4& view,
                            const glm::vec3& lightDir, const glm::vec3& viewPos) const {
  if (streetVAO == 0 || streetIndexCount == 0 || !streetShader) return;

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_TRUE);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(-3.0f, -3.0f);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  streetShader->use();
  streetShader->setMat4("projection", proj);
  streetShader->setMat4("view", view);
  streetShader->setMat4("model", glm::mat4(1.0f));
  streetShader->setVec3("lightDir", lightDir);
  streetShader->setVec3("viewPos", viewPos);

  const bool dbg = roadDebugView();
  if (dbg) { streetShader->setInt("u_debugUV", 1); glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); }
  if (roadEdgeOnlyView()) streetShader->setInt("u_debugEdges", 1);

  glBindVertexArray(streetVAO);
  glDrawElements(GL_TRIANGLES, streetIndexCount, GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);

  if (dbg) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glDisable(GL_BLEND);
  glDisable(GL_POLYGON_OFFSET_FILL);
}

void Terrain::renderOpaque(const glm::mat4 &proj, const glm::mat4 &view,
                           const glm::vec3 &cam, const glm::vec3 &light,
                           const glm::vec3 &lightColor, int selectedIdx,
                           bool editMode, int selectedAssetIdx,
                           float previewAngle, float previewScale,
                           bool isReflectionPass) const {
  // Extract view frustum planes from View-Projection Matrix
  glm::mat4 VP = proj * view;
  glm::mat4 transVP = glm::transpose(VP);
  glm::vec4 planes[6];
  planes[0] = transVP[3] + transVP[0]; // Left
  planes[1] = transVP[3] - transVP[0]; // Right
  planes[2] = transVP[3] + transVP[1]; // Bottom
  planes[3] = transVP[3] - transVP[1]; // Top
  planes[4] = transVP[3] + transVP[2]; // Near
  planes[5] = transVP[3] - transVP[2]; // Far

  for (int i = 0; i < 6; ++i) {
      float len = glm::length(glm::vec3(planes[i]));
      if (len > 0.0001f) {
          planes[i] /= len;
      }
  }

  static std::vector<glm::mat4> proceduralTrees;
  static std::vector<glm::mat4> proceduralRocks;
  static std::vector<glm::mat4> proceduralGrass;
  static bool proceduralInitialized = false;

  if (!proceduralInitialized && terrainManager) {
      FILE* f = fopen("assets/terrain/foliage.bin", "rb");
      if (f) {
          char magic[4];
          if (fread(magic, 1, 4, f) == 4 && memcmp(magic, "FOLI", 4) == 0) {
              uint32_t numTrees = 0, numRocks = 0, numGrass = 0;
              bool headerOk = fread(&numTrees, sizeof(uint32_t), 1, f) == 1 &&
                              fread(&numRocks, sizeof(uint32_t), 1, f) == 1 &&
                              fread(&numGrass, sizeof(uint32_t), 1, f) == 1;
              // Cap the counts before resizing: a truncated or corrupt file would
              // otherwise request an enormous allocation from a garbage header.
              constexpr uint32_t kMaxInstances = 4u * 1024u * 1024u;
              const bool countsSane = numTrees <= kMaxInstances &&
                                      numRocks <= kMaxInstances &&
                                      numGrass <= kMaxInstances;
              if (headerOk && countsSane) {
                  proceduralTrees.resize(numTrees);
                  proceduralRocks.resize(numRocks);
                  proceduralGrass.resize(numGrass);

                  const bool payloadOk =
                      fread(proceduralTrees.data(), sizeof(glm::mat4), numTrees, f) == numTrees &&
                      fread(proceduralRocks.data(), sizeof(glm::mat4), numRocks, f) == numRocks &&
                      fread(proceduralGrass.data(), sizeof(glm::mat4), numGrass, f) == numGrass;

                  if (payloadOk) {
                      proceduralInitialized = true;
                      std::cout << "[Terrain] Successfully loaded binary roadside foliage: "
                                << numTrees << " trees, "
                                << numRocks << " rocks, "
                                << numGrass << " grass patches." << std::endl;
                  } else {
                      std::cerr << "[Terrain] foliage.bin is truncated; regenerating procedurally"
                                << std::endl;
                      proceduralTrees.clear();
                      proceduralRocks.clear();
                      proceduralGrass.clear();
                  }
              } else {
                  std::cerr << "[Terrain] foliage.bin has an unusable header; regenerating procedurally"
                            << std::endl;
              }
          }
          fclose(f);
      }

      if (!proceduralInitialized) {
          std::cout << "[Terrain] Generating RDR2-style procedural foliage (lightweight budget)..." << std::endl;
          srand(12345); // Seed for deterministic generation

          glm::vec2 medinaCenter = {-530.0f, -1502.0f};

      // --- Simple noise function for natural patchiness ---
      auto noiseAt = [](float x, float z) -> float {
          float n = sinf(x * 0.017f) * cosf(z * 0.013f) * 0.5f
                  + sinf(x * 0.043f + z * 0.031f) * 0.25f
                  + cosf(x * 0.0071f - z * 0.0093f) * 0.25f;
          return n * 0.5f + 0.5f; // Normalize to [0, 1]
      };

      // ============================================================
      // PHASE 1: GRASS â€” Clustered carpet placement with tight budget
      // ============================================================
      int grassClusterSeeds = 1800; // Balanced coverage without GPU choke
      for (int s = 0; s < grassClusterSeeds; ++s) {
          float cx = (float)(rand() % 6000 - 3000);
          float cz = (float)(rand() % 6000 - 3000) + 500.0f;

          float distToCity = glm::length(glm::vec2(cx, cz) - medinaCenter);
          if (distToCity < 600.0f) continue;

          float wy = terrainManager->getHeightAt(cx, cz);
          if (wy < -44.0f) continue; // Skip sea

          glm::vec3 normal = terrainManager->getNormalAt(cx, cz);
          float slope = 1.0f - normal.y;

          float noise = noiseAt(cx, cz);
          if (noise < 0.12f) continue; // Natural bare dirt patches

          int clusterSize = 0;
          float baseScale = 0.70f; // Slightly larger footprint covers ground seamlessly
          if (slope < 0.012f) {
              clusterSize = 3 + rand() % 3;  // 3-5 patches per cluster
              baseScale = 0.70f + (rand() % 40) / 100.0f; // 0.70 - 1.10
          } else if (slope < 0.025f) {
              clusterSize = 2 + rand() % 2;  // 2-3 patches
              baseScale = 0.60f + (rand() % 35) / 100.0f; // 0.60 - 0.95
          } else if (slope < 0.038f) {
              clusterSize = 1;               // 1 patch
              baseScale = 0.50f + (rand() % 25) / 100.0f; // 0.50 - 0.75
          } else {
              continue; // No grass on steep cliffs
          }

          for (int p = 0; p < clusterSize; ++p) {
              float spreadRadius = 4.0f + (rand() % 100) / 15.0f; // 4 - 10 meter spread
              float angle = (float)(rand() % 360) * 3.14159f / 180.0f;
              float ox = cosf(angle) * spreadRadius * ((rand() % 100) / 100.0f);
              float oz = sinf(angle) * spreadRadius * ((rand() % 100) / 100.0f);

              float px = cx + ox;
              float pz = cz + oz;

              float py = terrainManager->getHeightAt(px, pz);
              if (py < -44.0f) continue;

              glm::mat4 mat(1.0f);
              mat = glm::translate(mat, glm::vec3(px, py, pz));
              mat = glm::rotate(mat, (float)(rand() % 360) * 3.14159f / 180.0f, glm::vec3(0, 1, 0));

              float distFromCenter = sqrtf(ox * ox + oz * oz) / spreadRadius;
              float scaleMult = 1.0f - distFromCenter * 0.25f;
              float finalScale = baseScale * scaleMult * (0.90f + (rand() % 25) / 100.0f);

              mat = glm::scale(mat, glm::vec3(finalScale));
              proceduralGrass.push_back(mat);
          }
      }

      // ============================================================
      // PHASE 2: TREES AND ROCKS (Using lightweight quiver tree & rock assets)
      // ============================================================
      int placementAttempts = 3500;
      for (int i = 0; i < placementAttempts; ++i) {
          float rx = (float)(rand() % 6000 - 3000);
          float rz = (float)(rand() % 6000 - 3000) + 500.0f;

          float distToCity = glm::length(glm::vec2(rx, rz) - medinaCenter);
          if (distToCity < 600.0f) continue;

          float wy = terrainManager->getHeightAt(rx, rz);
          if (wy < -44.0f) continue; // Skip sea

          glm::vec3 normal = terrainManager->getNormalAt(rx, rz);
          float slope = 1.0f - normal.y;

          glm::mat4 mat(1.0f);
          mat = glm::translate(mat, glm::vec3(rx, wy, rz));
          mat = glm::rotate(mat, (float)(rand() % 360) * 3.14159f / 180.0f, glm::vec3(0, 1, 0));

          if (slope > 0.032f) {
              // Rocks on steep slopes / cliff faces (slope > 14 deg)
              float scale = 2.5f + (rand() % 100 / 25.0f);
              mat = glm::scale(mat, glm::vec3(scale));
              mat[3].y -= scale * 0.25f;
              proceduralRocks.push_back(mat);
          } else if (slope < 0.028f) {
              int roll = rand() % 100;
              if (roll < 7) {
                  // Scattered boulder on flats (7%)
                  float scale = 1.5f + (rand() % 100 / 50.0f);
                  mat = glm::scale(mat, glm::vec3(scale));
                  mat[3].y -= scale * 0.15f;
                  proceduralRocks.push_back(mat);
              } else if (roll < 22) {
                  // Desert / Prairie Tree (quiver_tree_02.fbx, 2.48 MB)
                  float scale = 0.75f + (rand() % 100 / 100.0f) * 0.85f;
                  mat = glm::scale(mat, glm::vec3(scale));
                  proceduralTrees.push_back(mat);
              }
          }
      }

          proceduralInitialized = true;
          std::cout << "[Terrain] RDR2-Style Foliage Generated: " 
                    << proceduralTrees.size() << " trees, " 
                    << proceduralRocks.size() << " rocks, "
                    << proceduralGrass.size() << " grass patches." << std::endl;
      }
  }

  double t_tm_start = glfwGetTime();
  // TEMP DIAGNOSTIC: same-frame A/B scanline readback, see FALLAGA_PROBE.
  static int probeFrame = 0;
  const bool probeNow = !isReflectionPass && std::getenv("FALLAGA_PROBE") && (++probeFrame == 100);
  auto scanline = [&](const char* tag) {
    GLint vpArr[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_VIEWPORT, vpArr);
    const int y0 = vpArr[3] * 2 / 3;
    std::printf("[SCAN %s] vp=%dx%d row y=%d  x=600..1320 step 12\n", tag, vpArr[2], vpArr[3], y0);
    for (int i = 0; i <= 60; ++i) {
      const int px = 600 + i * 12, py = y0;
      unsigned char rgb[3] = { 0, 0, 0 };
      glReadPixels(px, py, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, rgb);
      const int lum = (rgb[0] * 299 + rgb[1] * 587 + rgb[2] * 114) / 1000;
      std::printf("[SCAN %s] x=%4d lum=%3d rgb=(%3d,%3d,%3d)%s\n",
                  tag, px, lum, rgb[0], rgb[1], rgb[2], lum > 120 ? "  <== BRIGHT" : "");
    }
    std::fflush(stdout);
  };
  if (terrainManager) {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    if (isReflectionPass) {
      glm::vec4 reflClip(0.0f, 1.0f, 0.0f, -waterLevelRuntime + 0.1f);
      terrainManager->render(proj, view, cam, light, lightColor, waterLevelRuntime, reflClip, 2500.0f);
    } else {
      float effectiveDrawDist = (cam.y > 100.0f) ? 6000.0f : 2500.0f;
      terrainManager->render(proj, view, cam, light, lightColor, waterLevelRuntime, glm::vec4(0.0f, -1.0f, 0.0f, 100000.0f), effectiveDrawDist);
    }
    if (!isReflectionPass) {
      // ── Tile road builder (Caesar IV style) ────────────────────────────────
      // OFF BY DEFAULT, deliberately. The tile system has correct grid and geometry
      // layers but no materials yet: drawn on its own it is flat dark splotches with no
      // cobble, no curb you can see and jagged tile edges, which is worse than the
      // spline roads it would sit next to. It stays behind an explicit opt-in until
      // phase 4 gives it road materials and a look that stands on its own.
      //   FALLAGA_TILE_ROADS=1  draw the tile layer
      static const bool tileRoadsEnabled = [] {
        const char* e = std::getenv("FALLAGA_TILE_ROADS");
        return e != nullptr && std::atoi(e) != 0;
      }();
      if (tileRoadsEnabled && streetShader) {
        roadgrid::TileRoadRenderer& tr = terrainManager->tileRoadRenderer();
        tr.update(terrainManager->tileRoadGrid());
        if (!tr.empty()) {
          streetShader->use();
          streetShader->setMat4("projection", proj);
          streetShader->setMat4("view", view);
          streetShader->setMat4("model", glm::mat4(1.0f));
          streetShader->setVec3("lightDir", light);
          streetShader->setVec3("viewPos", cam);
          glActiveTexture(GL_TEXTURE14);
          glBindTexture(GL_TEXTURE_2D, roadSurfaceTex);
          // Identical surface setup to the spline path. Setting roadSurfaceValid alone
          // left roadSurfaceGain at its default 0, which multiplied the albedo to black.
          setRoadSurfaceUniforms(*streetShader, roadSurfaceTex, roadNormalTex,
                                 std::getenv("FALLAGA_ROAD_NOTEX") != nullptr);
          // The tile path shades its surface from the world position, which for the
          // tile mesh is also what keeps the stones continuous between cells.
          tr.render(*streetShader);
        }
      }

      if (roadMeshBuilder && roadMeshBuilder->getTriangleCount() > 0 && streetShader) {
        streetShader->use();
        streetShader->setMat4("projection", proj);
        streetShader->setMat4("view", view);
        streetShader->setMat4("model", glm::mat4(1.0f));
        streetShader->setVec3("lightDir", light);
        streetShader->setVec3("viewPos", cam);
        // Tarred gravel surface. Unit 14 is clear of every terrain/water binding
        // (terrain layers 0-7, water 13, scene FBOs 10-12).
        glActiveTexture(GL_TEXTURE14);
        glBindTexture(GL_TEXTURE_2D, roadSurfaceTex);
        // FALLAGA_ROAD_NOTEX forces the procedural asphalt, so the textured and
        // untextured surfaces can be compared at the same camera without a rebuild.
        static const bool noRoadTexShared = std::getenv("FALLAGA_ROAD_NOTEX") != nullptr;
        setRoadSurfaceUniforms(*streetShader, roadSurfaceTex, roadNormalTex,
                               noRoadTexShared);
        const bool dbg = roadDebugView();
        if (dbg) { streetShader->setInt("u_debugUV", 1); glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); }
        if (roadEdgeOnlyView()) streetShader->setInt("u_debugEdges", 1);
        if (!std::getenv("FALLAGA_PROBE_NOMESH")) {
          if (probeNow) scanline("terrain-only");
          roadMeshBuilder->render(*streetShader);
          if (probeNow) scanline("terrain+mesh");
        }
        if (dbg) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
      } else {
        renderStreets(proj, view, light, cam);
      }
    }
  }
  double t_tm_end = glfwGetTime();

  // --- PHASE 1: CACHED BATCH COLLECTION ---
  // Rebuild the matrix cache once per frame IF dirty.
  static bool firstRebuildDone = false;
  if (SousseMapData::layoutDirty || batchesDirty ||
      (!firstRebuildDone && cachedBatches.empty())) {
    // Grid entries hold pointers into these cache containers.
    carCollisionGrid.clear();
    carCollisionGridDirty = true;
    cachedBatches.clear();
    cachedCustomRenderTasks.clear();

    // --- PROCEDURAL CITY WALL REBUILD ---
    if (wallVAO != 0) {
      glDeleteVertexArrays(1, &wallVAO);
      glDeleteBuffers(1, &wallVBO);
      glDeleteBuffers(1, &wallEBO);
      wallVAO = 0;
      // Zero these too: they used to keep their old names, so the re-gen below
      // leaked two buffer objects on every batchesDirty rebuild.
      wallVBO = 0;
      wallEBO = 0;
    }

    std::vector<glm::vec2> perimeter = SousseMapData::getMedinaPerimeter();
    struct WallVertex {
      glm::vec3 position;
      glm::vec3 normal;
      glm::vec2 texCoords;
      glm::vec3 tangent;
    };
    std::vector<WallVertex> vertices;
    std::vector<unsigned int> indices;

    float W = 20.0f; // thickness
    float H = 80.0f; // height (high walls!)

    for (size_t i = 0; i < perimeter.size() - 1; ++i) {
      glm::vec2 A = perimeter[i];
      glm::vec2 B = perimeter[i + 1];
      if (glm::length(B - A) < 0.1f)
        continue;

      float yA = terrainManager ? terrainManager->getHeightAt(A.x, A.y) : 0.0f;
      float yB = terrainManager ? terrainManager->getHeightAt(B.x, B.y) : 0.0f;

      glm::vec2 D = glm::normalize(B - A);
      glm::vec2 N = glm::vec2(-D.y, D.x);

      glm::vec2 C1 = A - N * (W / 2.0f);
      glm::vec2 C2 = B - N * (W / 2.0f);
      glm::vec2 C3 = B + N * (W / 2.0f);
      glm::vec2 C4 = A + N * (W / 2.0f);

      glm::vec3 V1b(C1.x, yA, C1.y);
      glm::vec3 V2b(C2.x, yB, C2.y);
      glm::vec3 V3b(C3.x, yB, C3.y);
      glm::vec3 V4b(C4.x, yA, C4.y);

      glm::vec3 V1t(C1.x, yA + H, C1.y);
      glm::vec3 V2t(C2.x, yB + H, C2.y);
      glm::vec3 V3t(C3.x, yB + H, C3.y);
      glm::vec3 V4t(C4.x, yA + H, C4.y);

      auto addQuad = [&](const glm::vec3 &p1, const glm::vec3 &p2,
                         const glm::vec3 &p3, const glm::vec3 &p4,
                         const glm::vec3 &normal) {
        unsigned int baseIdx = vertices.size();
        vertices.push_back(
            {p1, normal, glm::vec2(0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)});
        vertices.push_back(
            {p2, normal, glm::vec2(1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)});
        vertices.push_back(
            {p3, normal, glm::vec2(1.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f)});
        vertices.push_back(
            {p4, normal, glm::vec2(0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f)});

        indices.push_back(baseIdx + 0);
        indices.push_back(baseIdx + 1);
        indices.push_back(baseIdx + 2);
        indices.push_back(baseIdx + 0);
        indices.push_back(baseIdx + 2);
        indices.push_back(baseIdx + 3);
      };

      // Outer side: V1b -> V2b -> V2t -> V1t
      addQuad(V1b, V2b, V2t, V1t, glm::vec3(-N.x, 0.0f, -N.y));

      // Inner side: V3b -> V4b -> V4t -> V3t
      addQuad(V3b, V4b, V4t, V3t, glm::vec3(N.x, 0.0f, N.y));

      // Top face: V1t -> V4t -> V3t -> V2t
      addQuad(V1t, V4t, V3t, V2t, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    wallIndexCount = indices.size();
    if (wallIndexCount > 0) {
      glGenVertexArrays(1, &wallVAO);
      glGenBuffers(1, &wallVBO);
      glGenBuffers(1, &wallEBO);

      glBindVertexArray(wallVAO);

      glBindBuffer(GL_ARRAY_BUFFER, wallVBO);
      glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(WallVertex),
                   vertices.data(), GL_STATIC_DRAW);

      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, wallEBO);
      glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                   indices.size() * sizeof(unsigned int), indices.data(),
                   GL_STATIC_DRAW);

      // Position
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(WallVertex),
                            (void *)0);
      // Normal
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(WallVertex),
                            (void *)offsetof(WallVertex, normal));
      // TexCoords
      glEnableVertexAttribArray(2);
      glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(WallVertex),
                            (void *)offsetof(WallVertex, texCoords));
      // Tangent
      glEnableVertexAttribArray(7);
      glVertexAttribPointer(7, 3, GL_FLOAT, GL_FALSE, sizeof(WallVertex),
                            (void *)offsetof(WallVertex, tangent));

      glBindVertexArray(0);
    }

    int placedCount = 0;
    for (auto &landmark : SousseMapData::liveLandmarks) {
      if (!landmark.placed)
        continue;
      if (landmark.modelPath.empty())
        continue;

      glm::vec2 worldPos = SousseMapData::toWorld(landmark.localOffset);
      float finalY = landmark.bakedY;
      if (finalY < -500000.0) { // Not baked
        Vec3 snapPos(worldPos.x, 0, worldPos.y);
        PhysicsSystem::snapToGround(snapPos, this, 0.1f);
        landmark.bakedY = snapPos.y;
        finalY = snapPos.y;
      }

      // Calculate Transform ONCE and store in cache
      glm::mat4 modelMat = glm::translate(
          glm::mat4(1.0f),
          glm::vec3(worldPos.x, finalY + landmark.yOffset, worldPos.y));

      // Align object up-axis to terrain normal
      glm::vec3 terrainNormal = glm::vec3(0, 1, 0);
      if (terrainManager) {
          terrainNormal = glm::normalize(terrainManager->getNormalAt(worldPos.x, worldPos.y));
      }
      glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
      float dotNU = glm::dot(terrainNormal, worldUp);
      if (dotNU < 0.999f) { // Only rotate if normal deviates from straight up
          glm::vec3 rotAxis = glm::normalize(glm::cross(worldUp, terrainNormal));
          float rotAngle = acosf(glm::clamp(dotNU, -1.0f, 1.0f));
          modelMat = glm::rotate(modelMat, rotAngle, rotAxis);
      }

      modelMat = glm::rotate(modelMat, glm::radians(landmark.angle),
                             glm::vec3(0, 1, 0));
      modelMat =
          glm::scale(modelMat, glm::vec3(landmark.xScale, landmark.yScale,
                                         landmark.zScale));

      if (!landmark.customTexturePath.empty() || !landmark.customTexturePath2.empty()) {
          cachedCustomRenderTasks.push_back({
              landmark.modelPath,
              modelMat,
              landmark.customTexturePath,
              landmark.customTexturePath2,
              landmark.textureBlend
          });
      } else {
          cachedBatches[landmark.modelPath].push_back(modelMat);
          placedCount++;
      }
    }
    
    if (proceduralInitialized) {
        for (auto const &mat : proceduralTrees) {
            cachedBatches["assets/Tree_02/Tree.obj"].push_back(mat);
        }
        for (auto const &mat : proceduralRocks) {
            cachedBatches["assets/Rock1/Rock1.obj"].push_back(mat);
        }
        for (auto const &mat : proceduralGrass) {
            cachedBatches["assets/terrain/grass_medium_01_2k.fbx"].push_back(mat);
        }
    }

    SousseMapData::layoutDirty = false;
    batchesDirty = false;
    firstRebuildDone = true;
    if (placedCount > 0) {
      std::cout << "[Librarian] : Rendering cache rebuilt for " << placedCount
                << " objects.\n";
    }
    rebuildCarCollisionGrid();
  }

  // --- PHASE 2: MODERN INSTANCED RENDERING ---
  if (modelShader) {
    modelShader->use();
    modelShader->setMat4("projection", proj);
    modelShader->setMat4("view", view);
    modelShader->setVec3("viewPos", cam);
    modelShader->setVec3("lightDir", light);
    modelShader->setVec3("lightColor", glm::vec3(1.1f, 1.05f, 0.95f));
    modelShader->setVec3("ambientColor", glm::vec3(0.4f, 0.45f, 0.5f));
    modelShader->setBool("hasTexture", true);
    modelShader->setBool("hasNormalMap", true);
    modelShader->setBool("deformToTerrain", false);

    bool dataChanged = batchesDirty || SousseMapData::layoutDirty;

    // GTA-style: only recompute visibility when the camera moved enough to
    // matter, or periodically as a fallback (catches rotation-only changes
    // that a position-only check would miss). Otherwise reuse last frame's
    // culled instance lists and skip re-uploading GPU buffers entirely.
    const float CULL_REFRESH_DIST = 8.0f;
    const int CULL_REFRESH_MAX_FRAMES = 12;
    if (!isReflectionPass) {
        cullFrameCounter++;
    }
    bool needsCullRefresh = !isReflectionPass && (dataChanged || cachedCulledBatches.empty() ||
                            glm::distance(cam, lastCullCamPos) > CULL_REFRESH_DIST ||
                            cullFrameCounter >= CULL_REFRESH_MAX_FRAMES);
    if (needsCullRefresh && !isReflectionPass) {
      lastCullCamPos = cam;
      cullFrameCounter = 0;
    }

    for (auto const &[path, matrices] : cachedBatches) {
      if (assetCache.find(path) == assetCache.end()) {
        assetCache[path] = new ObjModel(path.c_str());
      }
      ObjModel *m = assetCache[path];
      if (m) {
        bool isDebris = (path.find("rock") != std::string::npos ||
                         path.find("Rock") != std::string::npos ||
                         path.find("rubble") != std::string::npos);
        bool isTree = (path.find("Tree") != std::string::npos ||
                       path.find("tree") != std::string::npos);
        bool isGrass = (path.find("grass") != std::string::npos ||
                        path.find("Grass") != std::string::npos);

        // In water reflection, skip grass, clutter, and trees entirely (only reflect city structures)
        if (isReflectionPass && (isGrass || isDebris || isTree)) {
            continue;
        }

        if (isReflectionPass) {
            // Fast lightweight reflection rendering (only objects above water within reflection distance)
            std::vector<glm::mat4> reflMatrices;
            reflMatrices.reserve(std::min(matrices.size(), (size_t)64));
            float maxDist = 1500.0f;
            float maxDistSq = maxDist * maxDist;
            for (const auto& mat : matrices) {
                glm::vec3 pos(mat[3]);
                if (pos.y < waterLevelRuntime - 1.0f) continue;
                float distSq = glm::dot(pos - cam, pos - cam);
                if (distSq < maxDistSq) {
                    bool inFrustum = true;
                    for (int p = 0; p < 6; ++p) {
                        if (glm::dot(glm::vec3(planes[p]), pos) + planes[p].w < -25.0f) {
                            inFrustum = false;
                            break;
                        }
                    }
                    if (inFrustum) {
                        reflMatrices.push_back(mat);
                    }
                }
            }
            if (!reflMatrices.empty()) {
                m->renderInstanced(*modelShader, reflMatrices);
            }
            continue;
        }

        bool needsThisBatchRefresh = needsCullRefresh ||
            cachedCulledBatches.find(path) == cachedCulledBatches.end();

        if (needsThisBatchRefresh) {
          // Perform distance and frustum-based culling for ALL meshes
          std::vector<glm::mat4> culledMatrices;
          culledMatrices.reserve(matrices.size());

          float maxDist = 2000.0f;
          float radius = 35.0f;
          if (isGrass) { maxDist = 150.0f; radius = 3.0f; }
          else if (isTree) { maxDist = 550.0f; radius = 20.0f; }
          else if (isDebris) { maxDist = 350.0f; radius = 6.0f; }

          float maxDistSq = maxDist * maxDist;
          for (const auto& mat : matrices) {
              glm::vec3 pos(mat[3]); // Column 3 contains translation vector
              float distSq = glm::dot(pos - cam, pos - cam);
              if (distSq < maxDistSq) {
                  bool inFrustum = true;
                  for (int p = 0; p < 6; ++p) {
                      if (glm::dot(glm::vec3(planes[p]), pos) + planes[p].w < -radius) {
                          inFrustum = false;
                          break;
                      }
                  }
                  if (inFrustum) {
                      culledMatrices.push_back(mat);
                  }
              }
          }

          cachedCulledBatches[path] = std::move(culledMatrices);
        }

        std::vector<glm::mat4>& culledMatrices = cachedCulledBatches[path];

        static int printCount = 0;
        if (isGrass && printCount++ % 60 == 0) {
            std::cout << "[Foliage Debug] Rendering " << culledMatrices.size()
                      << " grass patches (culled from " << matrices.size()
                      << "). CamPos: " << cam.x << ", " << cam.y << ", " << cam.z << std::endl;
        }
        if (isTree && printCount % 60 == 0) {
            std::cout << "[Tree Debug] Rendering " << culledMatrices.size()
                      << " trees (culled from " << matrices.size() << ")" << std::endl;
        }

        // Only re-upload the GPU instance buffer when the culled set actually changed
        m->renderInstanced(*modelShader, culledMatrices, needsThisBatchRefresh);
      }
    }
  }

  // --- PHASE 2.5: INDIVIDUAL CUSTOM TEXTURE RENDERING & EDITOR PREVIEW ---
  if (modelShader) {
      modelShader->setBool("isInstanced", false);
      
      // Render custom texture landmarks
      if (!cachedCustomRenderTasks.empty()) {
          for (const auto& task : cachedCustomRenderTasks) {
              if (assetCache.find(task.modelPath) == assetCache.end()) {
                  assetCache[task.modelPath] = new ObjModel(task.modelPath.c_str());
              }
              ObjModel *m = assetCache[task.modelPath];
              if (m) {
                  glm::vec3 pos(task.modelMatrix[3]);
                  
                  // Frustum culling check (sphere radius 35.0f for landmarks)
                  bool inFrustum = true;
                  for (int p = 0; p < 6; ++p) {
                      if (glm::dot(glm::vec3(planes[p]), pos) + planes[p].w < -35.0f) {
                          inFrustum = false;
                          break;
                      }
                  }
                   if (inFrustum) {
                       modelShader->setMat4("model", task.modelMatrix);
                       
                       unsigned int texID1 = 0;
                       if (!task.texturePath.empty()) {
                           texID1 = TextureEngine::Instance().loadTexture(task.texturePath);
                       }
                       
                       unsigned int texID2 = 0;
                       if (!task.texturePath2.empty()) {
                           texID2 = TextureEngine::Instance().loadTexture(task.texturePath2);
                       }

                       if (texID1 != 0) {
                           glActiveTexture(GL_TEXTURE0);
                           glBindTexture(GL_TEXTURE_2D, texID1);
                           modelShader->setInt("texture_diffuse1", 0);
                           modelShader->setBool("hasTexture", true);
                       }
                       
                       if (texID2 != 0) {
                           glActiveTexture(GL_TEXTURE2);
                           glBindTexture(GL_TEXTURE_2D, texID2);
                           modelShader->setInt("texture_diffuse2", 2);
                           modelShader->setBool("hasTexture2", true);
                       } else {
                           modelShader->setBool("hasTexture2", false);
                       }
                       
                       modelShader->setFloat("blendWeight", task.blendWeight);

                       m->render(*modelShader, texID1);

                       glActiveTexture(GL_TEXTURE2);
                       glBindTexture(GL_TEXTURE_2D, 0);
                       glActiveTexture(GL_TEXTURE0);
                   }
              }
          }
      }

      // Render Editor Preview Object
      if (editMode && selectedAssetIdx >= 0 && selectedAssetIdx < (int)SousseMapData::discoveredAssets.size()) {
          std::string previewPath = SousseMapData::discoveredAssets[selectedAssetIdx];
          if (assetCache.find(previewPath) == assetCache.end()) {
              assetCache[previewPath] = new ObjModel(previewPath.c_str());
          }
          ObjModel *m = assetCache[previewPath];
          if (m) {
              // Calculate preview position in front of camera
              glm::vec3 camDir = -glm::normalize(glm::vec3(view[0][2], view[1][2], view[2][2]));
              glm::vec3 horizontalDir = glm::normalize(glm::vec3(camDir.x, 0.0f, camDir.z));
              glm::vec3 previewPos = cam + horizontalDir * 15.0f;
              previewPos.y = getHeight(previewPos.x, previewPos.z);

              glm::mat4 previewMat = glm::translate(glm::mat4(1.0f), previewPos);

              // Align preview to terrain normal
              glm::vec3 terrainNormal = glm::vec3(0.0f, 1.0f, 0.0f);
              if (terrainManager) {
                  terrainNormal = glm::normalize(terrainManager->getNormalAt(previewPos.x, previewPos.z));
              }
              glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
              float dotNU = glm::dot(terrainNormal, worldUp);
              if (dotNU < 0.999f) {
                  glm::vec3 rotAxis = glm::normalize(glm::cross(worldUp, terrainNormal));
                  float rotAngle = acosf(glm::clamp(dotNU, -1.0f, 1.0f));
                  previewMat = glm::rotate(previewMat, rotAngle, rotAxis);
              }

              previewMat = glm::rotate(previewMat, glm::radians(previewAngle), glm::vec3(0, 1, 0));
              previewMat = glm::scale(previewMat, glm::vec3(previewScale));

              modelShader->setMat4("model", previewMat);
              
              // Draw preview model
              m->render(*modelShader);
          }
      }
  }

  // --- PHASE 3: USER-PROVIDED CITY WALLS ---
  // CPU-side terrain grounding: sample terrain densely along perimeter,
  // find the lowest point, and bury the wall so it intersects ground
  // everywhere.
  if (cityWallModel && modelShader) {
    static bool wallDebugPrinted = false;

    std::vector<glm::vec2> perimeter = SousseMapData::getMedinaPerimeter();
    glm::vec2 pMin(1e9f, 1e9f), pMax(-1e9f, -1e9f);
    for (const auto &p : perimeter) {
      pMin = glm::min(pMin, p);
      pMax = glm::max(pMax, p);
    }

    glm::vec3 mMin(1e9f, 1e9f, 1e9f), mMax(-1e9f, -1e9f, -1e9f);
    for (int i = 0; i < cityWallModel->getMeshCount(); ++i) {
      mMin = glm::min(mMin, cityWallModel->getMeshMinBound(i));
      mMax = glm::max(mMax, cityWallModel->getMeshMaxBound(i));
    }

    float footprintX = pMax.x - pMin.x;
    float footprintZ = pMax.y - pMin.y;
    float modelX = mMax.x - mMin.x;
    float modelZ = mMax.z - mMin.z;

    float scaleXZ = 1.0f;
    if (modelX > 0.001f && modelZ > 0.001f) {
      scaleXZ = std::max(footprintX / modelX, footprintZ / modelZ);
    }

    // --- MANUAL TWEAKS ---
    auto& manualScaleX = SousseMapData::CityWallConfig::manualScaleX;
    auto& manualScaleY = SousseMapData::CityWallConfig::manualScaleY;
    auto& manualScaleZ = SousseMapData::CityWallConfig::manualScaleZ;
    auto& manualOffsetX = SousseMapData::CityWallConfig::manualOffsetX;
    auto& manualOffsetY = SousseMapData::CityWallConfig::manualOffsetY;
    auto& manualOffsetZ = SousseMapData::CityWallConfig::manualOffsetZ;
    auto& manualRotationY = SousseMapData::CityWallConfig::manualRotationY;

    GLFWwindow *window = glfwGetCurrentContext();
    if (window) {
      float speed = 100.0f * 0.016f;
      if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
        manualOffsetZ -= speed;
      if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
        manualOffsetZ += speed;
      if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
        manualOffsetX -= speed;
      if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
        manualOffsetX += speed;
      if (glfwGetKey(window, GLFW_KEY_PAGE_UP) == GLFW_PRESS)
        manualOffsetY += speed;
      if (glfwGetKey(window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS)
        manualOffsetY -= speed;
      if (glfwGetKey(window, GLFW_KEY_KP_ADD) == GLFW_PRESS ||
          glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS) {
        manualScaleX += 0.01f;
        manualScaleY += 0.01f;
        manualScaleZ += 0.01f;
      }
      if (glfwGetKey(window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS ||
          glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS) {
        manualScaleX -= 0.01f;
        manualScaleY -= 0.01f;
        manualScaleZ -= 0.01f;
      }
      if (glfwGetKey(window, GLFW_KEY_U) == GLFW_PRESS)
        manualScaleX -= 0.01f;
      if (glfwGetKey(window, GLFW_KEY_I) == GLFW_PRESS)
        manualScaleX += 0.01f;
      if (glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS)
        manualScaleZ -= 0.01f;
      if (glfwGetKey(window, GLFW_KEY_H) == GLFW_PRESS)
        manualScaleZ += 0.01f;
      if (glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS ||
          glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS)
        manualRotationY -= 1.0f;
      if (glfwGetKey(window, GLFW_KEY_END) == GLFW_PRESS ||
          glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS)
        manualRotationY += 1.0f;

      static double lastPrint = 0.0;
      double now = glfwGetTime();
      if (now - lastPrint > 1.0) {
        std::cout << "[Live Wall Editor] Scale=(" << manualScaleX << ", "
                  << manualScaleY << ", " << manualScaleZ << ") Offset=("
                  << manualOffsetX << ", " << manualOffsetY << ", "
                  << manualOffsetZ << ") Rot=" << manualRotationY << std::endl;
        lastPrint = now;
      }
    }

    glm::vec3 scale(scaleXZ * manualScaleX, scaleXZ * manualScaleY,
                    scaleXZ * manualScaleZ);
    glm::vec2 pCenter = (pMin + pMax) * 0.5f;
    glm::vec3 mCenter = (mMin + mMax) * 0.5f;

    static float lastOffsetX = -999999.0f;
    static float lastOffsetY = -999999.0f;
    static float lastOffsetZ = -999999.0f;
    static float lastScaleX = -999999.0f;
    static float lastScaleY = -999999.0f;
    static float lastScaleZ = -999999.0f;
    static float lastRotationY = -999999.0f;

    bool wallChanged = firstWallBuild ||
                       SousseMapData::layoutDirty ||
                       (manualOffsetX != lastOffsetX) ||
                       (manualOffsetY != lastOffsetY) ||
                       (manualOffsetZ != lastOffsetZ) ||
                       (manualScaleX != lastScaleX) ||
                       (manualScaleY != lastScaleY) ||
                       (manualScaleZ != lastScaleZ) ||
                       (manualRotationY != lastRotationY);

    if (wallChanged) {
      // --- Dense terrain sampling at ACTUAL wall positions (transformed perimeter)
      auto transformedPerimeter = SousseMapData::getTransformedMedinaPerimeter();
      float minTerrainH = 1e9f;
      float maxTerrainH = -1e9f;
      if (terrainManager) {
        for (size_t i = 0; i + 1 < transformedPerimeter.size(); ++i) {
          glm::vec2 A = transformedPerimeter[i];
          glm::vec2 B = transformedPerimeter[i + 1];
          float segLen = glm::length(B - A);
          int steps = std::max(1, (int)(segLen / 5.0f));
          for (int s = 0; s <= steps; ++s) {
            float t = (float)s / (float)steps;
            glm::vec2 pt = A + t * (B - A);
            float h = terrainManager->getHeightAt(pt.x, pt.y);
            minTerrainH = std::min(minTerrainH, h);
            maxTerrainH = std::max(maxTerrainH, h);
          }
        }
        // Also sample at the offset center
        float hCenter = terrainManager->getHeightAt(pCenter.x + manualOffsetX,
                                                    pCenter.y + manualOffsetZ);
        minTerrainH = std::min(minTerrainH, hCenter);
        maxTerrainH = std::max(maxTerrainH, hCenter);
      } else {
        minTerrainH = 0.0f;
        maxTerrainH = 0.0f;
      }

      // The wall model has pillars/buttresses below the main wall body.
      // Bury the bottom half so the main wall section sits at terrain level.
      float modelScaledHeight = (mMax.y - mMin.y) * scale.y;
      float buryOffset = modelScaledHeight * 0.5f; // Hide bottom half underground
      cachedGroundY = maxTerrainH - buryOffset;

      if (!wallDebugPrinted) {
        std::cout << "[CityWall] Model bounds: min=(" << mMin.x << "," << mMin.y
                  << "," << mMin.z << ") max=(" << mMax.x << "," << mMax.y << ","
                  << mMax.z << ")" << std::endl;
        std::cout << "[CityWall] Perimeter center=(" << pCenter.x << ","
                  << pCenter.y << ") footprint=(" << footprintX << "x"
                  << footprintZ << ")" << std::endl;
        std::cout << "[CityWall] scaleXZ=" << scaleXZ << " finalScale=("
                  << scale.x << "," << scale.y << "," << scale.z << ")"
                  << std::endl;
        std::cout << "[CityWall] Terrain range: min=" << minTerrainH
                  << " max=" << maxTerrainH << std::endl;
        std::cout << "[CityWall] Wall base placed at Y=" << cachedGroundY
                  << " (bury=" << buryOffset << ")" << std::endl;
        wallDebugPrinted = true;
      }

      glm::mat4 modelMat(1.0f);
      modelMat = glm::translate(modelMat, glm::vec3(pCenter.x + manualOffsetX,
                                                    cachedGroundY + manualOffsetY,
                                                    pCenter.y + manualOffsetZ));
      modelMat = glm::rotate(modelMat, glm::radians(manualRotationY),
                             glm::vec3(0, 1, 0));
      modelMat = glm::scale(modelMat, scale);
      modelMat =
          glm::translate(modelMat, glm::vec3(-mCenter.x, -mMin.y, -mCenter.z));
      
      cityWallModelMat = modelMat; // Store for physics engine
      cachedRockInstances.clear();

      // Update static tracking variables
      lastOffsetX = manualOffsetX;
      lastOffsetY = manualOffsetY;
      lastOffsetZ = manualOffsetZ;
      lastScaleX = manualScaleX;
      lastScaleY = manualScaleY;
      lastScaleZ = manualScaleZ;
      lastRotationY = manualRotationY;
      firstWallBuild = false;
    }

    modelShader->use();
    // Rigid placement â€” no per-vertex deformation (keeps wall shape intact)
    modelShader->setBool("deformToTerrain", false);

    // Frustum culling check for the entire Medina wall structure
    glm::vec3 wallCenter(pCenter.x + manualOffsetX, cachedGroundY + manualOffsetY, pCenter.y + manualOffsetZ);
    float wallRadius = std::max(footprintX, footprintZ) * 0.6f;
    bool wallInFrustum = true;
    for (int p = 0; p < 6; ++p) {
        if (glm::dot(glm::vec3(planes[p]), wallCenter) + planes[p].w < -wallRadius) {
            wallInFrustum = false;
            break;
        }
    }

    if (wallInFrustum) {
        modelShader->setMat4("projection", proj);
        modelShader->setMat4("view", view);
        modelShader->setVec3("viewPos", cam);
        modelShader->setVec3("lightDir", light);
        modelShader->setVec3("lightColor", glm::vec3(1.1f, 1.05f, 0.95f));
        modelShader->setVec3("ambientColor", glm::vec3(0.4f, 0.45f, 0.5f));
        modelShader->setBool("hasTexture", true);
        modelShader->setBool("hasNormalMap", true);
        modelShader->setVec3("diffuseColor", glm::vec3(0.85f, 0.75f, 0.6f));
        cityWallModel->render(*modelShader);
    }
  }

  // --- PHASE 4: SELECTION GIZMOS / HUD ---
  if (editMode) {
    for (int i = 0; i < (int)SousseMapData::liveLandmarks.size(); ++i) {
      auto &landmark = SousseMapData::liveLandmarks[i];
      if (!landmark.placed)
        continue;

      bool isSelected = (i == selectedIdx);
      glm::vec2 worldPos = SousseMapData::toWorld(landmark.localOffset);
      float finalY = landmark.bakedY;
      if (finalY < -500000.0)
        continue;

      glDisable(GL_LIGHTING);
      glDisable(GL_TEXTURE_2D);

      if (isSelected) {
        glColor3f(0.0f, 1.0f, 1.0f);
        glLineWidth(4);
        glBegin(GL_LINES);
        glVertex3f(worldPos.x, finalY, worldPos.y);
        glVertex3f(worldPos.x, finalY + 150.0f, worldPos.y);
        glEnd();

        glPushMatrix();
        glTranslatef(worldPos.x + 2.0f, finalY + 10.0f, worldPos.y);
        glColor3f(1, 1, 1);
        std::stringstream hud;
        hud << landmark.name << " [R: " << (int)landmark.angle
            << " S: " << std::fixed << std::setprecision(1) << landmark.xScale
            << "]";
        std::string msg = hud.str();
        glRasterPos3f(0, 0, 0);
        for (char c : msg) {
#ifdef HAS_GLUT
          glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, c);
#endif
        }
        glPopMatrix();
      } else {
        float distSq = glm::dot(worldPos - glm::vec2(cam.x, cam.z),
                                worldPos - glm::vec2(cam.x, cam.z));
        if (distSq < (50.0f * 50.0f)) {
          glColor3f(0.3f, 0.3f, 0.3f);
          glLineWidth(1);
          glBegin(GL_LINES);
          glVertex3f(worldPos.x, finalY, worldPos.y);
          glVertex3f(worldPos.x, finalY + 30.0f, worldPos.y);
          glEnd();
        }
      }
      glEnable(GL_LIGHTING);
    }
  }

  // Draw 3D brush cursor ring on terrain
  if (editMode && rayHitTerrain && editorBrushMode >= 1) {
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDepthFunc(GL_LEQUAL);

    // Unreal Engine orange color for the brush outline!
    glColor3f(1.0f, 0.5f, 0.0f);
    glLineWidth(3);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < 36; ++i) {
        float angle = (float)i * 10.0f * 3.14159f / 180.0f;
        float cx = lastRayHitPos.x + cos(angle) * brushRadius;
        float cz = lastRayHitPos.z + sin(angle) * brushRadius;
        float cy = getHeight(cx, cz) + 0.3f; // Float slightly above terrain to avoid clipping
        glVertex3f(cx, cy, cz);
    }
    glEnd();

    // Angle Slope preview: the colored 3D profile shows the selected grade,
    // while the ground-plane arrow shows its 0-360 degree uphill direction.
    if (editorBrushMode == 5) {
        const float direction = editorSculptDirectionDegrees * 3.14159265f / 180.0f;
        const glm::vec2 dir(std::cos(direction), std::sin(direction));
        const glm::vec2 side(-dir.y, dir.x);
        const float grade = std::tan(glm::clamp(editorSculptGradeDegrees, 0.0f, 60.0f) * 3.14159265f / 180.0f);
        const glm::vec2 center(lastRayHitPos.x, lastRayHitPos.z);
        const float centerY = getHeight(center.x, center.y);
        const float arrowLength = brushRadius * 0.82f;
        const glm::vec2 tip = center + dir * arrowLength;
        const float tipY = getHeight(tip.x, tip.y) + 0.6f + arrowLength * grade;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        // The inner ring marks the area where the full configured grade is applied;
        // the space between this and the orange outer ring is the softened join.
        glColor4f(0.15f, 0.9f, 1.0f, 0.55f);
        glLineWidth(1.5f);
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < 48; ++i) {
            const float a = (float)i * (2.0f * 3.14159265f / 48.0f);
            const float px = center.x + std::cos(a) * brushRadius * 0.80f;
            const float pz = center.y + std::sin(a) * brushRadius * 0.80f;
            glVertex3f(px, getHeight(px, pz) + 0.4f, pz);
        }
        glEnd();

        // Bright cyan direction arrow laid on the ground.
        glColor4f(0.15f, 0.9f, 1.0f, 0.95f);
        glLineWidth(4.0f);
        glBegin(GL_LINES);
        glVertex3f(center.x, centerY + 0.55f, center.y);
        glVertex3f(tip.x, getHeight(tip.x, tip.y) + 0.55f, tip.y);
        glEnd();

        // Raised profile line, lifted by the selected grade so the angle can
        // be read in the world view before the terrain is modified.
        glColor4f(1.0f, 0.68f, 0.12f, 0.98f);
        glLineWidth(3.0f);
        glBegin(GL_LINES);
        glVertex3f(center.x, centerY + 0.6f, center.y);
        glVertex3f(tip.x, tipY, tip.y);
        const float headLen = brushRadius * 0.13f;
        const float headHalfW = brushRadius * 0.075f;
        const glm::vec2 headBase = tip - dir * headLen;
        const float headY = tipY - headLen * grade;
        for (float sign : {-1.0f, 1.0f}) {
            const glm::vec2 wing = headBase + side * (sign * headHalfW);
            glVertex3f(tip.x, tipY, tip.y);
            glVertex3f(wing.x, headY, wing.y);
        }
        glEnd();
        glDisable(GL_BLEND);
    }

    glDepthFunc(GL_LESS);
    glEnable(GL_LIGHTING);
  }

  // --- CITY WALL WIPED (User requested removal) ---
}

void Terrain::renderWater(const glm::mat4 &proj, const glm::mat4 &view,
                          const glm::vec3 &cam, const glm::vec3 &light,
                          const glm::vec3 &lightColor,
                          unsigned int sceneColorTex,
                          unsigned int sceneDepthTex) const {
  // DIAGNOSTIC: set FALLAGA_NO_WATER=1 to skip the ocean mesh entirely, to
  // check whether a visible "lake" is coming from this mesh or from the
  // terrain shader itself (e.g. its beach/fog blending).
  static bool skipWater = (std::getenv("FALLAGA_NO_WATER") != nullptr);
  if (skipWater)
    return;

  if (!waterVAO || !oceanShader)
    return;

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE); // Don't write water to depth buffer, just read from it

  oceanShader->use();
  oceanShader->setMat4("projection", proj);
  oceanShader->setMat4("view", view);
  oceanShader->setMat4("model", glm::mat4(1.0f));
  oceanShader->setVec3("cameraPos", cam);
  oceanShader->setVec3("viewPos", cam);
  oceanShader->setVec3("lightDir", light);   // Real-time sun/moon direction
  oceanShader->setFloat("sunTilt", light.y); // >0 = day, <0 = night
  oceanShader->setVec3("lightColor", lightColor);
  oceanShader->setFloat("time", (float)glfwGetTime());

  // Bind Textures
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, sceneColorTex);
  oceanShader->setInt("sceneColor", 0);

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, sceneDepthTex);
  oceanShader->setInt("depthMap", 1);

  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, normalMap1 ? normalMap1 : flatNormalTex);
  oceanShader->setInt("normalMap1", 2);

  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, normalMap2 ? normalMap2 : flatNormalTex);
  oceanShader->setInt("normalMap2", 3);

  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_2D, foamTex ? foamTex : flatNormalTex);
  oceanShader->setInt("foamTexture", 4);

  // New Uniforms from user
  glActiveTexture(GL_TEXTURE5);
  glBindTexture(GL_TEXTURE_2D,
                sceneColorTex); // Fallback planar reflection to scene color
  oceanShader->setInt("reflectionTex", 5);

  glActiveTexture(GL_TEXTURE6);
  glBindTexture(GL_TEXTURE_2D,
                flatNormalTex); // Fallback caustics to a normal texture noise
  oceanShader->setInt("causticsTex", 6);

  glActiveTexture(GL_TEXTURE7);
  glBindTexture(GL_TEXTURE_2D, foamTex ? foamTex : flatNormalTex);
  oceanShader->setInt("noiseTex", 7);

  // Heightmap masking
  if (terrainManager) {
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, terrainManager->getHeightmapTexture());
    oceanShader->setInt("heightmapTex", 8);
    
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, terrainManager->getRoadMask());
    oceanShader->setInt("roadMask", 9);

    oceanShader->setFloat("hmMinElev", -70.0f);
    oceanShader->setFloat("hmMaxElev", 87.8f);
    oceanShader->setFloat("hmWorldMinX", -13938.0f);
    oceanShader->setFloat("hmWorldMaxX", 13938.0f);
    oceanShader->setFloat("hmWorldMinZ", -13938.0f);
    oceanShader->setFloat("hmWorldMaxZ", 13938.0f);
    oceanShader->setFloat("waterLevel", waterLevelRuntime);
    oceanShader->setInt("useHeightmap", 1);
  } else {
    oceanShader->setInt("useHeightmap", 0);
  }

  // Bind reflection / refraction FBO textures (slots 10 and 11)
  if (fbosReady) {
    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_2D, reflColorTex);
    oceanShader->setInt("reflectionTex", 10);

    glActiveTexture(GL_TEXTURE11);
    glBindTexture(GL_TEXTURE_2D, refrColorTex);
    oceanShader->setInt("refractionTex", 11);

    glActiveTexture(GL_TEXTURE12);
    glBindTexture(GL_TEXTURE_2D, refrDepthTex);
    oceanShader->setInt("refractionDepthTex", 12);

    oceanShader->setVec2("screenSize", glm::vec2((float)fboW * 2.0f, (float)fboH * 2.0f));
    oceanShader->setInt("useFBOs", 1);
  } else {
    oceanShader->setInt("useFBOs", 0);
  }

  // Bind dynamic simulation water map texture
  glActiveTexture(GL_TEXTURE13);
  glBindTexture(GL_TEXTURE_2D, waterMapTex);
  oceanShader->setInt("waterMapTex", 13);

  glBindVertexArray(waterVAO);
  glDrawElements(GL_TRIANGLES, waterIndexCount, GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);

  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glUseProgram(0);
}

float Terrain::getHeight(float x, float z) const {
  if (isFlatMode)
    return WATER_BASE;
  return terrainManager ? terrainManager->getHeightAt(x, z) : 0.0f;
}

float Terrain::getGroundSurfaceHeight(float x, float z) const {
  const float terrainY = getHeight(x, z);
  if (!roadGraph || roadGraph->segments.empty()) return terrainY;

  // Match the road ribbon that is actually rendered: spline heights are snapped
  // to the visual terrain grid, and bends use RoadMeshBuilder's inner-edge width.
  constexpr float kEdgeConform = 0.02f;
  constexpr float kRoadSurfaceBias = 0.01f;
  float surfaceY = terrainY;

  auto sampleEdge = [&](size_t segmentIndex, size_t i) {
    const RoadSegment& road = roadGraph->segments[segmentIndex];
    const size_t n = road.splinePoints.size();
    if (n < 2 || road.width <= 0.0f || road.rightVecs.size() != n || i >= n) return;
    const size_t edgeCount = road.closed ? n : n - 1;
    if (i >= edgeCount) return;
    const size_t j = (i + 1) % n;
    const glm::vec2 a(road.splinePoints[i].x, road.splinePoints[i].z);
    const glm::vec2 b(road.splinePoints[j].x, road.splinePoints[j].z);
    const glm::vec2 d = b - a;
    const float len2 = glm::dot(d, d);
    if (len2 < 1e-8f) return;
    const glm::vec2 p(x, z);
    const float rawT = glm::dot(p - a, d) / len2;
    const float segmentLength = std::sqrt(len2);
    const float t = glm::clamp(rawT, 0.0f, 1.0f);
    const glm::vec2 center = a + d * t;
    const glm::vec2 delta = p - center;
    const glm::vec3 right3 = glm::mix(road.rightVecs[i], road.rightVecs[j], t);
    const glm::vec2 right(right3.x, right3.z);
    const float rightLen = glm::length(right);
    if (rightLen < 1e-5f) return;
    const glm::vec2 rightDir = right / rightLen;
    const float lateral = glm::dot(delta, rightDir);
    const float fullHalfWidth = road.width * 0.5f;
    const float halfLeft = glm::mix(
        RoadSpline::meshHalfWidth(road, static_cast<int>(i), road.closed, -1.0f, fullHalfWidth),
        RoadSpline::meshHalfWidth(road, static_cast<int>(j), road.closed, -1.0f, fullHalfWidth),
        t) * rightLen;
    const float halfRight = glm::mix(
        RoadSpline::meshHalfWidth(road, static_cast<int>(i), road.closed, 1.0f, fullHalfWidth),
        RoadSpline::meshHalfWidth(road, static_cast<int>(j), road.closed, 1.0f, fullHalfWidth),
        t) * rightLen;
    const float halfWidth = std::max(halfLeft, halfRight);
    // Allow a round end cap, but do not extend the segment along its tangent.
    if (lateral < -halfLeft || lateral > halfRight ||
        rawT < -halfWidth / segmentLength || rawT > 1.0f + halfWidth / segmentLength) return;

    const float centerY = glm::mix(road.splinePoints[i].y, road.splinePoints[j].y, t);
    // The ribbon is re-seated on the rendered (visual) heightfield in
    // snapRoadsToRenderedTerrain(). Sampling roadHeightSampler here used the
    // coarser source DEM instead, which could place car collision metres away
    // from the drawn road edge on steep terrain.
    const float edgeY = getHeight(x, z);
    const float sideHalfWidth = lateral < 0.0f ? halfLeft : halfRight;
    const float lateralT = glm::clamp(
        std::fabs(lateral) / std::max(sideHalfWidth, 1e-4f), 0.0f, 1.0f);
    const float crown = RoadSpline::kCrownHeight * (1.0f - lateralT * lateralT);
    const float roadY = centerY + (edgeY - centerY) * kEdgeConform +
                        crown + kRoadSurfaceBias;
    surfaceY = std::max(surfaceY, roadY);
  };

  if (roadSurfaceIndexReady) {
    const int cellX = static_cast<int>(std::floor(x / kRoadSurfaceCellSize));
    const int cellZ = static_cast<int>(std::floor(z / kRoadSurfaceCellSize));
    const auto cell = roadSurfaceEdgeGrid.find(roadSurfaceCellKey(cellX, cellZ));
    if (cell == roadSurfaceEdgeGrid.end()) return terrainY;
    for (const RoadSurfaceEdge& edge : cell->second)
      sampleEdge(edge.segmentIndex, edge.pointIndex);
    return surfaceY;
  }

  // Keep a correct fallback during startup before the road index is built.
  for (size_t segmentIndex = 0; segmentIndex < roadGraph->segments.size(); ++segmentIndex) {
    const RoadSegment& road = roadGraph->segments[segmentIndex];
    const size_t n = road.splinePoints.size();
    if (n < 2 || road.width <= 0.0f || road.rightVecs.size() != n) continue;
    if (road.surfaceBoundsValid) {
      const float reach = road.width + 1.0f;
      if (x < road.surfaceBounds.x - reach ||
          x > road.surfaceBounds.z + reach ||
          z < road.surfaceBounds.y - reach ||
          z > road.surfaceBounds.w + reach) continue;
    } else if (!road.controlPoints.empty()) {
      float minX = road.controlPoints.front().pos.x, maxX = minX;
      float minZ = road.controlPoints.front().pos.y, maxZ = minZ;
      for (const RoadPoint& cp : road.controlPoints) {
        minX = std::min(minX, cp.pos.x); maxX = std::max(maxX, cp.pos.x);
        minZ = std::min(minZ, cp.pos.y); maxZ = std::max(maxZ, cp.pos.y);
      }
      const float reach = road.width + 1.0f;
      if (x < minX - reach || x > maxX + reach ||
          z < minZ - reach || z > maxZ + reach) continue;
    }
    const size_t edgeCount = road.closed ? n : n - 1;
    for (size_t i = 0; i < edgeCount; ++i) sampleEdge(segmentIndex, i);
  }
  return surfaceY;
}

void Terrain::rebuildRoadSurfaceIndex() {
  roadSurfaceEdgeGrid.clear();
  roadSurfaceIndexReady = false;
  if (!roadGraph) {
    roadSurfaceIndexReady = true;
    return;
  }

  for (size_t segmentIndex = 0; segmentIndex < roadGraph->segments.size(); ++segmentIndex) {
    const RoadSegment& road = roadGraph->segments[segmentIndex];
    const size_t n = road.splinePoints.size();
    if (n < 2 || road.width <= 0.0f || road.rightVecs.size() != n) continue;
    const size_t edgeCount = road.closed ? n : n - 1;
    const float reach = road.width + 1.0f;
    for (size_t i = 0; i < edgeCount; ++i) {
      const size_t j = (i + 1) % n;
      const glm::vec3& a = road.splinePoints[i];
      const glm::vec3& b = road.splinePoints[j];
      const int minCellX = static_cast<int>(std::floor((std::min(a.x, b.x) - reach) / kRoadSurfaceCellSize));
      const int maxCellX = static_cast<int>(std::floor((std::max(a.x, b.x) + reach) / kRoadSurfaceCellSize));
      const int minCellZ = static_cast<int>(std::floor((std::min(a.z, b.z) - reach) / kRoadSurfaceCellSize));
      const int maxCellZ = static_cast<int>(std::floor((std::max(a.z, b.z) + reach) / kRoadSurfaceCellSize));
      const RoadSurfaceEdge edge{static_cast<uint32_t>(segmentIndex),
                                 static_cast<uint32_t>(i)};
      for (int cellZ = minCellZ; cellZ <= maxCellZ; ++cellZ) {
        for (int cellX = minCellX; cellX <= maxCellX; ++cellX) {
          roadSurfaceEdgeGrid[roadSurfaceCellKey(cellX, cellZ)].push_back(edge);
        }
      }
    }
  }
  roadSurfaceIndexReady = true;
}

glm::vec3 Terrain::getNormalAt(float x, float z) const {
  const float sampleDistance = 1.0f;
  float hL = getHeight(x - sampleDistance, z);
  float hR = getHeight(x + sampleDistance, z);
  float hS = getHeight(x, z - sampleDistance);
  float hN = getHeight(x, z + sampleDistance);
  return glm::normalize(glm::vec3(hL - hR, 2.0f * sampleDistance, hS - hN));
}
float Terrain::getRoadAt(float x, float z) const {
  if (roadDeformer && roadDeformer->isBuilt())
    return roadDeformer->getRoadInfluence(x, z);
  return 0.0f;
}

bool Terrain::resolveCarCollision(glm::vec3& position, glm::vec3& velocity,
                                  float carRadius, float restitution) const {
  if (carRadius <= 0.0f) return false;
  if (carCollisionGridDirty) rebuildCarCollisionGrid();

  bool collided = false;
  const float broadPhaseRadius = carRadius + 30.0f;
  const float broadPhaseRadiusSq = broadPhaseRadius * broadPhaseRadius;
  const float collisionCenterHeight = 0.75f;

  auto applyModelCollision = [&](const ObjModel* model, const glm::mat4& modelMatrix) {
    if (!model) return;
    glm::vec3 center(position.x, position.y + collisionCenterHeight, position.z);
    glm::vec3 normal(0.0f);
    float depth = 0.0f;
    if (!model->checkSphereCollisionWorld(center, carRadius, modelMatrix,
                                         normal, depth)) return;

    normal.y = 0.0f;
    const float normalLengthSq = glm::dot(normal, normal);
    if (normalLengthSq < 1e-6f) return;
    normal *= glm::inversesqrt(normalLengthSq);
    position.x += normal.x * (depth + 0.01f);
    position.z += normal.z * (depth + 0.01f);

    const float normalSpeed = velocity.x * normal.x + velocity.z * normal.z;
    if (normalSpeed < 0.0f) {
      const float bounce = glm::clamp(restitution, 0.0f, 0.5f);
      velocity.x -= (1.0f + bounce) * normalSpeed * normal.x;
      velocity.z -= (1.0f + bounce) * normalSpeed * normal.z;
      velocity.x *= 0.82f;
      velocity.z *= 0.82f;
    }
    collided = true;
  };

  // Resolve twice so a car wedged into a corner can be moved clear of both
  // nearby object bounds in one fixed physics step. Query only the cells that
  // overlap the broad-phase radius instead of walking every rendered instance.
  for (int pass = 0; pass < 2; ++pass) {
    const int minCellX = static_cast<int>(std::floor(
        (position.x - broadPhaseRadius) / kCarCollisionCellSize));
    const int maxCellX = static_cast<int>(std::floor(
        (position.x + broadPhaseRadius) / kCarCollisionCellSize));
    const int minCellZ = static_cast<int>(std::floor(
        (position.z - broadPhaseRadius) / kCarCollisionCellSize));
    const int maxCellZ = static_cast<int>(std::floor(
        (position.z + broadPhaseRadius) / kCarCollisionCellSize));

    for (int cellX = minCellX; cellX <= maxCellX; ++cellX) {
      for (int cellZ = minCellZ; cellZ <= maxCellZ; ++cellZ) {
        const auto cellIt = carCollisionGrid.find(
            carCollisionCellKey(cellX, cellZ));
        if (cellIt == carCollisionGrid.end()) continue;
        for (const CarCollisionInstance& instance : cellIt->second) {
          if (!instance.modelPath) continue;
          const glm::mat4& modelMatrix = instance.modelMatrix;
          const float dx = modelMatrix[3].x - position.x;
          const float dz = modelMatrix[3].z - position.z;
          if (dx * dx + dz * dz > broadPhaseRadiusSq) continue;

          const auto modelIt = assetCache.find(*instance.modelPath);
          if (modelIt == assetCache.end() || !modelIt->second) continue;
          applyModelCollision(modelIt->second, modelMatrix);
        }
      }
    }
  }

  return collided;
}

void Terrain::rebuildCarCollisionGrid() const {
  carCollisionGrid.clear();
  const auto insert = [this](const std::string& path,
                             const glm::mat4& modelMatrix) {
    if (path.find("grass") != std::string::npos ||
        path.find("Grass") != std::string::npos) return;
    const int cellX = static_cast<int>(std::floor(
        modelMatrix[3].x / kCarCollisionCellSize));
    const int cellZ = static_cast<int>(std::floor(
        modelMatrix[3].z / kCarCollisionCellSize));
    carCollisionGrid[carCollisionCellKey(cellX, cellZ)].push_back(
        {&path, modelMatrix});
  };

  for (const auto& [path, matrices] : cachedBatches)
    for (const glm::mat4& modelMatrix : matrices)
      insert(path, modelMatrix);
  for (const CustomRenderTask& task : cachedCustomRenderTasks)
    insert(task.modelPath, task.modelMatrix);

  carCollisionGridDirty = false;
}

Terrain::Zone Terrain::getZoneAt(float x, float z) const {
  if (!terrainManager) return Zone::PLAIN;

  const float groundHeight = getHeight(x, z);
  if (groundHeight <= getWaterLevel() - 0.5f) return Zone::SEA;

  // The historic Sousse plan is translated into terrain-world space by this
  // offset when its roads are built. Classify the districts in that same local
  // coordinate space so the map and physics agree.
  constexpr float kPlanToWorldX = 3600.0f;
  constexpr float kPlanToWorldZ = -701.0f;
  const float planX = x - kPlanToWorldX;
  const float planZ = z - kPlanToWorldZ;

  static const std::vector<glm::vec2> medina = {
      {-20.0f, 820.0f}, {150.0f, 900.0f}, {500.0f, 875.0f},
      {680.0f, 760.0f}, {700.0f, 480.0f}, {630.0f, 180.0f},
      {570.0f, -180.0f}, {390.0f, -520.0f}, {150.0f, -680.0f},
      {-80.0f, -520.0f}, {-100.0f, -160.0f}, {-60.0f, 300.0f}};
  static const std::vector<glm::vec2> kasbah = {
      {-430.0f, -650.0f}, {160.0f, -650.0f}, {230.0f, -980.0f},
      {120.0f, -1370.0f}, {-200.0f, -1510.0f}, {-470.0f, -1240.0f}};
  static const std::vector<glm::vec2> port = {
      {690.0f, 500.0f}, {1320.0f, 420.0f}, {1400.0f, -700.0f},
      {1110.0f, -1050.0f}, {760.0f, -780.0f}, {620.0f, -250.0f}};
  static const std::vector<glm::vec2> villeNouvelle = {
      {-900.0f, 1250.0f}, {720.0f, 1250.0f}, {780.0f, 850.0f},
      {680.0f, -580.0f}, {450.0f, -700.0f}, {260.0f, -580.0f},
      {-470.0f, -580.0f}, {-880.0f, -350.0f}};

  if (pointInPolygon(planX, planZ, medina)) return Zone::MEDINA;
  if (pointInPolygon(planX, planZ, kasbah)) return Zone::KASBAH;
  if (pointInPolygon(planX, planZ, port)) return Zone::PORT;
  if (pointInPolygon(planX, planZ, villeNouvelle)) return Zone::VILLE_NOUVELLE;

  // The coastal band is low and lies east of the historic plan; urban port
  // ground was handled above so this only labels exposed shoreline as beach.
  if (x > 4650.0f && groundHeight <= getWaterLevel() + 8.0f)
    return Zone::BEACH;

  if (!wadiSurfaceMask.empty() && wadiSurfaceMaskW > 0 && wadiSurfaceMaskH > 0) {
    const float u = glm::clamp(
        (x - terrainManager->getWorldMinX()) /
            (terrainManager->getWorldMaxX() - terrainManager->getWorldMinX()),
        0.0f, 1.0f);
    const float v = glm::clamp(
        (z - terrainManager->getWorldMinZ()) /
            (terrainManager->getWorldMaxZ() - terrainManager->getWorldMinZ()),
        0.0f, 1.0f);
    const int px = std::clamp(static_cast<int>(u * (wadiSurfaceMaskW - 1)),
                              0, wadiSurfaceMaskW - 1);
    const int pz = std::clamp(static_cast<int>(v * (wadiSurfaceMaskH - 1)),
                              0, wadiSurfaceMaskH - 1);
    // Black pixels in heightmap_wadis.png mark the incised wadi channels.
    if (wadiSurfaceMask[static_cast<size_t>(pz) * wadiSurfaceMaskW + px] < 128)
      return Zone::WADI;
  }

  return Zone::PLAIN;
}
bool Terrain::pointInPolygon(float x, float z,
                             const std::vector<glm::vec2> &polygon) const {
  if (polygon.size() < 3) return false;

  bool inside = false;
  for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
    const glm::vec2& a = polygon[i];
    const glm::vec2& b = polygon[j];
    const bool crosses = ((a.y > z) != (b.y > z)) &&
        (x < (b.x - a.x) * (z - a.y) / (b.y - a.y) + a.x);
    if (crosses) inside = !inside;
  }
  return inside;
}

void Terrain::update(const glm::vec3& camPos) {
  if (terrainManager) {
    terrainManager->update(camPos);
    // TerrainManager hot-reloads heightmap.png when its mtime changes, and that
    // reload reallocates `heights`. roadHeightSampler caches a raw pointer into
    // that buffer, so without this refresh every subsequent road sample would
    // read freed memory.
    refreshHeightDependentCaches();
  }
}

void Terrain::refreshHeightDependentCaches() {
  if (!terrainManager) return;
  if (cachedHeightRevision == terrainManager->getHeightRevision()) return;

  TerrainCoordinateMapper mapper;
  mapper.bounds = { terrainManager->getWorldMinX(), terrainManager->getWorldMaxX(),
                    terrainManager->getWorldMinZ(), terrainManager->getWorldMaxZ(),
                    terrainManager->getMinHeight(), terrainManager->getMaxHeight() };
  mapper.heightmapW = terrainManager->getGridW();
  mapper.heightmapH = terrainManager->getGridH();

  roadHeightSampler.init(mapper, terrainManager->getHeightsData(),
                         terrainManager->getGridW(), terrainManager->getGridH());
  roadMapper = mapper;
  cachedHeightRevision = terrainManager->getHeightRevision();
  std::cout << "[Terrain] Heightfield reloaded; road height sampler re-initialised."
            << std::endl;
}

void Terrain::saveTerrain() {
  if (terrainManager) {
    terrainManager->saveTerrain();
  }
}

void Terrain::rebuildRoadMesh() {
  if (!roadGraph || !roadMeshBuilder) return;
  // FALLAGA_ROADGAP: trace one probe point through the rebuild phases. Prints the
  // visual heightfield at (4313.14,-989.31) -- the worst measured terrain-above-road
  // point -- after every stage that can move it, so it is unambiguous which stage
  // leaves the ground above the asphalt.
  auto probeY = [&]() -> float {
    if (!std::getenv("FALLAGA_ROADGAP") || !terrainManager) return 0.0f;
    const int vw = terrainManager->getVisualGridW();
    const int vh = terrainManager->getVisualGridH();
    const std::vector<float>& H = terrainManager->getVisualHeightsData();
    if (vw < 2 || vh < 2) return 0.0f;
    const float nx = (4313.14f - terrainManager->getWorldMinX()) /
                     (terrainManager->getWorldMaxX() - terrainManager->getWorldMinX()) * (vw - 1);
    const float nz = (-989.314f - terrainManager->getWorldMinZ()) /
                     (terrainManager->getWorldMaxZ() - terrainManager->getWorldMinZ()) * (vh - 1);
    const int x0 = std::max(0, std::min((int)nx, vw - 2));
    const int z0 = std::max(0, std::min((int)nz, vh - 2));
    const float tx = nx - x0, tz = nz - z0;
    return H[(size_t)z0 * vw + x0] * (1 - tx) * (1 - tz) +
           H[(size_t)z0 * vw + x0 + 1] * tx * (1 - tz) +
           H[(size_t)(z0 + 1) * vw + x0] * (1 - tx) * tz +
           H[(size_t)(z0 + 1) * vw + x0 + 1] * tx * tz;
  };
  const bool traceProbe = std::getenv("FALLAGA_ROADGAP") != nullptr;
  if (traceProbe) std::cout << "[Phase] start        probeY=" << probeY() << std::endl;
  if (terrainManager) terrainManager->logHeightfieldSignature("rebuild/BEFORE");
  // Restore the current terrain before sampling/evaluating the edited network;
  // this prevents each rebuild from stacking another road cut into the DEM.
  if (terrainManager) terrainManager->applyRoadBedHeightfield(nullptr, 0, 0);
  if (traceProbe) std::cout << "[Phase] afterRestore probeY=" << probeY() << std::endl;
  for (auto& seg : roadGraph->segments) {
    RoadSpline::evaluate(seg, roadHeightSampler, 0.5f);
  }
  roadGraph->buildTopology();
  roadMeshBuilder->destroy();
  // Use a flat support ribbon while grading the terrain; snapRoads rebuilds
  // the final visible ribbon with its crown after the heightfield is settled.
  roadMeshBuilder->build(roadGraph->segments, roadHeightSampler, nullptr, 1.5f, false);
  roadMeshBuilder->upload();
  if (!roadDeformer) roadDeformer = new RoadDeformer();
  const int deformW = terrainManager ? terrainManager->getVisualGridW() : 0;
  const int deformH = terrainManager ? terrainManager->getVisualGridH() : 0;
  roadDeformer->build(roadGraph->segments, roadHeightSampler, roadMapper, deformW, deformH);
  if (terrainManager)
    terrainManager->setRoadGeometryMask(roadDeformer->getSurfaceMaskData(), deformW, deformH);
  if (terrainManager)
    terrainManager->applyRoadBedHeightfield(roadDeformer->getDeformationData(), deformW, deformH);
  if (traceProbe) std::cout << "[Phase] afterRoadBed  probeY=" << probeY() << std::endl;
  if (terrainManager && roadMeshBuilder)
    terrainManager->conformTerrainToRoadMesh(0.0f, 0.0f, 0.0f, 1.0f, 1.0f,
        roadMeshBuilder->debugVertices(), roadMeshBuilder->debugIndices(), true, 20.0f);
  if (traceProbe) std::cout << "[Phase] afterConform  probeY=" << probeY() << std::endl;
  if (roadTileField) roadTileField->build(roadGraph->segments, roadMapper);
  snapRoadsToRenderedTerrain();
  if (traceProbe) std::cout << "[Phase] afterSnap     probeY=" << probeY() << std::endl;
  if (terrainManager) terrainManager->logHeightfieldSignature("rebuild/AFTER");
  std::cout << "[Terrain] Road mesh rebuilt: " << roadGraph->segments.size()
            << " segments, " << roadMeshBuilder->getVertexCount() << " vertices, "
            << roadMeshBuilder->getTriangleCount() << " triangles" << std::endl;
}

void Terrain::snapRoadsToRenderedTerrain() {
  if (!terrainManager || !roadGraph || !roadMeshBuilder) {
    rebuildRoadSurfaceIndex();
    ++collisionSurfaceRevision;
    return;
  }
  const int vw = terrainManager->getVisualGridW();
  const int vh = terrainManager->getVisualGridH();
  const std::vector<float>& visual = terrainManager->getVisualHeightsData();
  if (vw < 2 || vh < 2 || visual.empty()) {
    rebuildRoadSurfaceIndex();
    ++collisionSurfaceRevision;
    return;
  }

  // Sample the exact array the terrain chunks are meshed from, so the ribbon and
  // the ground triangles are interpolated from the same bilinear surface. Anything
  // less leaves a visible step wherever the road crosses a heightfield cell line.
  TerrainCoordinateMapper visualMapper;
  visualMapper.bounds = { terrainManager->getWorldMinX(), terrainManager->getWorldMaxX(),
                          terrainManager->getWorldMinZ(), terrainManager->getWorldMaxZ(),
                          terrainManager->getMinHeight(), terrainManager->getMaxHeight() };
  visualMapper.heightmapW = vw;
  visualMapper.heightmapH = vh;
  HeightSampler visualSampler;
  visualSampler.init(visualMapper, visual.data(), vw, vh);

  for (auto& seg : roadGraph->segments) {
    RoadSpline::evaluate(seg, visualSampler, 0.5f);
  }
  roadMeshBuilder->destroy();
  roadMeshBuilder->build(roadGraph->segments, visualSampler, nullptr);
  roadMeshBuilder->upload();
  // Deliberately no conform pass here: the ribbon now sits on the surface, so
  // re-conforming would lower the ground another 2 cm on every rebuild and drift.
  rebuildRoadSurfaceIndex();
  ++collisionSurfaceRevision;
}

void Terrain::saveRoadsToJSON(const std::string& path) const {
  if (!roadGraph) return;
  // The in-memory graph lives in world coordinates (plan + offset); the JSON on
  // disk is stored in plan space, so translate back before writing.
  RoadGraph planSpace;
  planSpace.segments.reserve(roadGraph->segments.size());
  for (const auto& seg : roadGraph->segments) {
    RoadSegment copy = seg;
    copy.splinePoints.clear();
    copy.tangents.clear();
    copy.rightVecs.clear();
    copy.curvature.clear();
    copy.cumulativeDist.clear();
    for (auto& p : copy.controlPoints) {
      p.pos.x -= HISTORIC_PLAN_OFFSET_X;
      p.pos.y -= HISTORIC_PLAN_OFFSET_Z;
    }
    planSpace.segments.push_back(copy);
  }
  if (planSpace.saveToJSON(path)) {
    std::cout << "[Terrain] Saved road graph -> " << path << " ("
              << planSpace.segments.size() << " segments, plan space)" << std::endl;
  } else {
    std::cerr << "[Terrain] FAILED to write road graph to " << path << std::endl;
  }
}

// Compute the center of the whole road network (average of every control point).
static glm::vec2 roadNetworkCenter(const RoadGraph& graph) {
  glm::vec2 c(0.0f);
  size_t n = 0;
  for (const auto& seg : graph.segments) {
    for (const auto& p : seg.controlPoints) {
      c += p.pos;
      ++n;
    }
  }
  if (n > 0) c /= (float)n;
  return c;
}

void Terrain::mirrorRoads(bool flipX) {
  if (!roadGraph || roadGraph->segments.empty()) return;
  const glm::vec2 c = roadNetworkCenter(*roadGraph);
  for (auto& seg : roadGraph->segments) {
    for (auto& p : seg.controlPoints) {
      if (flipX) p.pos.x = 2.0f * c.x - p.pos.x;
      else       p.pos.y = 2.0f * c.y - p.pos.y;
    }
  }
  rebuildRoadMesh();
  std::cout << "[Road] Mirrored road network across the " << (flipX ? "Z-axis (X)" : "X-axis (Z)")
            << " (pivot at " << c.x << ", " << c.y << ")" << std::endl;
}

void Terrain::rotateRoads(float angleDeg) {
  if (!roadGraph || roadGraph->segments.empty()) return;
  const glm::vec2 c = roadNetworkCenter(*roadGraph);
  const float a = angleDeg * 3.14159265f / 180.0f;
  const float ca = std::cos(a), sa = std::sin(a);
  for (auto& seg : roadGraph->segments) {
    for (auto& p : seg.controlPoints) {
      const float x = p.pos.x - c.x, z = p.pos.y - c.y;
      p.pos.x = c.x + ca * x - sa * z;
      p.pos.y = c.y + sa * x + ca * z;
    }
  }
  rebuildRoadMesh();
  std::cout << "[Road] Rotated road network by " << angleDeg << " deg"
            << " (pivot at " << c.x << ", " << c.y << ")" << std::endl;
}

void Terrain::scaleRoads(float factor) {
  if (!roadGraph || roadGraph->segments.empty()) return;
  const glm::vec2 c = roadNetworkCenter(*roadGraph);
  for (auto& seg : roadGraph->segments) {
    for (auto& p : seg.controlPoints) {
      p.pos.x = c.x + (p.pos.x - c.x) * factor;
      p.pos.y = c.y + (p.pos.y - c.y) * factor;
    }
    // Widths and shoulders follow the scale so roads stay proportional.
    seg.width *= factor;
    seg.shoulderWidth *= factor;
  }
  rebuildRoadMesh();
  std::cout << "[Road] Scaled road network by " << factor << "x"
            << " (pivot at " << c.x << ", " << c.y << ")" << std::endl;
}

void Terrain::translateRoads(float dx, float dz) {
  if (!roadGraph || roadGraph->segments.empty()) return;
  for (auto& seg : roadGraph->segments) {
    for (auto& p : seg.controlPoints) {
      p.pos.x += dx;
      p.pos.y += dz;
    }
  }
  rebuildRoadMesh();
  std::cout << "[Road] Moved road network by (" << dx << ", " << dz << ")m" << std::endl;
}

void Terrain::connectRoads(float tolerance) {
  if (!roadGraph || roadGraph->segments.empty()) return;
  auto& segs = roadGraph->segments;

  // â”€â”€ Pass 1: snap each segment endpoint onto the nearest other road's
  //    centerline (forms true T-junctions instead of gaps).
  auto distPt = [](const RoadPoint& a, const RoadPoint& b) {
    const float dx = a.pos.x - b.pos.x, dz = a.pos.y - b.pos.y;
    return std::sqrt(dx * dx + dz * dz);
  };
  int snapped = 0;
  for (auto& seg : segs) {
    if (seg.controlPoints.empty()) continue;
    const int eIdx[2] = { 0, (int)seg.controlPoints.size() - 1 };
    for (int e = 0; e < 2; ++e) {
      RoadPoint& cp = seg.controlPoints[eIdx[e]];
      bool found = false; float bestPx = 0.0f, bestPz = 0.0f;
      for (auto& os : segs) {
        if (&os == &seg) continue;
        for (size_t k = 0; k + 1 < os.controlPoints.size(); ++k) {
          const float dx = os.controlPoints[k + 1].pos.x - os.controlPoints[k].pos.x;
          const float dz = os.controlPoints[k + 1].pos.y - os.controlPoints[k].pos.y;
          const float len2 = dx * dx + dz * dz;
          const float t = (len2 > 1e-8f)
                              ? glm::clamp(((cp.pos.x - os.controlPoints[k].pos.x) * dx +
                                            (cp.pos.y - os.controlPoints[k].pos.y) * dz) / len2, 0.0f, 1.0f)
                              : 0.0f;
          const float px = os.controlPoints[k].pos.x + t * dx;
          const float pz = os.controlPoints[k].pos.y + t * dz;
          const float d = std::sqrt((cp.pos.x - px) * (cp.pos.x - px) +
                                    (cp.pos.y - pz) * (cp.pos.y - pz));
          // Only connect to a road's interior (true T) â€” ignore hits on the
          // other road's own near endpoints to avoid squashing parallel roads.
          const bool nearEnd = (t * len2 <= tolerance * tolerance && t < 0.2f) ||
                               (len2 * (1.0f - t) * (1.0f - t) <= tolerance * tolerance && t > 0.8f);
          if (d < tolerance && !nearEnd && (!found || d < std::sqrt((cp.pos.x - bestPx) * (cp.pos.x - bestPx) + (cp.pos.y - bestPz) * (cp.pos.y - bestPz)))) {
            found = true; bestPx = px; bestPz = pz;
          }
        }
      }
      if (found) {
        cp.pos.x = bestPx;
        cp.pos.y = bestPz;
        ++snapped;
      }
    }
  }

  // â”€â”€ Pass 2: merge segments whose endpoints coincide into one polyline.
  int merged = 0;
  bool anyChange = true;
  while (anyChange) {
    anyChange = false;
    for (int i = 0; i < (int)segs.size() && !anyChange; ++i) {
      auto& A = segs[i];
      if (A.controlPoints.size() < 2) continue;
      for (int j = 0; j < (int)segs.size(); ++j) {
        if (i == j || anyChange) continue;
        auto& B = segs[j];
        if (B.controlPoints.size() < 2) continue;

        // Only merge segments that share the same road type and similar width.
        // Merging roads of different hierarchy (e.g. ColonialAvenue into Medina)
        // would destroy their independent rendering, so skip them.
        if (A.type != B.type || std::fabs(A.width - B.width) >= 0.2f) continue;

        bool attached = false;
        // A.last -> B.first : extend A with B
        if (distPt(A.controlPoints.back(), B.controlPoints.front()) <= tolerance) {
          A.controlPoints.pop_back();
          for (const auto& p : B.controlPoints) A.controlPoints.push_back(p);
          attached = true;
        }
        // A.first -> B.first : reverse A, then extend with B
        else if (distPt(A.controlPoints.front(), B.controlPoints.front()) <= tolerance) {
          std::reverse(A.controlPoints.begin(), A.controlPoints.end());
          A.controlPoints.pop_back();
          for (const auto& p : B.controlPoints) A.controlPoints.push_back(p);
          attached = true;
        }
        // A.last -> B.last : reverse B, then extend A with B
        else if (distPt(A.controlPoints.back(), B.controlPoints.back()) <= tolerance) {
          std::reverse(B.controlPoints.begin(), B.controlPoints.end());
          A.controlPoints.pop_back();
          for (const auto& p : B.controlPoints) A.controlPoints.push_back(p);
          attached = true;
        }
        // A.first -> B.last : prepend B to A
        else if (distPt(A.controlPoints.front(), B.controlPoints.back()) <= tolerance) {
          std::vector<RoadPoint> combined;
          for (const auto& p : B.controlPoints) combined.push_back(p);
          combined.pop_back();
          for (const auto& p : A.controlPoints) combined.push_back(p);
          A.controlPoints = std::move(combined);
          attached = true;
        }
        if (attached) {
          segs.erase(segs.begin() + j);
          ++merged;
          anyChange = true;
          break;
        }
      }
    }
  }

  rebuildRoadMesh();
  std::cout << "[Road] connectRoads(tolerance=" << tolerance << "m): " << snapped
            << " endpoints snapped, " << merged << " segments merged, "
            << segs.size() << " segments left" << std::endl;
}

void Terrain::setWaterLevel(float level) {
  waterLevelRuntime = level;
}

float Terrain::getWaterLevel() const {
  return waterLevelRuntime;
}

void Terrain::updateWaterMapTexture(const float* data) const {
  if (waterMapTex != 0 && data != nullptr) {
    glBindTexture(GL_TEXTURE_2D, waterMapTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WaterSimulation::SIM_SIZE, WaterSimulation::SIM_SIZE,
                    GL_RED, GL_FLOAT, data);
    glBindTexture(GL_TEXTURE_2D, 0);
  }
}
