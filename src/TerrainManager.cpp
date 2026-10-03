#include "TerrainManager.h"
#include "TextureEngine.h"
#include <GLFW/glfw3.h>
#include <stb_image.h>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <unordered_map>
#include <sys/stat.h>

TerrainManager::TerrainManager(const std::string& hmPath, const std::string& pmPath, float minX, float maxX, float minZ, float maxZ, float minH, float maxH)
    : worldMinX(minX), worldMaxX(maxX), worldMinZ(minZ), worldMaxZ(maxZ), minHeight(minH), maxHeight(maxH) {
    
    // Store heightmap file path and initial modification timestamp
    hmFilePath = hmPath;
    struct stat hmStat;
    if (stat(hmFilePath.c_str(), &hmStat) == 0) {
        lastHmModTime = hmStat.st_mtime;
    }

    std::cout << "[TerrainManager] Creating Shader..." << std::endl;
    terrainShader = new Shader("src/shaders/terrain_chunk.vert", "src/shaders/terrain_chunk.frag");
    std::cout << "[TerrainManager] Shader ID: " << terrainShader->ID << std::endl;
    
    std::cout << "[TerrainManager] Loading Heightmap: " << hmPath << std::endl;
    loadHeightmap(hmPath); 
    
    std::cout << "[TerrainManager] Setting up textures..." << std::endl;
    setupTextures(pmPath);
    
    if (gridW <= 0 || gridH <= 0) {
        std::cerr << "[TerrainManager] CRITICAL ERROR: Heightmap failed to load. Chunks will not be generated." << std::endl;
        return;
    }

    std::cout << "[TerrainManager] Generating chunks..." << std::endl;
    int stride = CHUNK_SIZE - 1;

    // --- OPTIMIZATION: Downsample visual mesh while keeping full-res heights for physics ---
    // Target a maximum visual resolution (e.g., 2049x2049) to keep rendering and loading fast.
    // Set to 1025 for even higher performance, or 2049 for high-quality balance.
    this->visualW = gridW;
    this->visualH = gridH;
    const int MAX_VISUAL_RES = 4097;
    
    if (gridW > MAX_VISUAL_RES) {
        int numChunksTarget = MAX_VISUAL_RES / stride;
        visualW = numChunksTarget * stride + 1;
        visualH = numChunksTarget * stride + 1;
        
        std::cout << "[TerrainManager] Downsampling heightmap for visual mesh: "
                  << gridW << "x" << gridH << " -> " << visualW << "x" << visualH << std::endl;
                  
        visualHeights.resize(visualW * visualH);
        for (int z = 0; z < visualH; ++z) {
            float fz = (float)z / (visualH - 1);
            for (int x = 0; x < visualW; ++x) {
                float fx = (float)x / (visualW - 1);
                
                float srcX = fx * (gridW - 1);
                float srcZ = fz * (gridH - 1);
                int x0 = (int)srcX, x1 = std::min(x0 + 1, gridW - 1);
                int z0 = (int)srcZ, z1 = std::min(z0 + 1, gridH - 1);
                float tx = srcX - x0, tz = srcZ - z0;
                
                visualHeights[z * visualW + x] = 
                    heights[z0 * gridW + x0] * (1.0f - tx) * (1.0f - tz) +
                    heights[z0 * gridW + x1] * tx * (1.0f - tz) +
                    heights[z1 * gridW + x0] * (1.0f - tx) * tz +
                    heights[z1 * gridW + x1] * tx * tz;
            }
        }
        physicsVisualHeights = visualHeights;
    } else {
        visualHeights = heights;
        physicsVisualHeights = visualHeights;
    }

    int numChunksX = (visualW - 1) / stride;
    int numChunksZ = (visualH - 1) / stride;
    
    std::cout << "[TerrainManager] Visual Grid: " << visualW << "x" << visualH 
              << " -> " << numChunksX << "x" << numChunksZ << " chunks (" 
              << numChunksX * numChunksZ << " total)." << std::endl;

    for (int z = 0; z < numChunksZ; ++z) {
        for (int x = 0; x < numChunksX; ++x) {
            TerrainChunk* chunk = new TerrainChunk(x, z, CHUNK_SIZE);
            chunk->generateMesh(visualHeights, visualW, visualH, worldMinX, worldMaxX, worldMinZ, worldMaxZ,
                               &roadMaskData, roadW, roadH);
            chunks.push_back(chunk);
        }
    }
    std::cout << "[TerrainManager] Initialization Complete." << std::endl;
}

TerrainManager::~TerrainManager() {
    for (auto c : chunks) delete c;
    chunks.clear();
    // Everything this manager allocated itself. Previously only roadGeometryMask
    // was freed, so each run leaked the 16-bit heightmap texture plus the two
    // 15-slice 1024^2 texture arrays (~160 MB with mipmaps).
    if (roadGeometryMask)  glDeleteTextures(1, &roadGeometryMask);
    if (heightmapTexID)    glDeleteTextures(1, &heightmapTexID);
    if (roadMask)          glDeleteTextures(1, &roadMask);
    if (terrainLayerArray) glDeleteTextures(1, &terrainLayerArray);
    if (terrainNormalArray) glDeleteTextures(1, &terrainNormalArray);
    if (terrainDispArray)  glDeleteTextures(1, &terrainDispArray);
    if (terrainRoughArray) glDeleteTextures(1, &terrainRoughArray);
    roadGeometryMask = heightmapTexID = roadMask = 0;
    terrainLayerArray = terrainNormalArray = terrainDispArray = terrainRoughArray = 0;
    // portMask / albedoTex / splatmap2Tex are owned by TextureEngine's cache and
    // are released by TextureEngine::cleanup(), so they are deliberately not
    // deleted here (that would be a double free).
    delete terrainShader;
    terrainShader = nullptr;
}

void TerrainManager::setRoadGeometryMask(const unsigned char* mask, int maskW, int maskH) {
    if (!mask || maskW < 2 || maskH < 2) {
        hasRoadGeometryMask = false;
        return;
    }
    if (roadGeometryMask == 0) glGenTextures(1, &roadGeometryMask);
    glBindTexture(GL_TEXTURE_2D, roadGeometryMask);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, maskW, maskH, 0,
                 GL_RED, GL_UNSIGNED_BYTE, mask);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    hasRoadGeometryMask = true;
}

void TerrainManager::loadHeightmap(const std::string& path) {
    int w, h, ch; stbi_set_flip_vertically_on_load(false);
    unsigned short* rawData = stbi_load_16(path.c_str(), &w, &h, &ch, 1);
    if (rawData) {
        std::cout << "[TerrainManager] Loading Heightmap at native resolution: " << w << "x" << h << "..." << std::endl;
        
        gridW = w; 
        gridH = h; 
        heights.resize(w * h);
        // `resize` may reallocate; tell caching consumers (HeightSampler) that any
        // pointer they hold to the previous buffer is now dangling.
        ++heightRevision;

        // Single loop to convert 16-bit raw data to float elevations
        float invDivisor = 1.0f / 65535.0f;
        float heightRange = maxHeight - minHeight;
        for (int i = 0; i < w * h; ++i) {
            heights[i] = minHeight + (rawData[i] * invDivisor) * heightRange;
        }

        // Native 16-bit precision preserves crisp cutbanks, wadi channels, and terrain relief without blurring

        // Create a float vector of normalized heights to upload as a widely compatible GL_R32F texture
        std::vector<float> texData(w * h);
        for (int i = 0; i < w * h; ++i) {
            texData[i] = std::clamp((heights[i] - minHeight) / heightRange, 0.0f, 1.0f);
        }

        if (heightmapTexID != 0) {
            glDeleteTextures(1, &heightmapTexID);
            heightmapTexID = 0;
        }
        glGenTextures(1, &heightmapTexID); 
        glBindTexture(GL_TEXTURE_2D, heightmapTexID);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, w, h, 0, GL_RED, GL_FLOAT, texData.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); 
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        
        stbi_image_free(rawData);
        std::cout << "[TerrainManager] Heightmap loaded and uploaded to GPU as float texture." << std::endl;
    }
}

