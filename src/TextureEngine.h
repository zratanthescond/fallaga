#pragma once
#include <string>
#include <vector>
#include <map>
#include <GL/glew.h>

class TextureEngine {
public:
    static TextureEngine& Instance() {
        static TextureEngine instance;
        return instance;
    }

    GLuint loadTexture(const std::string& path, bool sRGB = true);
    GLuint loadTextureArray(const std::vector<std::string>& paths, int width, int height, bool sRGB = true);
    void bind(GLuint textureID, unsigned int unit);
    void cleanup();

private:
    TextureEngine() = default;
    ~TextureEngine() { cleanup(); }
    TextureEngine(const TextureEngine&) = delete;
    TextureEngine& operator=(const TextureEngine&) = delete;

    std::map<std::string, GLuint> textureCache;
};
