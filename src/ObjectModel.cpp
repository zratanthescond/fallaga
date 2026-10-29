#include "ObjectModel.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <limits>
#include <cmath>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <assimp/Exporter.hpp>
#define GLM_ENABLE_EXPERIMENTAL
// 🌟 FIX: STB_IMAGE_IMPLEMENTATION moved to stb_impl.cpp 🌟
#include <stb_image.h>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h> 
#include <assimp/config.h>
#include <assimp/GltfMaterial.h>
#include "Vec3.h"
#include "Shader.h"
#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp> // For glm::normalize
bool ObjModel::rayTriangleIntersect(
     const Vec3& rayOrigin,
     const Vec3& rayDir,
     const Vec3& v0, 
     const Vec3& v1, 
     const Vec3& v2,
     float& outT) const {
     const float EPSILON = 0.0000001f;

     Vec3 edge1 = v1 - v0;
     Vec3 edge2 = v2 - v0;

     Vec3 h = rayDir.cross(edge2); 

     float a = edge1.dot( h);

     if (a > -EPSILON && a < EPSILON) { return false; }

     float f = 1.0f / a;
     Vec3 s = rayOrigin - v0;

     float u = f * s.dot( h);

     if (u < 0.0f || u > 1.0f) { return false; }

     Vec3 q = s.cross( edge1);

     float v = f * rayDir.dot( q);

     if (v < 0.0f || u + v > 1.0f) { return false; }

     float t = f * edge2.dot( q);

     if (t > EPSILON) {
         outT = t;
         return true;
     }

     return false;
}

// FIXED: Iterates over all vertices in all meshes for min/max Y.
void ObjModel::getMinMaxY(float& minY, float& maxY) const {
    if (meshes.empty()) {
        minY = 0.0f;
        maxY = 0.0f;
        return;
    }

    minY = std::numeric_limits<float>::max();
    maxY = -std::numeric_limits<float>::max();

    for (const auto& mesh : meshes) {
        const aiMesh* aimesh = mesh.assimpMesh;
        if (!aimesh || !aimesh->HasPositions()) continue;

        for (unsigned int i = 0; i < aimesh->mNumVertices; ++i) {
            float y = aimesh->mVertices[i].y;
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
        }
    }
    
    // Safety check for models with zero vertices in all meshes
    if (minY == std::numeric_limits<float>::max()) {
        minY = 0.0f;
        maxY = 0.0f;
    }
}

// ===============================
// Texture Loading (Kept)
// ===============================
static void configureModelTextureSampling() {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (GLEW_EXT_texture_filter_anisotropic) {
        GLfloat maxAnisotropy = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropy);
        if (maxAnisotropy > 1.0f) {
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                            std::min(maxAnisotropy, 8.0f));
        }
    }
}

static float srgbByteToLinear(unsigned char value) {
    const float encoded = value / 255.0f;
    return encoded <= 0.04045f
        ? encoded / 12.92f
        : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
}

static unsigned char linearToSrgbByte(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    const float encoded = value <= 0.0031308f
        ? value * 12.92f
        : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
    return static_cast<unsigned char>(std::clamp(encoded * 255.0f + 0.5f,
                                                  0.0f, 255.0f));
}

static float alphaCoverage(const std::vector<unsigned char>& rgba,
                           float cutoff, float scale = 1.0f) {
    if (rgba.empty()) return 0.0f;
    const float cutoffByte = cutoff * 255.0f;
    size_t covered = 0;
    for (size_t i = 3; i < rgba.size(); i += 4) {
        if (rgba[i] * scale >= cutoffByte) ++covered;
    }
    return static_cast<float>(covered) / static_cast<float>(rgba.size() / 4);
}

static std::vector<unsigned char> downsampleRgba(const std::vector<unsigned char>& source,
                                                 int width, int height,
                                                 int nextWidth, int nextHeight,
                                                 bool srgb) {
    std::vector<unsigned char> next(static_cast<size_t>(nextWidth) * nextHeight * 4);
    for (int y = 0; y < nextHeight; ++y) {
        for (int x = 0; x < nextWidth; ++x) {
            const int x0 = std::min(x * 2, width - 1);
            const int x1 = std::min(x0 + 1, width - 1);
            const int y0 = std::min(y * 2, height - 1);
            const int y1 = std::min(y0 + 1, height - 1);
            const int sourceIndices[4] = {
                (y0 * width + x0) * 4, (y0 * width + x1) * 4,
                (y1 * width + x0) * 4, (y1 * width + x1) * 4
            };
            float alphaSum = 0.0f;
            float premultiplied[3] = {0.0f, 0.0f, 0.0f};
            for (int sourceIndex : sourceIndices) {
                const float alpha = source[sourceIndex + 3] / 255.0f;
                alphaSum += alpha;
                for (int channel = 0; channel < 3; ++channel) {
                    const float color = srgb
                        ? srgbByteToLinear(source[sourceIndex + channel])
                        : source[sourceIndex + channel] / 255.0f;
                    premultiplied[channel] += color * alpha;
                }
            }

            const size_t destination = (static_cast<size_t>(y) * nextWidth + x) * 4;
            const float averageAlpha = alphaSum * 0.25f;
            for (int channel = 0; channel < 3; ++channel) {
                const float color = alphaSum > 1e-6f
                    ? premultiplied[channel] / alphaSum : 0.0f;
                next[destination + channel] = srgb
                    ? linearToSrgbByte(color)
                    : static_cast<unsigned char>(std::clamp(color * 255.0f + 0.5f,
                                                             0.0f, 255.0f));
            }
            next[destination + 3] = static_cast<unsigned char>(
                std::clamp(averageAlpha * 255.0f + 0.5f, 0.0f, 255.0f));
        }
    }
    return next;
}

static float alphaCoverageScale(const std::vector<unsigned char>& rgba,
                                float cutoff, float targetCoverage) {
    float bestScale = 0.0f;
    float bestError = std::abs(targetCoverage);
    float low = 0.0f;
    float high = 255.0f;
    for (int iteration = 0; iteration < 24; ++iteration) {
        const float scale = 0.5f * (low + high);
        const float coverage = alphaCoverage(rgba, cutoff, scale);
        const float error = std::abs(coverage - targetCoverage);
        if (error < bestError) {
            bestError = error;
            bestScale = scale;
        }
        if (coverage < targetCoverage) low = scale;
        else high = scale;
    }
    for (float scale : {low, high}) {
        const float error = std::abs(alphaCoverage(rgba, cutoff, scale) - targetCoverage);
        if (error < bestError) {
            bestError = error;
            bestScale = scale;
        }
    }
    return bestScale;
}

static void uploadModelTexture(int width, int height, const unsigned char* rgba,
                               GLint internalFormat, bool srgb,
                               float alphaCoverageCutoff) {
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (!(alphaCoverageCutoff > 0.0f && alphaCoverageCutoff <= 1.0f)) {
        glGenerateMipmap(GL_TEXTURE_2D);
        return;
    }

    // Standard mipmap averaging shrinks alpha-tested leaves as projected size
    // changes. Preserve the source mask's covered-pixel ratio at each level,
    // and downsample RGB in premultiplied alpha to avoid color fringes at leaf
    // edges when the camera sees the cards obliquely.
    std::vector<unsigned char> source(rgba, rgba + static_cast<size_t>(width) * height * 4);
    const float targetCoverage = alphaCoverage(source, alphaCoverageCutoff);
    int sourceWidth = width;
    int sourceHeight = height;
    int level = 1;
    while (sourceWidth > 1 || sourceHeight > 1) {
        const int mipWidth = std::max(1, sourceWidth / 2);
        const int mipHeight = std::max(1, sourceHeight / 2);
        std::vector<unsigned char> rawMip = downsampleRgba(
            source, sourceWidth, sourceHeight, mipWidth, mipHeight, srgb);
        std::vector<unsigned char> uploadMip = rawMip;
        const float scale = alphaCoverageScale(uploadMip, alphaCoverageCutoff,
                                               targetCoverage);
        for (size_t i = 3; i < uploadMip.size(); i += 4) {
            uploadMip[i] = static_cast<unsigned char>(std::clamp(
                uploadMip[i] * scale + 0.5f, 0.0f, 255.0f));
        }
        glTexImage2D(GL_TEXTURE_2D, level, internalFormat, mipWidth, mipHeight, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, uploadMip.data());
        source = std::move(rawMip);
        sourceWidth = mipWidth;
        sourceHeight = mipHeight;
        ++level;
    }
}

