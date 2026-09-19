#pragma once
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>

/**
 * SousseMapData — Mutable 'Architect Mode' Data Store
 * Allows real-time modification of city place and shape.
 */
namespace SousseMapData {

    // --- MUTABLE LIVE STATE ---
    inline bool layoutDirty = true; 
    // FLIPPED: North/South mirrored in C++ logic
    inline glm::vec2 WORLD_ANCHOR = {-530.0f, -1502.0f};

    struct Landmark {
        std::string name;
        glm::vec2 localOffset; // Relative to Kasbah D
        float yOffset;         // Offset above baked height
        float bakedY;          // NEW: Pre-calculated terrain height (BAKED)
        float xScale;          // NEW: Independent Width
        float yScale;          // Height
        float zScale;          // NEW: Independent Depth
        float angle;
        bool placed; // Is it actually on the map?
        std::string modelPath; // New: which asset is this?
        std::string customTexturePath; // NEW: custom texture override path
        std::string customTexturePath2; // NEW: custom texture layer 2 override path
        float textureBlend = 0.0f;     // NEW: blend weight between layer 1 and 2
    };

    inline std::vector<std::string> discoveredAssets;
    inline std::vector<std::string> discoveredTextures;

    // Initialize with definitive map-calibrated data
    inline std::vector<Landmark> liveLandmarks = {};