void TerrainManager::setupTextures(const std::string& pmPath) {
    portMask = TextureEngine::Instance().loadTexture(pmPath, false);
    albedoTex = TextureEngine::Instance().loadTexture("assets/terrain/splatmap1.png", false);
    splatmap2Tex = TextureEngine::Instance().loadTexture("assets/terrain/splatmap2.png", false);
    
    int rw, rh, rch;
    unsigned char* roadData = stbi_load("assets/terrain/roadMap.png", &rw, &rh, &rch, 4);
    if (roadData) {
        // If the alpha channel is uniformly 255 (standard unpainted opaque PNG), clear it to 0
        // so it does not accidentally overwrite the entire terrain with activePaintLayerIdx!
        bool isUnpaintedOpaque = true;
        for (int i = 3; i < rw * rh * 4; i += 4 * 64) {
            if (roadData[i] != 255) { isUnpaintedOpaque = false; break; }
        }
        for (int y = 0; y < rh; ++y) {
            for (int x = 0; x < rw; ++x) {
                int idx = (y * rw + x) * 4;
                if (isUnpaintedOpaque) {
                    roadData[idx + 3] = 0;
                }
                if (x < 5 || x > rw - 6 || y < 5 || y > rh - 6) {
                    roadData[idx + 0] = 0; // Red (Roads)
                    roadData[idx + 1] = 0; // Green (Railways)
                    roadData[idx + 2] = 0; // Blue (Rivers)
                }
            }
        }
        roadW = rw; roadH = rh;
        roadMaskData.assign(roadData, roadData + (rw * rh * 4));

        if (roadMask == 0) glGenTextures(1, &roadMask);
        glBindTexture(GL_TEXTURE_2D, roadMask);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rw, rh, 0, GL_RGBA, GL_UNSIGNED_BYTE, roadData);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        stbi_image_free(roadData);
        hasRoadMask = true;
    } else {
        std::cerr << "[TerrainManager] FAILED to load assets/terrain/roadMap.png" << std::endl;
        hasRoadMask = false;
    }
    
    // ── TERRAIN PALETTE — Authentic Mediterranean & Historical Sousse Port ────────────────
    const std::string GP = "assets/terrain/textures/TerrainMainTextures/";
    const std::string PP = "assets/terrain/textures/TerrainPlacementTextures/p_08_/";
    const std::string CT_RD = "assets/terrain/textures/CityTextures/bla_road_txd/";
    const std::string CT_EX = "assets/terrain/textures/CityTextures/bla_restaurant_ext+hidr/";

    struct TerrainLayer { const char* name; std::string alb; std::string nrm; };
    // 15-LAYER PALETTE: Authentic Mediterranean / Sahel landscape & Historic Sousse Port
    const TerrainLayer palette[] = {
        { "0 coastal sand",         GP + "greatplain1/gre_dirt_stones_01_abte.png",    GP + "greatplain1/gre_dirt_stones_01_nm.png" },
        { "1 clumpy grass tufts",   GP + "greatplainmain/grp_dirt_grass_01_abte.png",  GP + "greatplainmain/grp_dirt_grass_01_nm.png" },
        { "2 lush green grass",     PP + "hea_grass_lush_je_02_abte.png",              PP + "hea_grass_lush_je_02_nm.png" },
        { "3 rich warm loam soil",  GP + "greatplain2/hen_mud_soil_01_abte.png",       GP + "greatplain2/hen_mud_soil_01_nm.png" },
        { "4 limestone cliff",      GP + "greatplain3/cho_erosion_01_abte.png",        GP + "greatplain3/cho_erosion_01_nm.png" },
        { "5 rocky wadi bed",       PP + "hea_rock_riverbed_br_d_abte.png",            PP + "hea_rock_riverbed_br_d_nm.png" },
        { "6 wadi alluvial silt",   PP + "roa_silt_02_abte.png",                       PP + "roa_silt_02_nm.png" },
        { "7 riparian lush grass",  PP + "hea_grass_lush_je_02_abte.png",              PP + "hea_grass_lush_je_02_nm.png" },
        { "8 country dirt road",    PP + "gua_dirt_road_01c_abte.png",                 PP + "gua_dirt_road_01_nm.png" },
        { "9 chalk scree talus",    GP + "greatplaina/gre_scree_02_abte.png",          GP + "greatplaina/gre_scree_02_nm.png" },
        { "10 tarred road",         "assets/terrain/tarred_gravel_2k/textures/tarred_gravel_diff_2k.jpg", "assets/terrain/tarred_gravel_2k/textures/tarred_gravel_nor_dx_2k.png" },
        { "11 strata cliff scan",   PP + "gre_cliff_scan_ds_03_abte.png",             PP + "gre_cliff_scan_ds_03_nm.png" },
        { "12 port cobblestone",    CT_RD + "nbx_cobble_br_01_ab.png",                 CT_RD + "nbx_cobble_br_01_nm.png" },
        { "13 breakwater ashlar",   CT_EX + "nbx_brick_greyblock01b_ab.png",            CT_EX + "nbx_brick_greyblock01b_nm.png" },
        { "14 breakwater stones",   "assets/terrain/rock_wall_06/rock_wall_06_diff_1k.jpg", "assets/terrain/rock_wall_06/rock_wall_06_nor_dx_1k.jpg" },
    };

    std::vector<std::string> layers, normLayers;
    const int numLayers = sizeof(palette) / sizeof(palette[0]);
    for (int i = 0; i < numLayers; ++i) {
        layers.push_back(palette[i].alb);
        normLayers.push_back(palette[i].nrm);
        std::cout << "[TerrainManager] layer " << palette[i].name << " <- " << palette[i].alb << "\n";
    }
    // ALBEDO IS sRGB. These images are authored in gamma space, so they must be
    // uploaded as SRGB8 and let the GPU linearise them on sample. Uploading them as
    // plain RGBA8 fed gamma-encoded values straight into the lighting maths, and the
    // shader then applied pow(1/2.2) on the way out — every albedo was gamma-encoded
    // twice, which is what bleached the whole terrain to chalky near-white.
    // Normals, splatmaps and masks stay linear (false); only colour is sRGB.
    terrainLayerArray = TextureEngine::Instance().loadTextureArray(layers, 1024, 1024, true);

    // Normals are DATA, not colour — they must stay linear (sRGB = false).
    terrainNormalArray = TextureEngine::Instance().loadTextureArray(normLayers, 1024, 1024, false);

    // Displacement and roughness arrays disabled for performance
    terrainDispArray = 0;
    terrainRoughArray = 0;
}

void TerrainManager::update(const glm::vec3& camPos) {
    // Periodically check if heightmap file was updated (every 60 frames to prevent disk overhead)
    static int frameCounter = 0;
    frameCounter++;
    if (frameCounter % 60 == 0) {
        struct stat hmStat;
        if (stat(hmFilePath.c_str(), &hmStat) == 0) {
            if (hmStat.st_mtime != lastHmModTime) {
                lastHmModTime = hmStat.st_mtime;
                std::cout << "[TerrainManager] DETECTED heightmap.png change! Regenerating terrain..." << std::endl;
                
                // 1. Re-load the heightmap (loads new heights, recreates texture)
                loadHeightmap(hmFilePath);

                // 2. Re-populate visualHeights (handling downsampling if needed)
                int stride = CHUNK_SIZE - 1;
                this->visualW = gridW;
                this->visualH = gridH;
                const int MAX_VISUAL_RES = 4097;
                
                if (gridW > MAX_VISUAL_RES) {
                    int numChunksTarget = MAX_VISUAL_RES / stride;
                    visualW = numChunksTarget * stride + 1;
                    visualH = numChunksTarget * stride + 1;
                    visualHeights.resize(visualW * visualH);
                    for (int z = 0; z < visualH; ++z) {
                        float fz = (float)z / (visualH - 1);
                        for (int x = 0; x < visualW; ++x) {
                            float fx = (float)x / (visualW - 1);
                            float srcX = fx * (gridW - 1);
                            float srcZ = fz * (gridH - 1);
                            int x0 = (int)srcX, x1 = std::min(x0 + 1, gridW - 1);
                            int z0 = (int)srcZ, z1 = std::min(z0 + 1, gridH - 1);
                            float tx = srcX - x0, tz = srcZ - z0;
                            visualHeights[z * visualW + x] = 
                                heights[z0 * gridW + x0] * (1.0f - tx) * (1.0f - tz) +
                                heights[z0 * gridW + x1] * tx * (1.0f - tz) +
                                heights[z1 * gridW + x0] * (1.0f - tx) * tz +
                                heights[z1 * gridW + x1] * tx * tz;
                        }
                    }
                } else {
                    visualHeights = heights;
                }
                physicsVisualHeights = visualHeights;

                // 3. Regenerate mesh for each existing chunk!
                int numChunksX = (visualW - 1) / stride;
                int numChunksZ = (visualH - 1) / stride;
                
                // Clear and recreate chunks if visual resolution changed, otherwise update their meshes
                if ((int)chunks.size() != numChunksX * numChunksZ) {
                    for (auto c : chunks) delete c;
                    chunks.clear();
                    for (int z = 0; z < numChunksZ; ++z) {
                        for (int x = 0; x < numChunksX; ++x) {
                            TerrainChunk* chunk = new TerrainChunk(x, z, CHUNK_SIZE);
                            chunk->generateMesh(visualHeights, visualW, visualH, worldMinX, worldMaxX, worldMinZ, worldMaxZ,
                                               &roadMaskData, roadW, roadH);
                            chunks.push_back(chunk);
                        }
                    }
                } else {
                    int idx = 0;
                    for (int z = 0; z < numChunksZ; ++z) {
                        for (int x = 0; x < numChunksX; ++x) {
                            chunks[idx]->generateMesh(visualHeights, visualW, visualH, worldMinX, worldMaxX, worldMinZ, worldMaxZ,
                                                     &roadMaskData, roadW, roadH);
                            idx++;
                        }
                    }
                }
                
                std::cout << "[TerrainManager] Terrain regenerated successfully from updated heightmap.png!" << std::endl;
            }
        }
    }
}

void TerrainManager::render(const glm::mat4& proj, const glm::mat4& view, const glm::vec3& camPos, const glm::vec3& lightDir, const glm::vec3& lightColor, float waterLevel, const glm::vec4& clipPlane, float maxDrawDistance) {
    if (!terrainShader) return;

    // Force opaque depth-tested render state
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    if (clipPlane.w < 50000.0f) {
        glEnable(GL_CLIP_DISTANCE0);
    } else {
        glDisable(GL_CLIP_DISTANCE0);
    }

    terrainShader->use();
    terrainShader->setMat4("projection", proj); terrainShader->setMat4("view", view); terrainShader->setMat4("model", glm::mat4(1.0f));
    terrainShader->setVec3("viewPos", camPos); terrainShader->setVec3("lightDir", lightDir); terrainShader->setVec3("lightColor", lightColor);
    terrainShader->setFloat("sunTilt", lightDir.y);
    terrainShader->setFloat("waterLevel", waterLevel);
    terrainShader->setFloat("time", (float)glfwGetTime());
    terrainShader->setVec4("clipPlane", clipPlane);
    
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D_ARRAY, terrainLayerArray); terrainShader->setInt("terrainTex", 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, portMask); terrainShader->setInt("portMask", 1);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D_ARRAY, terrainNormalArray); terrainShader->setInt("terrainNormal", 2);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, roadMask); terrainShader->setInt("roadMask", 3);
    glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, albedoTex); terrainShader->setInt("splatmap1", 4);
    glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D, splatmap2Tex); terrainShader->setInt("splatmap2", 5);
    glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_2D, roadGeometryMask ? roadGeometryMask : roadMask);
    terrainShader->setInt("splineRoadMask", 6);
    terrainShader->setBool("hasRoadMask", hasRoadMask);
    terrainShader->setBool("hasSplineRoadMask", hasRoadGeometryMask);
    terrainShader->setInt("activePaintLayerIdx", activePaintLayerIdx);
    
    // Extract 6 frustum planes from View-Projection Matrix
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

    // Frustum and Distance Culling
    int renderedChunks = 0;
    for (auto c : chunks) {
        // Distance culling against chunk bounding sphere
        glm::vec3 center = (c->minBounds + c->maxBounds) * 0.5f;
        glm::vec3 halfExtent = (c->maxBounds - c->minBounds) * 0.5f;
        float radius = glm::length(halfExtent);

        float distCamToCenter = glm::distance(camPos, center);
        if (distCamToCenter - radius > maxDrawDistance) {
            continue;
        }

        // Frustum culling (AABB vs 6 planes)
        bool inFrustum = true;
        for (int p = 0; p < 6; ++p) {
            glm::vec3 pVertex(
                (planes[p].x > 0.0f) ? c->maxBounds.x : c->minBounds.x,
                (planes[p].y > 0.0f) ? c->maxBounds.y : c->minBounds.y,
                (planes[p].z > 0.0f) ? c->maxBounds.z : c->minBounds.z
            );
            if (glm::dot(glm::vec3(planes[p]), pVertex) + planes[p].w < 0.0f) {
                inFrustum = false;
                break;
            }
        }
        if (!inFrustum) {
            continue;
        }

        // Clip plane culling (e.g. reflection pass: skip chunks entirely on the culled side)
        if (clipPlane.w < 50000.0f) {
            glm::vec3 bestP(
                (clipPlane.x > 0.0f) ? c->maxBounds.x : c->minBounds.x,
                (clipPlane.y > 0.0f) ? c->maxBounds.y : c->minBounds.y,
                (clipPlane.z > 0.0f) ? c->maxBounds.z : c->minBounds.z
            );
            if (glm::dot(glm::vec3(clipPlane), bestP) + clipPlane.w < 0.0f) {
                continue;
            }
        }

        c->render();
        renderedChunks++;
    }

    static int printCounter = 0;
    if (printCounter++ % 120 == 0) {
        std::cout << "[Terrain Culling] Rendered " << renderedChunks << " / " << chunks.size() 
                  << " chunks (maxDist=" << maxDrawDistance << "m)" << std::endl;
    }

    if (clipPlane.w < 50000.0f) {
        glDisable(GL_CLIP_DISTANCE0);
    }
    glUseProgram(0);
}