void ObjModel::loadTexture(const std::string& textureFilename, unsigned int& textureID,
                           bool srgb, float alphaCoverageCutoff) {
    if (textureFilename.empty()) {
        textureID = 0;
        return;
    }

    // Try primary path
    std::string path1 = basepath + textureFilename;
    int width, height, nrChannels;
    unsigned char* data = stbi_load(path1.c_str(), &width, &height, &nrChannels, STBI_rgb_alpha);
    std::string loadedPath = path1;

    // Fallback: extract basename and look in local "textures/" or model base directory
    if (!data) {
        std::string basename = textureFilename;
        size_t lastSlashPos = textureFilename.find_last_of("/\\");
        if (lastSlashPos != std::string::npos) {
            basename = textureFilename.substr(lastSlashPos + 1);
        }
        
        std::string path2 = basepath + "textures/" + basename;
        data = stbi_load(path2.c_str(), &width, &height, &nrChannels, STBI_rgb_alpha);
        loadedPath = path2;

        if (!data) {
            std::string path3 = basepath + basename;
            data = stbi_load(path3.c_str(), &width, &height, &nrChannels, STBI_rgb_alpha);
            loadedPath = path3;
        }
    }

    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    configureModelTextureSampling();

    if (data) {
        const GLint internalFormat = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
        uploadModelTexture(width, height, data, internalFormat, srgb,
                           alphaCoverageCutoff);
        std::cout << "Texture loaded successfully: " << loadedPath << std::endl;
    } else {
        std::cerr << "Failed to load texture (tried fallbacks): " << textureFilename << std::endl;
        textureID = 0;
    }
    stbi_image_free(data);
}

void ObjModel::loadTextureFromMemory(const aiTexture* embeddedTexture,
                                     unsigned int& textureID, bool srgb,
                                     float alphaCoverageCutoff) {
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    configureModelTextureSampling();

    int width, height, nrChannels;
    unsigned char* data = nullptr;
    std::vector<unsigned char> rawRGBA;
    bool stbiOwned = false;

    if (embeddedTexture->mHeight == 0) {
        // Compressed PNG/JPEG payload, as used by the scanned GLB.
        data = stbi_load_from_memory(reinterpret_cast<unsigned char*>(embeddedTexture->pcData), 
                                     embeddedTexture->mWidth, &width, &height,
                                     &nrChannels, STBI_rgb_alpha);
        stbiOwned = true;
    } else {
        // Assimp stores uncompressed embedded images as aiTexel BGRA structs,
        // not as PNG/JPEG byte streams.
        width = static_cast<int>(embeddedTexture->mWidth);
        height = static_cast<int>(embeddedTexture->mHeight);
        nrChannels = 4;
        rawRGBA.resize(static_cast<size_t>(width) * height * 4);
        for (int i = 0; i < width * height; ++i) {
            const aiTexel& texel = embeddedTexture->pcData[i];
            rawRGBA[static_cast<size_t>(i) * 4 + 0] = texel.r;
            rawRGBA[static_cast<size_t>(i) * 4 + 1] = texel.g;
            rawRGBA[static_cast<size_t>(i) * 4 + 2] = texel.b;
            rawRGBA[static_cast<size_t>(i) * 4 + 3] = texel.a;
        }
        data = rawRGBA.data();
    }

    if (data) {
        const GLint internalFormat = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
        uploadModelTexture(width, height, data, internalFormat, srgb,
                           alphaCoverageCutoff);
        std::cout << "Embedded texture loaded successfully from memory, channels: " << nrChannels << std::endl;
        if (stbiOwned) stbi_image_free(data);
    } else {
        std::cerr << "Failed to load embedded texture from memory." << std::endl;
        textureID = 0;
    }
}


