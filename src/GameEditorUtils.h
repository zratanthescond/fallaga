#pragma once

#include <glm/glm.hpp>
#include <string>
#include <iostream>

// ============================================================================
// GameEditorUtils.h
// Centralized utility functions to eliminate redundancy in Game.cpp editor UI
// ============================================================================

namespace GameEditorUtils {

// Brush configuration constants (eliminates magic numbers)
struct BrushConfig {
    static constexpr float RADIUS_MIN = 5.0f;
    static constexpr float RADIUS_MAX = 200.0f;
    static constexpr float STRENGTH_MIN = 0.5f;
    static constexpr float STRENGTH_MAX = 50.0f;
};

// Water level configuration
struct WaterLevelConfig {
    static constexpr float MIN = -80.0f;
    static constexpr float MAX = 0.0f;
    static constexpr float STEP_COARSE = 0.5f;
    static constexpr float STEP_FINE = 0.1f;
    static constexpr float RESET_VALUE = -55.0f;
};

} // namespace GameEditorUtils

// ==== Sculpt panel layout ====
//
// One source of truth for the sculpt panel. The drawing and the click handling used
// to carry independent hardcoded y offsets and had drifted apart, so the sliders were
// drawn in one place and tested for clicks in another -- which is why dragging them
// did nothing. Both sides now read these numbers.
//
// Baseline geometry, since every overlap so far came from ignoring it: drawStr's y is
// a BASELINE, and its glyphs run from about y-8 down to y+3. A caption at y therefore
// needs its next element to start at y+14 or lower.
namespace SculptPanel {
constexpr float kTitleY      = 12.0f;
constexpr float kToolsLabelY = 36.0f;
constexpr float kToolsTop    = 48.0f;
// Row must clear two text baselines: the tool name at +11 and its hint at +21, both
// of which are ~10 px tall. 30 px overlapped the next row and clipped the hint.
constexpr float kToolRowH    = 34.0f;
constexpr int   kToolCount   = 7;
constexpr float kToolsBottom = kToolsTop + kToolRowH * kToolCount;   // 286
constexpr float kSliderLabelY = kToolsBottom + 14.0f;
constexpr float kSliderTop    = kToolsBottom + 34.0f;
constexpr float kSliderPitch  = 44.0f;
constexpr float kSliderTrackH = 12.0f;
constexpr float kDetailTop    = kSliderTop + 4 * kSliderPitch;
constexpr float kDetailBtnH   = 24.0f;
constexpr float kDetailHelpY  = 14.0f;   // help line, below the caption
constexpr float kDetailBtnY   = 20.0f;   // button top, below the help line
// Clearance for the section heading below: the detail buttons end at
// kDetailBtnY + kDetailBtnH, and the heading needs a full text line plus a gap.
constexpr float kGlobalTop    = kDetailTop + kDetailBtnY + kDetailBtnH + 34.0f;
} // namespace SculptPanel