float TerrainManager::getHeightAt(float x, float z) const {
    // Use physics-authoritative heights (separate from visual smoothing)
    const std::vector<float>* src = &physicsVisualHeights;
    if (src->empty()) src = &visualHeights;
    if (src->empty()) return 0.0f;
    float ux = std::clamp((x - worldMinX) / (worldMaxX - worldMinX), 0.0f, 1.0f);
    float uz = std::clamp((z - worldMinZ) / (worldMaxZ - worldMinZ), 0.0f, 1.0f);
    float fx = ux * (visualW - 1), fz = uz * (visualH - 1);
    int x0 = (int)fx, x1 = std::min(x0+1, visualW-1), z0 = (int)fz, z1 = std::min(z0+1, visualH-1);
    float tx = fx - x0, tz = fz - z0;
    const auto &ph = *src;
    return ph[z0*visualW+x0]*(1-tx)*(1-tz) + ph[z0*visualW+x1]*tx*(1-tz) + ph[z1*visualW+x0]*(1-tx)*tz + ph[z1*visualW+x1]*tx*tz;
}

void TerrainManager::exportToOBJ(const std::string& filename, int downsample) const {
    std::cout << "[TerrainManager] Exporting terrain to OBJ: " << filename << " (downsample=" << downsample << ")" << std::endl;
    std::ofstream out(filename);
    if (!out.is_open()) {
        std::cerr << "[TerrainManager] Failed to open " << filename << " for export." << std::endl;
        return;
    }

    // Grid dimensions for OBJ
    int dw = gridW / downsample;
    int dh = gridH / downsample;

    // Write vertices
    for (int z = 0; z < dh; ++z) {
        for (int x = 0; x < dw; ++x) {
            float ux = (float)x / (dw - 1);
            float uz = (float)z / (dh - 1);
            float worldX = worldMinX + ux * (worldMaxX - worldMinX);
            float worldZ = worldMinZ + uz * (worldMaxZ - worldMinZ);
            float worldY = heights[(z * downsample) * gridW + (x * downsample)];
            out << "v " << worldX << " " << worldY << " " << worldZ << "\n";
        }
    }

    // Write faces (1-based indices)
    for (int z = 0; z < dh - 1; ++z) {
        for (int x = 0; x < dw - 1; ++x) {
            int i0 = z * dw + x + 1;
            int i1 = z * dw + (x + 1) + 1;
            int i2 = (z + 1) * dw + (x + 1) + 1;
            int i3 = (z + 1) * dw + x + 1;
            out << "f " << i0 << " " << i1 << " " << i2 << " " << i3 << "\n";
        }
    }

    out.close();
    std::cout << "[TerrainManager] Export Complete." << std::endl;
}

glm::vec3 TerrainManager::getNormalAt(float x, float z) const {
    float e = 1.0f; float hL = getHeightAt(x-e, z), hR = getHeightAt(x+e, z), hS = getHeightAt(x, z-e), hN = getHeightAt(x, z+e);
    return glm::normalize(glm::vec3(hL - hR, 2.0f * e, hS - hN));
}

float TerrainManager::getRoadAt(float x, float z) const {
    if (roadMaskData.empty()) return 0.0f;
    float ux = std::clamp((x - worldMinX) / (worldMaxX - worldMinX), 0.0f, 1.0f);
    float uz = std::clamp((z - worldMinZ) / (worldMaxZ - worldMinZ), 0.0f, 1.0f);
    float fx = ux * (roadW - 1), fz = uz * (roadH - 1);
    int x0 = (int)fx, x1 = std::min(x0+1, roadW-1), z0 = (int)fz, z1 = std::min(z0+1, roadH-1);
    float tx = fx - x0, tz = fz - z0;
    float r00 = roadMaskData[(z0*roadW+x0)*4+0]/255.0f, r10 = roadMaskData[(z0*roadW+x1)*4+0]/255.0f;
    float r01 = roadMaskData[(z1*roadW+x0)*4+0]/255.0f, r11 = roadMaskData[(z1*roadW+x1)*4+0]/255.0f;
    return r00*(1-tx)*(1-tz) + r10*tx*(1-tz) + r01*(1-tx)*tz + r11*tx*tz;
}

float TerrainManager::getRiverAt(float x, float z) const {
    if (roadMaskData.empty()) return 0.0f;
    float ux = std::clamp((x - worldMinX) / (worldMaxX - worldMinX), 0.0f, 1.0f);
    float uz = std::clamp((z - worldMinZ) / (worldMaxZ - worldMinZ), 0.0f, 1.0f);
    float fx = ux * (roadW - 1), fz = uz * (roadH - 1);
    int x0 = (int)fx, x1 = std::min(x0+1, roadW-1), z0 = (int)fz, z1 = std::min(z0+1, roadH-1);
    float tx = fx - x0, tz = fz - z0;
    float r00 = roadMaskData[(z0*roadW+x0)*4+2]/255.0f, r10 = roadMaskData[(z0*roadW+x1)*4+2]/255.0f;
    float r01 = roadMaskData[(z1*roadW+x0)*4+2]/255.0f, r11 = roadMaskData[(z1*roadW+x1)*4+2]/255.0f;
    return r00*(1-tx)*(1-tz) + r10*tx*(1-tz) + r01*(1-tx)*tz + r11*tx*tz;
}