// ===============================
// Material Processing (FIXED: Added color extraction)
// ===============================
void ObjModel::processMaterials() {
    std::cout << "[ObjModel] processMaterials for " << basepath << ": materials=" << scene->mNumMaterials << ", embeddedTextures=" << scene->mNumTextures << std::endl;
    materials.resize(scene->mNumMaterials);

    for (unsigned int i = 0; i < scene->mNumMaterials; i++) {
        aiMaterial* material = scene->mMaterials[i];
        Material& currentMtl = materials[i]; // Use a reference to easily update 
        
        // 1. Get Material Name
        aiString name;
        if (material->Get(AI_MATKEY_NAME, name) == AI_SUCCESS) {
            currentMtl.name = name.C_Str();
        } else {
            currentMtl.name = "default_material_" + std::to_string(i);
        }
        // Keep the source glTF alpha-mask threshold. The scanned plant pack
        // uses cutoffs around 0.08-0.29; the old shader's fixed 0.45 removed
        // thin leaves and grass blades from these textures.
        material->Get(AI_MATKEY_GLTF_ALPHACUTOFF, currentMtl.alphaCutoff);
        currentMtl.alphaCutoff = std::clamp(currentMtl.alphaCutoff, 0.0f, 1.0f);
        const bool scannedFoliagePack =
            filename.find("scanned_plants_and_grass_free_download.glb") != std::string::npos;
        const float alphaCoverageCutoff = scannedFoliagePack
            ? currentMtl.alphaCutoff : -1.0f;
        
        // 2. Get and Load Texture. glTF stores foliage color images as PBR base
        // color textures; older OBJ/FBX assets usually expose diffuse textures.
        aiString texturePath;
        aiReturn textureResult = material->GetTexture(aiTextureType_BASE_COLOR, 0, &texturePath);
        if (textureResult != AI_SUCCESS)
            textureResult = material->GetTexture(aiTextureType_DIFFUSE, 0, &texturePath);
        currentMtl.textureSRGB =
            filename.find("scanned_plants_and_grass_free_download.glb") != std::string::npos;
        if (textureResult == AI_SUCCESS) {
            std::cout << "  Material " << i << " (" << currentMtl.name << ") has color texture: " << texturePath.C_Str() << std::endl;
            const aiTexture* embeddedTexture = scene->GetEmbeddedTexture(texturePath.C_Str());
            if (embeddedTexture) {
                std::cout << "  Detected EMBEDDED texture in ObjModel. Loading from memory..." << std::endl;
                loadTextureFromMemory(embeddedTexture, currentMtl.textureID,
                                      currentMtl.textureSRGB, alphaCoverageCutoff);
            } else {
                loadTexture(texturePath.C_Str(), currentMtl.textureID,
                            currentMtl.textureSRGB, alphaCoverageCutoff);
            }
        } else {
            currentMtl.textureID = 0;
        }

        // 3. Extract Material Colors and Properties (NEW)
        aiColor3D color;
        float fValue;
        
        // Ambient Color (Ka)
        if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_AMBIENT, color)) {
            currentMtl.ambient[0] = color.r;
            currentMtl.ambient[1] = color.g;
            currentMtl.ambient[2] = color.b;
        }

        // Diffuse Color (Kd)
        if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_DIFFUSE, color)) {
            currentMtl.diffuse[0] = color.r;
            currentMtl.diffuse[1] = color.g;
            currentMtl.diffuse[2] = color.b;
        }
        aiColor4D baseColor;
        if (AI_SUCCESS == material->Get(AI_MATKEY_BASE_COLOR, baseColor)) {
            currentMtl.diffuse[0] = baseColor.r;
            currentMtl.diffuse[1] = baseColor.g;
            currentMtl.diffuse[2] = baseColor.b;
            currentMtl.diffuse[3] = baseColor.a;
        }

        // Specular Color (Ks)
        if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_SPECULAR, color)) {
            currentMtl.specular[0] = color.r;
            currentMtl.specular[1] = color.g;
            currentMtl.specular[2] = color.b;
        }

        // Shininess (Ns)
        if (AI_SUCCESS == material->Get(AI_MATKEY_SHININESS, fValue)) {
            currentMtl.shininess = fValue;
            // Assimp uses "power of 2" shininess, but OpenGL uses 0-128. 
            // We'll cap it just in case, though 0-100 is common for .mtl.
            currentMtl.shininess = std::min(currentMtl.shininess, 128.0f);
        }
    }
}
glm::vec3 ObjModel::getNormalAt(float x, float z) {
    
    // --- Sample Points ---
    // Use a small offset to sample the height change around the point (x, z)
    // Adjust 'SAMPLE_OFFSET' based on the scale and resolution of your terrain grid.
    const float SAMPLE_OFFSET = 0.1f; 

    // 1. Get the height at the surrounding points
    // NOTE: This assumes you have a public getHeightAt(float x, float z) method.
    float hL = getHeightAt(x - SAMPLE_OFFSET, z); // Left
    float hR = getHeightAt(x + SAMPLE_OFFSET, z); // Right
    float hD = getHeightAt(x, z - SAMPLE_OFFSET); // Down (Back)
    float hU = getHeightAt(x, z + SAMPLE_OFFSET); // Up (Forward)

    // 2. Create the two tangent vectors that define the slope at (x, z)
    
    // Vector pointing from Left to Right (Tangent on X-axis)
    // The Y-component represents the height difference.
    glm::vec3 tangentX = glm::vec3(2.0f * SAMPLE_OFFSET, hR - hL, 0.0f); 

    // Vector pointing from Back to Forward (Tangent on Z-axis)
    // The Y-component represents the height difference.
    glm::vec3 tangentZ = glm::vec3(0.0f, hU - hD, 2.0f * SAMPLE_OFFSET);

    // 3. Calculate the normal vector (Normal is perpendicular to both tangents)
    // The order of the cross product matters for the direction (Up vs. Down).
    // Using cross(tangentX, tangentZ) gives the standard upward-pointing normal.
    glm::vec3 normal = glm::cross(tangentX, tangentZ);

    // 4. Normalize the result
    if (glm::length2(normal) > 1e-6) { // Safety check against a zero vector (perfectly flat area)
        return glm::normalize(normal);
    }

    // Default to a vertical normal if the area is perfectly flat or calculation failed
    return glm::vec3(0.0f, 1.0f, 0.0f);
}
// ===============================
// Scene Traversal (Kept)
// ===============================
void ObjModel::processNode(aiNode* node, const aiMatrix4x4& parentTransform) {
    const bool normalizeScannedPlants =
        filename.find("scanned_plants_and_grass_free_download.glb") != std::string::npos;
    const aiMatrix4x4 absoluteTransform = parentTransform * node->mTransformation;
    // Process all the node's meshes
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];

        GLenum primitiveMode = 0;
        if (mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE)
            primitiveMode = GL_TRIANGLES;
        else if (mesh->mPrimitiveTypes & aiPrimitiveType_LINE)
            primitiveMode = GL_LINES;
        else if (mesh->mPrimitiveTypes & aiPrimitiveType_POINT)
            primitiveMode = GL_POINTS;
        if (primitiveMode == 0) {
            std::cerr << "[ObjModel] Skipping unsupported primitive mesh: "
                      << mesh->mName.C_Str() << std::endl;
            continue;
        }

        // The scan pack is authored in large source-scene units. Normalize
        // each individual object around its footprint and place its base at Y=0
        // so the existing instance transforms scatter it correctly on terrain.
        // Bake the GLB node hierarchy first: this pack has a non-identity root
        // axis transform, which must be applied before measuring its up axis.
        if (normalizeScannedPlants && mesh->HasPositions() && mesh->mNumVertices > 0) {
            const aiMatrix3x3 linearTransform(absoluteTransform);
            aiMatrix3x3 normalTransform(linearTransform);
            normalTransform.Inverse().Transpose();
            for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
                mesh->mVertices[v] = absoluteTransform * mesh->mVertices[v];
                if (mesh->HasNormals()) {
                    mesh->mNormals[v] = normalTransform * mesh->mNormals[v];
                    mesh->mNormals[v].Normalize();
                }
                if (mesh->HasTangentsAndBitangents()) {
                    mesh->mTangents[v] = linearTransform * mesh->mTangents[v];
                    mesh->mTangents[v].Normalize();
                    mesh->mBitangents[v] = linearTransform * mesh->mBitangents[v];
                    mesh->mBitangents[v].Normalize();
                }
            }

            glm::vec3 sourceMin(std::numeric_limits<float>::max());
            glm::vec3 sourceMax(-std::numeric_limits<float>::max());
            for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
                const aiVector3D& p = mesh->mVertices[v];
                sourceMin = glm::min(sourceMin, glm::vec3(p.x, p.y, p.z));
                sourceMax = glm::max(sourceMax, glm::vec3(p.x, p.y, p.z));
            }
            const glm::vec3 sourceSize = sourceMax - sourceMin;
            const unsigned int materialIndex = mesh->mMaterialIndex;
            const std::string materialName = materialIndex < materials.size()
                ? materials[materialIndex].name : std::string();
            const bool isStandaloneFlowerHead =
                materialName.find("white_flower") != std::string::npos;

            float targetHeight = 0.58f;
            float targetFootprint = 0.85f;
            if (materialName.find("Grass_Blades") != std::string::npos) {
                targetHeight = 0.58f; targetFootprint = 0.90f;
            } else if (materialName.find("Grass_Cloves") != std::string::npos) {
                targetHeight = 0.52f; targetFootprint = 0.82f;
            } else if (materialName.find("5_leaf_ground_plant") != std::string::npos) {
                targetHeight = 0.55f; targetFootprint = 0.85f;
            } else if (materialName.find("Ground_Cover_Bunch") != std::string::npos) {
                targetHeight = 0.70f; targetFootprint = 1.05f;
            } else if (materialName.find("Purple_Flower") != std::string::npos) {
                targetHeight = 0.68f; targetFootprint = 0.85f;
            } else if (materialName.find("dandelion") != std::string::npos) {
                targetHeight = 0.55f; targetFootprint = 0.72f;
            }

            const float sourceHeight = sourceSize.y;
            const float sourceFootprint = std::max(sourceSize.x, sourceSize.z);
            // The 9-vertex white_flower mesh is only a flower head, not a full
            // plant. Preserve its authored dimensions so it cannot inflate
            // into a broad white patch when placed as a scatter variant.
            // Fit the footprint and plant height independently. A uniform fit
            // let a wide, shallow specimen dictate its height, flattening a few
            // grass meshes until they were visible only from overhead.
            float scaleXZ = isStandaloneFlowerHead || sourceFootprint <= 1e-5f
                ? 1.0f : targetFootprint / sourceFootprint;
            float scaleY = isStandaloneFlowerHead || sourceHeight <= 1e-5f
                ? 1.0f : targetHeight / sourceHeight;
            scaleXZ = std::clamp(scaleXZ, 0.005f, 10.0f);
            scaleY = std::clamp(scaleY, 0.005f, 10.0f);

            // Apply the inverse-transpose for normals and the geometry
            // transform for tangents before changing vertex positions.
            for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
                if (mesh->HasNormals()) {
                    aiVector3D& n = mesh->mNormals[v];
                    n.x /= scaleXZ;
                    n.y /= scaleY;
                    n.z /= scaleXZ;
                    n.Normalize();
                }
                if (mesh->HasTangentsAndBitangents()) {
                    aiVector3D& t = mesh->mTangents[v];
                    aiVector3D& b = mesh->mBitangents[v];
                    t.x *= scaleXZ; t.y *= scaleY; t.z *= scaleXZ;
                    b.x *= scaleXZ; b.y *= scaleY; b.z *= scaleXZ;
                    t.Normalize();
                    b.Normalize();
                }
            }

            const float centerX = 0.5f * (sourceMin.x + sourceMax.x);
            const float centerZ = 0.5f * (sourceMin.z + sourceMax.z);
            for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
                aiVector3D& p = mesh->mVertices[v];
                p.x = (p.x - centerX) * scaleXZ;
                p.y = (p.y - sourceMin.y) * scaleY;
                p.z = (p.z - centerZ) * scaleXZ;
            }
        }
        
        Mesh newMesh;
        newMesh.assimpMesh = mesh;
        newMesh.primitiveMode = primitiveMode;
        newMesh.materialIndex = mesh->mMaterialIndex;
        newMesh.name = mesh->mName.C_Str();
        newMesh.nodeName = node->mName.C_Str();
        if (mesh->HasTextureCoords(0) && mesh->mNumVertices > 0) {
            glm::vec2 uvMin(std::numeric_limits<float>::max());
            glm::vec2 uvMax(-std::numeric_limits<float>::max());
            for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
                const aiVector3D& uv = mesh->mTextureCoords[0][v];
                uvMin = glm::min(uvMin, glm::vec2(uv.x, uv.y));
                uvMax = glm::max(uvMax, glm::vec2(uv.x, uv.y));
            }
            const glm::vec2 uvRange = uvMax - uvMin;
            newMesh.hasUsableTexCoords = uvRange.x > 1e-5f || uvRange.y > 1e-5f;
        }

        // Calculate AABB for this mesh
        newMesh.minBound = glm::vec3(std::numeric_limits<float>::max());
        newMesh.maxBound = glm::vec3(-std::numeric_limits<float>::max());
        if (mesh->HasPositions()) {
            for (unsigned int j = 0; j < mesh->mNumVertices; j++) {
                newMesh.minBound.x = std::min(newMesh.minBound.x, mesh->mVertices[j].x);
                newMesh.minBound.y = std::min(newMesh.minBound.y, mesh->mVertices[j].y);
                newMesh.minBound.z = std::min(newMesh.minBound.z, mesh->mVertices[j].z);
                newMesh.maxBound.x = std::max(newMesh.maxBound.x, mesh->mVertices[j].x);
                newMesh.maxBound.y = std::max(newMesh.maxBound.y, mesh->mVertices[j].y);
                newMesh.maxBound.z = std::max(newMesh.maxBound.z, mesh->mVertices[j].z);
            }
        }

        // Extract indices (faces) from Assimp's face structure
        for (unsigned int j = 0; j < mesh->mNumFaces; j++) {
            aiFace face = mesh->mFaces[j];
            for (unsigned int k = 0; k < face.mNumIndices; k++) {
                newMesh.indices.push_back(face.mIndices[k]);
            }
        }
        
        meshes.push_back(newMesh);
    }
    
    // Recurse for all children
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        processNode(node->mChildren[i], absoluteTransform);
    }
}


