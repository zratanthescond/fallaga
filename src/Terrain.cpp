#include "Terrain.h"
#include "PhysicsSystem.h"
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
// Heightmap analysis: ~32% of the map sits in one contiguous flat basin at the
// absolute floor (-50), with a very gentle shelf rising from -49 up to -43 that
// adds only ~3.6% more area. A sea level of -43 let that gentle shelf bleed
// water across large stretches of otherwise-dry terrain (visible as scattered
// shallow "lakes" far from any real coastline). Keeping the level close to the
// true floor confines water to the actual basin instead of creeping onto land.
static constexpr float WATER_BASE = -48.5f; // Sea level
static float waterLevelOffset = 0.0f; // Positive raises terrain (lowers water)
static float waterLevelRuntime = WATER_BASE;  // Real-time editor override

namespace {
constexpr float HISTORIC_PLAN_OFFSET_X = 3600.0f;
constexpr float HISTORIC_PLAN_OFFSET_Z = -701.0f;

bool isInsideHistoricCity(const RoadSegment& segment) {
  for (const RoadPoint& point : segment.controlPoints) {
    if (point.pos.x >= 2450.0f && point.pos.x <= 4850.0f &&
        point.pos.y >= -2100.0f && point.pos.y <= 1550.0f) {
      return true;
    }
  }
  return false;
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
//  Hash-based value noise — NO sine/cosine grids!
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
  oceanShader = new Shader("src/shaders/ocean.vert", "src/shaders/ocean.frag");
  streetShader =
      new Shader("src/shaders/street.vert", "src/shaders/street.frag");
  modelShader = new Shader("src/shaders/model.vert", "src/shaders/model.frag");

  std::cout << "[Terrain] Initializing Water Mesh..." << std::endl;
  buildWaterMesh();

  // ── GEOMETRY-BASED ROAD SYSTEM ──────────────────────────────────────────
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

      // 2. Load road graph from JSON
      roadGraph = new RoadGraph();
      if (roadGraph->loadFromJSON("assets/roads/roads.json")) {
          replaceWithHistoricCityRoads(*roadGraph);
          roadGraph->buildTopology();

          // 3. Evaluate Catmull-Rom splines + sample heights
          std::cout << "[Terrain] Evaluating road splines..." << std::endl;
          for (auto& seg : roadGraph->segments) {
              RoadSpline::evaluate(seg, roadHeightSampler, 0.5f);
          }

          // 4. Build terrain deformation field (2048x2048 = ~1 pixel per 13.6m)
          roadDeformer = new RoadDeformer();
          roadDeformer->build(roadGraph->segments, roadHeightSampler, mapper, 2048, 2048);
          terrainManager->setRoadDeformationTexture(roadDeformer->getDeformationTexture());

          // 5. Generate and upload road ribbon mesh
          roadMeshBuilder = new RoadMeshBuilder();
          roadMeshBuilder->build(roadGraph->segments, roadHeightSampler, roadDeformer);
          roadMeshBuilder->upload();

          std::cout << "[Terrain] Road System ready: "
                    << roadGraph->segments.size() << " segments, "
                    << roadMeshBuilder->getVertexCount() << " vertices, "
                    << roadMeshBuilder->getTriangleCount() << " triangles" << std::endl;
      } else {
          std::cerr << "[Terrain] Road system: no roads.json found, using legacy fallback" << std::endl;
          buildStreetMesh(); // Legacy fallback
      }
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

  // Initialize dynamic waterMapTex (256x256 single-channel floating-point texture)
  glGenTextures(1, &waterMapTex);
  glBindTexture(GL_TEXTURE_2D, waterMapTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, 256, 256, 0, GL_RED, GL_FLOAT, nullptr);
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

  // Cleanup road system
  delete roadGraph;
  delete roadDeformer;
  delete roadMeshBuilder;
}

void Terrain::buildWaterFBOs(int width, int height) {
  // Halve resolution for performance — reflections don't need full res
  int w = width  / 2;
  int h = height / 2;
  fboW = w; fboH = h;

  // ── REFLECTION FBO ──────────────────────────────────────────────
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

  // ── REFRACTION FBO ──────────────────────────────────────────────
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
  fread(&vCount, sizeof(uint32_t), 1, f);
  fread(&iCount, sizeof(uint32_t), 1, f);

  std::vector<float> vData((size_t)vCount * 9);
  fread(vData.data(), sizeof(float), (size_t)vCount * 9, f);

  std::vector<uint32_t> iData((size_t)iCount);
  fread(iData.data(), sizeof(uint32_t), (size_t)iCount, f);
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

void Terrain::renderStreets(const glm::mat4& proj, const glm::mat4& view,
                            const glm::vec3& lightDir) const {
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

  glBindVertexArray(streetVAO);
  glDrawElements(GL_TRIANGLES, streetIndexCount, GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);

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
              fread(&numTrees, sizeof(uint32_t), 1, f);
              fread(&numRocks, sizeof(uint32_t), 1, f);
              fread(&numGrass, sizeof(uint32_t), 1, f);

              proceduralTrees.resize(numTrees);
              proceduralRocks.resize(numRocks);
              proceduralGrass.resize(numGrass);

              fread(proceduralTrees.data(), sizeof(glm::mat4), numTrees, f);
              fread(proceduralRocks.data(), sizeof(glm::mat4), numRocks, f);
              fread(proceduralGrass.data(), sizeof(glm::mat4), numGrass, f);

              proceduralInitialized = true;
              std::cout << "[Terrain] Successfully loaded binary roadside foliage: " 
                        << numTrees << " trees, " 
                        << numRocks << " rocks, " 
                        << numGrass << " grass patches." << std::endl;
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
      // PHASE 1: GRASS — Clustered carpet placement with tight budget
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
      if (roadMeshBuilder && roadMeshBuilder->getTriangleCount() > 0 && streetShader) {
        streetShader->use();
        streetShader->setMat4("projection", proj);
        streetShader->setMat4("view", view);
        streetShader->setMat4("model", glm::mat4(1.0f));
        streetShader->setVec3("lightDir", light);
        roadMeshBuilder->render(*streetShader);
      } else {
        renderStreets(proj, view, light);
      }
    }
  }
  double t_tm_end = glfwGetTime();