void TerrainManager::conformTerrainToRoadMesh(float wx, float wz, float radius, float speed,
                                               float dt,
                                               const std::vector<RoadVertex>& roadVertices,
                                               const std::vector<uint32_t>& roadIndices,
                                               bool wholeNetwork,
                                               float bermWidth) {
    if (visualW <= 1 || visualH <= 1 || visualHeights.empty() ||
        (!wholeNetwork && radius <= 0.0f) ||
        roadVertices.empty() || roadIndices.size() < 3) return;

    const float worldWidth = worldMaxX - worldMinX;
    const float worldDepth = worldMaxZ - worldMinZ;
    if (worldWidth <= 0.0f || worldDepth <= 0.0f) return;
    const float pixelX = worldWidth / (visualW - 1);
    const float pixelZ = worldDepth / (visualH - 1);
    // A terrain triangle touching the asphalt can have its far vertex a full
    // grid-cell diagonal from the road edge. Include that vertex so the terrain
    // triangle cannot rise through the road between sampled heightfield points.
    const float cellReach = std::sqrt(pixelX * pixelX + pixelZ * pixelZ) + 0.02f;
    const float minWorldX = wholeNetwork ? worldMinX : wx - radius - cellReach;
    const float maxWorldX = wholeNetwork ? worldMaxX : wx + radius + cellReach;
    const float minWorldZ = wholeNetwork ? worldMinZ : wz - radius - cellReach;
    const float maxWorldZ = wholeNetwork ? worldMaxZ : wz + radius + cellReach;
    const float brushBlend = wholeNetwork ? 1.0f : glm::clamp(speed * 2.0f * dt, 0.0f, 1.0f);
    if (brushBlend <= 0.0f) return;

    struct RoadTriangle {
        const RoadVertex* a;
        const RoadVertex* b;
        const RoadVertex* c;
        float minX, maxX, minZ, maxZ;
    };
    std::vector<RoadTriangle> nearbyTriangles;
    nearbyTriangles.reserve(roadIndices.size() / 12);
    for (size_t i = 0; i + 2 < roadIndices.size(); i += 3) {
        const uint32_t ia = roadIndices[i], ib = roadIndices[i + 1], ic = roadIndices[i + 2];
        if (ia >= roadVertices.size() || ib >= roadVertices.size() || ic >= roadVertices.size()) continue;
        const RoadVertex& a = roadVertices[ia];
        const RoadVertex& b = roadVertices[ib];
        const RoadVertex& c = roadVertices[ic];
        const float triMinX = std::min({a.position.x, b.position.x, c.position.x});
        const float triMaxX = std::max({a.position.x, b.position.x, c.position.x});
        const float triMinZ = std::min({a.position.z, b.position.z, c.position.z});
        const float triMaxZ = std::max({a.position.z, b.position.z, c.position.z});
        if (!wholeNetwork &&
            (triMaxX + cellReach < minWorldX || triMinX - cellReach > maxWorldX ||
             triMaxZ + cellReach < minWorldZ || triMinZ - cellReach > maxWorldZ)) continue;
        nearbyTriangles.push_back({&a, &b, &c, triMinX, triMaxX, triMinZ, triMaxZ});
    }
    if (nearbyTriangles.empty()) return;

    auto closestPointOnSegment = [](const glm::vec2& p, const glm::vec2& a,
                                    const glm::vec2& b, float& t) {
        const glm::vec2 d = b - a;
        const float d2 = glm::dot(d, d);
        t = d2 > 1e-10f ? glm::clamp(glm::dot(p - a, d) / d2, 0.0f, 1.0f) : 0.0f;
        return a + d * t;
    };
    // Distance to the ribbon AND the ribbon's own height there. Both are needed:
    // the road network can stack several surfaces over one XZ (a spliced extension,
    // a junction pad, a second branch), and the one that matters is the nearest,
    // not whichever triangle the index buffer happens to list first. Returning on
    // the first containing triangle graded the ground to whatever surface came
    // first -- measured 8.26 m of terrain left standing above a road whose real
    // surface was 9 m lower at the same point.
    auto sampleTriangle = [&](const RoadTriangle& tri, const glm::vec2& p,
                              float& distanceSquared, float& surfaceY) {
        const glm::vec2 a(tri.a->position.x, tri.a->position.z);
        const glm::vec2 b(tri.b->position.x, tri.b->position.z);
        const glm::vec2 c(tri.c->position.x, tri.c->position.z);
        const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (std::fabs(area) < 1e-8f) return;
        const float wb = ((p.x - a.x) * (c.y - a.y) - (p.y - a.y) * (c.x - a.x)) / area;
        const float wc = ((b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x)) / area;
        const float wa = 1.0f - wb - wc;
        if (wa >= 0.0f && wb >= 0.0f && wc >= 0.0f) {
            distanceSquared = 0.0f;
            surfaceY = wa * tri.a->position.y + wb * tri.b->position.y + wc * tri.c->position.y;
            return;
        }
        // Outside the triangle: the nearest point on its boundary gives the
        // distance, and the interpolated height at that point is the surface to
        // grade to.
        float t = 0.0f;
        float bestD2 = std::numeric_limits<float>::infinity();
        float bestY = 0.0f;
        const glm::vec2 abPoint = closestPointOnSegment(p, a, b, t);
        bestD2 = glm::dot(p - abPoint, p - abPoint);
        bestY = glm::mix(tri.a->position.y, tri.b->position.y, t);
        const glm::vec2 bcPoint = closestPointOnSegment(p, b, c, t);
        const float bcD2 = glm::dot(p - bcPoint, p - bcPoint);
        if (bcD2 < bestD2) { bestD2 = bcD2; bestY = glm::mix(tri.b->position.y, tri.c->position.y, t); }
        const glm::vec2 caPoint = closestPointOnSegment(p, c, a, t);
        const float caD2 = glm::dot(p - caPoint, p - caPoint);
        if (caD2 < bestD2) { bestD2 = caD2; bestY = glm::mix(tri.c->position.y, tri.a->position.y, t); }
        distanceSquared = bestD2;
        surfaceY = bestY;
    };

    // Leave the terrain 1 cm below the final road ribbon. The ribbon receives a
    // matching 1 cm vertical bias after it is snapped to the conformed surface;
    // polygon offset in the road pass handles any remaining depth-buffer ties.
    constexpr float kTerrainBelowRoad = 0.06f;
    // Only the strip directly under the ribbon is touched. The old pass wrote every
    // vertex within a full cell diagonal (31.5 m on a 22 m grid) at full strength with
    // no falloff, which flattened a 63 m band around a 22 m road and read as the whole
    // terrain collapsing to the lowest road point. The road ribbon is now sampled from
    // the same heightfield, so broad grading is unnecessary: this pass only cleans up
    // the seam, and the fade keeps it from becoming a plateau.
    // Two widths, not one. The corridor is held fully at road level so high ground
    // beside the asphalt actually gets cut down to touch it; the berm is the graded
    // slope that then eases the corridor back out to natural ground, so the cut is
    // visible on the terrain.
    //
    // The corridor has to span a full cell diagonal, not a nominal shoulder width. Any
    // grid triangle leaning over the ribbon has all three of its vertices within about
    // that distance, so a narrower corridor leaves those vertices only partially pulled
    // and the triangle still juts through the asphalt. Measured with an 8 m corridor:
    // 10.7% of probes had terrain more than 0.25 m proud of the road, worst 2.40 m.
    //
    // cellReach alone is not enough. A terrain cell is ~22 m across on the shipped DEM,
    // so a triangle whose vertices sit inside a one-cell corridor still spans the road
    // and rises over it: the measured poke extended past the old corridor and reached
    // 8.26 m at 3215 road vertices. The corridor is therefore widened by kCorridorReach
    // metres of ribbon distance, in which the ground is held at road level, and the
    // remaining slope out to natural ground is the graded berm beyond it.
    float kCorridorReach = 16.0f;
    float kRoadBermWidth = std::max(15.0f, bermWidth);
    if (const char* env = std::getenv("FALLAGA_CONFORM_REACH")) {
        const float requested = std::strtof(env, nullptr);
        if (requested > 0.0f) kCorridorReach = requested;
    }
    if (const char* env = std::getenv("FALLAGA_CONFORM_BERM")) {
        const float requested = std::strtof(env, nullptr);
        if (requested > 0.0f) kRoadBermWidth = requested;
    }
    const float kRoadCorridor = cellReach + kCorridorReach;
    float conformSkirt = kRoadCorridor + kRoadBermWidth;
    if (const char* env = std::getenv("FALLAGA_CONFORM_SKIRT")) {
        const float requested = std::strtof(env, nullptr);
        if (requested > 0.0f) conformSkirt = requested;
    }
    const float conformSkirtSquared = conformSkirt * conformSkirt;
    const float cellReachSquared = cellReach * cellReach;

    // ── Dominant-surface raster ───────────────────────────────────────────────
    // The road network stacks surfaces over one XZ: a junction pad over a ribbon, a
    // spliced extension, a second branch. The ground has to end up below the surface
    // the player actually drives on, and that surface is identified by the same
    // `surfacePriority` the road shader uses to decide which fragment wins.
    //
    // Rasterising that decision once -- per coarse cell keep the highest priority
    // seen together with the lowest height among triangles at that priority --
    // replaces a per-cell nearest-triangle search. That search was O(grid cells x
    // triangles) and took minutes on the shipped network (175k triangles over a
    // 1254x1254 visual grid), which is far too slow to run on every edit.
    const int surfRes = 640;
    const float surfMinX = wholeNetwork ? worldMinX : minWorldX;
    const float surfMinZ = wholeNetwork ? worldMinZ : minWorldZ;
    const float surfMaxX = wholeNetwork ? worldMaxX : maxWorldX;
    const float surfMaxZ = wholeNetwork ? worldMaxZ : maxWorldZ;
    const float surfCellX = std::max(1e-3f, (surfMaxX - surfMinX) / (float)surfRes);
    const float surfCellZ = std::max(1e-3f, (surfMaxZ - surfMinZ) / (float)surfRes);
    constexpr float kNoPriority = -1e30f;
    std::vector<float> surfPriority((size_t)surfRes * surfRes, kNoPriority);
    // Cell centre -> the index of the ribbon piece nearest that centre. Storing the
    // PIECE, not just a height, is what makes the grade accurate: a cell is ~43 m
    // across here while the road inside it can descend several metres, so a single
    // aggregate height for the whole cell would grade the ground against road that
    // is nowhere near the vertex -- which is how terrain was left standing over the
    // asphalt after the first raster version.
    std::vector<int32_t> surfTri((size_t)surfRes * surfRes, -1);
    std::vector<float> surfTriDist((size_t)surfRes * surfRes,
                                   std::numeric_limits<float>::infinity());
    for (uint32_t ti = 0; ti < (uint32_t)nearbyTriangles.size(); ++ti) {
        const RoadTriangle& tri = nearbyTriangles[ti];
        const float triPrio = std::max(tri.a->surfacePriority,
                                       std::max(tri.b->surfacePriority, tri.c->surfacePriority));
        const float cxm = (tri.minX + tri.maxX) * 0.5f;
        const float czm = (tri.minZ + tri.maxZ) * 0.5f;
        const int cx0 = std::clamp((int)((tri.minX - surfMinX) / surfCellX), 0, surfRes - 1);
        const int cx1 = std::clamp((int)((tri.maxX - surfMinX) / surfCellX), 0, surfRes - 1);
        const int cz0 = std::clamp((int)((tri.minZ - surfMinZ) / surfCellZ), 0, surfRes - 1);
        const int cz1 = std::clamp((int)((tri.maxZ - surfMinZ) / surfCellZ), 0, surfRes - 1);
        for (int cz = cz0; cz <= cz1; ++cz) {
            const float centreZ = surfMinZ + ((float)cz + 0.5f) * surfCellZ;
            for (int cx = cx0; cx <= cx1; ++cx) {
                const float centreX = surfMinX + ((float)cx + 0.5f) * surfCellX;
                const float d = glm::length(glm::vec2(centreX - cxm, centreZ - czm));
                const size_t slot = (size_t)cz * surfRes + cx;
                if (triPrio > surfPriority[slot]) {
                    // A strictly more dominant surface takes the cell.
                    surfPriority[slot] = triPrio;
                    surfTri[slot] = (int32_t)ti;
                    surfTriDist[slot] = d;
                } else if (triPrio == surfPriority[slot]) {
                    // Same surface: keep the piece closest to this cell's centre, so
                    // the graded height is the road that is actually overhead.
                    if (d < surfTriDist[slot]) {
                        surfTri[slot] = (int32_t)ti;
                        surfTriDist[slot] = d;
                    }
                }
            }
        }
    }
    // Road height on a ribbon triangle at an arbitrary XZ, by barycentric
    // interpolation, falling back to the nearest vertex/edge outside it.
    auto roadHeightOnTri = [&](const RoadTriangle& tri, float worldX, float worldZ) -> float {
        const glm::vec2 a(tri.a->position.x, tri.a->position.z);
        const glm::vec2 b(tri.b->position.x, tri.b->position.z);
        const glm::vec2 c(tri.c->position.x, tri.c->position.z);
        const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (std::fabs(area) < 1e-8f)
            return std::min(tri.a->position.y, std::min(tri.b->position.y, tri.c->position.y));
        const float wb = ((worldX - a.x) * (c.y - a.y) - (worldZ - a.y) * (c.x - a.x)) / area;
        const float wc = ((b.x - a.x) * (worldZ - a.y) - (b.y - a.y) * (worldX - a.x)) / area;
        const float wa = 1.0f - wb - wc;
        return wa * tri.a->position.y + wb * tri.b->position.y + wc * tri.c->position.y;
    };
    auto gradeTargetNear = [&](float worldX, float worldZ, float& outSurfaceY) -> bool {
        if (worldX < surfMinX - surfCellX || worldX > surfMaxX + surfCellX ||
            worldZ < surfMinZ - surfCellZ || worldZ > surfMaxZ + surfCellZ) return false;
        const int gx = std::clamp((int)((worldX - surfMinX) / surfCellX), 0, surfRes - 1);
        const int gz = std::clamp((int)((worldZ - surfMinZ) / surfCellZ), 0, surfRes - 1);
        // Only this vertex's own cell. Widening this to a 3x3 neighbourhood (tried)
        // does cut the terrain-over-road poke further, but it also drags a ~90 m band
        // of ground down to road level and the road then reads as raised: measured
        // meanGap 0.20 m -> 0.71 m and worst 9.8 m -> 20.6 m. That is the same
        // trade-off, just moved, so the narrow lookup is kept.
        const int32_t ti = surfTri[(size_t)gz * surfRes + gx];
        if (ti < 0) return false; // no ribbon here
        outSurfaceY = roadHeightOnTri(nearbyTriangles[(size_t)ti], worldX, worldZ);
        return true;
    };

    int changedMinX = visualW, changedMaxX = -1, changedMinZ = visualH, changedMaxZ = -1;
    // Grade every visual-grid vertex whose coarse cell carries a road surface. The
    // cost is bounded by the visual grid, not by the triangle count.
    for (int z = 0; z < visualH; ++z) {
        const float worldZ = worldMinZ + (float)z / (visualH - 1) * worldDepth;
        for (int x = 0; x < visualW; ++x) {
            const float worldX = worldMinX + (float)x / (visualW - 1) * worldWidth;
            if (!wholeNetwork && glm::length(glm::vec2(worldX - wx, worldZ - wz)) > radius) continue;
            const size_t idx = (size_t)z * visualW + x;
            const float oldHeight = visualHeights[idx];

            // Pass 1: soft grade of the corridor, with the berm falloff.
            float nextHeight = oldHeight;
            float surfaceY = 0.0f;
            if (gradeTargetNear(worldX, worldZ, surfaceY)) {
                // Distance from this vertex to the ribbon piece that governs its cell.
                // Using the true ribbon distance (not the distance to the cell centre)
                // keeps the corridor/berm falloff meaningful at ~43 m cells.
                const int dgx = std::clamp((int)((worldX - surfMinX) / surfCellX), 0, surfRes - 1);
                const int dgz = std::clamp((int)((worldZ - surfMinZ) / surfCellZ), 0, surfRes - 1);
                const float ribbonDist =
                    glm::distance(glm::vec2(worldX, worldZ),
                                  glm::vec2(surfMinX + ((float)dgx + 0.5f) * surfCellX,
                                            surfMinZ + ((float)dgz + 0.5f) * surfCellZ)) +
                    surfTriDist[(size_t)dgz * surfRes + dgx];
                const float corridorT = kRoadBermWidth > 1e-4f
                    ? glm::clamp((ribbonDist - kRoadCorridor) / kRoadBermWidth, 0.0f, 1.0f)
                    : (ribbonDist <= cellReach ? 0.0f : 1.0f);
                float falloff = 1.0f - corridorT * corridorT * (3.0f - 2.0f * corridorT);
                if (!wholeNetwork) {
                    const float brushDistance = glm::length(glm::vec2(worldX - wx, worldZ - wz));
                    const float t = glm::clamp(brushDistance / radius, 0.0f, 1.0f);
                    falloff *= 1.0f - t * t * (3.0f - 2.0f * t);
                }
                if (falloff > 0.0f) {
                    const float targetHeight = surfaceY - kTerrainBelowRoad;
                    nextHeight = glm::mix(oldHeight, targetHeight, brushBlend * falloff);
                }
            }

            // Pass 2: hard cap. The soft grade leaves the ground only partially pulled
            // down near the corridor edge, so a terrain triangle spanning from there
            // across a road can still crest over the asphalt. Any ground that would
            // stand above the road surface over its own cell is lowered to just under
            // it, at full strength. This only ever lowers ground, so unlike widening
            // the soft grade it cannot flatten the corridor.
            //
            // The cap deliberately uses the same cell as the grade, NOT a wider
            // search. Widening it was measured several ways (3x3 cells, 5x5 cells,
            // 5x5 with a distance filter) and every variant is worse overall: the cap
            // reaches lower road further away and drags the ground down to THAT,
            // which raises the road's apparent height instead of seating it.
            //   5x5 no filter : meanGap 0.20 -> 1.07 m, poke 1.27 -> 4.64 m
            //   5x5 filtered  : meanGap 0.20 -> 0.43 m, poke 1.27 -> 1.47 m
            //   own cell only : meanGap +0.20 m,       poke 1.27 m   <- kept
            float capSurfaceY = 0.0f;
            if (gradeTargetNear(worldX, worldZ, capSurfaceY)) {
                const float cap = capSurfaceY - (kTerrainBelowRoad * 0.5f);
                if (nextHeight > cap) nextHeight = cap;
            }

            nextHeight = glm::clamp(nextHeight, minHeight, maxHeight);
            const float appliedDelta = nextHeight - oldHeight;
            if (std::fabs(appliedDelta) < 0.001f) continue;
            if (appliedRoadConformOffsets.size() != visualHeights.size())
                appliedRoadConformOffsets.assign(visualHeights.size(), 0.0f);
            visualHeights[idx] = nextHeight;
            if ((size_t)idx < physicsVisualHeights.size())
                physicsVisualHeights[idx] = nextHeight;
            appliedRoadConformOffsets[idx] += appliedDelta;
            changedMinX = std::min(changedMinX, x); changedMaxX = std::max(changedMaxX, x);
            changedMinZ = std::min(changedMinZ, z); changedMaxZ = std::max(changedMaxZ, z);
        }
    }

    if (changedMaxX < changedMinX || changedMaxZ < changedMinZ) return;
    logHeightfieldSignature(wholeNetwork ? "conform/whole" : "conform/brush");
    if (wholeNetwork && std::getenv("FALLAGA_ROADGAP")) {
        const long long cells = (long long)(changedMaxX - changedMinX + 1) *
                                (changedMaxZ - changedMinZ + 1);
        // How many cells in the graded box actually carry a road surface, and how
        // far the outermost one is from a ribbon. The raster is only painted where a
        // triangle exists, so this shows whether the conform reaches the whole box.
        int gradedCells = 0;
        float probe = 0.0f;
        for (int z = std::max(0, changedMinZ); z <= std::min(visualH - 1, changedMaxZ); ++z) {
            for (int x = std::max(0, changedMinX); x <= std::min(visualW - 1, changedMaxX); ++x) {
                const float gx = worldMinX + (float)x / (visualW - 1) * worldWidth;
                const float gz = worldMinZ + (float)z / (visualH - 1) * worldDepth;
                if (gradeTargetNear(gx, gz, probe)) ++gradedCells;
            }
        }
        std::cout << "[Conform] skirt=" << conformSkirt << "m corridor=" << kRoadCorridor
                  << "m berm=" << kRoadBermWidth << "m cellReach=" << cellReach
                  << "m cells=" << cells
                  << " of " << (long long)visualW * visualH
                  << " box=" << (changedMaxX - changedMinX + 1) << "x"
                  << (changedMaxZ - changedMinZ + 1)
                  << " gradedCells=" << gradedCells << std::endl;
    }

    // Keep native-resolution height queries and collision aligned with the mesh.
    if (gridW > 1 && gridH > 1) {
        const float scaleX = (float)(visualW - 1) / (gridW - 1);
        const float scaleZ = (float)(visualH - 1) / (gridH - 1);
        const int nativeMinX = std::max(0, (int)std::floor(changedMinX / scaleX));
        const int nativeMaxX = std::min(gridW - 1, (int)std::ceil(changedMaxX / scaleX));
        const int nativeMinZ = std::max(0, (int)std::floor(changedMinZ / scaleZ));
        const int nativeMaxZ = std::min(gridH - 1, (int)std::ceil(changedMaxZ / scaleZ));
        for (int z = nativeMinZ; z <= nativeMaxZ; ++z) {
            const float srcZ = z * scaleZ;
            const int sz0 = (int)srcZ, sz1 = std::min(sz0 + 1, visualH - 1);
            const float tz = srcZ - sz0;
            for (int x = nativeMinX; x <= nativeMaxX; ++x) {
                const float srcX = x * scaleX;
                const int sx0 = (int)srcX, sx1 = std::min(sx0 + 1, visualW - 1);
                const float tx = srcX - sx0;
                heights[z * gridW + x] =
                    visualHeights[sz0 * visualW + sx0] * (1.0f - tx) * (1.0f - tz) +
                    visualHeights[sz0 * visualW + sx1] * tx * (1.0f - tz) +
                    visualHeights[sz1 * visualW + sx0] * (1.0f - tx) * tz +
                    visualHeights[sz1 * visualW + sx1] * tx * tz;
            }
        }
    }
    if (changedMaxX < changedMinX || changedMaxZ < changedMinZ) return;
    logHeightfieldSignature(wholeNetwork ? "conform/whole" : "conform/brush");
    // Upload only the texels this pass could have touched. changedMin/Max are in
    // visual-grid pixels, so scale them into the native grid the texture uses.
    {
        const float sx = (visualW > 1 && gridW > 1)
                             ? (float)(gridW - 1) / (float)(visualW - 1) : 1.0f;
        const float sz = (visualH > 1 && gridH > 1)
                             ? (float)(gridH - 1) / (float)(visualH - 1) : 1.0f;
        updateGPUHeightmapTextureRect(
            (int)std::floor(changedMinX * sx) - 1, (int)std::floor(changedMinZ * sz) - 1,
            (int)std::ceil (changedMaxX * sx) + 1, (int)std::ceil (changedMaxZ * sz) + 1);
    }
    // Widen the visual box by one cell for the chunk-overlap test below. The
    // texture upload above already adds its own margin, so this only affects
    // which chunk meshes are considered dirty.
    changedMinX = std::max(0, changedMinX - 1); changedMinZ = std::max(0, changedMinZ - 1);
    changedMaxX = std::min(visualW - 1, changedMaxX + 1); changedMaxZ = std::min(visualH - 1, changedMaxZ + 1);
    for (TerrainChunk* chunk : chunks) {
        const int cMinX = chunk->gridX * (chunk->size - 1);
        const int cMaxX = cMinX + chunk->size - 1;
        const int cMinZ = chunk->gridZ * (chunk->size - 1);
        const int cMaxZ = cMinZ + chunk->size - 1;
        if (changedMaxX < cMinX || changedMinX > cMaxX ||
            changedMaxZ < cMinZ || changedMinZ > cMaxZ) continue;
        chunk->generateMesh(visualHeights, visualW, visualH, worldMinX, worldMaxX,
                            worldMinZ, worldMaxZ,
                            hasRoadMask ? &roadMaskData : nullptr, roadW, roadH);
    }
}

