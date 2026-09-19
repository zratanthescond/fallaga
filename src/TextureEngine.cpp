#include "TextureEngine.h"
#include <stb_image.h>
#include <iostream>
#include <cmath>
#include <algorithm>

GLuint TextureEngine::loadTexture(const std::string& path, bool sRGB) {
    if (textureCache.find(path) != textureCache.end()) return textureCache[path];
    
    std::cout << "[TextureEngine] Opening: " << path << std::endl;
    int width, height, nrChannels;
    // Force 4 channels (RGBA) for safety and consistent memory alignment
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &nrChannels, 4);
    
    if (data) {
        GLuint textureID;
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);

        GLenum internalFormat = sRGB ? GL_SRGB8_ALPHA8 : GL_RGBA8;
        
        std::cout << "[TextureEngine]   - Uploading " << width << "x" << height << " (RGBA)..." << std::endl;
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        // Enable anisotropic filtering if supported
        GLfloat maxAnisotropy = 0.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropy);
        if (maxAnisotropy > 0.0f) {
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(maxAnisotropy, 16.0f));
        }

        stbi_image_free(data);
        textureCache[path] = textureID;
        std::cout << "[TextureEngine]   - Success." << std::endl;
        return textureID;
    } else {
        std::cerr << "[TextureEngine]   - FAILED: " << path << std::endl;
        return 0;
    }
}

GLuint TextureEngine::loadTextureArray(const std::vector<std::string>& paths, int width, int height, bool sRGB) {
    if (paths.empty()) return 0;
    std::cout << "[TextureEngine] Initializing Layered Texture Array (" << paths.size() << " slices)..." << std::endl;
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureID);

    GLenum internalFormat = sRGB ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    
    std::cout << "[TextureEngine]   - Allocating 3D Storage (glTexImage3D)..." << std::endl;
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, internalFormat, width, height, (GLsizei)paths.size(), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    for (int i = 0; i < (int)paths.size(); ++i) {
        std::cout << "[TextureEngine]   - Loading Layer " << i << ": " << paths[i] << std::endl;
        int w, h, ch;
        unsigned char* data = stbi_load(paths[i].c_str(), &w, &h, &ch, 4);
        if (data) {
            if (w != width || h != height) {
                std::cout << "[TextureEngine]     - Resizing from " << w << "x" << h << " to " << width << "x" << height << "..." << std::endl;
                std::vector<unsigned char> resizedData(width * height * 4);
                for (int dstY = 0; dstY < height; ++dstY) {
                    float srcY = ((float)dstY + 0.5f) * ((float)h / (float)height) - 0.5f;
                    int y0 = (int)std::floor(srcY);
                    int y1 = std::min(y0 + 1, h - 1);
                    float fy = srcY - y0;
                    y0 = std::max(y0, 0);

                    for (int dstX = 0; dstX < width; ++dstX) {
                        float srcX = ((float)dstX + 0.5f) * ((float)w / (float)width) - 0.5f;
                        int x0 = (int)std::floor(srcX);
                        int x1 = std::min(x0 + 1, w - 1);
                        float fx = srcX - x0;
                        x0 = std::max(x0, 0);

                        int idx00 = (y0 * w + x0) * 4;
                        int idx10 = (y0 * w + x1) * 4;
                        int idx01 = (y1 * w + x0) * 4;
                        int idx11 = (y1 * w + x1) * 4;

                        int dstIdx = (dstY * width + dstX) * 4;
                        for (int c = 0; c < 4; ++c) {
                            float top = data[idx00 + c] * (1.0f - fx) + data[idx10 + c] * fx;
                            float bot = data[idx01 + c] * (1.0f - fx) + data[idx11 + c] * fx;
                            resizedData[dstIdx + c] = (unsigned char)std::clamp(top * (1.0f - fy) + bot * fy, 0.0f, 255.0f);
                        }
                    }
                }
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, resizedData.data());
            } else {
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, data);
            }
            stbi_image_free(data);
        } else {
            std::cerr << "[TextureEngine]     - FAILED to load: " << paths[i] << std::endl;
        }
    }

    std::cout << "[TextureEngine]   - Generating Mipmaps..." << std::endl;
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    
    // Enable anisotropic filtering if supported
    GLfloat maxAnisotropy = 0.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropy);
    if (maxAnisotropy > 0.0f) {
        glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(maxAnisotropy, 16.0f));
    }
    
    std::cout << "[TextureEngine] Successfully initialized Texture Array with " << paths.size() << " layers." << std::endl;
    return textureID;
}

void TextureEngine::bind(GLuint textureID, unsigned int unit) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, textureID);
}

void TextureEngine::cleanup() {
    for (auto const& [path, id] : textureCache) glDeleteTextures(1, &id);
    textureCache.clear();
}