// ===============================
// Model Loader (Main Constructor)
// ===============================
ObjModel::ObjModel(const std::string& filename) : scene(nullptr), displayList(0), filename(filename) {
    std::cout << "Trying to load model with Assimp: " << filename << std::endl;
    std::cout << "Calling Assimp::Importer::ReadFile..." << std::endl;

    size_t lastSlash = filename.find_last_of("/\\");
    basepath = (lastSlash == std::string::npos) ? "" : filename.substr(0, lastSlash + 1);
 // importer.SetPropertyString(AI_CONFIG_PP_LMW_MATERIAL_PATH, basepath);
    unsigned int importFlags = aiProcess_Triangulate |
                               aiProcess_GenSmoothNormals |
                               aiProcess_CalcTangentSpace |
                               aiProcess_FlipUVs;
    // Keep every scanned GLB mesh separate so all plant variants survive
    // Assimp import. Its node transforms are identity and vertices are
    // node transforms are baked individually in processNode(), preserving all
    // 17 mesh variants without PreTransformVertices merging same-material ones.
    scene = importer.ReadFile(filename, importFlags);
    std::cout << "Assimp::Importer::ReadFile finished." << std::endl;
    
    // Check for errors
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cerr << "ASSIMP FAILED TO LOAD: " << importer.GetErrorString() << std::endl;
        createFallbackCube();
        return;
    }
    
    // 1. Process Materials/Textures/Colors
    std::cout << "Processing materials..." << std::endl;
    processMaterials();
    std::cout << "Materials processed." << std::endl;
    
    // 2. Process Nodes/Meshes
    std::cout << "Processing nodes..." << std::endl;
    processNode(scene->mRootNode, aiMatrix4x4());
    std::cout << "Nodes processed." << std::endl;

    // 3. REMOVED: Inefficient temporary vertex list population
    // The physics/raycasting now iterates over the mesh data directly.

    std::cout << "Successfully loaded model with Assimp. Total meshes: " << meshes.size() << std::endl;

    // 4. Setup OpenGL Display List
    setupDisplayList();

    // 5. Setup GPU Buffers (VBO/VAO)
    setupGPUBuffers();
}

ObjModel::~ObjModel()
{
    if (displayList) {
        glDeleteLists(displayList, 1);
        displayList = 0;
    }
    for (auto& mesh : meshes) {
        if (mesh.VBO) { glDeleteBuffers(1, &mesh.VBO); }
        if (mesh.EBO) { glDeleteBuffers(1, &mesh.EBO); }
        if (mesh.instanceVBO) { glDeleteBuffers(1, &mesh.instanceVBO); }
        if (mesh.VAO) { glDeleteVertexArrays(1, &mesh.VAO); }
    }
}

// ===============================
// Rendering (FIXED: Applies material colors)
// ===============================
void ObjModel::setupDisplayList() {
    displayList = glGenLists(1);
    glNewList(displayList, GL_COMPILE);

    // --- FIX: Force Opaque State ---
    glDisable(GL_BLEND);         // Ensure transparency is OFF for the terrain
    glEnable(GL_DEPTH_TEST);     // Ensure the depth buffer is used correctly
    glDepthMask(GL_TRUE);        // Allow writing to the depth buffer

    glShadeModel(GL_SMOOTH); 

    for (const auto& mesh : meshes) {
        const Material& mtl = getMaterial(mesh.materialIndex);

        glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mtl.ambient);
        glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mtl.diffuse);
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mtl.specular);
        glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, mtl.shininess);

        if (mtl.textureID && mesh.hasUsableTexCoords) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, mtl.textureID);
            
            // --- FIX: Force Texture to be Opaque ---
            // GL_REPLACE ignores the underlying vertex color/alpha
            // Use GL_MODULATE if you want lighting to affect the texture
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        } else {
            glDisable(GL_TEXTURE_2D);
        }

        glBegin(mesh.primitiveMode);
        const aiMesh* aimesh = mesh.assimpMesh;
        for (unsigned int index : mesh.indices) {
            if (aimesh->HasNormals()) {
                const aiVector3D& n = aimesh->mNormals[index];
                glNormal3f(n.x, n.y, n.z);
            }
            if (aimesh->HasTextureCoords(0)) {
                const aiVector3D& t = aimesh->mTextureCoords[0][index]; 
                glTexCoord2f(t.x, t.y); 
            }
            const aiVector3D& v = aimesh->mVertices[index];
            glVertex3f(v.x, v.y, v.z);
        }
        glEnd();
    }

    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);

    glEndList();
}

void ObjModel::createFallbackCube()
{
    // ... (Fallback cube code: kept as is, but glColor is less important if using lighting) ...
    displayList = glGenLists(1);
    glNewList(displayList, GL_COMPILE);

    // Apply a default material for the fallback cube
    GLfloat fallbackAmbient[] = {0.1f, 0.1f, 0.1f, 1.0f};
    GLfloat fallbackSpecular[] = {0.5f, 0.5f, 0.5f, 1.0f};
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, fallbackAmbient);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, fallbackSpecular);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 32.0f);


    glBegin(GL_QUADS);
    // Front face
    glNormal3f(0.0f, 0.0f, 1.0f);
    glColor3f(1.0f, 0.0f, 0.0f); // Red (Diffuse fallback if GL_COLOR_MATERIAL is on)
    glVertex3f(-1.0f, -1.0f,1.0f);
    glVertex3f( 1.0f, -1.0f,1.0f);
    glVertex3f( 1.0f,1.0f,1.0f);
    glVertex3f(-1.0f,1.0f,1.0f);
    // Back face
    glNormal3f(0.0f, 0.0f, -1.0f);
    glColor3f(0.0f, 1.0f, 0.0f); // Green
    glVertex3f(-1.0f, -1.0f, -1.0f);
    glVertex3f(-1.0f,1.0f, -1.0f);
    glVertex3f( 1.0f,1.0f, -1.0f);
    glVertex3f( 1.0f, -1.0f, -1.0f);
    // Left face
    glNormal3f(-1.0f, 0.0f, 0.0f);
    glColor3f(0.0f, 0.0f, 1.0f); // Blue
    glVertex3f(-1.0f, -1.0f, -1.0f);
    glVertex3f(-1.0f, -1.0f,1.0f);
    glVertex3f(-1.0f,1.0f,1.0f);
    glVertex3f(-1.0f,1.0f, -1.0f);
    // Right face
    glNormal3f(1.0f, 0.0f, 0.0f);
    glColor3f(1.0f, 1.0f, 0.0f); // Yellow
    glVertex3f(1.0f, -1.0f, -1.0f);
    glVertex3f(1.0f,1.0f, -1.0f);
    glVertex3f(1.0f,1.0f,1.0f);
    glVertex3f(1.0f, -1.0f,1.0f);
    // Top face
    glNormal3f(0.0f, 1.0f, 0.0f);
    glColor3f(1.0f, 0.0f, 1.0f); // Magenta
    glVertex3f(-1.0f,1.0f, -1.0f);
    glVertex3f(-1.0f,1.0f,1.0f);
    glVertex3f( 1.0f,1.0f,1.0f);
    glVertex3f( 1.0f,1.0f, -1.0f);
    // Bottom face
    glNormal3f(0.0f, -1.0f, 0.0f);
    glColor3f(0.0f, 1.0f, 1.0f); // Cyan
    glVertex3f(-1.0f, -1.0f, -1.0f);
    glVertex3f( 1.0f, -1.0f, -1.0f);
    glVertex3f( 1.0f, -1.0f,1.0f);
    glVertex3f(-1.0f, -1.0f,1.0f);
    glEnd();

    glEndList();
}


void ObjModel::render() const {
    if (displayList) glCallList(displayList);
}