void TerrainManager::sculpt(float wx, float wz, float radius, float speed, int mode, float dt,
                            float directionDegrees, float gradeDegrees) {
    // Mode: 1=Raise, 2=Lower, 3=Smooth, 4=Erode, 5=Angle Slope
    if (visualW <= 1 || visualH <= 1 || radius <= 0.0f) return;
    float ux = std::clamp((wx - worldMinX) / (worldMaxX - worldMinX), 0.0f, 1.0f);
    float uz = std::clamp((wz - worldMinZ) / (worldMaxZ - worldMinZ), 0.0f, 1.0f);

    float gridCenterX = ux * (visualW - 1);
    float gridCenterZ = uz * (visualH - 1);
    float gridRadiusX = (radius / (worldMaxX - worldMinX)) * (visualW - 1);
    float gridRadiusZ = (radius / (worldMaxZ - worldMinZ)) * (visualH - 1);
    float gridRadius = std::max(gridRadiusX, gridRadiusZ);

    int minX = std::max(0, (int)(gridCenterX - gridRadius));
    int maxX = std::min(visualW - 1, (int)(gridCenterX + gridRadius));
    int minZ = std::max(0, (int)(gridCenterZ - gridRadius));
    int maxZ = std::min(visualH - 1, (int)(gridCenterZ + gridRadius));

    if (mode == 1 || mode == 2) {
        float dir = (mode == 1) ? 1.0f : -1.0f;
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                float dx = (float)x - gridCenterX;
                float dz = (float)z - gridCenterZ;
                float dist = sqrt(dx*dx + dz*dz);
                if (dist <= gridRadius) {
                    float t = 1.0f - (dist / gridRadius);
                    float falloff = t * t * (3.0f - 2.0f * t); // Smoothstep curve (Hermite cubic)
                    visualHeights[z * visualW + x] = std::clamp(visualHeights[z * visualW + x] + dir * speed * 10.0f * falloff * dt, minHeight, maxHeight);
                }
            }
        }
    } else if (mode == 3) { // Smooth
        std::vector<float> smoothed = visualHeights;
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                float dx = (float)x - gridCenterX;
                float dz = (float)z - gridCenterZ;
                float dist = sqrt(dx*dx + dz*dz);
                if (dist <= gridRadius) {
                    float sum = 0.0f;
                    int count = 0;
                    for (int sz = -1; sz <= 1; ++sz) {
                        for (int sx = -1; sx <= 1; ++sx) {
                            int nx = x + sx;
                            int nz = z + sz;
                            if (nx >= 0 && nx < visualW && nz >= 0 && nz < visualH) {
                                sum += visualHeights[nz * visualW + nx];
                                count++;
                            }
                        }
                    }
                    float avg = sum / count;
                    float t = 1.0f - (dist / gridRadius);
                    float falloff = t * t * (3.0f - 2.0f * t); // Smoothstep curve
                    smoothed[z * visualW + x] = std::clamp(glm::mix(visualHeights[z * visualW + x], avg, speed * 2.0f * falloff * dt), minHeight, maxHeight);
                }
            }
        }
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                float dx = (float)x - gridCenterX;
                float dz = (float)z - gridCenterZ;
                float dist = sqrt(dx*dx + dz*dz);
                if (dist <= gridRadius) {
                    visualHeights[z * visualW + x] = smoothed[z * visualW + x];
                }
            }
        }
        // The sync block below mirrors this into physicsVisualHeights (and from
        // there into the native `heights` grid), so the smooth brush can no longer
        // reshape the rendered surface while collision keeps the old height.
    } else if (mode == 4) { // Erode brush
        int numDroplets = (int)(radius * speed * dt * 30.0f);
        if (numDroplets < 1) numDroplets = 1;
        erode(numDroplets, wx, wz, radius, true);
    } else if (mode == 5) { // Fit a smooth, oriented grade through the brush centre.
        const int centerX = std::clamp((int)std::lround(gridCenterX), 0, visualW - 1);
        const int centerZ = std::clamp((int)std::lround(gridCenterZ), 0, visualH - 1);
        const float anchorHeight = visualHeights[centerZ * visualW + centerX];
        const float direction = glm::radians(std::fmod(std::max(directionDegrees, 0.0f), 360.0f));
        const glm::vec2 slopeDirection(std::cos(direction), std::sin(direction));
        const float grade = std::tan(glm::radians(glm::clamp(gradeDegrees, 0.0f, 60.0f)));
        const float worldPerPixelX = (worldMaxX - worldMinX) / (visualW - 1);
        const float worldPerPixelZ = (worldMaxZ - worldMinZ) / (visualH - 1);
        const float blendPerSecond = glm::clamp(speed * 2.0f, 0.0f, 100.0f);
        const float worldRadius = std::max(radius, 0.001f);
        const float coreRadius = worldRadius * 0.80f;
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                const float dx = (float)x - gridCenterX;
                const float dz = (float)z - gridCenterZ;
                const float worldDx = dx * worldPerPixelX;
                const float worldDz = dz * worldPerPixelZ;
                const float distance = std::sqrt(worldDx * worldDx + worldDz * worldDz);
                if (distance > worldRadius) continue;
                const float alongSlope = worldDx * slopeDirection.x + worldDz * slopeDirection.y;
                const float planeHeight = anchorHeight + grade * alongSlope;
                // Keep the requested angle exact across most of the brush, then
                // feather only its outer rim so it joins the untouched terrain cleanly.
                const float edgeT = glm::clamp((distance - coreRadius) / (worldRadius - coreRadius), 0.0f, 1.0f);
                const float edgeBlend = edgeT * edgeT * (3.0f - 2.0f * edgeT);
                const int idx = z * visualW + x;
                const float target = glm::mix(planeHeight, visualHeights[idx], edgeBlend);
                const float blend = glm::clamp(blendPerSecond * dt, 0.0f, 1.0f);
                visualHeights[idx] = std::clamp(glm::mix(visualHeights[idx], target, blend), minHeight, maxHeight);
            }
        }
    }

    // Sync edited heights back to physics/native heights. Every brush that is
    // visible must also move the collision surface, otherwise the mesh and
    // getHeightAt() disagree and characters/cars float or sink by the full brush
    // delta. This used to be conditional on `visualSmoothingAffectsPhysics`.
    {
        // Mirror visual edits to physics heights
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                float dx = (float)x - gridCenterX;
                float dz = (float)z - gridCenterZ;
                float dist = sqrt(dx*dx + dz*dz);
                if (dist <= gridRadius) {
                    size_t idx = (size_t)z * visualW + x;
                    if (idx < physicsVisualHeights.size())
                        physicsVisualHeights[idx] = visualHeights[idx];
                }
            }
        }
        if (gridW > 0 && gridH > 0 && !heights.empty()) {
            float scaleX = (float)(visualW - 1) / (gridW - 1);
            float scaleZ = (float)(visualH - 1) / (gridH - 1);

            int physMinX = std::max(0, (int)(minX / scaleX));
            int physMaxX = std::min(gridW - 1, (int)(maxX / scaleX) + 1);
            int physMinZ = std::max(0, (int)(minZ / scaleZ));
            int physMaxZ = std::min(gridH - 1, (int)(maxZ / scaleZ) + 1);

            for (int z = physMinZ; z <= physMaxZ; ++z) {
                float srcZ = z * scaleZ;
                int z0 = (int)srcZ;
                int z1 = std::min(z0 + 1, visualH - 1);
                float tz = srcZ - z0;
                for (int x = physMinX; x <= physMaxX; ++x) {
                    float srcX = x * scaleX;
                    int x0 = (int)srcX;
                    int x1 = std::min(x0 + 1, visualW - 1);
                    float tx = srcX - x0;

                    heights[z * gridW + x] =
                        physicsVisualHeights[z0 * visualW + x0] * (1.0f - tx) * (1.0f - tz) +
                        physicsVisualHeights[z0 * visualW + x1] * tx * (1.0f - tz) +
                        physicsVisualHeights[z1 * visualW + x0] * (1.0f - tx) * tz +
                        physicsVisualHeights[z1 * visualW + x1] * tx * tz;
                }
            }
        }
    }

    // Update GPU heightmap texture in real-time. The sync block above only wrote
    // native pixels inside the scaled brush box, so upload just those.
    {
        const float sx = (visualW > 1 && gridW > 1)
                             ? (float)(gridW - 1) / (float)(visualW - 1) : 1.0f;
        const float sz = (visualH > 1 && gridH > 1)
                             ? (float)(gridH - 1) / (float)(visualH - 1) : 1.0f;
        updateGPUHeightmapTextureRect(
            (int)std::floor(minX * sx) - 1, (int)std::floor(minZ * sz) - 1,
            (int)std::ceil (maxX * sx) + 1, (int)std::ceil (maxZ * sz) + 1);
    }

    // Rebuild affected chunks
    for (auto c : chunks) {
        int cMinX = c->gridX * (c->size - 1);
        int cMaxX = cMinX + c->size;
        int cMinZ = c->gridZ * (c->size - 1);
        int cMaxZ = cMinZ + c->size;

        if (maxX >= cMinX && minX <= cMaxX && maxZ >= cMinZ && minZ <= cMaxZ) {
            c->generateMesh(visualHeights, visualW, visualH, worldMinX, worldMaxX, worldMinZ, worldMaxZ, 
                            hasRoadMask ? &roadMaskData : nullptr, roadW, roadH);
        }
    }
}

