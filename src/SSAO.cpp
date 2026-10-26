#include "SSAO.h"

#include <GL/glew.h>
#include <cmath>
#include <iostream>
#include <random>

namespace {
constexpr int kKernelSize = 8; // must match KERNEL_SIZE in ssao.frag (was 32: the main GPU cost)
constexpr int kNoiseSize = 4;

// A single colour attachment (R8-class grayscale written as RGBA) on an FBO.
bool makeColorFbo(GLuint fbo, GLuint tex, int w, int h) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return ok;
}
}  // namespace

SSAO::SSAO()
    : ssaoShader_("src/shaders/ssao.vert", "src/shaders/ssao.frag"),
      blurShader_("src/shaders/ssao.vert", "src/shaders/ssao_blur.frag") {
    if (kernel_.empty()) {
        // Deterministic hemisphere kernel: samples pushed toward the origin so the
        // innermost probes dominate the contact shading where it matters most.
        std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f);
        std::default_random_engine generator(20260929u);
        kernel_.reserve(kKernelSize);
        for (int i = 0; i < kKernelSize; ++i) {
            glm::vec3 sample(randomFloats(generator) * 2.0f - 1.0f,
                             randomFloats(generator) * 2.0f - 1.0f,
                             randomFloats(generator));
            sample = glm::normalize(sample);
            float scale = static_cast<float>(i) / static_cast<float>(kKernelSize);
            scale = 0.1f + 0.9f * scale * scale;
            sample *= scale;
            kernel_.push_back(sample);
        }
    }
}

SSAO::~SSAO() {
    release();
    if (noiseTex_ != 0) glDeleteTextures(1, &noiseTex_);
    if (vao_ != 0) glDeleteVertexArrays(1, &vao_);
}

void SSAO::release() {
    if (aoTex_ != 0) { glDeleteTextures(1, &aoTex_); aoTex_ = 0; }
    if (blurTex_ != 0) { glDeleteTextures(1, &blurTex_); blurTex_ = 0; }
    if (fbo_ != 0) { glDeleteFramebuffers(1, &fbo_); fbo_ = 0; }
    if (blurFbo_ != 0) { glDeleteFramebuffers(1, &blurFbo_); blurFbo_ = 0; }
    width_ = 0;
    height_ = 0;
}

bool SSAO::ensure(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    const int w = width / 2, h = height / 2;  // half res: AO is low frequency
    if (w <= 0 || h <= 0) return false;
    if (fbo_ != 0 && blurFbo_ != 0 && w == width_ && h == height_) return valid();

    release();

    if (!ssaoShader_.isValid() || !blurShader_.isValid()) {
        std::cerr << "[SSAO] shader failed to load\n";
        return false;
    }

    glGenFramebuffers(1, &fbo_);
    glGenFramebuffers(1, &blurFbo_);
    glGenTextures(1, &aoTex_);
    glGenTextures(1, &blurTex_);
    const bool ok = makeColorFbo(fbo_, aoTex_, w, h) && makeColorFbo(blurFbo_, blurTex_, w, h);
    if (!ok) {
        std::cerr << "[SSAO] framebuffer incomplete (" << w << "x" << h << ")\n";
        release();
        return false;
    }

    // The blurred result is magnified onto the full-res screen in the composite, so a
    // linear filter keeps the upsampling from introducing block edges.
    glBindTexture(GL_TEXTURE_2D, blurTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // The 4-tap blur reads the raw AO map with bilinear taps, so it needs LINEAR too.
    glBindTexture(GL_TEXTURE_2D, aoTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (noiseTex_ == 0) {
        // 4x4 tile of in-plane random directions, sampled per screen tile, that rotate
        // the hemisphere basis without needing a per-pixel hash in the shader.
        unsigned char pixels[kNoiseSize * kNoiseSize * 4];
        std::uniform_real_distribution<float> randomFloats(-1.0f, 1.0f);
        std::default_random_engine generator(777u);
        for (int i = 0; i < kNoiseSize * kNoiseSize; ++i) {
            float x = randomFloats(generator);
            float y = randomFloats(generator);
            float len = std::sqrt(x * x + y * y);
            if (len < 1e-4f) { x = 1.0f; y = 0.0f; } else { x /= len; y /= len; }
            pixels[i * 4 + 0] = static_cast<unsigned char>((x * 0.5f + 0.5f) * 255.0f + 0.5f);
            pixels[i * 4 + 1] = static_cast<unsigned char>((y * 0.5f + 0.5f) * 255.0f + 0.5f);
            pixels[i * 4 + 2] = 0;
            pixels[i * 4 + 3] = 255;
        }
        glGenTextures(1, &noiseTex_);
        glBindTexture(GL_TEXTURE_2D, noiseTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kNoiseSize, kNoiseSize, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    if (vao_ == 0) glGenVertexArrays(1, &vao_);

    width_ = w;
    height_ = h;
    return true;
}

void SSAO::drawFullscreen() {
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void SSAO::render(unsigned int sceneDepthTex, const glm::mat4& projection,
                  float radius, float bias, float power) {
    SSAOParams p;
    p.radius = radius;
    p.bias = bias;
    p.power = power;
    render(sceneDepthTex, projection, p);
}

void SSAO::render(unsigned int sceneDepthTex, const glm::mat4& projection, const SSAOParams& params) {
    if (!valid() || sceneDepthTex == 0) return;

    // --- AO pass -------------------------------------------------------------
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width_, height_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClear(GL_COLOR_BUFFER_BIT);

    ssaoShader_.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneDepthTex);
    ssaoShader_.setInt("u_depth", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, noiseTex_);
    ssaoShader_.setInt("u_noise", 1);

    ssaoShader_.setMat4("u_proj", projection);
    ssaoShader_.setVec2("u_res", glm::vec2(width_, height_));
    ssaoShader_.setVec2("u_noiseScale", glm::vec2(width_ / (float)kNoiseSize,
                                                  height_ / (float)kNoiseSize));
    ssaoShader_.setFloat("u_radius", params.radius);
    ssaoShader_.setFloat("u_bias", params.bias);
    ssaoShader_.setFloat("u_power", params.power);
    ssaoShader_.setFloat("u_fadeStart", params.fadeStart);
    ssaoShader_.setFloat("u_fadeEnd", params.fadeEnd);
    ssaoShader_.setInt("u_shadowEnabled", params.shadowEnabled ? 1 : 0);
    {
        const GLint loc = glGetUniformLocation(ssaoShader_.ID, "u_sunView");
        if (loc >= 0) glUniform3f(loc, params.sunView.x, params.sunView.y, params.sunView.z);
    }
    ssaoShader_.setFloat("u_shadowDist", params.shadowDist);
    ssaoShader_.setFloat("u_shadowThickness", params.shadowThickness);
    {
        const GLint loc = glGetUniformLocation(ssaoShader_.ID, "u_kernel");
        if (loc >= 0 && !kernel_.empty())
            glUniform3fv(loc, kKernelSize, &kernel_[0].x);
    }
    drawFullscreen();

    // --- blur pass -----------------------------------------------------------
    glBindFramebuffer(GL_FRAMEBUFFER, blurFbo_);
    glViewport(0, 0, width_, height_);
    glClear(GL_COLOR_BUFFER_BIT);

    blurShader_.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, aoTex_);
    blurShader_.setInt("u_ao", 0);
    blurShader_.setVec2("u_res", glm::vec2(width_, height_));
    drawFullscreen();

    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