// ===============================
// Height Query (Kept the correct Assimp adaptation)
// ===============================
float ObjModel::getHeightAt(float x, float z) const {
    float maxHeight = -std::numeric_limits<float>::max();
    Vec3 rayOrigin = { x, 1000.0f, z };
    Vec3 rayDir = { 0.0f, -1.0f, 0.0f };
    
    // Iterate over ALL meshes
    for (const auto& mesh : meshes) {
        if (mesh.primitiveMode != GL_TRIANGLES) continue;
        // Optimization: Skip meshes whose AABB doesn't contain (x, z)
        if (x < mesh.minBound.x || x > mesh.maxBound.x || z < mesh.minBound.z || z > mesh.maxBound.z) {
            continue;
        }

        const aiMesh* aimesh = mesh.assimpMesh;
        if (!aimesh || !aimesh->HasPositions()) continue;

        // Iterate through indices 3 at a time (since they are guaranteed to be triangles)
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            unsigned int idx0 = mesh.indices[i];
            unsigned int idx1 = mesh.indices[i+1];
            unsigned int idx2 = mesh.indices[i+2];

            const aiVector3D& av0 = aimesh->mVertices[idx0];
            const aiVector3D& av1 = aimesh->mVertices[idx1];
            const aiVector3D& av2 = aimesh->mVertices[idx2];
            
            Vec3 v0 = {av0.x, av0.y, av0.z};
            Vec3 v1 = {av1.x, av1.y, av1.z};
            Vec3 v2 = {av2.x, av2.y, av2.z};

            float t = 0.0f;
            if (rayTriangleIntersect(rayOrigin, rayDir, v0, v1, v2, t)) {
                float intersectionY = rayOrigin.y + t * rayDir.y;
                if (intersectionY > maxHeight) {
                    maxHeight = intersectionY;
                }
            }
        }
    }
    
    return (maxHeight == -std::numeric_limits<float>::max()) ? 0.0f : maxHeight;
}

// ===============================
// Modern Shader-Based Rendering
// ===============================
void ObjModel::render(Shader& shader, unsigned int overrideTextureID) const {
    for (const auto& mesh : meshes) {
        const Material& mtl = getMaterial(mesh.materialIndex);
        shader.setFloat("alphaCutoff", mtl.alphaCutoff);
        
        shader.setVec3("diffuseColor", glm::vec3(mtl.diffuse[0], mtl.diffuse[1], mtl.diffuse[2]));
        
        unsigned int texID = mesh.hasUsableTexCoords
            ? (overrideTextureID ? overrideTextureID : mtl.textureID) : 0;
        if (texID) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texID);
            shader.setInt("texture_diffuse1", 0);
            shader.setBool("hasTexture", true);
            shader.setBool("textureIsSRGB", !overrideTextureID && mtl.textureSRGB);
        } else {
            shader.setBool("hasTexture", false);
            shader.setBool("textureIsSRGB", false);
        }
        
        if (mtl.normalMapID) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, mtl.normalMapID);
            shader.setInt("texture_normal1", 1);
            shader.setBool("hasNormalMap", true);
        } else {
            shader.setBool("hasNormalMap", false);
        }
        
        // Use GPU buffers if available, else fallback to display list per-mesh
        if (mesh.VAO != 0) {
            glBindVertexArray(mesh.VAO);
            glDrawElements(mesh.primitiveMode, mesh.indexCount, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
        } else {
            // Fallback: immediate mode per mesh
            const aiMesh* aimesh = mesh.assimpMesh;
            if (!aimesh) continue;
            
            glBegin(mesh.primitiveMode);
            for (unsigned int index : mesh.indices) {
                if (aimesh->HasNormals()) {
                    const aiVector3D& n = aimesh->mNormals[index];
                    glNormal3f(n.x, n.y, n.z);
                }
                if (aimesh->HasTextureCoords(0)) {
                    const aiVector3D& t = aimesh->mTextureCoords[0][index];
                    glTexCoord2f(t.x, t.y);
                }
                const aiVector3D& v = aimesh->mVertices[index];
                glVertex3f(v.x, v.y, v.z);
            }
            glEnd();
        }
    }
}

void ObjModel::renderMesh(int meshIndex, Shader& shader) const {
    if (meshIndex < 0 || meshIndex >= (int)meshes.size()) return;
    const auto& mesh = meshes[meshIndex];
    const Material& mtl = getMaterial(mesh.materialIndex);
    shader.setFloat("alphaCutoff", mtl.alphaCutoff);
    
    shader.setVec3("diffuseColor", glm::vec3(mtl.diffuse[0], mtl.diffuse[1], mtl.diffuse[2]));
    
    if (mtl.textureID && mesh.hasUsableTexCoords) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, mtl.textureID);
        shader.setInt("texture_diffuse1", 0);
        shader.setBool("hasTexture", true);
        shader.setBool("textureIsSRGB", mtl.textureSRGB);
    } else {
        shader.setBool("hasTexture", false);
        shader.setBool("textureIsSRGB", false);
    }
    
    if (mtl.normalMapID) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, mtl.normalMapID);
        shader.setInt("texture_normal1", 1);
        shader.setBool("hasNormalMap", true);
    } else {
        shader.setBool("hasNormalMap", false);
    }
    
    if (mesh.VAO != 0) {
        glBindVertexArray(mesh.VAO);
        glDrawElements(mesh.primitiveMode, mesh.indexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    } else {
        const aiMesh* aimesh = mesh.assimpMesh;
        if (!aimesh) return;
        
        glBegin(mesh.primitiveMode);
        for (unsigned int index : mesh.indices) {
            if (aimesh->HasNormals()) {
                const aiVector3D& n = aimesh->mNormals[index];
                glNormal3f(n.x, n.y, n.z);
            }
            if (aimesh->HasTextureCoords(0)) {
                const aiVector3D& t = aimesh->mTextureCoords[0][index];
                glTexCoord2f(t.x, t.y);
            }
            const aiVector3D& v = aimesh->mVertices[index];
            glVertex3f(v.x, v.y, v.z);
        }
        glEnd();
    }
}