    inline void scanAssets(const std::string& root) {
        discoveredAssets.clear();
        discoveredTextures.clear();
        try {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    std::string path = entry.path().string();
                    std::replace(path.begin(), path.end(), '\\', '/'); // Normalize path slashes
                    
                    if (ext == ".obj" || ext == ".fbx") {
                        discoveredAssets.push_back(path);
                    } else if (ext == ".jpg" || ext == ".png" || ext == ".tga" || ext == ".jpeg") {
                        // Skip system-generated files or cache files
                        if (path.find(".gemini") == std::string::npos && 
                            path.find("saves") == std::string::npos) {
                            discoveredTextures.push_back(path);
                        }
                    }
                }
            }
            std::cout << "[Librarian] : Discovered " << discoveredAssets.size() << " assets and " 
                      << discoveredTextures.size() << " textures in " << root << "\n";
        } catch (const std::exception& e) {
            std::cerr << "[Librarian ERROR] : " << e.what() << "\n";
        }
    }

    struct CityWallConfig {
        static inline float manualOffsetX = -51.2f;
        static inline float manualOffsetY = 0.0f;
        static inline float manualOffsetZ = -500.801f;
        static inline float manualRotationY = 0.0f;
        static inline float manualScaleX = 1.0f;
        static inline float manualScaleY = 1.2f;
        static inline float manualScaleZ = 1.4f;
    };

    inline glm::vec2 toWorld(glm::vec2 local) {
        return WORLD_ANCHOR + local;
    }

    /**
     * Re-calculates perimeter based on current WORLD_ANCHOR.
     */
    inline std::vector<glm::vec2> getMedinaPerimeter() {
        return {
            toWorld({0.0f, 0.0f}),      
            toWorld({150.0f, 50.0f}),   // Mirror: -50 -> 50
            toWorld({520.0f, 0.0f}),    
            toWorld({550.0f, -400.0f}), // Mirror: 400 -> -400
            toWorld({500.0f, -750.0f}), // Mirror: 750 -> -750
            toWorld({250.0f, -820.0f}), // Mirror: 820 -> -820
            toWorld({-100.0f, -750.0f}),// Mirror: 750 -> -750
            toWorld({-150.0f, -400.0f}),// Mirror: 400 -> -400
            toWorld({0.0f, 0.0f})       
        };
    }

    inline std::vector<glm::vec2> getTransformedMedinaPerimeter() {
        auto perimeter = getMedinaPerimeter();
        glm::vec2 pCenter(0.0f, 0.0f);
        float minX = 99999.0f, maxX = -99999.0f;
        float minZ = 99999.0f, maxZ = -99999.0f;
        for (auto &pt : perimeter) {
            pCenter += pt;
            minX = std::min(minX, pt.x);
            maxX = std::max(maxX, pt.x);
            minZ = std::min(minZ, pt.y);
            maxZ = std::max(maxZ, pt.y);
        }
        pCenter /= (float)perimeter.size();
        
        std::vector<glm::vec2> transformed;
        float rad = glm::radians(CityWallConfig::manualRotationY);
        float c = std::cos(rad);
        float s = std::sin(rad);
        
        glm::vec2 offset(CityWallConfig::manualOffsetX, CityWallConfig::manualOffsetZ);
        
        for (auto &pt : perimeter) {
            glm::vec2 local = pt - pCenter;
            local.x *= CityWallConfig::manualScaleX;
            local.y *= CityWallConfig::manualScaleZ;
            
            glm::vec2 rotLocal;
            rotLocal.x = local.x * c - local.y * s;
            rotLocal.y = local.x * s + local.y * c;
            
            transformed.push_back(pCenter + offset + rotLocal);
        }
        return transformed;
    }

    inline void resetLayout() {
        WORLD_ANCHOR = {0.0f, 0.0f};
        liveLandmarks.clear();
        // Add a default entry for each discovered asset in an 'unplaced' state
        for (const auto& path : discoveredAssets) {
             Landmark l;
             l.name = std::filesystem::path(path).filename().string();
             l.localOffset = {0,0};
             l.yOffset = 0.0f;
             l.xScale = 50.0f; l.yScale = 50.0f; l.zScale = 50.0f;
             l.angle = 0.0f;
             l.placed = false;
             l.modelPath = path;
             l.customTexturePath = "";
             l.customTexturePath2 = "";
             l.textureBlend = 0.0f;
             liveLandmarks.push_back(l);
        }
        std::cout << "[Architect Mode] : Librarian Ready - " << liveLandmarks.size() << " assets available.\n";
    }

    inline void saveLayout(const std::string& path) {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        std::ofstream file(path);
        if (!file.is_open()) return;
        
        file << "ANCHOR " << WORLD_ANCHOR.x << " " << WORLD_ANCHOR.y << "\n";
         for (const auto& l : liveLandmarks) {
            if (l.placed) {
                std::string tex = l.customTexturePath.empty() ? "NONE" : l.customTexturePath;
                std::string tex2 = l.customTexturePath2.empty() ? "NONE" : l.customTexturePath2;
                file << "OBJ_V6 " << l.name << " " << l.localOffset.x << " " << l.localOffset.y << " " 
                     << l.yOffset << " " << l.bakedY << " " << l.xScale << " " << l.yScale << " " << l.zScale << " " 
                     << l.angle << " " << l.modelPath << " " << tex << " " << tex2 << " " << l.textureBlend << "\n";
            }
        }
        std::cout << "[Librarian] : City Layout Saved to " << path << "\n";
    }

    inline void loadLayout(const std::string& path) {
        std::cout << "[Librarian] : Attempting to load layout from: " << path << std::endl;
        std::ifstream file(path);
        if (!file.is_open()) {
            std::cerr << "[Librarian ERROR] : Could not open " << path << " for reading! (Check path or file existence)" << std::endl;
            return;
        }
        
        int loadedCount = 0;
        int totalLines = 0;
        std::string line, type;
        while (std::getline(file, line)) {
            totalLines++;
            if (line.empty() || line[0] == '#') continue;
            std::stringstream ss(line);
            if (!(ss >> type)) continue;
            
            if (type == "ANCHOR") {
                ss >> WORLD_ANCHOR.x >> WORLD_ANCHOR.y;
                std::cout << "[Librarian] : Anchor set to " << WORLD_ANCHOR.x << ", " << WORLD_ANCHOR.y << "\n";
            } else if (type == "OBJ") {
                Landmark l;
                float s;
                if (ss >> l.name >> l.localOffset.x >> l.localOffset.y >> s >> l.yScale >> l.angle >> l.modelPath) {
                    l.xScale = s; l.zScale = s;
                    l.yOffset = 0.0f; l.bakedY = -1e9; l.placed = true;
                    l.customTexturePath = ""; l.customTexturePath2 = ""; l.textureBlend = 0.0f;
                    liveLandmarks.push_back(l);
                    loadedCount++;
                }
            } else if (type == "OBJ_V2") {
                Landmark l;
                float s;
                if (ss >> l.name >> l.localOffset.x >> l.localOffset.y >> l.yOffset >> s >> l.yScale >> l.angle >> l.modelPath) {
                    l.xScale = s; l.zScale = s;
                    l.bakedY = -1e9; l.placed = true;
                    l.customTexturePath = ""; l.customTexturePath2 = ""; l.textureBlend = 0.0f;
                    liveLandmarks.push_back(l);
                    loadedCount++;
                }
            } else if (type == "OBJ_V3") {
                Landmark l;
                float s;
                if (ss >> l.name >> l.localOffset.x >> l.localOffset.y >> l.yOffset >> l.bakedY >> s >> l.yScale >> l.angle >> l.modelPath) {
                    l.xScale = s; l.zScale = s;
                    l.placed = true;
                    l.customTexturePath = ""; l.customTexturePath2 = ""; l.textureBlend = 0.0f;
                    liveLandmarks.push_back(l);
                    loadedCount++;
                } else {
                    if (loadedCount < 5) std::cerr << "[Librarian ERROR] : Parse fail at line " << totalLines << ": " << line << std::endl;
                }
            } else if (type == "OBJ_V4") {
                Landmark l;
                if (ss >> l.name >> l.localOffset.x >> l.localOffset.y >> l.yOffset >> l.bakedY >> l.xScale >> l.yScale >> l.zScale >> l.angle >> l.modelPath) {
                    l.placed = true;
                    l.customTexturePath = ""; l.customTexturePath2 = ""; l.textureBlend = 0.0f;
                    liveLandmarks.push_back(l);
                    loadedCount++;
                } else {
                    if (loadedCount < 5) std::cerr << "[Librarian ERROR] : Parse fail at line " << totalLines << ": " << line << std::endl;
                }
            } else if (type == "OBJ_V5") {
                Landmark l;
                std::string tex;
                if (ss >> l.name >> l.localOffset.x >> l.localOffset.y >> l.yOffset >> l.bakedY >> l.xScale >> l.yScale >> l.zScale >> l.angle >> l.modelPath >> tex) {
                    l.placed = true;
                    l.customTexturePath = (tex == "NONE") ? "" : tex;
                    l.customTexturePath2 = ""; l.textureBlend = 0.0f;
                    liveLandmarks.push_back(l);
                    loadedCount++;
                } else {
                    if (loadedCount < 5) std::cerr << "[Librarian ERROR] : Parse fail at line " << totalLines << ": " << line << std::endl;
                }
            } else if (type == "OBJ_V6") {
                Landmark l;
                std::string tex, tex2;
                float blend;
                if (ss >> l.name >> l.localOffset.x >> l.localOffset.y >> l.yOffset >> l.bakedY >> l.xScale >> l.yScale >> l.zScale >> l.angle >> l.modelPath >> tex >> tex2 >> blend) {
                    l.placed = true;
                    l.customTexturePath = (tex == "NONE") ? "" : tex;
                    l.customTexturePath2 = (tex2 == "NONE") ? "" : tex2;
                    l.textureBlend = blend;
                    liveLandmarks.push_back(l);
                    loadedCount++;
                } else {
                    if (loadedCount < 5) std::cerr << "[Librarian ERROR] : Parse fail at line " << totalLines << ": " << line << std::endl;
                }
            }
        }
        std::cout << "[Librarian] : City Layout Loaded from " << path << ". Total lines: " << totalLines << ". Added " << loadedCount << " new objects.\n";
        std::cout << "[Librarian] : City Layout Loaded. Total objects now in list: " << liveLandmarks.size() << " (Loaded " << loadedCount << " new)\n";
    }

    /**
     * Console Save Helper: Prints out the current state as valid C++ code.
     */
    inline void printSaveState() {
        std::cout << "\n=== SOUSSE ARCHITECT STATE ===\n";
        std::cout << "WORLD_ANCHOR: " << WORLD_ANCHOR.x << ", " << WORLD_ANCHOR.y << "\n";
        for (const auto& l : liveLandmarks) {
            if (l.placed) {
                std::cout << "LANDMARK: " << l.name << " AT " << l.localOffset.x << ", " << l.localOffset.y << " ROT " << l.angle << " PATH " << l.modelPath << "\n";
            }
        }
        std::cout << "=============================\n";
        saveLayout("assets/saves/city.txt"); 
    }
}
