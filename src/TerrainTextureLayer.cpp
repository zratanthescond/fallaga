#include "TerrainTextureLayer.h"
#include <stb_image.h>
#include <iostream>
#include <cmath>
#include <algorithm>

// Define texture layer information with PNG color identification
const TerrainTextureLayer::TextureTypeInfo TerrainTextureLayer::TEXTURE_INFO[NUM_LAYERS] = {
    {GRAVEL_CONCRETE, "Red Laterite Soil (Land Ground)", 
     "assets/terrain/textures/TerrainMainTextures/red_laterite_soil_stones_2k/textures/red_laterite_soil_stones_diff_2k.jpg",
     "assets/terrain/textures/TerrainMainTextures/red_laterite_soil_stones_2k/textures/red_laterite_soil_stones_nor_dx_2k.png",
     "assets/terrain/textures/TerrainMainTextures/red_laterite_soil_stones_2k/textures/red_laterite_soil_stones_disp_2k.png",
     glm::vec3(64, 64, 64)},  // Dark gray = main land soil
    
    {ROCKS, "Rocky Trail (Rocks)",
     "assets/terrain/textures/TerrainMainTextures/rocky_trail_2k/textures/rocky_trail_diff_2k.jpg",
     "assets/terrain/textures/TerrainMainTextures/rocky_trail_2k/textures/rocky_trail_nor_dx_2k.png",
     "assets/terrain/textures/TerrainMainTextures/rocky_trail_2k/textures/rocky_trail_disp_2k.png",
     glm::vec3(128, 128, 128)},  // Mid gray = rocks
    
    {RUBBLE, "Rocky Trail (Rubble)",
     "assets/terrain/textures/TerrainMainTextures/rocky_trail_2k/textures/rocky_trail_diff_2k.jpg",
     "assets/terrain/textures/TerrainMainTextures/rocky_trail_2k/textures/rocky_trail_nor_dx_2k.png",
     "assets/terrain/textures/TerrainMainTextures/rocky_trail_2k/textures/rocky_trail_disp_2k.png",
     glm::vec3(192, 192, 192)},  // Light gray = rubble
    
    {TARRED_GRAVEL, "Tarred Gravel (Road)",
     "assets/terrain/tarred_gravel_2k/textures/tarred_gravel_diff_2k.jpg",
     "assets/terrain/tarred_gravel_2k/textures/tarred_gravel_nor_dx_2k.png",
     "assets/terrain/tarred_gravel_2k/textures/tarred_gravel_disp_2k.png",
     glm::vec3(32, 32, 32)},  // Very dark = roads
    
    {SANDY_GRAVEL, "Coastline Sand (Beach)",
     "assets/terrain/coast_sand_01_diff_2k.jpg",
     "assets/terrain/coast_sand_01_nor_gl_2k.jpg",
     "assets/terrain/coast_sand_01_nor_gl_2k.jpg",
     glm::vec3(255, 200, 100)},  // Sandy/beige = beach/coastline
};

std::vector<unsigned char> TerrainTextureLayer::LoadTextureLayerMask(const std::string& path, int& width, int& height) {
    int channels;
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    
    std::vector<unsigned char> result;
    if (data) {
        result.assign(data, data + (width * height * 4));
        std::cout << "[TerrainTextureLayer] Loaded texture mask: " << path 
                  << " (" << width << "x" << height << "x" << channels << ")" << std::endl;
        stbi_image_free(data);
    } else {
        std::cerr << "[TerrainTextureLayer] FAILED to load texture mask: " << path << std::endl;
    }
    
    return result;
}

// Calculate color distance in RGB space
static float ColorDistance(const glm::vec3& c1, const glm::vec3& c2) {
    glm::vec3 diff = c1 - c2;
    return glm::length(diff);
}