namespace GameEditorUtils {

// ==== Brush Slider Handling ====

// Updates brush radius and strength sliders based on mouse input
inline void updateBrushSliders(float mouseX, float mouseY,
                                float lpX, float lpY, float lpW,
                                float& outBrushRadius, float& outBrushStrength,
                                bool sculptLayout = false, float* outSculptDirection = nullptr,
                                float* outSculptGrade = nullptr) {
    const float SLIDER_WIDTH = lpW - 30.0f;
    const float SLIDER_X_START = lpX + 15.0f;

    if (sculptLayout) {
        // Positions come from SculptPanel so they cannot drift from the drawing code.
        for (int i = 0; i < 4; ++i) {
            const float capY = lpY + SculptPanel::kSliderTop + i * SculptPanel::kSliderPitch;
            if (mouseY < capY + 6.0f || mouseY > capY + 6.0f + SculptPanel::kSliderTrackH)
                continue;
            const float norm = glm::clamp((mouseX - SLIDER_X_START) / SLIDER_WIDTH, 0.0f, 1.0f);
            if (i == 0)
                outBrushRadius = BrushConfig::RADIUS_MIN + norm * (BrushConfig::RADIUS_MAX - BrushConfig::RADIUS_MIN);
            else if (i == 1)
                outBrushStrength = BrushConfig::STRENGTH_MIN + norm * (BrushConfig::STRENGTH_MAX - BrushConfig::STRENGTH_MIN);
            else if (i == 2 && outSculptDirection)
                *outSculptDirection = norm * 360.0f;
            else if (i == 3 && outSculptGrade)
                *outSculptGrade = norm * 60.0f;
        }
        return;
    }

    const float radiusY = 170.0f;
    const float strengthY = 202.0f;
    if (mouseY >= lpY + radiusY && mouseY <= lpY + radiusY + 25.0f) {
        float norm = glm::clamp((mouseX - SLIDER_X_START) / SLIDER_WIDTH, 0.0f, 1.0f);
        outBrushRadius = BrushConfig::RADIUS_MIN + norm * (BrushConfig::RADIUS_MAX - BrushConfig::RADIUS_MIN);
    }
    if (mouseY >= lpY + strengthY && mouseY <= lpY + strengthY + 25.0f) {
        float norm = glm::clamp((mouseX - SLIDER_X_START) / SLIDER_WIDTH, 0.0f, 1.0f);
        outBrushStrength = BrushConfig::STRENGTH_MIN + norm * (BrushConfig::STRENGTH_MAX - BrushConfig::STRENGTH_MIN);
    }
}

// ==== Brush Slider Rendering ====

// Forward declarations for rendering functions (defined in Game.cpp)
void drawStr(float x, float y, const std::string& text, float r, float g, float b);
void drawRect(float x, float y, float w, float h, float r, float g, float b, float a);
void drawRectOutline(float x, float y, float w, float h, float r, float g, float b);

// Renders brush radius and strength sliders at the given positions
inline void drawBrushSliders(float lpX, float sliderStartY, float lpW,
                             float brushRadius, float brushStrength) {
    const float SLIDER_WIDTH = lpW - 30.0f;
    const float SLIDER_X_START = lpX + 15.0f;
    const float SLIDER_THUMB_WIDTH = 10.0f;

    // Radius slider
    float rNorm = glm::clamp((brushRadius - BrushConfig::RADIUS_MIN) / (BrushConfig::RADIUS_MAX - BrushConfig::RADIUS_MIN), 0.0f, 1.0f);
    drawStr(SLIDER_X_START, sliderStartY + 30, "Radius: " + std::to_string((int)brushRadius) + "m", 0.8f, 0.8f, 0.8f);
    drawRect(SLIDER_X_START, sliderStartY + 35, SLIDER_WIDTH, 10, 0.06f, 0.06f, 0.06f, 1.0f);
    drawRectOutline(SLIDER_X_START, sliderStartY + 35, SLIDER_WIDTH, 10, 0.2f, 0.2f, 0.2f);
    drawRect(SLIDER_X_START + rNorm * (SLIDER_WIDTH - SLIDER_THUMB_WIDTH), sliderStartY + 32, SLIDER_THUMB_WIDTH, 16, 0.89f, 0.36f, 0.13f, 1.0f);

    // Strength slider
    float sNorm = glm::clamp((brushStrength - BrushConfig::STRENGTH_MIN) / (BrushConfig::STRENGTH_MAX - BrushConfig::STRENGTH_MIN), 0.0f, 1.0f);
    drawStr(SLIDER_X_START, sliderStartY + 62, "Strength: " + std::to_string((int)brushStrength), 0.8f, 0.8f, 0.8f);
    drawRect(SLIDER_X_START, sliderStartY + 67, SLIDER_WIDTH, 10, 0.06f, 0.06f, 0.06f, 1.0f);
    drawRectOutline(SLIDER_X_START, sliderStartY + 67, SLIDER_WIDTH, 10, 0.2f, 0.2f, 0.2f);
    drawRect(SLIDER_X_START + sNorm * (SLIDER_WIDTH - SLIDER_THUMB_WIDTH), sliderStartY + 64, SLIDER_THUMB_WIDTH, 16, 0.89f, 0.36f, 0.13f, 1.0f);
}

// ==== Brush Parameter Adjustment (keyboard input) ====

// Adjusts a brush parameter (radius or strength) with clamping
inline void adjustBrushParam(float& param, float step, float minVal, float maxVal, const char* paramName) {
    float oldVal = param;
    param = glm::clamp(param + step, minVal, maxVal);
    if (param != oldVal) {
        std::cout << "[Brush] " << paramName << ": " << param << "\n";
    }
}

// ==== Water Level Slider ====

// Updates water level based on slider mouse input
inline void updateWaterLevelSlider(float mouseX, float mouseY,
                                   float lpX, float lpY, float lpW,
                                   float& outWaterLevel) {
    if (mouseY >= lpY + 81 && mouseY <= lpY + 99) {
        float norm = glm::clamp((mouseX - (lpX + 15.0f)) / (lpW - 30.0f), 0.0f, 1.0f);
        outWaterLevel = WaterLevelConfig::MIN + norm * (WaterLevelConfig::MAX - WaterLevelConfig::MIN);
    }
}

// Updates water level based on button clicks (+/- coarse or fine)
inline void updateWaterLevelButtons(float mouseX, float mouseY, float lpX, float lpY,
                                    float lpW, bool coarse, float& outWaterLevel) {
    float step = coarse ? WaterLevelConfig::STEP_COARSE : WaterLevelConfig::STEP_FINE;
    float btnY = coarse ? (lpY + 120) : (lpY + 150);
    float btnWidth = 60.0f;
    float btnGap = 10.0f;

    if (mouseY >= btnY && mouseY <= btnY + 22) {
        // Minus button
        if (mouseX >= lpX + 15 && mouseX <= lpX + 15 + btnWidth) {
            outWaterLevel = glm::max(WaterLevelConfig::MIN, outWaterLevel - step);
        }
        // Plus button
        if (mouseX >= lpX + 15 + btnWidth + btnGap && mouseX <= lpX + 15 + 2 * btnWidth + btnGap) {
            outWaterLevel = glm::min(WaterLevelConfig::MAX, outWaterLevel + step);
        }
    }
}

} // namespace GameEditorUtils
