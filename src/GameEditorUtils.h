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

// ==== Brush Slider Handling ====

// Updates brush radius and strength sliders based on mouse input
inline void updateBrushSliders(float mouseX, float mouseY,
                                float lpX, float lpY, float lpW,
                                float& outBrushRadius, float& outBrushStrength,
                                bool sculptLayout = false, float* outSculptDirection = nullptr,
                                float* outSculptGrade = nullptr) {
    const float SLIDER_WIDTH = lpW - 30.0f;
    const float SLIDER_X_START = lpX + 15.0f;

    const float radiusY = sculptLayout ? 195.0f : 170.0f;
    const float strengthY = sculptLayout ? 227.0f : 202.0f;
    // Sculpt mode makes room for its fifth tool and angle control.
    if (mouseY >= lpY + radiusY && mouseY <= lpY + radiusY + 25.0f) {
        float norm = glm::clamp((mouseX - SLIDER_X_START) / SLIDER_WIDTH, 0.0f, 1.0f);
        outBrushRadius = BrushConfig::RADIUS_MIN + norm * (BrushConfig::RADIUS_MAX - BrushConfig::RADIUS_MIN);
    }

    // Strength slider (y range: lpY + 202 to lpY + 227)
    if (mouseY >= lpY + strengthY && mouseY <= lpY + strengthY + 25.0f) {
        float norm = glm::clamp((mouseX - SLIDER_X_START) / SLIDER_WIDTH, 0.0f, 1.0f);
        outBrushStrength = BrushConfig::STRENGTH_MIN + norm * (BrushConfig::STRENGTH_MAX - BrushConfig::STRENGTH_MIN);
    }
    if (sculptLayout && outSculptDirection && mouseY >= lpY + 259.0f && mouseY <= lpY + 284.0f) {
        const float norm = glm::clamp((mouseX - SLIDER_X_START) / SLIDER_WIDTH, 0.0f, 1.0f);
        *outSculptDirection = norm * 360.0f;
    }
    if (sculptLayout && outSculptGrade && mouseY >= lpY + 291.0f && mouseY <= lpY + 316.0f) {
        const float norm = glm::clamp((mouseX - SLIDER_X_START) / SLIDER_WIDTH, 0.0f, 1.0f);
        *outSculptGrade = norm * 60.0f;
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
