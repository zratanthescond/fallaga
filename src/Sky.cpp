#include "Sky.h"
#include <glm/gtc/type_ptr.hpp>

Sky::Sky() {
    skyShader = new Shader("src/shaders/sky.vert", "src/shaders/sky.frag");
    setupQuad();
}

Sky::~Sky() {
    delete skyShader;
    glDeleteVertexArrays(1, &skyVAO);
    glDeleteBuffers(1, &skyVBO);
}

void Sky::setupQuad() {
    float skyCoords[] = {
        // Positions
        -1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f, -1.0f,

        -1.0f,  1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f
    };

    glGenVertexArrays(1, &skyVAO);
    glGenBuffers(1, &skyVBO);
    glBindVertexArray(skyVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skyVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyCoords), &skyCoords, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);
}

void Sky::render(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& lightDir) {
    glDepthFunc(GL_LEQUAL); // Ensure sky renders at far plane
    skyShader->use();

    glm::mat4 invProj = glm::inverse(projection);
    glm::mat4 invView = glm::inverse(glm::mat4(glm::mat3(view))); // Remove translation

    skyShader->setMat4("invProj", invProj);
    skyShader->setMat4("invView", invView);
    skyShader->setVec3("lightDir", lightDir);

    glBindVertexArray(skyVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glDepthFunc(GL_LESS);
}