TerrainTextureLayer::TextureType TerrainTextureLayer::GetTextureTypeAt(
    const std::vector<unsigned char>& maskData, int width, int height, float u, float v) {
    
    if (maskData.empty() || width <= 0 || height <= 0) {
        return GRAVEL_CONCRETE; // Default fallback
    }

    // Clamp UV to [0, 1]
    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);

    // Map UV to pixel coordinates
    int x = (int)(u * (width - 1));
    int y = (int)(v * (height - 1));
    int pixelIdx = (y * width + x) * 4;

    // Extract RGB color from mask
    glm::vec3 pixelColor(
        maskData[pixelIdx + 0],
        maskData[pixelIdx + 1],
        maskData[pixelIdx + 2]
    );

    // Find which texture type this color is closest to
    TextureType bestType = GRAVEL_CONCRETE;
    float minDist = std::numeric_limits<float>::max();

    for (int i = 0; i < NUM_LAYERS; ++i) {
        float dist = ColorDistance(pixelColor, TEXTURE_INFO[i].colorID);
        if (dist < minDist) {
            minDist = dist;
            bestType = TEXTURE_INFO[i].type;
        }
    }

    return bestType;
}

void TerrainTextureLayer::GetBlendedTextureLayers(
    const std::vector<unsigned char>& maskData, int width, int height,
    float u, float v,
    std::vector<TextureType>& outLayers,
    std::vector<float>& outWeights) {
    
    outLayers.clear();
    outWeights.clear();

    if (maskData.empty() || width <= 0 || height <= 0) {
        outLayers.push_back(GRAVEL_CONCRETE);
        outWeights.push_back(1.0f);
        return;
    }

    // Clamp UV to [0, 1]
    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);

    // Map UV to pixel coordinates with sub-pixel precision for smooth blending
    float fx = u * (width - 1);
    float fy = v * (height - 1);
    int x0 = (int)fx, y0 = (int)fy;
    int x1 = std::min(x0 + 1, width - 1);
    int y1 = std::min(y0 + 1, height - 1);
    float tx = fx - x0;
    float ty = fy - y0;

    // Sample 4 corners and blend
    std::vector<glm::vec3> cornerColors;
    cornerColors.push_back(glm::vec3(
        maskData[(y0 * width + x0) * 4 + 0],
        maskData[(y0 * width + x0) * 4 + 1],
        maskData[(y0 * width + x0) * 4 + 2]
    ));
    cornerColors.push_back(glm::vec3(
        maskData[(y0 * width + x1) * 4 + 0],
        maskData[(y0 * width + x1) * 4 + 1],
        maskData[(y0 * width + x1) * 4 + 2]
    ));
    cornerColors.push_back(glm::vec3(
        maskData[(y1 * width + x0) * 4 + 0],
        maskData[(y1 * width + x0) * 4 + 1],
        maskData[(y1 * width + x0) * 4 + 2]
    ));
    cornerColors.push_back(glm::vec3(
        maskData[(y1 * width + x1) * 4 + 0],
        maskData[(y1 * width + x1) * 4 + 1],
        maskData[(y1 * width + x1) * 4 + 2]
    ));

    // Find closest textures for each corner
    std::vector<TextureType> cornerTypes(4);
    for (int i = 0; i < 4; ++i) {
        cornerTypes[i] = GRAVEL_CONCRETE;
        float minDist = std::numeric_limits<float>::max();
        
        for (int j = 0; j < NUM_LAYERS; ++j) {
            float dist = ColorDistance(cornerColors[i], TEXTURE_INFO[j].colorID);
            if (dist < minDist) {
                minDist = dist;
                cornerTypes[i] = TEXTURE_INFO[j].type;
            }
        }
    }

    // Bilinear blend weights
    float w00 = (1.0f - tx) * (1.0f - ty);
    float w10 = tx * (1.0f - ty);
    float w01 = (1.0f - tx) * ty;
    float w11 = tx * ty;

    // Accumulate weights for each layer
    std::vector<float> layerWeights(NUM_LAYERS, 0.0f);
    layerWeights[cornerTypes[0]] += w00;
    layerWeights[cornerTypes[1]] += w10;
    layerWeights[cornerTypes[2]] += w01;
    layerWeights[cornerTypes[3]] += w11;

    // Return layers with significant contribution (> 0.01 threshold)
    for (int i = 0; i < NUM_LAYERS; ++i) {
        if (layerWeights[i] > 0.01f) {
            outLayers.push_back((TextureType)i);
            outWeights.push_back(layerWeights[i]);
        }
    }

    // Fallback: always return at least one layer
    if (outLayers.empty()) {
        outLayers.push_back(GRAVEL_CONCRETE);
        outWeights.push_back(1.0f);
    }
}
