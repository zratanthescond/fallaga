#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include "Shader.h"

class Sky {
public:
    Sky();
    ~Sky();
    void render(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& lightDir);

private:
    Shader* skyShader;
    unsigned int skyVAO, skyVBO;
    void setupQuad();
};