void ObjModel::renderInstanced(Shader& shader, const std::vector<glm::mat4>& instanceMatrices,
                               bool forceUpdate,
                               std::function<float(float, float)> riparianAt,
                               glm::vec3 cameraPosition) const {
    if (instanceMatrices.empty()) return;

    bool isGrassPack = (filename.find("grass") != std::string::npos ||
                        filename.find("Grass") != std::string::npos) &&
                       (meshes.size() > 1);
    const bool isScannedPlantPack =
        filename.find("scanned_plants_and_grass_free_download.glb") != std::string::npos;

    if (isGrassPack) {
        // Keep detailed scanned specimens for a capped number of nearby
        // accents; the ordinary scattered field uses the lighter variants.
        // Per-model state, keyed by filename. These used to be plain
        // function-local statics, which meant they were computed once for the
        // first grass-pack model and then silently reused for every later one:
        // mesh indices no longer matched that model's mesh array (out-of-range
        // read), and an instance count that happened to equal the previous
        // model's skipped the buffer upload, leaving the model invisible.
        struct GrassVariant {
            int meshIndex = -1;
            float baseWeight = 1.0f;
            float riparianWeight = 0.0f;
            float scale = 1.0f;
            bool highDetail = false;
            bool componentOnly = false;
            std::string family;
        };
        static std::unordered_map<std::string, std::vector<GrassVariant>> variantsByFile;
        std::vector<GrassVariant>& grassVariants = variantsByFile[filename];
        if (grassVariants.empty()) {
            int totalMeshes = (int)meshes.size();
            int triBudget = isScannedPlantPack ? 60000 : 1000;
            if (!isScannedPlantPack) {
                if (const char* e = std::getenv("FALLAGA_GRASS_TRIS")) {
                    int v = atoi(e); if (v > 0) triBudget = v;
                }
            }
            // The supplied GLB is a curated 17-mesh pack; register every mesh
            // as a variant. High-triangle meshes are still limited to nearby
            // representatives below, so this does not multiply their cost
            // across the full scatter field.
            int maxVariants = isScannedPlantPack ? totalMeshes : 12;
            // Cache cheap species metadata once per model. Repeatedly searching
            // mesh names for every instance caused avoidable work on camera moves.
            auto appendVariant = [&](int mi) {
                GrassVariant variant;
                variant.meshIndex = mi;
                const std::string& name = isScannedPlantPack
                    ? getMaterial(meshes[mi].materialIndex).name : meshes[mi].name;
                variant.family = name;
                variant.highDetail = isScannedPlantPack &&
                                     meshes[mi].primitiveMode == GL_TRIANGLES &&
                                     meshes[mi].indexCount / 3 > 1000;
                variant.componentOnly = isScannedPlantPack &&
                    name.find("white_flower") != std::string::npos;
                if (isScannedPlantPack) {
                    // The scanned GLB names meshes Object_0..Object_16. Its
                    // material names carry the actual vegetation category.
                    if (name.find("Grass_Blades") != std::string::npos) {
                        variant.baseWeight = 4.0f;
                        variant.riparianWeight = 1.0f;
                        variant.scale = 1.0f;
                    } else if (name.find("Grass_Cloves") != std::string::npos) {
                        variant.baseWeight = 1.2f;
                        variant.riparianWeight = 2.0f;
                        variant.scale = 0.95f;
                    } else if (name.find("5_leaf_ground_plant") != std::string::npos) {
                        variant.baseWeight = 0.65f;
                        variant.riparianWeight = 1.1f;
                        variant.scale = 0.90f;
                    } else if (name.find("Ground_Cover_Bunch") != std::string::npos) {
                        variant.baseWeight = 0.85f;
                        variant.riparianWeight = 1.45f;
                        variant.scale = 0.92f;
                    } else if (name.find("Purple_Flower") != std::string::npos) {
                        variant.baseWeight = 0.45f;
                        variant.riparianWeight = 0.35f;
                        variant.scale = 0.95f;
                    } else if (name.find("dandelion") != std::string::npos) {
                        variant.baseWeight = 0.55f;
                        variant.riparianWeight = 0.45f;
                        variant.scale = 0.95f;
                    } else if (name.find("white_flower") != std::string::npos) {
                        variant.baseWeight = 0.35f;
                        variant.riparianWeight = 0.3f;
                        variant.scale = 0.95f;
                    }
                } else if (name.find("Grass_Blades_Bunch") != std::string::npos) {
                    variant.baseWeight = 5.0f;
                    variant.riparianWeight = 1.0f;
                    variant.scale = 0.85f;
                } else if (name.find("Grass_Blades.001") != std::string::npos) {
                    variant.baseWeight = 2.2f;
                    variant.riparianWeight = 1.0f;
                    variant.scale = 1.25f;
                } else if (name.find("Grass_Blades") != std::string::npos) {
                    variant.baseWeight = 2.2f;
                    variant.riparianWeight = 1.0f;
                    variant.scale = 1.10f;
                } else if (name.find("Ground_Cloves") != std::string::npos) {
                    variant.baseWeight = 0.8f;
                    variant.riparianWeight = 2.4f;
                    variant.scale = 1.15f;
                } else if (name.find("5_leaf") != std::string::npos) {
                    variant.baseWeight = 0.55f;
                    variant.riparianWeight = 1.4f;
                    variant.scale = 0.65f;
                } else if (name.find("dandelion") != std::string::npos) {
                    variant.baseWeight = 0.75f;
                    variant.riparianWeight = 0.45f;
                    variant.scale = 0.60f;
                }
                grassVariants.push_back(variant);
            };
            // Keep every non-empty scan mesh eligible. Loose flower heads stay
            // in the pack and are attached to complete flowering plants below.
            for (int mi = 0; mi < totalMeshes; ++mi) {
                const Mesh& candidate = meshes[mi];
                const int primitiveCount = candidate.primitiveMode == GL_TRIANGLES
                    ? static_cast<int>(candidate.indexCount / 3)
                    : candidate.primitiveMode == GL_LINES
                        ? static_cast<int>(candidate.indexCount / 2)
                        : static_cast<int>(candidate.indexCount);
                const int tris = candidate.primitiveMode == GL_TRIANGLES
                    ? primitiveCount : 0;
                if (primitiveCount > 0 &&
                    (isScannedPlantPack || tris <= triBudget) &&
                    (int)grassVariants.size() < maxVariants) {
                    appendVariant(mi);
                }
            }
            if (grassVariants.empty()) {
                for (int i = 0; i < totalMeshes; ++i) appendVariant(i);
            }
            if (isScannedPlantPack) {
                // Several scan meshes are alternate specimens of one material
                // family. Divide family weight across those specimens so the
                // large Ground_Cover_Bunch family does not drown out flowers.
                std::unordered_map<std::string, int> familyCounts;
                for (const GrassVariant& variant : grassVariants) {
                    if (variant.componentOnly) continue;
                    const std::string key = variant.family +
                        (variant.highDetail ? "#detail" : "#regular");
                    ++familyCounts[key];
                }
                for (GrassVariant& variant : grassVariants) {
                    if (variant.componentOnly) continue;
                    const std::string key = variant.family +
                        (variant.highDetail ? "#detail" : "#regular");
                    const float divisor = static_cast<float>(
                        std::max(1, familyCounts[key]));
                    variant.baseWeight /= divisor;
                    variant.riparianWeight /= divisor;
                }
            }
            size_t totalPrimitives = 0;
            for (const GrassVariant& variant : grassVariants) {
                const Mesh& selected = meshes[variant.meshIndex];
                totalPrimitives += selected.primitiveMode == GL_TRIANGLES
                    ? selected.indexCount / 3
                    : selected.primitiveMode == GL_LINES
                        ? selected.indexCount / 2
                        : selected.indexCount;
            }
            std::cout << "[GrassOptim] Selected " << grassVariants.size()
                      << " of " << totalMeshes << " meshes"
                      << (isScannedPlantPack ? " (all specimens eligible; flower heads become accents)."
                                             : " within the configured triangle budget.")
                      << " primitives/mesh-pack=" << totalPrimitives
                      << " (avg " << (totalPrimitives / std::max<size_t>(1, grassVariants.size())) << " primitives/mesh). Objects:";
            for (const GrassVariant& variant : grassVariants)
                std::cout << " " << meshes[variant.meshIndex].name;
            std::cout << std::endl;
        }

        static std::unordered_map<std::string, size_t> lastCountByFile;
        size_t& lastGrassTotalCount = lastCountByFile[filename];
        bool grassNeedsUpdate = forceUpdate || (instanceMatrices.size() != lastGrassTotalCount);

        if (grassNeedsUpdate) {
            // Group instances into buckets by deterministic weighted hash.
            static std::unordered_map<std::string, std::vector<std::vector<glm::mat4>>> meshByFile;
            std::vector<std::vector<glm::mat4>>& meshInstances = meshByFile[filename];
            meshInstances.resize(grassVariants.size());
            for (auto& v : meshInstances) v.clear();
            std::vector<float> cumulativeWeights(grassVariants.size(), 0.0f);
            std::vector<size_t> regularBuckets;
            std::vector<size_t> detailBuckets;
            size_t flowerHeadBucket = std::numeric_limits<size_t>::max();
            for (size_t i = 0; i < grassVariants.size(); ++i) {
                if (grassVariants[i].componentOnly) {
                    flowerHeadBucket = i;
                    continue;
                }
                (grassVariants[i].highDetail ? detailBuckets : regularBuckets).push_back(i);
            }
            if (regularBuckets.empty()) regularBuckets = detailBuckets;
            const auto speciesHash01 = [](const glm::vec3& pos) {
                const std::uint32_t x = static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(std::floor(pos.x * 4.0f)));
                const std::uint32_t z = static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(std::floor(pos.z * 4.0f)));
                std::uint32_t h = x * 0x8DA6B343u ^ z * 0xD8163841u;
                h ^= h >> 16;
                h *= 0x7FEB352Du;
                h ^= h >> 15;
                h *= 0x846CA68Bu;
                h ^= h >> 16;
                return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
            };

            const auto addFlowerHeadAccent = [&](const glm::vec3& plantPos,
                                                  size_t plantBucket,
                                                  const glm::mat4& plantMatrix) {
                if (!isScannedPlantPack ||
                    flowerHeadBucket == std::numeric_limits<size_t>::max())
                    return;
                const std::string& family = grassVariants[plantBucket].family;
                if (family.find("dandelion") == std::string::npos &&
                    family.find("Purple_Flower") == std::string::npos)
                    return;

                // This 9-vertex source mesh is a blossom without a stem. Place
                // it on a small share of complete flowering plants instead of
                // scattering a flat flower head directly on the soil.
                if (speciesHash01(plantPos + glm::vec3(73.0f, 0.0f, -41.0f)) > 0.12f)
                    return;

                const Mesh& plantMesh = meshes[grassVariants[plantBucket].meshIndex];
                const float plantUpScale = glm::length(glm::vec3(plantMatrix[1]));
                glm::mat4 headMatrix = plantMatrix;
                headMatrix[3].y += plantMesh.maxBound.y * plantUpScale * 0.88f;
                const float headScale = grassVariants[flowerHeadBucket].scale;
                headMatrix[0] *= headScale;
                headMatrix[1] *= headScale;
                headMatrix[2] *= headScale;
                meshInstances[flowerHeadBucket].push_back(headMatrix);
            };

            // Hero specimens: a FIXED subset of instances chosen by world-position
            // hash, so the choice never changes when the camera moves.
            const float heroRadiusSquared = 90.0f * 90.0f;
            const float heroFraction = 0.02f;
            const size_t noBucket = std::numeric_limits<size_t>::max();
            std::vector<size_t> detailBucketForInstance(instanceMatrices.size(), noBucket);

            if (isScannedPlantPack && !detailBuckets.empty()) {
                std::vector<size_t> visibleBuckets;
                visibleBuckets.reserve(grassVariants.size());
                for (size_t i = 0; i < std::max(detailBuckets.size(), regularBuckets.size()); ++i) {
                    if (i < detailBuckets.size()) visibleBuckets.push_back(detailBuckets[i]);
                    if (i < regularBuckets.size()) visibleBuckets.push_back(regularBuckets[i]);
                }
                for (size_t i = 0; i < instanceMatrices.size(); ++i) {
                    const glm::vec3 pos(instanceMatrices[i][3]);
                    const glm::vec3 delta = pos - cameraPosition;
                    if (glm::dot(delta, delta) > heroRadiusSquared) continue;
                    if (speciesHash01(pos + glm::vec3(31.0f, 0.0f, 17.0f)) >= heroFraction) continue;
                    const float pick = speciesHash01(pos + glm::vec3(-53.0f, 0.0f, 89.0f));
                    detailBucketForInstance[i] = visibleBuckets[std::min(
                        visibleBuckets.size() - 1,
                        static_cast<size_t>(pick * static_cast<float>(visibleBuckets.size())))];
                }
            }

            for (size_t instanceIndex = 0; instanceIndex < instanceMatrices.size(); ++instanceIndex) {
                const auto& mat = instanceMatrices[instanceIndex];
                glm::vec3 pos(mat[3]);
                const size_t selectedDetailBucket = detailBucketForInstance[instanceIndex];
                if (selectedDetailBucket != noBucket) {
                    glm::mat4 speciesMatrix = mat;
                    const float speciesScale = grassVariants[selectedDetailBucket].scale;
                    if (speciesScale != 1.0f) {
                        speciesMatrix[0] *= speciesScale;
                        speciesMatrix[1] *= speciesScale;
                        speciesMatrix[2] *= speciesScale;
                    }
                    meshInstances[selectedDetailBucket].push_back(speciesMatrix);
                    addFlowerHeadAccent(pos, selectedDetailBucket, speciesMatrix);
                    continue;
                }

                const float riparian = riparianAt
                    ? std::clamp(riparianAt(pos.x, pos.z), 0.0f, 1.0f) : 0.0f;
                float weightTotal = 0.0f;
                for (size_t bucketIndex = 0; bucketIndex < regularBuckets.size(); ++bucketIndex) {
                    const GrassVariant& variant = grassVariants[regularBuckets[bucketIndex]];
                    const float weight = variant.baseWeight +
                                         variant.riparianWeight * riparian;
                    weightTotal += weight;
                    cumulativeWeights[bucketIndex] = weightTotal;
                }
                // Fractional parts of large world-space floats have poor
                // precision and clustered values, which underused some of the
                // 12 supplied species. Hash quantized coordinates to a uniform,
                // reproducible 24-bit choice instead.
                const float unit = speciesHash01(pos);
                const auto pick = std::lower_bound(
                    cumulativeWeights.begin(), cumulativeWeights.begin() + regularBuckets.size(),
                    unit * weightTotal);
                int selectableBucket = static_cast<int>(
                    pick - cumulativeWeights.begin());
                selectableBucket = std::clamp(selectableBucket, 0,
                    static_cast<int>(regularBuckets.size()) - 1);
                size_t bucket = regularBuckets[selectableBucket];
                const float speciesScale = grassVariants[bucket].scale;
                glm::mat4 speciesMatrix = mat;
                if (speciesScale != 1.0f) {
                    speciesMatrix[0] *= speciesScale;
                    speciesMatrix[1] *= speciesScale;
                    speciesMatrix[2] *= speciesScale;
                }
                meshInstances[bucket].push_back(speciesMatrix);
                addFlowerHeadAccent(pos, bucket, speciesMatrix);

            }

            for (size_t bIdx = 0; bIdx < grassVariants.size(); ++bIdx) {
                const auto& matricesForMesh = meshInstances[bIdx];
                int mIdx = grassVariants[bIdx].meshIndex;
                const auto& mesh = meshes[mIdx];
                if (mesh.VAO == 0) continue;

                auto& mutableMesh = const_cast<Mesh&>(mesh);
                if (matricesForMesh.empty()) {
                    mutableMesh.lastUploadedSize = 0;
                    continue;
                }

                glBindVertexArray(mesh.VAO);

                if (mutableMesh.instanceVBO == 0) {
                    glGenBuffers(1, &mutableMesh.instanceVBO);
                    glBindBuffer(GL_ARRAY_BUFFER, mutableMesh.instanceVBO);
                    glBufferData(GL_ARRAY_BUFFER, matricesForMesh.size() * sizeof(glm::mat4), matricesForMesh.data(), GL_STATIC_DRAW);
                    for (unsigned int i = 0; i < 4; i++) {
                        glEnableVertexAttribArray(3 + i);
                        glVertexAttribPointer(3 + i, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4), (void*)(i * sizeof(glm::vec4)));
                        glVertexAttribDivisor(3 + i, 1);
                    }
                } else {
                    glBindBuffer(GL_ARRAY_BUFFER, mutableMesh.instanceVBO);
                    glBufferData(GL_ARRAY_BUFFER, matricesForMesh.size() * sizeof(glm::mat4), matricesForMesh.data(), GL_STATIC_DRAW);
                }
                mutableMesh.lastUploadedSize = matricesForMesh.size();
            }
            lastGrassTotalCount = instanceMatrices.size();
        }

        shader.setBool("isInstanced", true);
        shader.setFloat("grassLod", 1.0f);
        shader.setBool("hasTexture2", false);

        // Grass blades are authored with a consistent winding, so back-face
        // culling can roughly halve the fragmented area (and thus fill-rate
        // load) versus the engine-wide two-sided default. Measured gain is only
        // ~15%, and the pack's winding is not guaranteed, so this is opt-in via
        // FALLAGA_GRASS_CULL=1 to avoid silently dropping faces.
        static const bool grassCull = [](){ const char* e = std::getenv("FALLAGA_GRASS_CULL"); return e && atoi(e) != 0; }();
        const bool cullWasEnabled = glIsEnabled(GL_CULL_FACE) == GL_TRUE;
        GLint previousCullMode = GL_BACK;
        glGetIntegerv(GL_CULL_FACE_MODE, &previousCullMode);
        if (isScannedPlantPack) {
            // Every scanned GLB material is double-sided. Never apply the
            // optional back-face optimization to these cards: their authored
            // winding varies between specimens, so it makes them vanish while
            // orbiting the camera.
            glDisable(GL_CULL_FACE);
        } else if (grassCull) {
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
        }

        for (size_t bIdx = 0; bIdx < grassVariants.size(); ++bIdx) {
            int mIdx = grassVariants[bIdx].meshIndex;
            const auto& mesh = meshes[mIdx];
            if (mesh.VAO == 0 || mesh.instanceVBO == 0 || mesh.lastUploadedSize == 0) continue;

            const Material& mtl = getMaterial(mesh.materialIndex);
            shader.setFloat("alphaCutoff", mtl.alphaCutoff);
            glm::vec3 diffuseColor(mtl.diffuse[0], mtl.diffuse[1], mtl.diffuse[2]);
            if (isScannedPlantPack && !mesh.hasUsableTexCoords &&
                mtl.name.find("Grass_Blades") != std::string::npos) {
                // Object_2 has collapsed UVs; sample no texture and give the
                // small untextured blade mesh a foliage-colored fallback.
                diffuseColor = glm::vec3(0.24f, 0.42f, 0.12f);
            }
            shader.setVec3("diffuseColor", diffuseColor);

            if (mtl.textureID && mesh.hasUsableTexCoords) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, mtl.textureID);
                shader.setInt("texture_diffuse1", 0);
                shader.setBool("hasTexture", true);
                shader.setBool("textureIsSRGB", mtl.textureSRGB);
            } else {
                shader.setBool("hasTexture", false);
                shader.setBool("textureIsSRGB", false);
            }
            shader.setBool("hasNormalMap", false);

            glBindVertexArray(mesh.VAO);
            glDrawElementsInstanced(mesh.primitiveMode, mesh.indexCount, GL_UNSIGNED_INT, 0, (GLsizei)mesh.lastUploadedSize);
            glBindVertexArray(0);
        }

        if (isScannedPlantPack || grassCull) {
            glCullFace(static_cast<GLenum>(previousCullMode));
            if (cullWasEnabled) glEnable(GL_CULL_FACE);
            else glDisable(GL_CULL_FACE);
        }

        shader.setBool("isInstanced", false);
        shader.setFloat("grassLod", 0.0f);

    } else {
        shader.setBool("isInstanced", true);

        for (const auto& mesh : meshes) {
            if (mesh.VAO == 0) continue;

            auto& mutableMesh = const_cast<Mesh&>(mesh);
            glBindVertexArray(mesh.VAO);

            if (mutableMesh.instanceVBO == 0) {
                glGenBuffers(1, &mutableMesh.instanceVBO);
                glBindBuffer(GL_ARRAY_BUFFER, mutableMesh.instanceVBO);
                glBufferData(GL_ARRAY_BUFFER, instanceMatrices.size() * sizeof(glm::mat4), instanceMatrices.data(), GL_STATIC_DRAW);
                for (unsigned int i = 0; i < 4; i++) {
                    glEnableVertexAttribArray(3 + i);
                    glVertexAttribPointer(3 + i, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4), (void*)(i * sizeof(glm::vec4)));
                    glVertexAttribDivisor(3 + i, 1);
                }
                mutableMesh.lastUploadedSize = instanceMatrices.size();
            } else if (forceUpdate || instanceMatrices.size() != mutableMesh.lastUploadedSize) {
                glBindBuffer(GL_ARRAY_BUFFER, mutableMesh.instanceVBO);
                glBufferData(GL_ARRAY_BUFFER, instanceMatrices.size() * sizeof(glm::mat4), instanceMatrices.data(), GL_STATIC_DRAW);
                mutableMesh.lastUploadedSize = instanceMatrices.size();
            }

            const Material& mtl = getMaterial(mesh.materialIndex);
            shader.setFloat("alphaCutoff", mtl.alphaCutoff);
            shader.setVec3("diffuseColor", glm::vec3(mtl.diffuse[0], mtl.diffuse[1], mtl.diffuse[2]));

            if (mtl.textureID && mesh.hasUsableTexCoords) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, mtl.textureID);
                shader.setInt("texture_diffuse1", 0);
                shader.setBool("hasTexture", true);
                shader.setBool("textureIsSRGB", mtl.textureSRGB);
            } else {
                shader.setBool("hasTexture", false);
                shader.setBool("textureIsSRGB", false);
            }

            if (mtl.normalMapID) {
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, mtl.normalMapID);
                shader.setInt("texture_normal1", 1);
                shader.setBool("hasNormalMap", true);
            } else {
                shader.setBool("hasNormalMap", false);
            }

            glDrawElementsInstanced(mesh.primitiveMode, mesh.indexCount, GL_UNSIGNED_INT, 0, (GLsizei)mutableMesh.lastUploadedSize);
            glBindVertexArray(0);
        }

        shader.setBool("isInstanced", false);
    }
}