void TerrainManager::paint(float wx, float wz, float radius, float speed, int layerIdx, float dt) {
    if (roadMaskData.empty()) return;
    if (layerIdx >= 0 && layerIdx < 10) {
        activePaintLayerIdx = layerIdx;
    }

    float ux = std::clamp((wx - worldMinX) / (worldMaxX - worldMinX), 0.0f, 1.0f);
    float uz = std::clamp((wz - worldMinZ) / (worldMaxZ - worldMinZ), 0.0f, 1.0f);

    float gridCenterX = ux * (roadW - 1);
    float gridCenterZ = uz * (roadH - 1);
    float gridRadiusX = (radius / (worldMaxX - worldMinX)) * (roadW - 1);
    float gridRadiusZ = (radius / (worldMaxZ - worldMinZ)) * (roadH - 1);
    float gridRadius = std::max(gridRadiusX, gridRadiusZ);

    int minX = std::max(0, (int)(gridCenterX - gridRadius));
    int maxX = std::min(roadW - 1, (int)(gridCenterX + gridRadius));
    int minZ = std::max(0, (int)(gridCenterZ - gridRadius));
    int maxZ = std::min(roadH - 1, (int)(gridCenterZ + gridRadius));

    for (int z = minZ; z <= maxZ; ++z) {
        for (int x = minX; x <= maxX; ++x) {
            float dx = (float)x - gridCenterX;
            float dz = (float)z - gridCenterZ;
            float dist = sqrt(dx*dx + dz*dz);
            if (dist <= gridRadius) {
                float falloff = 1.0f - (dist / gridRadius);
                int idx = (z * roadW + x) * 4;
                float change = speed * 255.0f * falloff * dt;
                
                if (layerIdx == 12) { // Erase All Painted Mask Weights
                    roadMaskData[idx + 0] = (unsigned char)std::max(0.0f, roadMaskData[idx + 0] - change);
                    roadMaskData[idx + 1] = (unsigned char)std::max(0.0f, roadMaskData[idx + 1] - change);
                    roadMaskData[idx + 2] = (unsigned char)std::max(0.0f, roadMaskData[idx + 2] - change);
                    roadMaskData[idx + 3] = (unsigned char)std::max(0.0f, roadMaskData[idx + 3] - change);
                } else if (layerIdx == 3 || layerIdx == 11) { // Paint Road / Muddy Road -> Red Channel
                    roadMaskData[idx + 0] = (unsigned char)std::min(255.0f, roadMaskData[idx + 0] + change);
                } else if (layerIdx == 6) { // Paint Riverbed Gravel -> Green Channel
                    roadMaskData[idx + 1] = (unsigned char)std::min(255.0f, roadMaskData[idx + 1] + change);
                } else if (layerIdx == 5 || layerIdx == 10) { // Paint Watery Mud / Silt -> Blue Channel
                    roadMaskData[idx + 2] = (unsigned char)std::min(255.0f, roadMaskData[idx + 2] + change);
                } else { // Paint Custom Layer / Sand / Rock / Grass -> Alpha Channel
                    roadMaskData[idx + 3] = (unsigned char)std::min(255.0f, roadMaskData[idx + 3] + change);
                }
            }
        }
    }

    // Re-upload only the painted rectangle. roadMaskData is a full roadW x roadH
    // RGBA image, so GL_UNPACK_ROW_LENGTH lets glTexSubImage2D read the
    // sub-rectangle straight out of it without a staging copy. (The whole mask
    // used to be re-uploaded on every paint frame.) GL_UNPACK_ROW_LENGTH is
    // restored because it is global state other uploads rely on being 0.
    glBindTexture(GL_TEXTURE_2D, roadMask);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, roadW);
    glTexSubImage2D(GL_TEXTURE_2D, 0, minX, minZ, maxX - minX + 1, maxZ - minZ + 1,
                    GL_RGBA, GL_UNSIGNED_BYTE,
                    roadMaskData.data() + ((size_t)minZ * roadW + minX) * 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

#include <stb_image_write.h>
#include <cstdlib>

void TerrainManager::saveTerrain() {
    // 1. Save painted roadMask (roadMap.png)
    if (roadW > 0 && roadH > 0 && !roadMaskData.empty()) {
        std::cout << "[TerrainManager] Saving roadMap.png (" << roadW << "x" << roadH << ")..." << std::endl;
        std::string roadMapPath = "assets/terrain/roadMap.png";
        stbi_write_png(roadMapPath.c_str(), roadW, roadH, 4, roadMaskData.data(), roadW * 4);
    }

    // 2. Save sculpted heights (heightmap.png) via raw + python script
    if (gridW > 0 && gridH > 0 && !heights.empty()) {
        std::cout << "[TerrainManager] Writing temporary raw heights for heightmap.png..." << std::endl;
        std::string rawPath = "assets/terrain/heightmap.raw";
        std::ofstream file(rawPath, std::ios::binary);
        if (file.is_open()) {
            file.write((char*)&gridW, sizeof(int));
            file.write((char*)&gridH, sizeof(int));

            // Roads are regenerated from the road graph on startup. Persist the
            // sculpted land beneath them, but strip reversible road shaping so
            // deleting roads cannot leave their old cuts in the saved heightmap.
            std::vector<float> persistableHeights = heights;
            const size_t visualCount = (size_t)visualW * (size_t)visualH;
            const bool hasRoadBedLayer = appliedRoadOffsets.size() == visualCount;
            const bool hasRoadConformLayer = appliedRoadConformOffsets.size() == visualCount;
            if (visualW > 1 && visualH > 1 && gridW > 1 && gridH > 1 &&
                (hasRoadBedLayer || hasRoadConformLayer)) {
                auto sampleLayer = [&](const std::vector<float>& layer, float fx, float fz) {
                    if (layer.size() != visualCount) return 0.0f;
                    const int x0 = (int)fx, z0 = (int)fz;
                    const int x1 = std::min(x0 + 1, visualW - 1);
                    const int z1 = std::min(z0 + 1, visualH - 1);
                    const float tx = fx - x0, tz = fz - z0;
                    const float a = layer[(size_t)z0 * visualW + x0] * (1.0f - tx) +
                                    layer[(size_t)z0 * visualW + x1] * tx;
                    const float b = layer[(size_t)z1 * visualW + x0] * (1.0f - tx) +
                                    layer[(size_t)z1 * visualW + x1] * tx;
                    return a * (1.0f - tz) + b * tz;
                };
                for (int z = 0; z < gridH; ++z) {
                    const float fz = (float)z / (gridH - 1) * (visualH - 1);
                    for (int x = 0; x < gridW; ++x) {
                        const float fx = (float)x / (gridW - 1) * (visualW - 1);
                        const size_t idx = (size_t)z * gridW + x;
                        persistableHeights[idx] = std::clamp(
                            persistableHeights[idx] -
                                sampleLayer(appliedRoadOffsets, fx, fz) -
                                sampleLayer(appliedRoadConformOffsets, fx, fz),
                            minHeight, maxHeight);
                    }
                }
            }

            std::vector<unsigned short> rawHeights(gridW * gridH);
            float heightRange = maxHeight - minHeight;
            for (int i = 0; i < gridW * gridH; ++i) {
                float norm = std::max(0.0f, std::min((persistableHeights[i] - minHeight) / heightRange, 1.0f));
                rawHeights[i] = (unsigned short)(norm * 65535.0f);
            }
            file.write((char*)rawHeights.data(), rawHeights.size() * sizeof(unsigned short));
            file.close();
            
            std::cout << "[TerrainManager] Invoking Python script to save 16-bit heightmap.png..." << std::endl;
            // Run python script synchronously with python3 fallback
            int ret = std::system("python src/save_heightmap.py");
            if (ret != 0) {
                std::cout << "[TerrainManager] 'python' command failed, trying 'python3'..." << std::endl;
                std::system("python3 src/save_heightmap.py");
            }
        }
    }
}

void TerrainManager::erode(int numDroplets, float brushX, float brushZ, float radius, bool isLocal) {
    if (visualW <= 0 || visualH <= 0 || visualHeights.empty()) return;

    // Simulation parameters
    const int maxLifetime = 30;
    const float inertia = 0.15f;
    const float capacityFactor = 4.0f;
    const float depositionRate = 0.3f;
    const float erosionRate = 0.3f;
    const float evaporationRate = 0.02f;
    const float gravity = 4.0f;
    const int erosionRadius = 3;

    // Fast Linear Congruential Generator seed
    unsigned int lcgSeed = 1337;

    for (int i = 0; i < numDroplets; ++i) {
        float posX, posZ;

        if (isLocal) {
            // Spawn droplets in a circle around the brush center
            lcgSeed = lcgSeed * 1103515245 + 12345;
            float rand1 = (float)(lcgSeed / 65536 % 32768) / 32768.0f;
            lcgSeed = lcgSeed * 1103515245 + 12345;
            float rand2 = (float)(lcgSeed / 65536 % 32768) / 32768.0f;

            float angle = rand1 * 2.0f * 3.14159f;
            float r = rand2 * radius;
            
            float wx = brushX + cos(angle) * r;
            float wz = brushZ + sin(angle) * r;
            
            float ux = std::clamp((wx - worldMinX) / (worldMaxX - worldMinX), 0.0f, 1.0f);
            float uz = std::clamp((wz - worldMinZ) / (worldMaxZ - worldMinZ), 0.0f, 1.0f);
            
            posX = ux * (visualW - 1);
            posZ = uz * (visualH - 1);
        } else {
            // Spawn randomly across the map (leaving a border margin)
            lcgSeed = lcgSeed * 1103515245 + 12345;
            float rand1 = (float)(lcgSeed / 65536 % 32768) / 32768.0f;
            lcgSeed = lcgSeed * 1103515245 + 12345;
            float rand2 = (float)(lcgSeed / 65536 % 32768) / 32768.0f;

            posX = 2.0f + rand1 * (visualW - 5);
            posZ = 2.0f + rand2 * (visualH - 5);
        }

        float dirX = 0.0f;
        float dirZ = 0.0f;
        float speed = 1.0f;
        float water = 1.0f;
        float sediment = 0.0f;

        for (int lifetime = 0; lifetime < maxLifetime; ++lifetime) {
            int x0 = (int)posX;
            int z0 = (int)posZ;
            float tx = posX - x0;
            float tz = posZ - z0;

            if (x0 < 1 || x0 >= visualW - 2 || z0 < 1 || z0 >= visualH - 2) break;

            int idx00 = z0 * visualW + x0;
            int idx10 = idx00 + 1;
            int idx01 = (z0 + 1) * visualW + x0;
            int idx11 = idx01 + 1;

            float h00 = visualHeights[idx00];
            float h10 = visualHeights[idx10];
            float h01 = visualHeights[idx01];
            float h11 = visualHeights[idx11];

            float height = h00 * (1.0f - tx) * (1.0f - tz) +
                           h10 * tx * (1.0f - tz) +
                           h01 * (1.0f - tx) * tz +
                           h11 * tx * tz;

            // Gradient
            float gradX = (h10 - h00) * (1.0f - tz) + (h11 - h01) * tz;
            float gradZ = (h01 - h00) * (1.0f - tx) + (h11 - h10) * tx;

            // Update direction with inertia
            dirX = dirX * inertia - gradX * (1.0f - inertia);
            dirZ = dirZ * inertia - gradZ * (1.0f - inertia);

            // Normalize direction
            float len = std::sqrt(dirX * dirX + dirZ * dirZ);
            if (len > 0.0001f) {
                dirX /= len;
                dirZ /= len;
            } else {
                lcgSeed = lcgSeed * 1103515245 + 12345;
                float randAngle = ((float)(lcgSeed / 65536 % 32768) / 32768.0f) * 2.0f * 3.14159f;
                dirX = cos(randAngle);
                dirZ = sin(randAngle);
            }

            // Move
            float newX = posX + dirX;
            float newZ = posZ + dirZ;

            int nx0 = (int)newX;
            int nz0 = (int)newZ;
            if (nx0 < 1 || nx0 >= visualW - 2 || nz0 < 1 || nz0 >= visualH - 2) break;

            float n_tx = newX - nx0;
            float n_tz = newZ - nz0;
            float nh00 = visualHeights[nz0 * visualW + nx0];
            float nh10 = visualHeights[nz0 * visualW + nx0 + 1];
            float nh01 = visualHeights[(nz0 + 1) * visualW + nx0];
            float nh11 = visualHeights[(nz0 + 1) * visualW + nx0 + 1];
            float newHeight = nh00 * (1.0f - n_tx) * (1.0f - n_tz) +
                              nh10 * n_tx * (1.0f - n_tz) +
                              nh01 * (1.0f - n_tx) * n_tz +
                              nh11 * n_tx * n_tz;

            float deltaHeight = newHeight - height;

            // Sediment capacity
            float capacity = std::max(0.0f, -deltaHeight) * speed * water * capacityFactor;

            if (sediment > capacity || deltaHeight > 0.0f) {
                // Deposit
                float depositAmount = (deltaHeight > 0.0f) ? std::min(deltaHeight, sediment) : (sediment - capacity) * depositionRate;
                sediment -= depositAmount;

                // Bilinear deposit at old position
                visualHeights[idx00] = std::clamp(visualHeights[idx00] + depositAmount * (1.0f - tx) * (1.0f - tz), minHeight, maxHeight);
                visualHeights[idx10] = std::clamp(visualHeights[idx10] + depositAmount * tx * (1.0f - tz), minHeight, maxHeight);
                visualHeights[idx01] = std::clamp(visualHeights[idx01] + depositAmount * (1.0f - tx) * tz, minHeight, maxHeight);
                visualHeights[idx11] = std::clamp(visualHeights[idx11] + depositAmount * tx * tz, minHeight, maxHeight);
            } else {
                // Erode
                float erodeAmount = std::min((capacity - sediment) * erosionRate, -deltaHeight);
                sediment += erodeAmount;

                // Kernel erosion
                float totalWeight = 0.0f;
                std::vector<int> nearXs, nearZs;
                std::vector<float> weights;
                nearXs.reserve((erosionRadius * 2 + 1) * (erosionRadius * 2 + 1));
                nearZs.reserve((erosionRadius * 2 + 1) * (erosionRadius * 2 + 1));
                weights.reserve((erosionRadius * 2 + 1) * (erosionRadius * 2 + 1));

                for (int sz = z0 - erosionRadius; sz <= z0 + erosionRadius; ++sz) {
                    for (int sx = x0 - erosionRadius; sx <= x0 + erosionRadius; ++sx) {
                        if (sx >= 0 && sx < visualW && sz >= 0 && sz < visualH) {
                            float dx = sx - posX;
                            float dz = sz - posZ;
                            float d = std::sqrt(dx * dx + dz * dz);
                            if (d <= erosionRadius) {
                                float w = std::max(0.0f, (float)erosionRadius - d);
                                nearXs.push_back(sx);
                                nearZs.push_back(sz);
                                weights.push_back(w);
                                totalWeight += w;
                            }
                        }
                    }
                }

                if (totalWeight > 0.0f) {
                    float invTotalWeight = 1.0f / totalWeight;
                    for (size_t k = 0; k < weights.size(); ++k) {
                        int sx = nearXs[k];
                        int sz = nearZs[k];
                        visualHeights[sz * visualW + sx] = std::clamp(visualHeights[sz * visualW + sx] - erodeAmount * weights[k] * invTotalWeight, minHeight, maxHeight);
                    }
                }
            }

            // Speed update
            float nextSpeedSqr = speed * speed + deltaHeight * gravity;
            if (nextSpeedSqr <= 0.0f || std::isnan(nextSpeedSqr)) {
                break;
            }
            speed = std::sqrt(nextSpeedSqr);
            water *= (1.0f - evaporationRate);

            posX = newX;
            posZ = newZ;
        }
    }
}

void TerrainManager::erodeGlobal() {
    std::cout << "[TerrainManager] Running global hydraulic erosion pass (50k droplets)..." << std::endl;
    erode(50000, 0.0f, 0.0f, 0.0f, false);

    // Sync visualHeights back to native heights
    if (gridW > 0 && gridH > 0 && !heights.empty()) {
        if (visualW == gridW && visualH == gridH) {
            heights = visualHeights;
        } else {
            float scaleX = (float)(visualW - 1) / (gridW - 1);
            float scaleZ = (float)(visualH - 1) / (gridH - 1);
            for (int z = 0; z < gridH; ++z) {
                float srcZ = z * scaleZ;
                int z0 = (int)srcZ;
                int z1 = std::min(z0 + 1, visualH - 1);
                float tz = srcZ - z0;
                for (int x = 0; x < gridW; ++x) {
                    float srcX = x * scaleX;
                    int x0 = (int)srcX;
                    int x1 = std::min(x0 + 1, visualW - 1);
                    float tx = srcX - x0;

                    heights[z * gridW + x] =
                        visualHeights[z0 * visualW + x0] * (1.0f - tx) * (1.0f - tz) +
                        visualHeights[z0 * visualW + x1] * tx * (1.0f - tz) +
                        visualHeights[z1 * visualW + x0] * (1.0f - tx) * tz +
                        visualHeights[z1 * visualW + x1] * tx * tz;
                }
            }
        }
    }

    // Re-upload entire heightmap to GPU
    updateGPUHeightmapTexture();

    // Rebuild all visual chunks
    std::cout << "[TerrainManager] Rebuilding visual chunk meshes..." << std::endl;
    for (auto c : chunks) {
        c->generateMesh(visualHeights, visualW, visualH, worldMinX, worldMaxX, worldMinZ, worldMaxZ,
                        hasRoadMask ? &roadMaskData : nullptr, roadW, roadH);
    }
    std::cout << "[TerrainManager] Global erosion pass complete." << std::endl;
}

void TerrainManager::updateGPUHeightmapTexture() {
    updateGPUHeightmapTextureRect(0, 0, gridW - 1, gridH - 1);
}

void TerrainManager::updateGPUHeightmapTextureRect(int minX, int minZ, int maxX,
                                                   int maxZ) {
    if (heightmapTexID == 0 || heights.empty() || gridW <= 0 || gridH <= 0) return;

    // Clamp to the grid; an inverted rectangle becomes empty.
    minX = std::max(0, std::min(minX, gridW - 1));
    minZ = std::max(0, std::min(minZ, gridH - 1));
    maxX = std::max(0, std::min(maxX, gridW - 1));
    maxZ = std::max(0, std::min(maxZ, gridH - 1));
    if (maxX < minX || maxZ < minZ) return;

    const int w = maxX - minX + 1;
    const int h = maxZ - minZ + 1;

    // Reuse one scratch buffer instead of allocating a full-grid vector per call.
    hmScratch.resize((size_t)w * (size_t)h);

    const float heightRange = maxHeight - minHeight;
    const float invHeightRange = (heightRange > 0.0001f) ? (1.0f / heightRange) : 1.0f;
    for (int z = 0; z < h; ++z) {
        const float* src = heights.data() + (size_t)(minZ + z) * gridW + minX;
        float* dst = hmScratch.data() + (size_t)z * w;
        for (int x = 0; x < w; ++x) {
            dst[x] = std::min(std::max((src[x] - minHeight) * invHeightRange, 0.0f), 1.0f);
        }
    }

    glBindTexture(GL_TEXTURE_2D, heightmapTexID);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, minX, minZ, w, h, GL_RED, GL_FLOAT,
                    hmScratch.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void TerrainManager::logHeightfieldSignature(const char* stage) const {
    if (!std::getenv("FALLAGA_ROADGAP") || visualHeights.empty()) return;
    double sum = 0.0;
    double lo = (double)visualHeights[0], hi = (double)visualHeights[0];
    for (float h : visualHeights) {
        sum += (double)h;
        lo = std::min(lo, (double)h);
        hi = std::max(hi, (double)h);
    }
    std::cout << "[TerrainSig] " << stage << " sum=" << sum
              << " mean=" << (sum / (double)visualHeights.size())
              << " min=" << lo << " max=" << hi << std::endl;
}

void TerrainManager::applyRoadBedHeightfield(const float* offsets, int offsetW, int offsetH) {
    if (visualW <= 1 || visualH <= 1 || visualHeights.empty()) return;
    const size_t visualCount = (size_t)visualW * (size_t)visualH;
    if (appliedRoadOffsets.size() != visualCount)
        appliedRoadOffsets.assign(visualCount, 0.0f);
    if (appliedRoadConformOffsets.size() != visualCount)
        appliedRoadConformOffsets.assign(visualCount, 0.0f);

    int minX = visualW, minZ = visualH, maxX = -1, maxZ = -1;
    for (int z = 0; z < visualH; ++z) {
        const float v = (float)z / (visualH - 1);
        const float fz = (offsets && offsetW > 1 && offsetH > 1) ? v * (offsetH - 1) : 0.0f;
        const int z0 = (int)fz;
        const int z1 = offsets ? std::min(z0 + 1, offsetH - 1) : 0;
        const float tz = fz - z0;
        for (int x = 0; x < visualW; ++x) {
            const float u = (float)x / (visualW - 1);
            const float fx = (offsets && offsetW > 1 && offsetH > 1) ? u * (offsetW - 1) : 0.0f;
            const int x0 = (int)fx;
            const int x1 = offsets ? std::min(x0 + 1, offsetW - 1) : 0;
            const float tx = fx - x0;
            float targetOffset = 0.0f;
            if (offsets && offsetW > 1 && offsetH > 1) {
                const float a = offsets[z0 * offsetW + x0] * (1.0f - tx) + offsets[z0 * offsetW + x1] * tx;
                const float b = offsets[z1 * offsetW + x0] * (1.0f - tx) + offsets[z1 * offsetW + x1] * tx;
                targetOffset = a * (1.0f - tz) + b * tz;
            }
            const size_t idx = (size_t)z * visualW + x;
            if (!offsets && std::fabs(appliedRoadConformOffsets[idx]) > 1e-5f) {
                const float oldHeight = visualHeights[idx];
                const float restoredHeight = std::clamp(
                    oldHeight - appliedRoadConformOffsets[idx], minHeight, maxHeight);
                const float undoDelta = restoredHeight - oldHeight;
                visualHeights[idx] = restoredHeight;
                appliedRoadConformOffsets[idx] += undoDelta;
                if (std::fabs(undoDelta) > 1e-5f) {
                    minX = std::min(minX, x); maxX = std::max(maxX, x);
                    minZ = std::min(minZ, z); maxZ = std::max(maxZ, z);
                }
            }
            const float delta = targetOffset - appliedRoadOffsets[idx];
            if (std::fabs(delta) <= 1e-5f) continue;
            const float oldHeight = visualHeights[idx];
            visualHeights[idx] = std::clamp(oldHeight + delta, minHeight, maxHeight);
            appliedRoadOffsets[idx] += visualHeights[idx] - oldHeight;
            if (std::fabs(visualHeights[idx] - oldHeight) > 1e-5f) {
                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minZ = std::min(minZ, z); maxZ = std::max(maxZ, z);
            }
        }
    }
    if (maxX < minX || maxZ < minZ) return;
    logHeightfieldSignature(offsets ? "roadbed/apply" : "roadbed/RESTORE");

    // Keep native-resolution physics and height queries in sync with the edited
    // visual heightfield, including terrains whose source DEM is downsampled.
    // Only the native pixels inside the edited visual box can have changed, so the
    // resample and the texture upload are both restricted to that box instead of
    // sweeping the entire native grid on every roadbed frame.
    if (gridW > 1 && gridH > 1) {
        const float scaleX = (float)(visualW - 1) / (gridW - 1);
        const float scaleZ = (float)(visualH - 1) / (gridH - 1);
        const int natMinX = (scaleX > 0.0f) ? (int)std::floor(minX / scaleX) - 1 : 0;
        const int natMaxX = (scaleX > 0.0f) ? (int)std::ceil (maxX / scaleX) + 1 : gridW - 1;
        const int natMinZ = (scaleZ > 0.0f) ? (int)std::floor(minZ / scaleZ) - 1 : 0;
        const int natMaxZ = (scaleZ > 0.0f) ? (int)std::ceil (maxZ / scaleZ) + 1 : gridH - 1;
        const int safeMinX = std::max(0, std::min(natMinX, gridW - 1));
        const int safeMaxX = std::max(0, std::min(natMaxX, gridW - 1));
        const int safeMinZ = std::max(0, std::min(natMinZ, gridH - 1));
        const int safeMaxZ = std::max(0, std::min(natMaxZ, gridH - 1));
        for (int z = safeMinZ; z <= safeMaxZ; ++z) {
            const float srcZ = z * scaleZ;
            const int z0 = (int)srcZ, z1 = std::min(z0 + 1, visualH - 1);
            const float tz = srcZ - z0;
            for (int x = safeMinX; x <= safeMaxX; ++x) {
                const float srcX = x * scaleX;
                const int x0 = (int)srcX, x1 = std::min(x0 + 1, visualW - 1);
                const float tx = srcX - x0;
                heights[z * gridW + x] =
                    visualHeights[z0 * visualW + x0] * (1.0f - tx) * (1.0f - tz) +
                    visualHeights[z0 * visualW + x1] * tx * (1.0f - tz) +
                    visualHeights[z1 * visualW + x0] * (1.0f - tx) * tz +
                    visualHeights[z1 * visualW + x1] * tx * tz;
            }
        }
        updateGPUHeightmapTextureRect(safeMinX, safeMinZ, safeMaxX, safeMaxZ);
    }

    minX = std::max(0, minX - 1); minZ = std::max(0, minZ - 1);
    maxX = std::min(visualW - 1, maxX + 1); maxZ = std::min(visualH - 1, maxZ + 1);
    for (TerrainChunk* chunk : chunks) {
        const int chunkMinX = chunk->gridX * (chunk->size - 1);
        const int chunkMaxX = chunkMinX + chunk->size - 1;
        const int chunkMinZ = chunk->gridZ * (chunk->size - 1);
        const int chunkMaxZ = chunkMinZ + chunk->size - 1;
        if (maxX < chunkMinX || minX > chunkMaxX || maxZ < chunkMinZ || minZ > chunkMaxZ) continue;
        chunk->generateMesh(visualHeights, visualW, visualH, worldMinX, worldMaxX,
                            worldMinZ, worldMaxZ,
                            hasRoadMask ? &roadMaskData : nullptr, roadW, roadH);
    }
}
