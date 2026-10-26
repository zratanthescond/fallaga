#pragma once

#include "Shader.h"
#include <glm/glm.hpp>
#include <vector>

// Screen-space ambient occlusion for the off-screen scene target (SceneTarget).
//
// The scene is already rendered into an FBO that owns a depth texture, so there is no
// separate geometry pre-pass: the AO pass unprojects that depth to view space, rebuilds
// normals from neighbouring depths, samples a cosine-ish hemisphere with a per-tile
// random rotation, and writes the occlusion factor (1 = unoccluded) to a half-res target
// that is then box-blurred. The composite in SceneTarget multiplies the scene colour by it.
//
// Nothing here is bound into the frame permanently: render() sets its own FBO/shader, and
// aoTexture() is only read by SceneTarget::compositeToDefault().
// Tunables for one AO + sun-contact-shadow pass. All distances are metres.
struct SSAOParams {
    float radius = 0.5f;
    float bias = 0.025f;
    float power = 1.5f;
    // Beyond fadeEnd the pass exits after one depth fetch (most of an open-world frame);
    // between fadeStart and fadeEnd the effect fades out.
    float fadeStart = 20.0f;
    float fadeEnd = 40.0f;
    // Screen-space ray march towards the sun (the "low-cost ray tracing" part).
    bool shadowEnabled = false;
    glm::vec3 sunView{0.0f, 1.0f, 0.0f}; // unit vector TOWARDS the sun, in VIEW space
    float shadowDist = 1.2f;
    float shadowThickness = 0.6f;
};

class SSAO {
public:
    SSAO();
    ~SSAO();

    SSAO(const SSAO&) = delete;
    SSAO& operator=(const SSAO&) = delete;

    // (Re)create the half-res AO/blur targets and the 4x4 noise rotation tile.
    // Returns false when either target is incomplete or a shader failed to load, in which
    // case the caller must leave SSAO disabled (fall back to an identity composite).
    bool ensure(int width, int height);

    bool valid() const { return fbo_ != 0 && blurFbo_ != 0 && ssaoShader_.isValid() && blurShader_.isValid(); }
    int width() const { return width_; }
    int height() const { return height_; }

    // Runs the AO pass (reading sceneDepthTex) then the blur pass. Leaves GL state that
    // the composite expects; the frame's own passes re-establish everything they need.
    void render(unsigned int sceneDepthTex, const glm::mat4& projection, float radius, float bias, float power);
    void render(unsigned int sceneDepthTex, const glm::mat4& projection, const SSAOParams& params);

    // The blurred, ready-to-composite map (half res): .r = ambient occlusion, .g = sun contact shadow.
    unsigned int aoTexture() const { return blurTex_; }

private:
    void release();
    void drawFullscreen();

    unsigned int fbo_ = 0;
    unsigned int aoTex_ = 0;
    unsigned int blurFbo_ = 0;
    unsigned int blurTex_ = 0;
    unsigned int noiseTex_ = 0;
    unsigned int vao_ = 0;
    int width_ = 0;
    int height_ = 0;
    Shader ssaoShader_;
    Shader blurShader_;
    std::vector<glm::vec3> kernel_;
};