  // --- PHASE 1: CACHED BATCH COLLECTION ---
  // Rebuild the matrix cache once per frame IF dirty.
  static bool firstRebuildDone = false;
  if (SousseMapData::layoutDirty || batchesDirty ||
      (!firstRebuildDone && cachedBatches.empty())) {
    cachedBatches.clear();
    cachedCustomRenderTasks.clear();

    // --- PROCEDURAL CITY WALL REBUILD ---
    if (wallVAO != 0) {
      glDeleteVertexArrays(1, &wallVAO);
      glDeleteBuffers(1, &wallVBO);
      glDeleteBuffers(1, &wallEBO);
      wallVAO = 0;
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
    // Rigid placement — no per-vertex deformation (keeps wall shape intact)
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
  float baseH = terrainManager ? terrainManager->getHeightAt(x, z) : 0.0f;
  // Apply road deformation
  if (roadDeformer && roadDeformer->isBuilt()) {
      baseH += roadDeformer->getDeformation(x, z);
  }
  return baseH;
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

Terrain::Zone Terrain::getZoneAt(float x, float z) const { return Zone::PLAIN; }
bool Terrain::pointInPolygon(float x, float z,
                             const std::vector<glm::vec2> &polygon) const {
  return false;
}

void Terrain::update(const glm::vec3& camPos) {
  if (terrainManager) {
    terrainManager->update(camPos);
  }
}

void Terrain::saveTerrain() {
  if (terrainManager) {
    terrainManager->saveTerrain();
  }
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
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 256, GL_RED, GL_FLOAT, data);
    glBindTexture(GL_TEXTURE_2D, 0);
  }
}
