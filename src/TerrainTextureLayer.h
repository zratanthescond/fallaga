#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <GL/glew.h>
#include <string>

/**
 * Terrain Texture Layer System
 * Manages multi-layer terrain texturing using PNG masks for identification
 * 
 * Texture Types (indices 0-4):
 * 0: Gravel/Concrete - sandy/light ground
 * 1: Rocks - rocky outcrops, hills
 * 2: Rubble - weathered stone, ruins
 * 3: Tarred Gravel - roads, paved surfaces
 * 4: Sandy Gravel - beaches, soft surfaces
 */

class TerrainTextureLayer {
public:
    enum TextureType : int {
        GRAVEL_CONCRETE = 0,
        ROCKS = 1,
        RUBBLE = 2,
        TARRED_GRAVEL = 3,
        SANDY_GRAVEL = 4,
        NUM_LAYERS = 5
    };

    struct TextureTypeInfo {
        TextureType type;
        std::string name;
        std::string diffusePath;
        std::string normalPath;
        std::string dispPath;
        glm::vec3 colorID; // RGB value in PNG that identifies this layer (0-255)
    };

    static const TextureTypeInfo TEXTURE_INFO[NUM_LAYERS];

    // Load texture data from a PNG layer mask
    static std::vector<unsigned char> LoadTextureLayerMask(const std::string& path, int& width, int& height);
    
    // Get the dominant texture type at UV coordinates from mask data
    static TextureType GetTextureTypeAt(const std::vector<unsigned char>& maskData, int width, int height, float u, float v);
    
    // Get blending factors for multiple layers (for smooth transitions)
    static void GetBlendedTextureLayers(const std::vector<unsigned char>& maskData, int width, int height, 
                                        float u, float v, 
                                        std::vector<TextureType>& outLayers, 
                                        std::vector<float>& outWeights);
};