void ObjModel::setupGPUBuffers() {
    // Setup modern GPU buffers for each mesh
    for (auto& mesh : meshes) {
        const aiMesh* aimesh = mesh.assimpMesh;
        if (!aimesh || !aimesh->HasPositions()) continue;
        
        struct GPUVertex {
            glm::vec3 position;
            glm::vec3 normal;
            glm::vec2 texcoord;
            glm::vec3 tangent;
        };
        
        std::vector<GPUVertex> gpuVerts(aimesh->mNumVertices);
        for (unsigned int i = 0; i < aimesh->mNumVertices; ++i) {
            gpuVerts[i].position = glm::vec3(aimesh->mVertices[i].x, aimesh->mVertices[i].y, aimesh->mVertices[i].z);
            gpuVerts[i].normal = aimesh->HasNormals() ? glm::vec3(aimesh->mNormals[i].x, aimesh->mNormals[i].y, aimesh->mNormals[i].z) : glm::vec3(0, 1, 0);
            gpuVerts[i].texcoord = aimesh->HasTextureCoords(0) ? glm::vec2(aimesh->mTextureCoords[0][i].x, aimesh->mTextureCoords[0][i].y) : glm::vec2(0);
            gpuVerts[i].tangent = aimesh->HasTangentsAndBitangents() ? glm::vec3(aimesh->mTangents[i].x, aimesh->mTangents[i].y, aimesh->mTangents[i].z) : glm::vec3(1, 0, 0);
        }
        
        glGenVertexArrays(1, &mesh.VAO);
        glGenBuffers(1, &mesh.VBO);
        glGenBuffers(1, &mesh.EBO);
        
        glBindVertexArray(mesh.VAO);
        
        glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
        glBufferData(GL_ARRAY_BUFFER, gpuVerts.size() * sizeof(GPUVertex), gpuVerts.data(), GL_STATIC_DRAW);
        
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(unsigned int), mesh.indices.data(), GL_STATIC_DRAW);
        mesh.indexCount = (unsigned int)mesh.indices.size();
        
        // Position
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GPUVertex), (void*)0);
        // Normal
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GPUVertex), (void*)offsetof(GPUVertex, normal));
        // TexCoord
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(GPUVertex), (void*)offsetof(GPUVertex, texcoord));
        // Tangent
        glEnableVertexAttribArray(7);
        glVertexAttribPointer(7, 3, GL_FLOAT, GL_FALSE, sizeof(GPUVertex), (void*)offsetof(GPUVertex, tangent));
        
        glBindVertexArray(0);
    }
}

