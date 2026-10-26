#include "SceneTarget.h"

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <cstdio>
#include <iostream>
#include <vector>

SceneTarget::SceneTarget()
    : composite_("src/shaders/composite.vert", "src/shaders/composite.frag") {}

SceneTarget::~SceneTarget() {
    release();
    if (quadVAO_ != 0) glDeleteVertexArrays(1, &quadVAO_);
}

void SceneTarget::release() {
    if (colorTex_ != 0) { glDeleteTextures(1, &colorTex_); colorTex_ = 0; }
    if (depthTex_ != 0) { glDeleteTextures(1, &depthTex_); depthTex_ = 0; }
    if (fbo_ != 0) { glDeleteFramebuffers(1, &fbo_); fbo_ = 0; }
    if (worldDepthTex_ != 0) { glDeleteTextures(1, &worldDepthTex_); worldDepthTex_ = 0; }
    if (worldFbo_ != 0) { glDeleteFramebuffers(1, &worldFbo_); worldFbo_ = 0; }
    worldDepthValid_ = false;
    width_ = 0;
    height_ = 0;
}

bool SceneTarget::ensure(int width, int height) {
    // A new frame begins here (Game::render calls this before anything is drawn), so
    // forget any snapshot from the previous frame. The reset used to live in bind(),
    // but bind() is also used as a plain re-bind after the water refraction blit, which
    // runs between captureWorldDepth() and the AO pass and would silently invalidate
    // the very snapshot the world-depth feature depends on.
    worldDepthValid_ = false;
    if (width <= 0 || height <= 0) return false;
    if (fbo_ != 0 && width == width_ && height == height_) return valid();

    release();

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    glGenTextures(1, &colorTex_);
    glBindTexture(GL_TEXTURE_2D, colorTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex_, 0);

    // A depth *texture* rather than a renderbuffer: the screen-space passes read it.
    glGenTextures(1, &depthTex_);
    glBindTexture(GL_TEXTURE_2D, depthTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex_, 0);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[SceneTarget] framebuffer incomplete: 0x" << std::hex << status
                  << std::dec << " (" << width << "x" << height << ")\n";
        release();
        return false;
    }
    // Depth-only copy target for captureWorldDepth(). Failure here is not fatal: the AO pass just
    // falls back to the full depth buffer (characters included).
    {
        glGenTextures(1, &worldDepthTex_);
        glBindTexture(GL_TEXTURE_2D, worldDepthTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &worldFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, worldFbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, worldDepthTex_, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        const bool worldOk = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (!worldOk) {
            std::cerr << "[SceneTarget] world-depth copy target incomplete; AO will include characters\n";
            glDeleteFramebuffers(1, &worldFbo_);  worldFbo_ = 0;
            glDeleteTextures(1, &worldDepthTex_); worldDepthTex_ = 0;
        }
    }
    if (!composite_.isValid()) {
        std::cerr << "[SceneTarget] composite shader failed to load\n";
        release();
        return false;
    }
    if (quadVAO_ == 0) glGenVertexArrays(1, &quadVAO_);

    width_ = width;
    height_ = height;
    return true;
}

void SceneTarget::captureWorldDepth() {
    if (!valid() || worldFbo_ == 0) return;
    GLint prevRead = 0, prevDraw = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, worldFbo_);
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_,
                      GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevRead);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDraw);
    worldDepthValid_ = true;
}

void SceneTarget::bind() {
    // Deliberately does NOT clear worldDepthValid_. bind() is not only the frame-start
    // bind: it is also used to re-bind after the water refraction blit, and the
    // world-depth snapshot taken before the entities were drawn must stay valid until
    // the AO pass reads it. ensure() clears it once per frame instead.
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width_, height_);
}

void SceneTarget::setAmbientOcclusion(unsigned int aoTexture, float strength) {
    aoTex_ = aoTexture;
    aoStrength_ = strength;
}

void SceneTarget::setScreenSpaceParams(float aoFloor, float shadowStrength, float proj22,
                                       float proj32, int aoW, int aoH) {
    aoFloor_ = aoFloor;
    shadowStrength_ = shadowStrength;
    proj22_ = proj22;
    proj32_ = proj32;
    aoW_ = aoW;
    aoH_ = aoH;
}

void SceneTarget::compositeToDefault() {
    if (!valid()) return;
    // The fullscreen triangle uses gl_VertexID only, so an empty VAO is enough, but
    // one must be bound: a core-profile-style draw with no VAO is undefined.
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glBindVertexArray(quadVAO_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, colorTex_);
    composite_.use();
    composite_.setInt("u_scene", 0);

    const bool useAo = (aoTex_ != 0);
    composite_.setInt("u_aoEnabled", useAo ? 1 : 0);
    composite_.setFloat("u_aoStrength", aoStrength_);
    composite_.setFloat("u_aoFloor", aoFloor_);
    composite_.setFloat("u_shadowStrength", shadowStrength_);
    if (useAo) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, aoTex_);
        composite_.setInt("u_ao", 1);
        // Full-res depth for the depth-aware upsample of the half-res AO / shadow map.
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, depthTex_);
        composite_.setInt("u_depth", 2);
        composite_.setVec2("u_aoSize", glm::vec2((float)aoW_, (float)aoH_));
        composite_.setVec2("u_projZ", glm::vec2(proj22_, proj32_));
        glActiveTexture(GL_TEXTURE0);
    }

    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
    // Restore the defaults the rest of the frame (and the next frame's clear) expects:
    // a masked-off depth buffer would make the next glClear of depth a no-op.
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void SceneTarget::dumpColor(const char* path) const {
    if (!valid() || width_ <= 0 || height_ <= 0) return;
    std::vector<unsigned char> pixels(static_cast<size_t>(width_) * height_ * 3);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width_, height_, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    std::FILE* f = std::fopen(path, "wb");
    if (!f) {
        std::cerr << "[SceneTarget] cannot write " << path << "\n";
        return;
    }
    std::fprintf(f, "P6\n%d %d\n255\n", width_, height_);
    for (int y = height_ - 1; y >= 0; --y) {
        std::fwrite(&pixels[static_cast<size_t>(y) * width_ * 3], 1,
                    static_cast<size_t>(width_) * 3, f);
    }
    std::fclose(f);
}
