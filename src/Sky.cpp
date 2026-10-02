#include "Sky.h"
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr int kCloudNoiseSize = 1024;
constexpr int kCloudNoiseOctaves = 6;

unsigned int cloudHash(int x, int y, int period) {
    x %= period;
    y %= period;
    if (x < 0) x += period;
    if (y < 0) y += period;
    unsigned int value = static_cast<unsigned int>(x) * 0x8da6b343u ^
                         static_cast<unsigned int>(y) * 0xd8163841u;
    value ^= value >> 13;
    value *= 0x85ebca6bu;
    value ^= value >> 16;
    return value;
}

float cloudValueNoise(float u, float v, int cells) {
    const float x = u * cells;
    const float y = v * cells;
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float fx = x - x0;
    const float fy = y - y0;
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sy = fy * fy * (3.0f - 2.0f * fy);
    const float invMax = 1.0f / 4294967295.0f;
    const auto sample = [&](int px, int py) {
        return static_cast<float>(cloudHash(px, py, cells)) * invMax;
    };
    const float a = sample(x0, y0);
    const float b = sample(x0 + 1, y0);
    const float c = sample(x0, y0 + 1);
    const float d = sample(x0 + 1, y0 + 1);
    return (a + (b - a) * sx) * (1.0f - sy) +
           (c + (d - c) * sx) * sy;
}
}

Sky::Sky() {
    skyShader = new Shader("src/shaders/sky.vert", "src/shaders/sky.frag");
    setupQuad();
    setupCloudNoise();
}

Sky::~Sky() {
    delete skyShader;
    glDeleteVertexArrays(1, &skyVAO);
    glDeleteBuffers(1, &skyVBO);
    if (cloudNoiseTexture != 0) glDeleteTextures(1, &cloudNoiseTexture);
}

void Sky::setupCloudNoise() {
    std::vector<unsigned char> pixels(kCloudNoiseSize * kCloudNoiseSize);
    for (int y = 0; y < kCloudNoiseSize; ++y) {
        const float v = static_cast<float>(y) / kCloudNoiseSize;
        for (int x = 0; x < kCloudNoiseSize; ++x) {
            const float u = static_cast<float>(x) / kCloudNoiseSize;
            float value = 0.0f;
            float amplitude = 0.5f;
            for (int octave = 0; octave < kCloudNoiseOctaves; ++octave) {
                value += amplitude * cloudValueNoise(u, v, 4 << octave);
                amplitude *= 0.5f;
            }
            pixels[y * kCloudNoiseSize + x] = static_cast<unsigned char>(
                std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    }

    glGenTextures(1, &cloudNoiseTexture);
    glBindTexture(GL_TEXTURE_2D, cloudNoiseTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kCloudNoiseSize, kCloudNoiseSize, 0,
                 GL_RED, GL_UNSIGNED_BYTE, pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
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
    skyShader->setFloat("time", (float)glfwGetTime());
    skyShader->setInt("cloudNoise", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, cloudNoiseTexture);

    glBindVertexArray(skyVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDepthFunc(GL_LESS);
}
