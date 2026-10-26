#pragma once

#include "Shader.h"

// Off-screen render target for the main 3D scene.
//
// Step 0 of the screen-space pipeline: the scene is drawn into this FBO instead of
// straight to the default framebuffer, then copied back with a fullscreen pass. With
// an identity composite the result is pixel-identical to rendering directly, which is
// what makes it a safe foundation for the screen-space effects that follow (SSAO reads
// depthTexture() and darkens the composite; the aoTexture() hook is added then).
//
// The colour attachment is GL_RGBA8 on purpose. The default framebuffer is RGBA8 and
// is not multisampled (no GLFW_SAMPLES hint), so a plain texture FBO round-trips the
// same bytes. GL_RGBA16F would introduce a rounding difference versus the direct path
// for no benefit until an effect actually needs values above 1.0.
class SceneTarget {
public:
    SceneTarget();
    ~SceneTarget();

    SceneTarget(const SceneTarget&) = delete;
    SceneTarget& operator=(const SceneTarget&) = delete;

    // Create the target, or recreate it if the size changed. Returns false when the
    // FBO is incomplete or the composite shader failed, in which case the caller must
    // fall back to rendering straight to the default framebuffer. Sizes <= 0 are
    // rejected (minimised window).
    bool ensure(int width, int height);

    bool valid() const { return fbo_ != 0 && composite_.isValid(); }
    int width() const { return width_; }
    int height() const { return height_; }

    // Binds the FBO and sets the viewport to the scene size.
    void bind();

    unsigned int colorTexture() const { return colorTex_; }
    unsigned int depthTexture() const { return depthTex_; }

    // Copies the scene depth buffer into a separate texture. Call it right before the dynamic
    // entities (horse, cars, player) are drawn: the screen-space AO / sun-shadow pass then reads
    // a depth buffer WITHOUT them, so it adds no second shadow or AO halo around characters that
    // already have their own shading. worldDepthTexture() falls back to the full depth when no
    // capture happened this frame.
    void captureWorldDepth();
    unsigned int worldDepthTexture() const { return worldDepthValid_ ? worldDepthTex_ : depthTex_; }
    // The FBO name, for binding as a blit read source (water refraction).
    unsigned int framebuffer() const { return fbo_; }

    // Fullscreen triangle sampling colorTexture() into the currently bound framebuffer.
    // Depth test and blend are disabled; call after binding the destination.
    void compositeToDefault();

    // The AO map the next composite multiplies the scene by (pass 0 to go back to the
    // exact identity copy). strength is lerp'd in by the shader, so a weak value keeps the
    // effect subtle without re-running the AO pass.
    void setAmbientOcclusion(unsigned int aoTexture, float strength);

    // aoFloor: AO never darkens the scene below this factor (0.45 keeps contact shadows from
    // ever going black). shadowStrength: how much of the sun contact shadow (.g of the map)
    // is applied; 0 disables it.
    // proj22 / proj32 are projection[2][2] and projection[3][2] (used to linearise depth in the
    // composite's depth-aware upsample); aoW / aoH are the size of the half-res AO map.
    void setScreenSpaceParams(float aoFloor, float shadowStrength, float proj22, float proj32,
                              int aoW, int aoH);

    // Debug: write the colour attachment to a binary PPM (top-down, like the PNG
    // captures) so the composite can be compared against its own source on one frame.
    void dumpColor(const char* path) const;

private:
    void release();

    unsigned int fbo_ = 0;
    unsigned int colorTex_ = 0;
    unsigned int depthTex_ = 0;
    unsigned int aoTex_ = 0;
    unsigned int worldFbo_ = 0;
    unsigned int worldDepthTex_ = 0;
    bool worldDepthValid_ = false;
    float aoStrength_ = 1.0f;
    float aoFloor_ = 0.45f;
    float shadowStrength_ = 0.0f;
    float proj22_ = -1.0f;
    float proj32_ = -0.2f;
    int aoW_ = 1;
    int aoH_ = 1;
    int width_ = 0;
    int height_ = 0;
    unsigned int quadVAO_ = 0;
    Shader composite_;
};