bool ObjModel::getNodeTransform(const std::string& name, glm::mat4& outTransform) const {
    const aiNode* node = findNode(name);
    if (!node) return false;
    outTransform = getNodeAbsoluteTransform(node);
    return true;
}

bool ObjModel::checkSphereCollision(const glm::vec3& centerLocal, float radiusLocal, glm::vec3& outNormalLocal, float& outDepthLocal) const {
    // Sphere vs mesh AABB. If the sphere center is inside the box, choose the
    // nearest face instead of returning an arbitrary upward normal; that lets
    // callers depenetrate sideways from walls and buildings.
    for (const auto& mesh : meshes) {
        glm::vec3 closest = glm::clamp(centerLocal, mesh.minBound, mesh.maxBound);
        glm::vec3 diff = centerLocal - closest;
        float distSq = glm::dot(diff, diff);
        if (distSq < radiusLocal * radiusLocal) {
            float dist = sqrtf(distSq);
            if (dist > 0.001f) {
                outNormalLocal = diff / dist;
                outDepthLocal = radiusLocal - dist;
            } else {
                const float faceDistance[6] = {
                    centerLocal.x - mesh.minBound.x,
                    mesh.maxBound.x - centerLocal.x,
                    centerLocal.y - mesh.minBound.y,
                    mesh.maxBound.y - centerLocal.y,
                    centerLocal.z - mesh.minBound.z,
                    mesh.maxBound.z - centerLocal.z};
                int face = 0;
                for (int i = 1; i < 6; ++i)
                    if (faceDistance[i] < faceDistance[face]) face = i;
                static const glm::vec3 faceNormals[6] = {
                    glm::vec3(-1, 0, 0), glm::vec3(1, 0, 0),
                    glm::vec3(0, -1, 0), glm::vec3(0, 1, 0),
                    glm::vec3(0, 0, -1), glm::vec3(0, 0, 1)};
                outNormalLocal = faceNormals[face];
                outDepthLocal = radiusLocal + faceDistance[face];
            }
            return true;
        }
    }
    return false;
}

bool ObjModel::checkSphereCollisionWorld(const glm::vec3& centerWorld, float radiusWorld, const glm::mat4& modelMatrix, glm::vec3& outNormalWorld, float& outDepthWorld) const {
    glm::mat4 invModel = glm::inverse(modelMatrix);
    glm::vec3 centerLocal = glm::vec3(invModel * glm::vec4(centerWorld, 1.0f));
    
    // Approximate local radius (assumes uniform scale)
    float scaleApprox = glm::length(glm::vec3(modelMatrix[0]));
    float radiusLocal = (scaleApprox > 0.001f) ? radiusWorld / scaleApprox : radiusWorld;
    
    glm::vec3 normalLocal;
    float depthLocal;
    if (checkSphereCollision(centerLocal, radiusLocal, normalLocal, depthLocal)) {
        // Transform normal back to world space
        outNormalWorld = glm::normalize(glm::vec3(glm::transpose(invModel) * glm::vec4(normalLocal, 0.0f)));
        outDepthWorld = depthLocal * scaleApprox;
        return true;
    }
    return false;
}

void ObjModel::computeVertexNormals(const std::vector<Face>& faces) {
    // Stub — vertex normals are computed by Assimp
}
