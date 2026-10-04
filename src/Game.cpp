#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <GL/glut.h>
#include "Game.h"
#include "Character.h"
#include "Camera.h"
#include "Terrain.h"
#include "Horse.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <cstdlib>
#include <cmath>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <vector>
#include "Shader.h"
#include "AudioSystem.h"
#include "Car.h"
#include "SousseMapData.h"
#include "TextureEngine.h"
#include "GameEditorUtils.h"
#include "GameplayUtils.h"
#include <stb_image.h>

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

// ── Road Edit mode helpers ──
namespace {
const char* roadTypeLabel(int t) {
    switch ((RoadType)t) {
        case RoadType::ColonialAvenue: return "Colonial Avenue";
        case RoadType::UrbanStreet:    return "Urban Street";
        case RoadType::CountryHighway: return "Country Highway";
        case RoadType::CountryDirt:    return "Country Dirt";
        case RoadType::Port:           return "Port";
        case RoadType::Medina:         return "Medina";
    }
    return "?";
}
float roadDefaultWidthFor(RoadType t) {
    switch (t) {
        case RoadType::ColonialAvenue: return 11.5f;
        case RoadType::UrbanStreet:    return 8.0f;
        case RoadType::CountryHighway: return 8.5f;
        case RoadType::CountryDirt:    return 6.0f;
        case RoadType::Port:           return 10.0f;
        case RoadType::Medina:         return 5.0f;
    }
    return 6.0f;
}
float roadDefaultShoulderFor(RoadType t) {
    return (t == RoadType::Medina) ? 0.75f : 1.5f;
}
// Distance from point (px,pz) to the segment (ax,az)-(bx,bz)
float distPointToPolylineSegment(float px, float pz, float ax, float az, float bx, float bz) {
    float dx = bx - ax, dz = bz - az;
    float len2 = dx * dx + dz * dz;
    float t = (len2 > 1e-8f) ? glm::clamp(((px - ax) * dx + (pz - az) * dz) / len2, 0.0f, 1.0f) : 0.0f;
    float cx = ax + t * dx, cz = az + t * dz;
    return std::sqrt((px - cx) * (px - cx) + (pz - cz) * (pz - cz));
}
// Reference map files shown as a picture-in-picture overlay in road edit mode ([ ] cycles)
// NOTE: roadMap.png is a generated all-black mask - NEVER include it here (renders as a black frame).
const char* roadRefPaths[] = {
    "assets/terrain/sousseMap1899.png",
    "assets/terrain/sousseMap1899-roads.png",
    "assets/terrain/sousseMap1899-roads+monimet.png"
};
const int roadRefPathCount = (int)(sizeof(roadRefPaths) / sizeof(roadRefPaths[0]));
} // namespace

Game::Game() : lastFrameTime(0.0), deltaTime(0.0) {
    AudioSystem::getInstance().init();
    player = new Character();
    terrain = new Terrain(); // Initialize terrain first
    
    // DEBUG: Print the map to console on startup
    std::cout << "\n=== TERRAIN ZONE MAP (Ascii) ===\n";
    terrain->printDebugMap();
    std::cout << "================================\n\n";

    // Scan assets (models and textures) and load level layout
    SousseMapData::scanAssets("assets");
    SousseMapData::loadLayout("assets/saves/city.txt");
    if (SousseMapData::liveLandmarks.empty()) {
        std::cout << "[Librarian] : city.txt is empty, loading city.txt.bak fallback...\n";
        SousseMapData::loadLayout("assets/saves/city.txt.bak");
        SousseMapData::saveLayout("assets/saves/city.txt");
    }

    camera = new Camera(player);
    camera->setTerrain(terrain); // Set the terrain for camera height clipping
    camera->setAngles(90.0f, 12.0f);
    camera->reset();
    horse = new Horse();
    car = new Car(Car::State::PARKED);
    sky = new Sky();
    lastFrameTime = glfwGetTime();
    totalTime = 150.0f; // High noon bright daylight matching RDR2 reference photo

    // Initialize Water Simulation grid with terrain manager bounds and base water level
    if (terrain && terrain->terrainManager) {
        waterSim.initialize(terrain->terrainManager, -13938.0f, 13938.0f, -13938.0f, 13938.0f, waterLevelAdjust);
        // CRITICAL: Upload initial water simulation data to GPU texture immediately after initialization
        terrain->updateWaterMapTexture(waterSim.getTextureData());
        std::cout << "[Water] Initialization complete at level " << waterLevelAdjust << "\n";
    }
}

Game::~Game() {
    AudioSystem::getInstance().cleanup();
    if (gpuTimerQueries[0]) glDeleteQueries(4, gpuTimerQueries);
    if (gpuPhaseQueries[0][0]) {
        for (int s = 0; s < kGpuPhaseSlots; ++s)
            for (int p = 0; p < kGpuPhaseCount; ++p)
                if (gpuPhaseQueries[s][p]) glDeleteQueries(1, &gpuPhaseQueries[s][p]);
    }
    if (roadRefTex) glDeleteTextures(1, &roadRefTex);
    if (roadRefDrawTex) glDeleteTextures(1, &roadRefDrawTex);
    delete player;
    delete camera;
    delete terrain;
    delete sky;
    delete horse;
    delete car;
}
void Game::setup(int width, int height) {
    windowWidth = width;
    windowHeight = height;
    // Build water reflection/refraction FBOs now that we have the window size
    terrain->buildWaterFBOs(width, height);
    // ... other initial setup for Game
}

void Game::update( float mouseDeltaX, float mouseDeltaY, float mouseScroll) {
    using PerfClock = std::chrono::steady_clock;
    struct UpdatePerfStats {
        double elapsed = 0.0;
        int frames = 0;
        double prepMs = 0.0;
        double charactersMs = 0.0;
        double carMs = 0.0;
        double cameraMs = 0.0;
        double terrainMs = 0.0;
        double waterStepMs = 0.0;
        double waterUploadMs = 0.0;
        double otherMs = 0.0;
    };
    static const bool profileUpdate =
        std::getenv("FALLAGA_UPDATE_PERF") != nullptr;
    static UpdatePerfStats perf;
    PerfClock::time_point phaseStart{};
    if (profileUpdate) phaseStart = PerfClock::now();
    auto recordPhase = [&](double& bucketMs) {
        if (!profileUpdate) return;
        const auto now = PerfClock::now();
        bucketMs += std::chrono::duration<double, std::milli>(
                        now - phaseStart).count();
        phaseStart = now;
    };

    double currentTime = glfwGetTime();
   
    deltaTime = currentTime - lastFrameTime;
    lastFrameTime = currentTime;
    // Clamp the frame step. Without this an alt-tab, a window drag or a debugger
    // breakpoint feeds a multi-second dt straight into character movement, gravity,
    // sculpt and paint. The car already bounds its own catch-up internally.
    constexpr double kMaxDeltaTime = 0.10; // 10 fps floor
    if (deltaTime > kMaxDeltaTime) deltaTime = kMaxDeltaTime;
    if (deltaTime < 0.0) deltaTime = 0.0;
    totalTime += deltaTime;
    // TerrainManager can reallocate its heightfield when heightmap.png changes on
    // disk. Refresh the road height sampler before anything samples terrain this
    // frame, so no consumer ever reads through a freed pointer.
    if (terrain) terrain->refreshHeightDependentCaches();
    if (projSaveFlash > 0.0f) projSaveFlash = glm::max(0.0f, projSaveFlash - (float)deltaTime);

    // Auto-persist projected map placement whenever it changes (throttled, no button needed)
    bool projChanged =
        projRefCenter.x != projSavedCenter.x || projRefCenter.y != projSavedCenter.y ||
        projRefScale != projSavedScale || projRefOpacity != projSavedOpacity ||
        projRefYOffset != projSavedYOffset || projRefFlipX != projSavedFlipX ||
        projRefFlipZ != projSavedFlipZ || roadRefIdx != projSavedIdx ||
        projRefMapW != projSavedMapW;
    if (projChanged && (currentTime - projLastSave) > 0.5) {
        saveProjRefMap();
    }
    recordPhase(perf.prepMs);
   // std::cout << "Delta Time: " << deltaTime << " seconds\n";
    player->update(camera, static_cast<float>(deltaTime), terrain);
    horse->update(static_cast<float>(deltaTime), terrain);
    recordPhase(perf.charactersMs);
    if (car) {
        car->update(static_cast<float>(deltaTime), terrain, static_cast<float>(totalTime), camera);
    }
    recordPhase(perf.carMs);
    // Update the camera after vehicle physics so the driving view follows the
    // car's current frame position instead of trailing one physics update.
    camera->update(mouseDeltaX, mouseDeltaY, mouseScroll, static_cast<float>(deltaTime), editMode);
    recordPhase(perf.cameraMs);
    
    // Periodically update terrain (e.g. check for heightmap file changes)
    {
        auto camPos = camera->getPosition();
        terrain->update(glm::vec3(camPos.x, camPos.y, camPos.z));
    }
    recordPhase(perf.terrainMs);

    // The rendered water map is static between terrain/water-level edits.
    // WaterSimulation::update() only changes private flow state; its texture
    // data is derived from the unchanged masks and heights. Recomputing it and
    // uploading the full 512x512 texture every frame caused a GPU sync stall.

    // ----------------------------------------------------
    // EDITOR MOUSE & RAYCAST INTERACTION (UI CLICKS & 3D INTERACTION)
    // ----------------------------------------------------
    rayHitTerrain = false;
    if (editMode) {
        GLFWwindow* window = glfwGetCurrentContext();
        if (window && (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) != GLFW_PRESS)) {
            double mouseX, mouseY;
            glfwGetCursorPos(window, &mouseX, &mouseY);
            
            // Resolve high-DPI scaling mismatch by mapping window points to framebuffer pixels
            int fbW = 0, fbH = 0;
            glfwGetFramebufferSize(window, &fbW, &fbH);
            int winW = 0, winH = 0;
            glfwGetWindowSize(window, &winW, &winH);
            if (winW > 0 && winH > 0) {
                mouseX = mouseX * (double)fbW / (double)winW;
                mouseY = mouseY * (double)fbH / (double)winH;
            }

            float lpX = 10, lpY = 42, lpW = 320, lpH = (float)windowHeight - 52;
            float rpX = (float)windowWidth - 350, rpY = 42, rpW = 340, rpH = (float)windowHeight - 52;

            bool clickedUI = false;
            bool leftMouseDown = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
            static bool lastLeftMouseDown = false;
            bool leftMouseClicked = leftMouseDown && !lastLeftMouseDown;
            lastLeftMouseDown = leftMouseDown;

            // Check Mode Switcher Clicks (Top Bar)
            if (mouseY >= 0 && mouseY <= 32) {
                const float tabW = 110.0f, tabGap = 8.0f, edgeM = 10.0f;
                float spanW = 6 * tabW + 5 * tabGap;
                float startX = (float)windowWidth * 0.5f - spanW * 0.5f;
                if (startX < edgeM) startX = edgeM;
                if (leftMouseClicked) {
                    for (int i = 0; i < 6; ++i) {
                        float tx = startX + i * (tabW + tabGap);
                        if (mouseX >= tx && mouseX <= tx + tabW) {
                            activeEditorMode = i;
                            editMode = (i != 0);
                            editorBrushMode = (i == 2) ? 1 : (i == 3) ? 7 : 0;
                            std::cout << "[Editor Mode] Switched to Mode " << i << "...\n";
                            break;
                        }
                    }
                }
                clickedUI = true;
            }

            // Check Left Panel Clicks
            if (!clickedUI && roadPanelsVisible && mouseX >= lpX && mouseX <= lpX + lpW && mouseY >= lpY && mouseY <= lpY + lpH) {
                clickedUI = true;
                if (leftMouseDown) {
                    if (activeEditorMode == 1) {
                        // Actor Edit Mode: Asset List
                        static int assetPage = 0;
                        float listStartY = lpY + 50;
                        for (int i = 0; i < 12; ++i) { // Show more assets (12 instead of 8) since we have space!
                            float btnY = listStartY + i * 24;
                            int idx = assetPage * 12 + i;
                            if (idx < (int)SousseMapData::discoveredAssets.size()) {
                                if (leftMouseClicked && mouseY >= btnY && mouseY <= btnY + 20) {
                                    selectedAssetIdx = idx;
                                    std::cout << "[Editor UI] Selected asset: " << SousseMapData::discoveredAssets[selectedAssetIdx] << "\n";
                                }
                            }
                        }
                        // Page controls
                        float pageY = listStartY + 12 * 24 + 10;
                        if (leftMouseClicked && mouseY >= pageY && mouseY <= pageY + 20) {
                            if (mouseX >= lpX + 15 && mouseX <= lpX + 100 && assetPage > 0) {
                                assetPage--;
                            }
                            if (mouseX >= lpX + 110 && mouseX <= lpX + 195 && (assetPage + 1) * 12 < (int)SousseMapData::discoveredAssets.size()) {
                                assetPage++;
                            }
                        }
                    } 
                    else if (activeEditorMode == 2) {
                        // Sculpt Mode: Tool selection
                        for (int i = 0; i < 6; ++i) { // includes road-bed conform
                            float btnY = lpY + 45 + i * 22;
                            if (leftMouseClicked && mouseY >= btnY && mouseY <= btnY + 20) {
                                editorBrushMode = 1 + i; // sculpt brush ids 1-6
                                std::cout << "[Editor UI] Sculpt tool: " << editorBrushMode << "\n";
                            }
                        }

                        GameEditorUtils::updateBrushSliders(mouseX, mouseY, lpX, lpY, lpW,
                            brushRadius, brushStrength, true, &sculptAngleDegrees, &sculptGradeDegrees);

                        if (leftMouseClicked && mouseY >= lpY + 350 && mouseY <= lpY + 370 && mouseX >= lpX + 15 && mouseX <= lpX + lpW - 15) {
                            std::cout << "[Editor UI] Triggering Global Hydraulic Erosion Pass...\n";
                            terrain->terrainManager->erodeGlobal();
                            waterSim.initialize(terrain->terrainManager, -13938.0f, 13938.0f, -13938.0f, 13938.0f, waterLevelAdjust);
                            terrain->updateWaterMapTexture(waterSim.getTextureData());
                        }
                    }
                    else if (activeEditorMode == 3) {
                        // Paint Mode: Tool selection
                        for (int i = 0; i < 3; ++i) {
                            float btnY = lpY + 45 + i * 25;
                            if (leftMouseClicked && mouseY >= btnY && mouseY <= btnY + 20) {
                                editorBrushMode = 8 + i; // 8 = Paint Road, 9 = Paint River, 10 = Erase
                                std::cout << "[Editor UI] Paint tool: " << editorBrushMode << "\n";
                            }
                        }

                        // Brush Settings (Radius & Strength)
                        GameEditorUtils::updateBrushSliders(mouseX, mouseY, lpX, lpY, lpW, brushRadius, brushStrength);
                    }

                    else if (activeEditorMode == 4) {
                        // Water Level Mode: slider + step buttons
                        GameEditorUtils::updateWaterLevelSlider(mouseX, mouseY, lpX, lpY, lpW, waterLevelAdjust);
                        if (leftMouseClicked) {
                            GameEditorUtils::updateWaterLevelButtons(mouseX, mouseY, lpX, lpY, lpW, true, waterLevelAdjust);
                            GameEditorUtils::updateWaterLevelButtons(mouseX, mouseY, lpX, lpY, lpW, false, waterLevelAdjust);

                            // Reset button
                            float resetY = lpY + 185;
                            if (mouseY >= resetY && mouseY <= resetY + 22
                                && mouseX >= lpX + 15 && mouseX <= lpX + lpW - 15) {
                                waterLevelAdjust = GameEditorUtils::WaterLevelConfig::RESET_VALUE;
                                std::cout << "[Water Level] Reset to " << waterLevelAdjust << "m\n";
                            }
                        }
                        terrain->setWaterLevel(waterLevelAdjust);
                        waterSim.setOceanWaterLevel(waterLevelAdjust);
                        terrain->updateWaterMapTexture(waterSim.getTextureData());
                    }
                    else if (activeEditorMode == 5) {
                        // Road Edit Mode: tool buttons
                        for (int i = 0; i < 4; ++i) {
                            float btnY = lpY + 45 + i * 26;
                            if (leftMouseClicked && mouseY >= btnY - 2 && mouseY <= btnY + 20) {
                                roadToolMode = i;
                                if (roadToolMode == 1 && i != 1) roadDraftPoints.clear();
                                std::cout << "[Road] Tool -> " << roadToolMode << "\n";
                            }
                        }
                        RoadGraph* rg2 = terrain->getRoadGraph();
                        if (roadToolMode == 1) {
                            // NEW ROAD PROPERTY EDITOR (type, width, shoulder)
                            for (int i = 0; i < 6; ++i) {
                                float btnY = lpY + 215 + i * 22;
                                if (leftMouseClicked && mouseY >= btnY && mouseY <= btnY + 20) {
                                    roadDrawType = i;
                                    roadDrawWidth = roadDefaultWidthFor((RoadType)i);
                                    roadDrawShoulder = roadDefaultShoulderFor((RoadType)i);
                                    std::cout << "[Road] New road type -> " << roadTypeLabel(i)
                                              << " (w " << roadDrawWidth << "m, s " << roadDrawShoulder << "m)\n";
                                }
                            }
                            float wY = lpY + 358;
                            if (leftMouseClicked && mouseY >= wY && mouseY <= wY + 24) {
                if (mouseX >= lpX + 8 && mouseX <= lpX + 68) roadDrawWidth = glm::max(2.0f, roadDrawWidth - 0.5f);
                // 20 m ceiling. The road type presets top out at 11.5 m; past ~20 m the
                // fixed cross-section bands (curb at 2.8% of u, edge line at 8.5%) spread
                // out so far that the paint stops reading as a road at all.
                else if (mouseX >= lpX + 78 && mouseX <= lpX + 138) roadDrawWidth = glm::min(20.0f, roadDrawWidth + 0.5f);

                                std::cout << "[Road] New road width = " << roadDrawWidth << "m\n";
                            }
                            float sY = lpY + 396;
                            if (leftMouseClicked && mouseY >= sY && mouseY <= sY + 24) {
                                if (mouseX >= lpX + 8 && mouseX <= lpX + 68) roadDrawShoulder = glm::max(0.0f, roadDrawShoulder - 0.25f);
                                else if (mouseX >= lpX + 78 && mouseX <= lpX + 138) roadDrawShoulder = glm::min(10.0f, roadDrawShoulder + 0.25f);
                                std::cout << "[Road] New road shoulder = " << roadDrawShoulder << "m\n";
                            }
                            // New-road corner shaping. Zero keeps the automatic
                            // width-based curve; explicit values soften tight bends.
                            float cY = lpY + 430;
                            if (leftMouseClicked && mouseY >= cY && mouseY <= cY + 22) {
                                if (mouseX < lpX + lpW * 0.5f)
                                    roadDrawCornerRadius = glm::max(0.0f, roadDrawCornerRadius - 2.0f);
                                else
                                    roadDrawCornerRadius = glm::min(60.0f, roadDrawCornerRadius + 2.0f);
                            }
                            // Draw-tool option toggles (STRAIGHT / SNAP)
                            float sLy = lpY + 480;
                            if (leftMouseClicked && mouseY >= sLy && mouseY <= sLy + 22) {
                                roadStraightMode = !roadStraightMode;
                                std::cout << "[Road] Straight-line drawing " << (roadStraightMode ? "ON" : "OFF")
                                          << " (or hold Shift for one-shot)\n";
                            }
                            float sNy = lpY + 502;
                            if (leftMouseClicked && mouseY >= sNy && mouseY <= sNy + 22) {
                                roadSnapToRoads = !roadSnapToRoads;
                                std::cout << "[Road] Snap-to-road-endpoints " << (roadSnapToRoads ? "ON" : "OFF") << "\n";
                            }
                            float loopY = lpY + 524;
                            if (leftMouseClicked && mouseY >= loopY && mouseY <= loopY + 20) {
                                roadDrawClosedLoop = !roadDrawClosedLoop;
                            }
                            // DEL PT / OK COMMIT / X CANCEL buttons for the active drawing
                            float oY = lpY + 548, thirdW = (lpW - 16 - 12) / 3.0f;
                            if (leftMouseClicked && mouseY >= oY && mouseY <= oY + 22) {
                                if (mouseX >= lpX + 8 && mouseX <= lpX + 8 + thirdW) {
                                    if (!roadDraftPoints.empty()) {
                                        roadDraftPoints.pop_back();
                                        draftDragIdx = -1;
                                        std::cout << "[Road] Removed last draft point (" << roadDraftPoints.size() << " left)\n";
                                    }
                                } else if (mouseX >= lpX + 8 + thirdW + 6 && mouseX <= lpX + 8 + 2 * (thirdW + 6)) commitRoadDraft();
                                else if (mouseX >= lpX + 8 + 2 * (thirdW + 6) && mouseX <= lpX + 8 + lpW - 16) cancelRoadDraft();
                            }
                        } else if (roadToolMode == 3) {
                            // PROJECTED REFERENCE MAP CONTROLS
                            if (leftMouseClicked && mouseY >= lpY + 210 && mouseY <= lpY + 232) {
                                projRefVisible = !projRefVisible;
                                std::cout << "[Road] Terrain projection " << (projRefVisible ? "shown" : "hidden") << "\n";
                            }
                            const float cW = 140.0f, cGap = 12.0f;
                            const float x1 = lpX + 8, x2 = lpX + 8 + cW + cGap;
                            // Move X
                            if (leftMouseClicked && mouseY >= lpY + 244 && mouseY <= lpY + 266) {
                                const float step = 20.0f;
                                if (mouseX >= x1 && mouseX <= x1 + cW) projRefCenter.x -= step;
                                else if (mouseX >= x2 && mouseX <= x2 + cW) projRefCenter.x += step;
                            }
                            // Move Y (height)
                            if (leftMouseClicked && mouseY >= lpY + 278 && mouseY <= lpY + 300) {
                                const float step = 5.0f;
                                if (mouseX >= x1 && mouseX <= x1 + cW) projRefYOffset = glm::max(-200.0f, projRefYOffset - step);
                                else if (mouseX >= x2 && mouseX <= x2 + cW) projRefYOffset = glm::min(200.0f, projRefYOffset + step);
                                std::cout << "[Road] Projection height = " << projRefYOffset << "m\n";
                            }
                            // Move Z
                            if (leftMouseClicked && mouseY >= lpY + 312 && mouseY <= lpY + 334) {
                                const float step = 20.0f;
                                if (mouseX >= x1 && mouseX <= x1 + cW) projRefCenter.y -= step;
                                else if (mouseX >= x2 && mouseX <= x2 + cW) projRefCenter.y += step;
                            }
                            // Scale
                            if (leftMouseClicked && mouseY >= lpY + 354 && mouseY <= lpY + 376) {
                                if (mouseX >= x1 && mouseX <= x1 + cW) projRefScale = glm::max(0.1f, projRefScale - 0.1f);
                                else if (mouseX >= x2 && mouseX <= x2 + cW) projRefScale = glm::min(10.0f, projRefScale + 0.1f);
                            }
                            // Flip
                            if (leftMouseClicked && mouseY >= lpY + 388 && mouseY <= lpY + 410) {
                                if (mouseX >= x1 && mouseX <= x1 + cW) projRefFlipX = !projRefFlipX;
                                else if (mouseX >= x2 && mouseX <= x2 + cW) projRefFlipZ = !projRefFlipZ;
                                std::cout << "[Road] Flip X=" << projRefFlipX << " Z=" << projRefFlipZ << "\n";
                            }
                            // Opacity
                            if (leftMouseClicked && mouseY >= lpY + 422 && mouseY <= lpY + 444) {
                                if (mouseX >= x1 && mouseX <= x1 + cW) projRefOpacity = glm::max(0.1f, projRefOpacity - 0.1f);
                                else if (mouseX >= x2 && mouseX <= x2 + cW) projRefOpacity = glm::min(1.0f, projRefOpacity + 0.1f);
                            }
                            // Reset
                            if (leftMouseClicked && mouseY >= lpY + 456 && mouseY <= lpY + 478) {
                                projRefVisible = true;
                                projRefScale = 1.0f;
                                projRefFlipX = projRefFlipZ = false;
                                projRefOpacity = 0.85f;
                                projRefYOffset = 0.0f;
                                projRefInitialized = false;
                                std::cout << "[Road] Projection reset (next render re-places from saved file or Port)\n";
                            }
                            // Save placement & scale to disk (restored on next launch)
                            if (leftMouseClicked && mouseY >= lpY + 494 && mouseY <= lpY + 516) {
                                saveProjRefMap();
                            }
                            // Reference map image: prev / next
                            const float mapBtnW = (lpW - 16 - 6) * 0.5f;
                            float mY = lpY + 556;
                            if (leftMouseClicked && mouseY >= mY && mouseY <= mY + 22) {
                                int dir = 0;
                                if (mouseX >= lpX + 8 && mouseX <= lpX + 8 + mapBtnW) dir = -1;
                                else if (mouseX >= lpX + 8 + mapBtnW + 6 && mouseX <= lpX + 8 + lpW - 16) dir = +1;
                                if (dir) {
                                    int next = (roadRefIdx + dir + roadRefPathCount) % roadRefPathCount;
                                    if (next != roadRefIdx) {
                                        roadRefIdx = next;
                                        roadRefLoaded = false;
                                        std::cout << "[Road] Reference map -> " << roadRefPaths[roadRefIdx] << "\n";
                                    }
                                }
                            }
                            // Picture-in-picture toggle
                            float ppY = lpY + 586;
                            if (leftMouseClicked && mouseY >= ppY && mouseY <= ppY + 22) {
                                roadRefVisible = !roadRefVisible;
                                std::cout << "[Road] Reference image " << (roadRefVisible ? "shown" : "hidden") << "\n";
                            }
                        } else if (rg2 && selectedRoadIdx >= 0 && selectedRoadIdx < (int)rg2->segments.size()) {
                            // Texture (road type) buttons for the selected road
                            for (int i = 0; i < 6; ++i) {
                                float btnY = lpY + 260 + i * 22;
                                if (leftMouseClicked && mouseY >= btnY && mouseY <= btnY + 18) {
                                    auto& seg = rg2->segments[selectedRoadIdx];
                                    seg.type = (RoadType)i;
                                    seg.width = roadDefaultWidthFor(seg.type);
                                    seg.shoulderWidth = roadDefaultShoulderFor(seg.type);
                                    roadGraphDirty = true;
                                    std::cout << "[Road] Segment texture -> " << roadTypeLabel(i) << "\n";
                                }
                            }
                            // Width -10% / +10% for the selected road (was '=' / '-')
                            float wYb = lpY + 438;
                            if (leftMouseClicked && mouseY >= wYb && mouseY <= wYb + 22) {
                                auto& segW = rg2->segments[selectedRoadIdx];
                                if (mouseX >= lpX + 70 && mouseX <= lpX + 130) segW.width = glm::clamp(segW.width / 1.1f, 2.0f, 40.0f);
                                else if (mouseX >= lpX + 136 && mouseX <= lpX + 196) segW.width = glm::clamp(segW.width * 1.1f, 2.0f, 40.0f);
                                roadGraphDirty = true;
                                std::cout << "[Road] Selected width -> " << segW.width << "m (id=" << segW.id << ")\n";
                            }
                            // Corner RADIUS slider for the selected road: drag to round
                            // every interior corner of that road. Left end = auto (half the
                            // road width, the historical behaviour).
                            float cYb = lpY + 468;
                            if (leftMouseDown && mouseY >= cYb && mouseY <= cYb + 22) {
                                auto& segC = rg2->segments[selectedRoadIdx];
                                const float trackX0 = lpX + 90.0f, trackW = 210.0f;
                                const float mx = (float)mouseX;
                                float norm = glm::clamp((mx - trackX0) / trackW, 0.0f, 1.0f);
                                float want = std::floor(norm * 60.0f + 0.5f) * 0.5f;   // 0..30 m, 0.5 m steps
                                if (want != segC.cornerRadius) {
                                    segC.cornerRadius = want;
                                    roadGraphDirty = true;
                                    std::cout << "[Road] Corner radius -> " << want
                                              << "m (id=" << segC.id << ")\n";
                                }
                            }
                            // Delete selected point/road (was Del key)
                            float dYb = lpY + 498;
                            if (leftMouseClicked && mouseY >= dYb && mouseY <= dYb + 22) {
                                if (selectedRoadPointIdx >= 0 &&
                                    (int)rg2->segments[selectedRoadIdx].controlPoints.size() > 2) {
                                    rg2->segments[selectedRoadIdx].controlPoints.erase(
                                        rg2->segments[selectedRoadIdx].controlPoints.begin() + selectedRoadPointIdx);
                                    selectedRoadPointIdx = -1;
                                    std::cout << "[Road] Removed control point\n";
                                } else {
                                    rg2->segments.erase(rg2->segments.begin() + selectedRoadIdx);
                                    selectedRoadIdx = -1;
                                    selectedRoadPointIdx = -1;
                                    std::cout << "[Road] Deleted selected road\n";
                                }
                                roadGraphDirty = true;
                            }
                        }
                    }
                    float saveBtnY = lpH - 35 + lpY;
                    if (leftMouseClicked && mouseY >= saveBtnY && mouseY <= saveBtnY + 26) {
                        performSave();
                    }
                }
            }
            // Check Right Panel Clicks (Road Graph Transform, mode 5)
            else if (activeEditorMode == 5 && roadPanelsVisible && mouseX >= rpX && mouseX <= rpX + rpW && mouseY >= rpY && mouseY <= rpY + rpH) {
                clickedUI = true;
                if (leftMouseClicked) {
                    const float colW = 150.0f, gap = 10.0f, smallColW = 96.0f;
                    float dy = rpY + 45;

                    // Mirror buttons
                    float mbY = dy + 22;
                    if (mouseY >= mbY && mouseY <= mbY + 26) {
                        if (mouseX >= rpX + 15 && mouseX <= rpX + 15 + colW) terrain->mirrorRoads(true);
                        else if (mouseX >= rpX + 15 + colW + gap && mouseX <= rpX + 15 + colW + gap + colW) terrain->mirrorRoads(false);
                    }
                    dy = mbY + 40;

                    // Rotate buttons
                    float rbY = dy + 22;
                    if (mouseY >= rbY && mouseY <= rbY + 26) {
                        if (mouseX >= rpX + 15 && mouseX <= rpX + 15 + smallColW) terrain->rotateRoads(-90.0f);
                        else if (mouseX >= rpX + 15 + smallColW + gap && mouseX <= rpX + 15 + 2 * (smallColW + gap)) terrain->rotateRoads(180.0f);
                        else if (mouseX >= rpX + 15 + 2 * (smallColW + gap) && mouseX <= rpX + 15 + 3 * smallColW + 2 * gap) terrain->rotateRoads(90.0f);
                    }
                    dy = rbY + 40;

                    // Scale buttons
                    float scY = dy + 22;
                    if (mouseY >= scY && mouseY <= scY + 26) {
                        if (mouseX >= rpX + 15 && mouseX <= rpX + 15 + colW) roadTransformScale = glm::clamp(roadTransformScale - 0.05f, 0.2f, 3.0f);
                        else if (mouseX >= rpX + 15 + colW + gap && mouseX <= rpX + 15 + colW + gap + colW) roadTransformScale = glm::clamp(roadTransformScale + 0.05f, 0.2f, 3.0f);
                    }
                    float applyY = scY + 32;
                    if (mouseY >= applyY && mouseY <= applyY + 26 && mouseX >= rpX + 15 && mouseX <= rpX + rpW - 15) {
                        terrain->scaleRoads(roadTransformScale);
                    }
                    float resetY = applyY + 32;
                    if (mouseY >= resetY && mouseY <= resetY + 26 && mouseX >= rpX + 15 && mouseX <= rpX + rpW - 15) {
                        roadTransformScale = 1.0f;
                        std::cout << "[Road] Scale reset to 1.0x\n";
                    }
                    dy = resetY + 40;

                    // Connect buttons
                    float cbY = dy + 22;
                    if (mouseY >= cbY && mouseY <= cbY + 26 && mouseX >= rpX + 15 && mouseX <= rpX + rpW - 15) {
                        terrain->connectRoads(roadConnectTolerance);
                    }
                    float tolY = cbY + 32;
                    if (mouseY >= tolY && mouseY <= tolY + 26) {
                        if (mouseX >= rpX + 15 && mouseX <= rpX + 15 + colW) roadConnectTolerance = glm::clamp(roadConnectTolerance - 5.0f, 5.0f, 100.0f);
                        else if (mouseX >= rpX + 15 + colW + gap && mouseX <= rpX + 15 + colW + gap + colW) roadConnectTolerance = glm::clamp(roadConnectTolerance + 5.0f, 5.0f, 100.0f);
                    }
                    float mdy = tolY + 40;
                    float mY1 = mdy + 22;
                    if (mouseY >= mY1 && mouseY <= mY1 + 26) {
                        const float st = roadMoveStep;
                        if (mouseX >= rpX + 15 && mouseX <= rpX + 15 + colW) terrain->translateRoads(-st, 0.0f);
                        else if (mouseX >= rpX + 15 + colW + gap && mouseX <= rpX + 15 + colW + gap + colW) terrain->translateRoads(st, 0.0f);
                    }
                    float mY2 = mY1 + 32;
                    if (mouseY >= mY2 && mouseY <= mY2 + 26) {
                        const float st = roadMoveStep;
                        if (mouseX >= rpX + 15 && mouseX <= rpX + 15 + colW) terrain->translateRoads(0.0f, -st);
                        else if (mouseX >= rpX + 15 + colW + gap && mouseX <= rpX + 15 + colW + gap + colW) terrain->translateRoads(0.0f, st);
                    }
                    float mY3 = mY2 + 32;
                    if (mouseY >= mY3 && mouseY <= mY3 + 26) {
                        if (mouseX >= rpX + 15 && mouseX <= rpX + 15 + colW) roadMoveStep = glm::clamp(roadMoveStep - 5.0f, 5.0f, 200.0f);
                        else if (mouseX >= rpX + 15 + colW + gap && mouseX <= rpX + 15 + colW + gap + colW) roadMoveStep = glm::clamp(roadMoveStep + 5.0f, 5.0f, 200.0f);
                    }
                }
            }
            // Check Right Panel Clicks (Details)
            else if (selectedPlacedIdx >= 0 &&
                     selectedPlacedIdx < (int)SousseMapData::liveLandmarks.size() &&
                     mouseX >= rpX && mouseX <= rpX + rpW && mouseY >= rpY && mouseY <= rpY + rpH) {
                clickedUI = true;
                auto& landmark = SousseMapData::liveLandmarks[selectedPlacedIdx];
                if (leftMouseDown) {
                    // 1. Height Offset Slider
                    float hSliderY = rpY + 110;
                    if (mouseY >= hSliderY && mouseY <= hSliderY + 15) {
                        float norm = glm::clamp(((float)mouseX - (rpX + 15)) / (rpW - 30), 0.0f, 1.0f);
                        landmark.yOffset = -15.0f + norm * 30.0f; // -15m to +15m
                        SousseMapData::layoutDirty = true;
                        terrain->markBatchesDirty();
                    }

                    // 2. Yaw Rotation Slider
                    float rSliderY = rpY + 160;
                    if (mouseY >= rSliderY && mouseY <= rSliderY + 15) {
                        float norm = glm::clamp(((float)mouseX - (rpX + 15)) / (rpW - 30), 0.0f, 1.0f);
                        landmark.angle = norm * 360.0f; // 0 to 360 deg
                        SousseMapData::layoutDirty = true;
                        terrain->markBatchesDirty();
                    }

                    // 3. Scale Slider
                    float sSliderY = rpY + 210;
                    if (mouseY >= sSliderY && mouseY <= sSliderY + 15) {
                        float norm = glm::clamp(((float)mouseX - (rpX + 15)) / (rpW - 30), 0.0f, 1.0f);
                        float scale = 0.1f + norm * 4.9f; // 0.1 to 5.0
                        landmark.xScale = scale;
                        landmark.yScale = scale;
                        landmark.zScale = scale;
                        SousseMapData::layoutDirty = true;
                        terrain->markBatchesDirty();
                    }

                    // 4. Materials List (Textures) & Multi-layer selection
                    float layerTabY = rpY + 260;
                    if (leftMouseClicked && mouseY >= layerTabY && mouseY <= layerTabY + 20) {
                        if (mouseX >= rpX + 15 && mouseX <= rpX + 165) {
                            activeTextureLayer = 0;
                            std::cout << "[Editor UI] Active texture layer set to 1 (Base)\n";
                        }
                        if (mouseX >= rpX + 175 && mouseX <= rpX + 325) {
                            activeTextureLayer = 1;
                            std::cout << "[Editor UI] Active texture layer set to 2 (Blend)\n";
                        }
                    }

                    static int texturePage = 0;
                    float texStartY = rpY + 290;
                    for (int i = 0; i < 6; ++i) {
                        float btnY = texStartY + i * 24;
                        int idx = texturePage * 6 + i;
                        if (idx < (int)SousseMapData::discoveredTextures.size()) {
                            if (leftMouseClicked && mouseY >= btnY && mouseY <= btnY + 20) {
                                if (activeTextureLayer == 0) {
                                    landmark.customTexturePath = SousseMapData::discoveredTextures[idx];
                                } else {
                                    landmark.customTexturePath2 = SousseMapData::discoveredTextures[idx];
                                }
                                selectedTextureIdx = idx;
                                SousseMapData::layoutDirty = true;
                                terrain->markBatchesDirty();
                            }
                        }
                    }

                    // Reset texture button
                    float resetY = texStartY + 6 * 24 + 10;
                    if (leftMouseClicked && mouseY >= resetY && mouseY <= resetY + 20) {
                        if (activeTextureLayer == 0) {
                            landmark.customTexturePath = "";
                        } else {
                            landmark.customTexturePath2 = "";
                        }
                        SousseMapData::layoutDirty = true;
                        terrain->markBatchesDirty();
                    }

                    // Paging controls for textures
                    if (leftMouseClicked && mouseY >= resetY && mouseY <= resetY + 20) {
                        int texPerPage = 6;
                        if (mouseX >= rpX + 180 && mouseX <= rpX + 230 && texturePage > 0) {
                            texturePage--;
                        }
                        if (mouseX >= rpX + 240 && mouseX <= rpX + 320 && (texturePage + 1) * texPerPage < (int)SousseMapData::discoveredTextures.size()) {
                            texturePage++;
                        }
                    }

                    // Texture Blend Weight Slider (rpY + 465)
                    float bSliderY = rpY + 465;
                    if (mouseY >= bSliderY && mouseY <= bSliderY + 15) {
                        float norm = glm::clamp(((float)mouseX - (rpX + 15)) / (rpW - 30), 0.0f, 1.0f);
                        landmark.textureBlend = norm;
                        SousseMapData::layoutDirty = true;
                        terrain->markBatchesDirty();
                    }

                    // 5. Actions (Clone/Delete) (shifted down to accommodate blend slider)
                    float cloneY = rpY + 520;
                    if (leftMouseClicked && mouseY >= cloneY && mouseY <= cloneY + 26) {
                        auto clone = landmark;
                        glm::vec3 playerPos = glm::vec3(player->getPosition().x, player->getPosition().y, player->getPosition().z);
                        glm::vec3 dir = player->getForwardDirection();
                        glm::vec3 placementPos = playerPos + dir * 15.0f;
                        placementPos.y = terrain->getHeight(placementPos.x, placementPos.z);
                        
                        clone.name = clone.name + "_copy";
                        clone.localOffset = glm::vec2(placementPos.x, placementPos.z) - SousseMapData::WORLD_ANCHOR;
                        clone.bakedY = placementPos.y;
                        clone.placed = true;
                        
                        SousseMapData::liveLandmarks.push_back(clone);
                        selectedPlacedIdx = (int)SousseMapData::liveLandmarks.size() - 1;
                        SousseMapData::layoutDirty = true;
                        terrain->markBatchesDirty();
                    }

                    float deleteY = rpY + 555;
                    if (leftMouseClicked && mouseY >= deleteY && mouseY <= deleteY + 26) {
                        landmark.placed = false;
                        selectedPlacedIdx = -1;
                        SousseMapData::layoutDirty = true;
                        terrain->markBatchesDirty();
                    }
                }
            }

            // Only run 3D raycasting and editing if we didn't click on UI panels
            if (!clickedUI) {
                float aspectRatio = (float)windowWidth / (float)windowHeight;
                glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspectRatio, 0.5f, 10000.0f);
                glm::mat4 viewMatrix = camera->getViewMatrix();

                // Calculate ray direction in world space
                float x = (2.0f * (float)mouseX) / windowWidth - 1.0f;
                float y = 1.0f - (2.0f * (float)mouseY) / windowHeight;
                glm::vec4 ray_clip = glm::vec4(x, y, -1.0f, 1.0f);
                glm::vec4 ray_eye = glm::inverse(proj) * ray_clip;
                ray_eye = glm::vec4(ray_eye.x, ray_eye.y, -1.0f, 0.0f);
                glm::vec3 rayDir = glm::normalize(glm::vec3(glm::inverse(viewMatrix) * ray_eye));
                
                auto cameraPosCustom = camera->getPosition();
                glm::vec3 rayOrigin = glm::vec3(cameraPosCustom.x, cameraPosCustom.y, cameraPosCustom.z);

                // Step-based ray intersection on terrain
                glm::vec3 currentPos = rayOrigin;
                float maxDistance = 2000.0f;
                float stepSize = 3.0f;
                bool hit = false;
                for (float dist = 0.0f; dist < maxDistance; dist += stepSize) {
                    currentPos = rayOrigin + rayDir * dist;
                    float terrainH = terrain->getHeight(currentPos.x, currentPos.z);
                    if (currentPos.y <= terrainH) {
                        float startDist = dist - stepSize;
                        float endDist = dist;
                        for (int i = 0; i < 8; ++i) {
                            float midDist = (startDist + endDist) * 0.5f;
                            glm::vec3 midPos = rayOrigin + rayDir * midDist;
                            float midH = terrain->getHeight(midPos.x, midPos.z);
                            if (midPos.y <= midH) endDist = midDist;
                            else startDist = midDist;
                        }
                        lastRayHitPos = rayOrigin + rayDir * ((startDist + endDist) * 0.5f);
                        rayHitTerrain = true;
                        hit = true;
                        break;
                    }
                }

                if (hit) {
                    if (activeEditorMode == 5) {
                        // ── Road Edit tool interactions ──
                        RoadGraph* rg = terrain->getRoadGraph();
                        if (rg) {
                            if (roadToolMode == 0) { // Select road
                                if (leftMouseClicked) {
                                    // Pick against the EVALUATED centreline, not the raw
                                    // control points: between control points the drawn
                                    // road follows a Catmull-Rom curve, so a chord test
                                    // misses the road across the middle of every long
                                    // straight and rounds the wrong corner. The pick
                                    // radius follows each road's own width.
                                    int best = -1; float bestD = 1e9f; float bestR = 10.0f;
                                    for (size_t s = 0; s < rg->segments.size(); ++s) {
                                        const auto& seg = rg->segments[s];
                                        const auto& sp = seg.splinePoints;
                                        const float r = glm::max(4.0f, seg.width * 0.75f);
                                        if (sp.size() < 2) continue;
                                        for (size_t i = 0; i + 1 < sp.size(); ++i) {
                                            float d = distPointToPolylineSegment(lastRayHitPos.x, lastRayHitPos.z,
                                                                                sp[i].x, sp[i].z,
                                                                                sp[i+1].x, sp[i+1].z);
                                            if (d < r && d < bestD) { bestD = d; best = (int)s; bestR = r; }
                                        }
                                    }
                                    selectedRoadIdx = best;
                                    selectedRoadPointIdx = -1;
                                    if (best >= 0) {
                                        std::cout << "[Road] Selected id=" << rg->segments[best].id
                                                  << " type=" << roadTypeLabel((int)rg->segments[best].type)
                                                  << " (" << rg->segments[best].controlPoints.size()
                                                  << " pts, pick radius " << bestR << "m)\n";
                                    } else {
                                        std::cout << "[Road] No road under the cursor (Select tool)\n";
                                    }
                                }
                            }
                            else if (roadToolMode == 1) { // Draw road
                                if (!leftMouseDown) draftDragIdx = -1; // mouse released, stop moving
                                // Straight-line axis lock (0/45/90 deg): Shift-hold or roadStraightMode, from the previous point
                                auto makePos = [&](const glm::vec3& in) -> glm::vec3 {
                                    glm::vec3 p = in;
                                    if ((roadShiftHeld || roadStraightMode) && !roadDraftPoints.empty()) {
                                        const glm::vec3& last = roadDraftPoints.back();
                                        float dx = p.x - last.x, dz = p.z - last.z;
                                        const float adx = std::fabs(dx), adz = std::fabs(dz);
                                        if (adx >= adz) {
                                            dz = (adz > adx * 0.4142f) ? (dz < 0 ? -adx : adx) : 0.0f;  // diagonal or horizontal
                                        } else {
                                            dx = (adx > adz * 0.4142f) ? (dx < 0 ? -adz : adz) : 0.0f;  // diagonal or vertical
                                        }
                                        p.x = last.x + dx; p.z = last.z + dz;
                                    }
                                    return p;
                                };
                                // Snap onto the nearest existing road ENDPOINT (first/last control point)
                                // Also snap to the draft's own FIRST point so the user can close a loop.
                                auto snapPt = [&](const glm::vec3& in) -> glm::vec3 {
                                    if (!roadSnapToRoads) return in;
                                    glm::vec3 out = in;
                                    float bestD = roadSnapTolerance;
                                    // Snap to existing committed road endpoints
                                    for (const auto& seg : rg->segments) {
                                        const auto& cps = seg.controlPoints;
                                        if (cps.empty()) continue;
                                        auto probe = [&](const RoadPoint& cp) {
                                            float dx = out.x - cp.pos.x, dz = out.z - cp.pos.y;
                                            float d = std::sqrt(dx * dx + dz * dz);
                                            if (d < bestD) { bestD = d; out = glm::vec3(cp.pos.x, in.y, cp.pos.y); }
                                        };
                                        probe(cps.front());
                                        if (cps.size() > 1) probe(cps.back());
                                    }
                                    // Snap onto an existing road's CENTRELINE. Endpoint-only
                                    // snapping made a T-junction impossible to draw: you can
                                    // never end a street on the middle of another one, so the
                                    // branch always stopped short, missed the endpoint cluster
                                    // and got a rounded cap floating over open ground. A
                                    // branch that ends anywhere on a host centreline welds
                                    // onto that host's near edge instead.
                                    for (const auto& seg : rg->segments) {
                                        const auto& sp = seg.splinePoints;
                                        if (sp.size() < 2) continue;
                                        for (size_t i = 0; i + 1 < sp.size(); ++i) {
                                            const glm::vec2 a(sp[i].x, sp[i].z);
                                            const glm::vec2 ab(sp[i+1].x - a.x, sp[i+1].z - a.y);
                                            const float len2 = glm::dot(ab, ab);
                                            if (len2 < 1e-8f) continue;
                                            const float t = glm::clamp(
                                                glm::dot(glm::vec2(out.x, out.z) - a, ab) / len2, 0.0f, 1.0f);
                                            const glm::vec2 q = a + ab * t;
                                            const float d = glm::length(glm::vec2(out.x, out.z) - q);
                                            if (d < bestD) { bestD = d; out = glm::vec3(q.x, in.y, q.y); }
                                        }
                                    }
                                    // Snap to the draft's own first point (closes a loop / round road)
                                    if (roadDraftPoints.size() >= 3) {
                                        const glm::vec3& front = roadDraftPoints.front();
                                        float dx = out.x - front.x, dz = out.z - front.z;
                                        float d = std::sqrt(dx * dx + dz * dz);
                                        if (d < bestD) { bestD = d; out = glm::vec3(front.x, in.y, front.z); }
                                    }
                                    return out;
                                };
                                auto emitP = [&](const glm::vec3& p) {
                                    if (!roadDraftPoints.empty()) {
                                        float dx = p.x - roadDraftPoints.back().x, dz = p.z - roadDraftPoints.back().z;
                                        if (dx * dx + dz * dz < 1.0f) return; // ignore degenerate duplicates
                                    }
                                    roadDraftPoints.push_back(p);
                                    std::cout << "[Road] Draft point " << roadDraftPoints.size()
                                              << " @ (" << std::fixed << std::setprecision(1)
                                              << p.x << ", " << p.z << ")\n";
                                };
                                if (leftMouseClicked) {
                                    // 1) Grab & move an existing draft control point (only when clicked right ON one)
                                    int nearestP = -1; float nearestD = 3.0f;
                                    for (int i = 0; i < (int)roadDraftPoints.size(); ++i) {
                                        float dx = lastRayHitPos.x - roadDraftPoints[i].x;
                                        float dz = lastRayHitPos.z - roadDraftPoints[i].z;
                                        float d = std::sqrt(dx * dx + dz * dz);
                                        if (d < nearestD) { nearestD = d; nearestP = i; }
                                    }
                                    if (nearestP >= 0) {
                                        draftDragIdx = nearestP;
                                        roadDraftPoints[draftDragIdx] = snapPt(lastRayHitPos);
                                        std::cout << "[Road] Moving draft point " << nearestP << "\n";
                                    }
                                    // 2) Insert a new control point into the nearest draft edge
                                    else if (roadDraftPoints.size() >= 2) {
                                        float bestE = 400.0f; int bestI = -1;
                                        for (int i = 0; i + 1 < (int)roadDraftPoints.size(); ++i) {
                                            float d = distPointToPolylineSegment(lastRayHitPos.x, lastRayHitPos.z,
                                                                                roadDraftPoints[i].x, roadDraftPoints[i].z,
                                                                                roadDraftPoints[i + 1].x, roadDraftPoints[i + 1].z);
                                            if (d < bestE && d < 12.0f) { bestE = d; bestI = i; }
                                        }
                                        if (bestI >= 0) {
                                            roadDraftPoints.insert(roadDraftPoints.begin() + bestI + 1, snapPt(lastRayHitPos));
                                            draftDragIdx = bestI + 1;
                                            draftInsertNewly = true;
                                            std::cout << "[Road] Inserted draft point " << (bestI + 1)
                                                      << " (drag to position it)\n";
                                        } else {
                                            draftDragIdx = -1;
                                            emitP(snapPt(makePos(lastRayHitPos)));
                                        }
                                    } else {
                                        emitP(snapPt(makePos(lastRayHitPos)));
                                    }
                                } else if (leftMouseDown && draftDragIdx >= 0 &&
                                           draftDragIdx < (int)roadDraftPoints.size()) {
                                    // Drag the grabbed/inserted point along the terrain
                                    roadDraftPoints[draftDragIdx] = snapPt(lastRayHitPos);
                                    // First drag frame after an insert: publish it as a real point
                                    if (draftInsertNewly) {
                                        draftInsertNewly = false;
                                        std::cout << "[Road] Placed inserted point " << draftDragIdx << "\n";
                                    }
                                } else if (leftMouseDown && !roadDraftPoints.empty()) {
                                    // Drag to draw: append a point every 5m the cursor travels
                                    const glm::vec3& last = roadDraftPoints.back();
                                    glm::vec3 p = snapPt(lastRayHitPos);
                                    const float dx = p.x - last.x, dz = p.z - last.z;
                                    if (dx * dx + dz * dz >= 25.0f) {
                                        emitP(p);
                                    }
                                }
                            }
                            else if (roadToolMode == 2) { // Edit points
                                if (selectedRoadIdx >= 0 && selectedRoadIdx < (int)rg->segments.size()) {
                                    auto& cps = rg->segments[selectedRoadIdx].controlPoints;
                                    if (leftMouseClicked) {
                                        int bestP = -1; float bestPD = 12.0f;
                                        for (size_t i = 0; i < cps.size(); ++i) {
                                            float dx = lastRayHitPos.x - cps[i].pos.x;
                                            float dz = lastRayHitPos.z - cps[i].pos.y;
                                            float d = std::sqrt(dx*dx + dz*dz);
                                            if (d < bestPD) { bestPD = d; bestP = (int)i; }
                                        }
                                        if (bestP >= 0) {
                                            selectedRoadPointIdx = bestP;
                                            std::cout << "[Road] Grabbed control point " << bestP << "\n";
                                        } else if (cps.size() >= 2) {
                                            // Insert a new control point on the nearest segment
                                            float bestDD = 400.0f; int bestSI = -1;
                                            for (size_t i = 0; i + 1 < cps.size(); ++i) {
                                                float ax = cps[i].pos.x, az = cps[i].pos.y;
                                                float bx = cps[i+1].pos.x, bz = cps[i+1].pos.y;
                                                float dx = bx - ax, dz = bz - az;
                                                float len2 = dx*dx + dz*dz;
                                                float t = (len2 > 1e-8f) ? glm::clamp(((lastRayHitPos.x - ax) * dx + (lastRayHitPos.z - az) * dz) / len2, 0.0f, 1.0f) : 0.0f;
                                                float px = ax + t * dx, pz = az + t * dz;
                                                float d = (lastRayHitPos.x - px) * (lastRayHitPos.x - px) + (lastRayHitPos.z - pz) * (lastRayHitPos.z - pz);
                                                if (d < bestDD) { bestDD = d; bestSI = (int)i; }
                                            }
                                            if (bestSI >= 0) {
                                                RoadPoint np; np.pos.x = lastRayHitPos.x; np.pos.y = lastRayHitPos.z;
                                                cps.insert(cps.begin() + bestSI + 1, np);
                                                selectedRoadPointIdx = bestSI + 1;
                                                roadGraphDirty = true;
                                                std::cout << "[Road] Inserted control point " << (bestSI + 1) << "\n";
                                            }
                                        }
                                    } else if (leftMouseDown && selectedRoadPointIdx >= 0 && selectedRoadPointIdx < (int)cps.size()) {
                                        auto& cp = cps[selectedRoadPointIdx];
                                        glm::vec2 dragged(lastRayHitPos.x, lastRayHitPos.z);
                                        const bool isEndpoint = selectedRoadPointIdx == 0 ||
                                            selectedRoadPointIdx == (int)cps.size() - 1;
                                        if (roadSnapToRoads && isEndpoint && cps.size() >= 2) {
                                            const int adjacent = selectedRoadPointIdx == 0 ? 1 : (int)cps.size() - 2;
                                            glm::vec2 branchInterior = cps[adjacent].pos - dragged;
                                            const float interiorLen = glm::length(branchInterior);
                                            if (interiorLen > 1e-4f) {
                                                branchInterior /= interiorLen;
                                                const float snapRange = std::max(roadSnapTolerance,
                                                    rg->segments[selectedRoadIdx].width);
                                                float bestSnap = snapRange;
                                                glm::vec2 snapped = dragged;
                                                for (int h = 0; h < (int)rg->segments.size(); ++h) {
                                                    if (h == selectedRoadIdx) continue;
                                                    const RoadSegment& host = rg->segments[h];
                                                    const int pointCount = (int)host.splinePoints.size();
                                                    if (pointCount < 2) continue;
                                                    const int edgeCount = host.closed ? pointCount : pointCount - 1;
                                                    for (int k = 0; k < edgeCount; ++k) {
                                                        const int next = (k + 1) % pointCount;
                                                        const glm::vec2 a(host.splinePoints[k].x, host.splinePoints[k].z);
                                                        const glm::vec2 b(host.splinePoints[next].x, host.splinePoints[next].z);
                                                        const glm::vec2 ab = b - a;
                                                        const float ab2 = glm::dot(ab, ab);
                                                        if (ab2 < 1e-8f) continue;
                                                        const float t = glm::clamp(glm::dot(dragged - a, ab) / ab2, 0.0f, 1.0f);
                                                        const glm::vec2 center = a + ab * t;
                                                        const glm::vec2 tangent = glm::normalize(ab);
                                                        if (std::fabs(glm::dot(branchInterior, tangent)) > kRoadJunctionApproachCos)
                                                            continue;
                                                        const glm::vec2 right(tangent.y, -tangent.x);
                                                        const float side = glm::dot(branchInterior, right) >= 0.0f ? 1.0f : -1.0f;
                                                        const glm::vec2 edge = center + right *
                                                            (side * roadWhiteEdgeLineOffset(host.width));
                                                        const float d = glm::length(dragged - edge);
                                                        if (d < bestSnap) { bestSnap = d; snapped = edge; }
                                                    }
                                                }
                                                dragged = snapped;
                                            }
                                        }
                                        if (cp.pos.x != dragged.x || cp.pos.y != dragged.y) {
                                            cp.pos.x = dragged.x;
                                            cp.pos.y = dragged.y;
                                            roadGraphDirty = true;
                                        }
                                    }
                                }
                            }
                            else if (roadToolMode == 3) { // Move projected reference map
                                static bool projDragging = false;
                                static float projLastX = 0.0f, projLastZ = 0.0f;
                                if (leftMouseDown && projRefVisible) {
                                    if (projDragging) {
                                        projRefCenter.x += lastRayHitPos.x - projLastX;
                                        projRefCenter.y += lastRayHitPos.z - projLastZ;
                                    }
                                    projDragging = true;
                                    projLastX = lastRayHitPos.x;
                                    projLastZ = lastRayHitPos.z;
                                } else {
                                    projDragging = false;
                                }
                            }
                        }
                    }
                    else if (editorBrushMode == 0) { // Select / Move
                        static bool lastLMB = false;
                        if (leftMouseDown && !lastLMB) {
                            int closestIdx = -1;
                            float minSelectDist = 1e9f;
                            for (int i = 0; i < (int)SousseMapData::liveLandmarks.size(); ++i) {
                                auto& landmark = SousseMapData::liveLandmarks[i];
                                if (!landmark.placed) continue;
                                glm::vec2 worldPos = SousseMapData::toWorld(landmark.localOffset);
                                glm::vec3 center(worldPos.x, landmark.bakedY + landmark.yOffset, worldPos.y);
                                
                                glm::vec3 v = center - rayOrigin;
                                float t = glm::dot(v, rayDir);
                                if (t > 0.0f) {
                                    glm::vec3 closestPoint = rayOrigin + rayDir * t;
                                    float distToRay = glm::length(center - closestPoint);
                                    float hitRadius = 25.0f * std::max({landmark.xScale, landmark.yScale, landmark.zScale});
                                    if (distToRay < hitRadius && distToRay < minSelectDist) {
                                        minSelectDist = distToRay;
                                        closestIdx = i;
                                    }
                                }
                            }
                            if (closestIdx != -1) {
                                selectedPlacedIdx = closestIdx;
                                std::cout << "[Editor] Selected placed actor: " << SousseMapData::liveLandmarks[selectedPlacedIdx].name << "\n";
                            }
                        } else if (leftMouseDown && lastLMB && selectedPlacedIdx != -1) {
                            auto& landmark = SousseMapData::liveLandmarks[selectedPlacedIdx];
                            landmark.localOffset = glm::vec2(lastRayHitPos.x, lastRayHitPos.z) - SousseMapData::WORLD_ANCHOR;
                            landmark.bakedY = -1e9; // Re-snap to terrain
                            SousseMapData::layoutDirty = true;
                            terrain->markBatchesDirty();
                        }
                        lastLMB = leftMouseDown;
                    }
                    else if (activeEditorMode == 2 && editorBrushMode >= 1 && editorBrushMode <= 5) { // Sculpt brushes
                        if (leftMouseDown && terrain->terrainManager) {
                            terrain->terrainManager->sculpt(lastRayHitPos.x, lastRayHitPos.z, brushRadius,
                                brushStrength, editorBrushMode, static_cast<float>(deltaTime),
                                sculptAngleDegrees, sculptGradeDegrees);
                            waterSim.updateTerrainArea(terrain->terrainManager, lastRayHitPos.x, lastRayHitPos.z, brushRadius);
                            terrain->updateWaterMapTexture(waterSim.getTextureData());
                        }
                    }
                    else if (activeEditorMode == 2 && editorBrushMode == 6) { // Fit terrain to the road bed
                        RoadMeshBuilder* roadMesh = terrain->getRoadMeshBuilder();
                        static bool shapingRoadBed = false;
                        if (leftMouseDown && terrain->terrainManager && roadMesh) {
                            shapingRoadBed = true;
                            // Cuts where the ground is above the asphalt and fills where
                            // it is below, so the graded rise shows on the terrain itself.
                            terrain->terrainManager->conformTerrainToRoadMesh(
                                lastRayHitPos.x, lastRayHitPos.z, brushRadius, brushStrength,
                                static_cast<float>(deltaTime), roadMesh->debugVertices(),
                                roadMesh->debugIndices());
                        } else if (shapingRoadBed) {
                            // Re-seat the ribbon on release, not every frame: doing it
                            // per frame made the conform chase terrain it had just
                            // lowered, sinking the road a little further on each pass.
                            shapingRoadBed = false;
                            if (terrain->terrainManager)
                                terrain->terrainManager->logHeightfieldSignature("brush/RELEASE-before-snap");
                            terrain->snapRoadsToRenderedTerrain();
                        }
                    }
                    else if (activeEditorMode == 2 && editorBrushMode == 7) { // Add geometry, then smooth it
                        if (leftMouseDown && terrain->terrainManager) {
                            // Adds resolution inside the footprint, then smooths the new
                            // vertices on the same stroke. Seeded vertices sit exactly on
                            // the existing surface, so nothing moves until the smooth has
                            // fine neighbours to average -- which is the point: the brush
                            // makes a place smoothable instead of only moving the coarse
                            // grid, where whole 22 m quads shift and nothing gets smoother.
                            terrain->terrainManager->addGeometry(
                                lastRayHitPos.x, lastRayHitPos.z, brushRadius, addGeomLevel);
                            terrain->terrainManager->smoothFine(
                                lastRayHitPos.x, lastRayHitPos.z, brushRadius,
                                brushStrength, static_cast<float>(deltaTime));
                            waterSim.updateTerrainArea(terrain->terrainManager, lastRayHitPos.x, lastRayHitPos.z, brushRadius);
                            terrain->updateWaterMapTexture(waterSim.getTextureData());
                        }
                    }
                    else if (activeEditorMode == 3 && editorBrushMode >= 8 && editorBrushMode <= 10) { // Paint modes
                        if (leftMouseDown && terrain->terrainManager) {
                            int paintIdx = (editorBrushMode == 8) ? 1 : ((editorBrushMode == 9) ? 2 : 0);
                            terrain->terrainManager->paint(lastRayHitPos.x, lastRayHitPos.z, brushRadius, brushStrength, paintIdx, static_cast<float>(deltaTime));
                        }
                    }
                }
            }
        }
    }

    // Sync variables with the Terrain rendering context
    terrain->rayHitTerrain = rayHitTerrain;
    terrain->lastRayHitPos = lastRayHitPos;
    terrain->editorBrushMode = editorBrushMode;
    terrain->brushRadius = brushRadius;
    terrain->editorSculptDirectionDegrees = sculptAngleDegrees;
    terrain->editorSculptGradeDegrees = sculptGradeDegrees;

    // Rebuild the road ribbon mesh after any road edit operation
    if (roadGraphDirty) {
        terrain->rebuildRoadMesh();
        roadGraphDirty = false;
        // Auto-persist drawn roads (throttled) so a restart always keeps them —
        // mirrors how the projected reference map placement auto-saves above.
        static double lastRoadAutosave = 0.0;
        if (currentTime - lastRoadAutosave > 1.0) {
            lastRoadAutosave = currentTime;
            terrain->saveRoadsToJSON("assets/roads/roads_manual.json");
            std::cout << "[Editor] Road graph auto-saved to roads_manual.json\n";
        }
    }

    static int frameCount = 0;
    frameCount++;

    enum class TestState {
        INIT,
        TELEPORT_CAR,
        MOUNT_CAR,
        WAIT_SEATED,
        DRIVE_CAR,
        DISMOUNT_CAR,
        WAIT_EXIT_CAR,
        TELEPORT_HORSE,
        MOUNT_HORSE,
        WAIT_MOUNTED_HORSE,
        RIDE_HORSE,
        DISMOUNT_HORSE,
        WAIT_EXIT_HORSE
    };

    static TestState testState = TestState::INIT;
    static double stateTimer = 0.0;

    static bool testMode = (std::getenv("FALLAGA_TEST_MODE") != nullptr);
    if (testMode) {
        double now = glfwGetTime();
        switch (testState) {
            case TestState::INIT:
                if (frameCount > 20) {
                    std::cout << "[TEST] Step 1: Teleporting player close to the car...\n";
                    Vec3 carPos = car->getPosition();
                    player->setPosition(Vec3(carPos.x, carPos.y, carPos.z - 2.0f)); 
                    testState = TestState::TELEPORT_CAR;
                    stateTimer = now;
                }
                break;
                
            case TestState::TELEPORT_CAR:
                if (now - stateTimer > 0.5) {
                    std::cout << "[TEST] Step 2: Triggering car mount...\n";
                    keyDown(GLFW_KEY_E);
                    keyUp(GLFW_KEY_E);
                    testState = TestState::MOUNT_CAR;
                    stateTimer = now;
                }
                break;
                
            case TestState::MOUNT_CAR:
                if (player->isDrivingOrTransitioning()) {
                    std::cout << "[TEST] Step 3: Character has started entering the car successfully!\n";
                    testState = TestState::WAIT_SEATED;
                    stateTimer = now;
                } else if (now - stateTimer > 5.0) {
                    std::cerr << "[TEST] ERROR: Character failed to start entering the car!\n";
                    requestQuit(1);
                }
                break;
                
            case TestState::WAIT_SEATED:
                if (player->isDriving()) {
                    std::cout << "[TEST] Step 4: Seated! Driving car forward...\n";
                    player->keyDown('z'); 
                    testState = TestState::DRIVE_CAR;
                    stateTimer = now;
                } else if (now - stateTimer > 10.0) {
                    std::cerr << "[TEST] ERROR: Character failed to get seated in the car!\n";
                    requestQuit(1);
                }
                break;
                
            case TestState::DRIVE_CAR:
                if (now - stateTimer > 2.0) {
                    player->keyUp('z');
                    Vec3 carPos = car->getPosition();
                    std::cout << "[TEST] Step 5: Checking if car moved. Position: (" 
                              << carPos.x << ", " << carPos.y << ", " << carPos.z << ")\n";
                    testState = TestState::DISMOUNT_CAR;
                    stateTimer = now;
                }
                break;
                
            case TestState::DISMOUNT_CAR:
                if (now - stateTimer > 0.5) {
                    std::cout << "[TEST] Step 6: Triggering dismount...\n";
                    player->keyDown('e');
                    testState = TestState::WAIT_EXIT_CAR;
                    stateTimer = now;
                }
                break;
                
            case TestState::WAIT_EXIT_CAR:
                if (!player->isDrivingOrTransitioning()) {
                    std::cout << "[TEST] Step 7: Exited car successfully!\n";
                    testState = TestState::TELEPORT_HORSE;
                    stateTimer = now;
                } else if (now - stateTimer > 10.0) {
                    std::cerr << "[TEST] ERROR: Character failed to exit the car!\n";
                    requestQuit(1);
                }
                break;
                
            case TestState::TELEPORT_HORSE:
                if (now - stateTimer > 0.5) {
                    std::cout << "[TEST] Step 8: Teleporting player close to the horse...\n";
                    Vec3 horsePos = horse->getPosition();
                    player->setPosition(Vec3(horsePos.x, horsePos.y, horsePos.z - 2.0f));
                    testState = TestState::MOUNT_HORSE;
                    stateTimer = now;
                }
                break;
                
            case TestState::MOUNT_HORSE:
                if (now - stateTimer > 0.5) {
                    std::cout << "[TEST] Step 9: Triggering horse mount...\n";
                    keyDown(GLFW_KEY_E);
                    keyUp(GLFW_KEY_E);
                    testState = TestState::WAIT_MOUNTED_HORSE;
                    stateTimer = now;
                }
                break;
                
            case TestState::WAIT_MOUNTED_HORSE:
                if (player->isRiding()) {
                    std::cout << "[TEST] Step 10: Mounted horse successfully! Riding forward...\n";
                    player->keyDown('z');
                    testState = TestState::RIDE_HORSE;
                    stateTimer = now;
                } else if (now - stateTimer > 10.0) {
                    std::cerr << "[TEST] ERROR: Character failed to mount the horse!\n";
                    requestQuit(1);
                }
                break;
                
            case TestState::RIDE_HORSE:
                if (now - stateTimer > 2.0) {
                    player->keyUp('z');
                    Vec3 horsePos = horse->getPosition();
                    std::cout << "[TEST] Step 11: Checking if horse moved. Position: (" 
                              << horsePos.x << ", " << horsePos.y << ", " << horsePos.z << ")\n";
                    testState = TestState::DISMOUNT_HORSE;
                    stateTimer = now;
                }
                break;
                
            case TestState::DISMOUNT_HORSE:
                if (now - stateTimer > 0.5) {
                    std::cout << "[TEST] Step 12: Triggering horse dismount...\n";
                    player->keyDown('e');
                    testState = TestState::WAIT_EXIT_HORSE;
                    stateTimer = now;
                }
                break;
                
            case TestState::WAIT_EXIT_HORSE:
                if (!player->isRiding()) {
                    std::cout << "[TEST] Step 13: Exited horse successfully!\n";
                    std::cout << "[TEST] ALL TESTS COMPLETED SUCCESSFULLY!\n";
                    requestQuit(0);
                } else if (now - stateTimer > 10.0) {
                    std::cerr << "[TEST] ERROR: Character failed to dismount from the horse!\n";
                    requestQuit(1);
                }
                break;
                
            default:
                break;
        }
    } else {
        if (frameCount % 60 == 0) {
            Vec3 pPos = player->getPosition();
            Vec3 hPos = horse ? horse->getPosition() : Vec3(0,0,0);
            Vec3 cPos = car ? car->getPosition() : Vec3(0,0,0);
            std::cout << "[TRACK] Player: (" << pPos.x << ", " << pPos.y << ", " << pPos.z 
                      << ") | Horse: (" << hPos.x << ", " << hPos.y << ", " << hPos.z 
                      << ") | Car: (" << cPos.x << ", " << cPos.y << ", " << cPos.z 
                      << ") | isRiding: " << player->isRiding() 
                      << " | isDrivingOrTransitioning: " << player->isDrivingOrTransitioning() << "\n";
        }
    }

    // Update 3D audio listener position and direction
    Vec3 camPos = camera->getPosition();
    Vec3 camForward = camera->getForward();
    AudioSystem::getInstance().updateListener(
        glm::vec3(camPos.x, camPos.y, camPos.z),
        glm::vec3(camForward.x, camForward.y, camForward.z),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    if (profileUpdate) {
        recordPhase(perf.otherMs);
        perf.elapsed += deltaTime;
        ++perf.frames;
        if (perf.elapsed >= 0.5) {
            const double scale = 1.0 / static_cast<double>(perf.frames);
            std::cout << "[UPDATE PERF] avg ms: prep=" << perf.prepMs * scale
                      << " characters=" << perf.charactersMs * scale
                      << " car=" << perf.carMs * scale
                      << " camera=" << perf.cameraMs * scale
                      << " terrain=" << perf.terrainMs * scale
                      << " waterStep=" << perf.waterStepMs * scale
                      << " waterUpload=" << perf.waterUploadMs * scale
                      << " other=" << perf.otherMs * scale << '\n';
            perf = {};
        }
    }
}

// void Game::render() {
//   ///  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


//   // 1. Clear everything and reset global states
//     glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
//     glEnable(GL_DEPTH_TEST);
//     glDepthFunc(GL_LESS);
//     glDisable(GL_BLEND); // Start with blending OFF
   
// float aspectRatio = (float)windowWidth / (float)windowHeight;
//     // Apply camera transformation
//     glm::mat4 projection = glm::perspective(
//         glm::radians(camera->distance), 
//         aspectRatio, // Use your window dimensions here
//         0.5f, 1000.0f
//     );
//     glm::mat4 view = camera->getViewMatrix();
//     //camera->apply();
    
//     // Render terrain first (largest object)
   




//     Shader& characterShader = player->getShader(); // Supposons que Character a un getter pour le Shader
//     characterShader.use();
// Shader& OceanShader = terrain->getShader();
//     terrain->render(OceanShader);

//     // 1. Définir les matrices Model/View/Projection (Déjà fait dans player->render, mais assurez-vous que c'est bien la même chose)
//     // 2. Définir les uniforms d'éclairage
//    auto cameraPosCustom = camera->getPosition(); // Récupère l'objet Vec3
//     glm::vec3 cameraPosition = glm::vec3(cameraPosCustom.x, cameraPosCustom.y, cameraPosCustom.z);
//     // Les valeurs de la lumière du pipeline fixe ne sont pas utilisées, nous les définissons ici :
//     glm::vec3 lightWorldPos = glm::vec3(100.0f, 500.0f, 100.0f); // Reprend la position de main.cpp
//     glm::vec3 lightColor = glm::vec3(1.0f, 1.0f, 1.0f); // Lumière blanche par défaut
    
//     characterShader.setVec3("viewPos", cameraPosition);
//     characterShader.setVec3("lightPos", lightWorldPos);
//     characterShader.setVec3("lightColor", lightColor);
    
//     // --- FIN NOUVEAU CODE CRITIQUE ---
//   horse->render( projection, view);
//     // Le player->render() doit utiliser le shader déjà configuré
//     player->render( projection, view);
//     // Render player last
 
//     glUseProgram(0);
//     // --- A. Load Projection Matrix for Legacy Objects ---
//     // We must manually push the calculated GLM Projection matrix onto the GL_PROJECTION stack
//     glMatrixMode(GL_PROJECTION);
//     glLoadIdentity(); 
//     // Load the GLM matrix directly into the OpenGL state
//     glMultMatrixf(glm::value_ptr(projection)); 

//     // --- B. Load View Matrix for Legacy Objects ---
//     // We must manually push the calculated GLM View matrix onto the GL_MODELVIEW stack
//     glMatrixMode(GL_MODELVIEW);
//     glLoadIdentity(); 
//     // Load the GLM matrix directly into the OpenGL state
//     glMultMatrixf(glm::value_ptr(view));
     
// }
void Game::render() {
    using RenderPerfClock = std::chrono::steady_clock;
    static const bool profileRender = std::getenv("FALLAGA_RENDER_PERF") != nullptr;
    static bool gpuTimerPending[4] = {};
    static int gpuTimerFrame = 0;
    static int gpuTimerSamples = 0;
    static double gpuTimerAccumulatedMs = 0.0;
    int activeGpuTimerSlot = -1;
    if (profileRender && GLEW_ARB_timer_query) {
        if (gpuTimerQueries[0] == 0) glGenQueries(4, gpuTimerQueries);
        const int slot = gpuTimerFrame++ % 4;
        if (gpuTimerPending[slot]) {
            GLuint available = GL_FALSE;
            glGetQueryObjectuiv(gpuTimerQueries[slot], GL_QUERY_RESULT_AVAILABLE, &available);
            if (available == GL_TRUE) {
                GLuint64 elapsedNs = 0;
                glGetQueryObjectui64v(gpuTimerQueries[slot], GL_QUERY_RESULT, &elapsedNs);
                gpuTimerAccumulatedMs += static_cast<double>(elapsedNs) / 1000000.0;
                gpuTimerPending[slot] = false;
                if (++gpuTimerSamples >= 60) {
                    std::cout << "[RENDER GPU PERF] avg ms: "
                              << gpuTimerAccumulatedMs / gpuTimerSamples << '\n';
                    gpuTimerSamples = 0;
                    gpuTimerAccumulatedMs = 0.0;
                }
            }
        }
        if (!gpuTimerPending[slot]) {
            activeGpuTimerSlot = slot;
            glBeginQuery(GL_TIME_ELAPSED, gpuTimerQueries[slot]);
        }
    }
    RenderPerfClock::time_point renderPhaseStart{};
    double skyRenderMs = 0.0;
    double preTerrainRenderMs = 0.0;
    double terrainRenderMs = 0.0;
    double objectsRenderMs = 0.0;
    double waterRenderMs = 0.0;
    double renderPostMs = 0.0;

    // ── Per-phase GPU timing ────────────────────────────────────────────────
    // The CPU-phase numbers below only measure submission, which is why a frame that
    // submits 0.5 ms of terrain can still take 90 ms on the GPU. These queries time
    // the actual GPU work per phase. Results are collected two frames after the slot
    // is issued, so QUERY_RESULT_AVAILABLE is always true by then and the read never
    // stalls -- a synchronous read would serialise the pipeline and change the thing
    // being measured.
    static const char* const kGpuPhaseNames[kGpuPhaseCount] = {
        "sky", "terrain", "objects", "water", "post"};
    // The sky query is never trusted: leaving a GL_TIME_ELAPSED query open across
    // Sky::render reliably reports GL_INVALID_OPERATION and a nonsense value (measured
    // 2.3e12 ms), so that phase is opened but its result is discarded and reported as
    // n/a. The CPU sky phase is still timed above.
    constexpr bool kGpuTrustSky = false;
    const bool gpuPhaseProfile = profileRender && GLEW_ARB_timer_query;
    static int gpuPhaseSlot = 0;
    if (gpuPhaseProfile) {
        if (gpuPhaseQueries[0][0] == 0) {
            for (int s = 0; s < kGpuPhaseSlots; ++s)
                for (int p = 0; p < kGpuPhaseCount; ++p)
                    glGenQueries(1, &gpuPhaseQueries[s][p]);
        }
        // Collect the slot issued two frames ago. The first slots are discarded: the
        // opening frame straddles one-time startup work (shader compiles, texture
        // uploads, the road mesh build), which the sky query spans and which makes
        // that sample both huge and GL_INVALID_OPERATION on some drivers.
        const int readSlot = (gpuPhaseSlot + 2) % kGpuPhaseSlots;
        static int warmupSlots = 0;
        if (gpuPhasePending[readSlot] && warmupSlots >= kGpuPhaseSlots) {
            for (int p = 0; p < kGpuPhaseCount; ++p) {
                GLuint available = GL_FALSE;
                glGetQueryObjectuiv(gpuPhaseQueries[readSlot][p],
                                    GL_QUERY_RESULT_AVAILABLE, &available);
                if (available != GL_TRUE) continue;
                GLuint64 ns = 0;
                glGetQueryObjectui64v(gpuPhaseQueries[readSlot][p],
                                      GL_QUERY_RESULT, &ns);
                const double ms = static_cast<double>(ns) / 1000000.0;
                // Reject impossible samples outright rather than average them in: one
                // invalid query otherwise poisons the whole report.
                if (p == 0 && !kGpuTrustSky) continue;
                if (ms < 0.0 || ms > 10000.0) continue;
                gpuPhaseAccum[p] += ms;
            }
            gpuPhasePending[readSlot] = false;
            if (++gpuPhaseSampleCount >= 60) {
                std::cout << "[RENDER GPU PER-PHASE] avg ms:";
                for (int p = 0; p < kGpuPhaseCount; ++p) {
                    if (p == 0 && !kGpuTrustSky) {
                        std::cout << ' ' << kGpuPhaseNames[p] << "=n/a";
                        continue;
                    }
                    std::cout << ' ' << kGpuPhaseNames[p] << '='
                              << gpuPhaseAccum[p] / gpuPhaseSampleCount;
                }
                std::cout << '\n';
                for (int p = 0; p < kGpuPhaseCount; ++p) gpuPhaseAccum[p] = 0.0;
                gpuPhaseSampleCount = 0;
            }
        } else if (gpuPhasePending[readSlot]) {
            gpuPhasePending[readSlot] = false; // warmup: drop the sample
        }
        if (warmupSlots < kGpuPhaseSlots) ++warmupSlots;
        // Issue the whole current frame's phases into the next slot, opening only the
        // first. gpuPhaseNext then closes each as its section of render() finishes.
        gpuPhaseSlot = (gpuPhaseSlot + 1) % kGpuPhaseSlots;
        gpuPhasePending[gpuPhaseSlot] = true;
        glBeginQuery(GL_TIME_ELAPSED, gpuPhaseQueries[gpuPhaseSlot][0]);
    }
    // Advances to the next phase: closes the query that has just finished and opens
    // the next one. Guarded so extra calls cannot leave a query open past the last
    // phase.
    int gpuPhaseCurrent = 0;
    auto gpuPhaseNext = [&]() {
        if (!gpuPhaseProfile) return;
        if (gpuPhaseCurrent >= kGpuPhaseCount) return; // already closed
        glEndQuery(GL_TIME_ELAPSED);
        if (++gpuPhaseCurrent < kGpuPhaseCount)
            glBeginQuery(GL_TIME_ELAPSED,
                         gpuPhaseQueries[gpuPhaseSlot][gpuPhaseCurrent]);
    };
    auto recordRenderPhase = [&](double& bucketMs) {
        if (!profileRender) return;
        const auto now = RenderPerfClock::now();
        bucketMs += std::chrono::duration<double, std::milli>(now - renderPhaseStart).count();
        renderPhaseStart = now;
    };
    if (profileRender) renderPhaseStart = RenderPerfClock::now();

    // 1. Reset Buffer and Global States
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND); 

    // 2. Prepare Matrices
    float aspectRatio = (float)windowWidth / (float)windowHeight;
    glm::mat4 projection = glm::perspective(
        glm::radians(45.0f), 
        aspectRatio, 
        0.5f, 10000.0f
    );
    glm::mat4 view = camera->getViewMatrix();

    // Update camera view frustum planes for visual culling
    camera->updateFrustum(projection, view);

    // 3. Render Sky (behind everything)
    // 120 seconds for a full day cycle
    static const char* sunTimeEnv = std::getenv("FALLAGA_SUN_TIME");
    float effectiveTime = sunTimeEnv ? (float)atof(sunTimeEnv) : totalTime;
    float sunAngle = effectiveTime * (2.0f * 3.14159f / 600.0f);
    glm::vec3 lightDir = glm::normalize(glm::vec3(cos(sunAngle), sin(sunAngle), 0.2f));
    if (editMode) {
        // High noon light vector for clear visibility during editing
        lightDir = glm::normalize(glm::vec3(0.2f, 0.96f, 0.2f));
    }
    sky->render(projection, view, lightDir);
    recordRenderPhase(skyRenderMs);
    gpuPhaseNext(); // sky -> terrain

    // 4. Load Matrices into the Legacy Pipeline
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity(); 
    glMultMatrixf(glm::value_ptr(projection)); 

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity(); 
    glMultMatrixf(glm::value_ptr(view));

    // --- NEW: Setup Legacy Lighting for ObjModels ---
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    
    float sunTilt = lightDir.y;
    glm::vec3 lColor = glm::vec3(1.0f);
    if (editMode) {
        lColor = glm::vec3(1.1f, 1.1f, 1.1f); // Bright daylight
    } else {
        if (sunTilt > 0.1f) {
            lColor = glm::mix(glm::vec3(1.0, 0.5, 0.2), glm::vec3(1.0, 1.0, 1.0), glm::smoothstep(0.1f, 0.4f, sunTilt));
        } else if (sunTilt > -0.1f) {
            lColor = glm::mix(glm::vec3(0.05, 0.1, 0.2), glm::vec3(1.0, 0.5, 0.2), glm::smoothstep(-0.1f, 0.1f, sunTilt));
        } else {
            lColor = glm::vec3(0.05, 0.1, 0.2); // Night light
        }
    }

    GLfloat lightPosFixed[] = { lightDir.x, lightDir.y, lightDir.z, 0.0f }; // 0.0 for directional
    GLfloat lightColorFixed[] = { lColor.r, lColor.g, lColor.b, 1.0f };
    GLfloat ambientColorFixed[] = { lColor.r * (editMode ? 0.5f : 0.2f), lColor.g * (editMode ? 0.5f : 0.2f), lColor.b * (editMode ? 0.5f : 0.2f), 1.0f };

    glLightfv(GL_LIGHT0, GL_POSITION, lightPosFixed);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, lightColorFixed);
    glLightfv(GL_LIGHT0, GL_AMBIENT, ambientColorFixed);

    // 5. Camera Position
    auto cameraPosCustom = camera->getPosition();
    glm::vec3 cameraPosition = glm::vec3(cameraPosCustom.x, cameraPosCustom.y, cameraPosCustom.z);

    // ── WATER PRE-PASSES (Bypassed: procedural atmospheric PBR reflection is used) ──
    // Eliminates duplicate terrain render pass, saving ~8-12ms per frame.

    // 6. Render Terrain (opaque pass - terrain chunks, city walls, landmarks, and editor preview)
    recordRenderPhase(preTerrainRenderMs);
    terrain->renderOpaque(projection, view, cameraPosition, lightDir, lColor, selectedPlacedIdx, (activeEditorMode == 1), selectedAssetIdx, previewAngle, previewScale, false);
    recordRenderPhase(terrainRenderMs);
    gpuPhaseNext(); // terrain -> objects

    // ── PROJECTED REFERENCE MAP ON THE TERRAIN (road edit mode, reference only) ──
    if (editMode && activeEditorMode == 5) {
        if (!roadRefLoaded) {
            const char* path = roadRefPaths[roadRefIdx];
            // A failed load must not latch. roadRefLoaded is set true only after the
            // pixels are on the GPU, and a per-index latch remembers the failure so a
            // missing file is not re-read (and re-logged) on every single frame.
            static int failedRoadRefIdx = -1;
            {
            int w = 0, h = 0, ch = 0;
            const bool attemptedLoad = (failedRoadRefIdx != roadRefIdx);
            unsigned char* d = attemptedLoad ? stbi_load(path, &w, &h, &ch, 4) : nullptr;
            if (d) {
                if (roadRefTex) glDeleteTextures(1, &roadRefTex);
                if (roadRefDrawTex) glDeleteTextures(1, &roadRefDrawTex);
                glGenTextures(1, &roadRefTex);
                glBindTexture(GL_TEXTURE_2D, roadRefTex);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, d);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

                // Keep the reference untouched for normal viewing, but build a
                // higher-contrast, slightly more saturated copy for road drawing.
                const size_t byteCount = (size_t)w * (size_t)h * 4;
                std::vector<unsigned char> drawPixels(d, d + byteCount);
                for (size_t i = 0; i < byteCount; i += 4) {
                    const float r = d[i + 0] / 255.0f;
                    const float g = d[i + 1] / 255.0f;
                    const float b = d[i + 2] / 255.0f;
                    const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                    const float contrast = glm::clamp((luma - 0.5f) * 1.35f + 0.515f,
                                                      0.0f, 1.0f);
                    const float enhanced[3] = {
                        glm::clamp(contrast + (r - luma) * 1.2f + 0.012f, 0.0f, 1.0f),
                        glm::clamp(contrast + (g - luma) * 1.2f + 0.006f, 0.0f, 1.0f),
                        glm::clamp(contrast + (b - luma) * 1.2f - 0.008f, 0.0f, 1.0f)
                    };
                    drawPixels[i + 0] = (unsigned char)(enhanced[0] * 255.0f + 0.5f);
                    drawPixels[i + 1] = (unsigned char)(enhanced[1] * 255.0f + 0.5f);
                    drawPixels[i + 2] = (unsigned char)(enhanced[2] * 255.0f + 0.5f);
                }
                glGenTextures(1, &roadRefDrawTex);
                glBindTexture(GL_TEXTURE_2D, roadRefDrawTex);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                             drawPixels.data());
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                stbi_image_free(d);
                roadRefW = w; roadRefH = h;
                roadRefLoaded = true;   // only now is a texture really loaded
                failedRoadRefIdx = -1;
                std::cout << "[Road] Reference image loaded: " << path << " (" << w << "x" << h << ")\n";
            } else if (attemptedLoad) {
                // Drop whatever was bound before, so the panel does not keep showing
                // the previous map while claiming the new file loaded.
                if (roadRefTex) { glDeleteTextures(1, &roadRefTex); roadRefTex = 0; }
                if (roadRefDrawTex) { glDeleteTextures(1, &roadRefDrawTex); roadRefDrawTex = 0; }
                roadRefW = 0; roadRefH = 0;
                roadRefLoaded = false;
                failedRoadRefIdx = roadRefIdx;
                std::cerr << "[Road] Failed to load reference image " << path
                          << " (no texture bound; the REF MAP panel stays hidden)\n";
            }
            }
        }
        RoadGraph* rgp = terrain->getRoadGraph();
        if (!projRefInitialized) {
            projRefInitialized = true;
            int texIdxUsed = roadRefIdx; // index the current texture was loaded for
            // Restore a previously SAVED placement if one exists (button in the Reference Map panel)
            if (loadProjRefMap() && projRefMapW > 0.0f) {
                std::cout << "[Road] Projection uses saved placement\n";
                if (roadRefIdx != texIdxUsed) roadRefLoaded = false; // saved map image differs -> reload
            } else {
                // No saved placement: start over the Port quay (where the camera spawns): plan (585,200)+offset
                projRefCenter = glm::vec2(4185.0f, -501.0f);
                float aspect = (roadRefH > 0) ? (float)roadRefH / (float)roadRefW : 0.7f;
                if (rgp && !rgp->segments.empty()) {
                    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
                    for (const auto& seg : rgp->segments)
                        for (const auto& p : seg.controlPoints) {
                            minX = std::min(minX, p.pos.x); maxX = std::max(maxX, p.pos.x);
                            minZ = std::min(minZ, p.pos.y); maxZ = std::max(maxZ, p.pos.y);
                        }
                    float bw = maxX - minX, bh = maxZ - minZ;
                    projRefMapW = std::max(bw, bh / std::max(aspect, 0.001f));
                } else {
                    projRefMapW = 3000.0f;
                }
            }
            projSavedCenter = projRefCenter;
            projSavedScale = projRefScale;
            projSavedFlipX = projRefFlipX;
            projSavedFlipZ = projRefFlipZ;
            projSavedOpacity = projRefOpacity;
            projSavedYOffset = projRefYOffset;
            projSavedIdx = roadRefIdx;
            projSavedMapW = projRefMapW;
            projLastSave = glfwGetTime();
            std::cout << "[Road] Projection placed at center (" << projRefCenter.x << ", "
                      << projRefCenter.y << "), width " << projRefMapW << "m, scale " << projRefScale << "x\n";
        }
        if (roadRefTex && projRefVisible) {
            glUseProgram(0); // leave the GLSL terrain/water program; restore fixed-function immediate mode
            glBindVertexArray(0);
            glDisableVertexAttribArray(0);
            float mw = projRefMapW * projRefScale;
            float mh = (roadRefH > 0) ? mw * ((float)roadRefH / (float)roadRefW) : mw * 0.7f;
            const float halfW = mw * 0.5f, halfH = mh * 0.5f;
            glDisable(GL_LIGHTING);
            glDepthMask(GL_FALSE);
            glDepthFunc(GL_LEQUAL);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_TEXTURE_2D);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D,
                          (roadToolMode == 1 && roadRefDrawTex) ? roadRefDrawTex : roadRefTex);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            if (std::getenv("FALLAGA_PROJ_DEBUG")) {
                static bool dbgDone = false;
                if (!dbgDone) {
                    dbgDone = true;
                    std::cout << "[DBG] projRefVisible=" << projRefVisible << " tex=" << roadRefTex
                              << " loaded=" << roadRefLoaded << " refW=" << roadRefW << " refH=" << roadRefH
                              << " idx=" << roadRefIdx << " flipX=" << projRefFlipX << " flipZ=" << projRefFlipZ
                              << " center=(" << projRefCenter.x << "," << projRefCenter.y << ") mw=" << mw << " mh=" << mh << "\n";
                    Vec3 cp = camera->getPosition();
                    std::cout << "[DBG] cam=(" << cp.x << "," << cp.y << "," << cp.z << ")\n";
                    Vec3 cr = camera->getRight();
                    Vec3 cf = camera->getForward();
                    std::cout << "[DBG] camF=(" << cf.x << "," << cf.y << "," << cf.z << ") camR=(" << cr.x << ","
                              << cr.y << "," << cr.z << ")\n";
                }
                glDisable(GL_DEPTH_TEST);
                glDisable(GL_BLEND);
                glDisable(GL_TEXTURE_2D);
                glDepthMask(GL_FALSE);
                glMatrixMode(GL_PROJECTION); glLoadIdentity();
                glMatrixMode(GL_MODELVIEW); glLoadIdentity();
                glColor3f(0.0f, 0.0f, 1.0f);
                glBegin(GL_QUADS);
                glVertex3f(-1.0f, -1.0f, 0.0f); glVertex3f(1.0f, -1.0f, 0.0f);
                glVertex3f(1.0f, 1.0f, 0.0f); glVertex3f(-1.0f, 1.0f, 0.0f);
                glEnd();
                glMatrixMode(GL_PROJECTION); glLoadIdentity(); glMultMatrixf(glm::value_ptr(projection));
                glMatrixMode(GL_MODELVIEW); glLoadIdentity(); glMultMatrixf(glm::value_ptr(view));
                glColor3f(1.0f, 0.0f, 0.0f);
                glBegin(GL_QUADS);
                const float dbgLift = 2.0f;
                GLfloat quadC[4][3] = {
                    { projRefCenter.x - halfW, terrain->getHeight(projRefCenter.x - halfW, projRefCenter.y - halfH) + dbgLift, projRefCenter.y - halfH },
                    { projRefCenter.x + halfW, terrain->getHeight(projRefCenter.x + halfW, projRefCenter.y - halfH) + dbgLift, projRefCenter.y - halfH },
                    { projRefCenter.x + halfW, terrain->getHeight(projRefCenter.x + halfW, projRefCenter.y + halfH) + dbgLift, projRefCenter.y + halfH },
                    { projRefCenter.x - halfW, terrain->getHeight(projRefCenter.x - halfW, projRefCenter.y + halfH) + dbgLift, projRefCenter.y + halfH }
                };
                for (int i = 0; i < 4; ++i) { glVertex3fv(quadC[i]); }
                glEnd();
                glEnable(GL_DEPTH_TEST);
                glEnable(GL_LIGHTING);
            } else {
                // While drawing roads, dim the map so the road mesh + terrain read through
                float effOpacity = (roadToolMode == 1) ? 0.58f : projRefOpacity;
                glColor4f(1.0f, 1.0f, 1.0f, effOpacity);
            }
            glBegin(GL_QUADS);
            const float lift = 0.7f + projRefYOffset; // small vertical lift above the draped surface
            const int G = 32;   // grid resolution: drapes the map onto the terrain surface
            const int GN = G + 1;
            // Cache key: the two terrain revisions (a heightmap reload reallocates the
            // heights buffer, a road-mesh rebuild re-seats/conforms the surface) plus every
            // placement input the draped grid depends on. mh is derived from mw and the
            // reference aspect, so those two are covered.
            const unsigned int heightRev =
                terrain->terrainManager ? terrain->terrainManager->getHeightRevision() : 0u;
            const std::uint64_t surfaceRev = terrain->getCollisionSurfaceRevision();
            const bool gridCacheHit =
                projRefHeightGridCount == (std::size_t)GN * GN &&
                projRefHeightGridHeightRev == heightRev &&
                projRefHeightGridSurfaceRev == surfaceRev &&
                projRefHeightGridMapW == projRefMapW &&
                projRefHeightGridScale == projRefScale &&
                projRefHeightGridFlipX == projRefFlipX &&
                projRefHeightGridFlipZ == projRefFlipZ &&
                projRefHeightGridYOffset == projRefYOffset &&
                projRefHeightGridCenter == projRefCenter;
            if (!gridCacheHit) {
                // Cache miss: resample the whole draped grid exactly as before.
                projRefHeightGrid.assign((size_t)GN * GN, 0.0f);
                for (int gyy = 0; gyy < GN; ++gyy) {
                    const float wz = projRefCenter.y - halfH + (float)gyy * (mh / G);
                    for (int gxx = 0; gxx < GN; ++gxx) {
                        const float wx = projRefCenter.x - halfW + (float)gxx * (mw / G);
                        projRefHeightGrid[(size_t)gyy * GN + gxx] = terrain->getHeight(wx, wz) + lift;
                    }
                }
                projRefHeightGridCount = (std::size_t)GN * GN;
                projRefHeightGridHeightRev = heightRev;
                projRefHeightGridSurfaceRev = surfaceRev;
                projRefHeightGridCenter = projRefCenter;
                projRefHeightGridMapW = projRefMapW;
                projRefHeightGridScale = projRefScale;
                projRefHeightGridFlipX = projRefFlipX;
                projRefHeightGridFlipZ = projRefFlipZ;
                projRefHeightGridYOffset = projRefYOffset;
            }
            // The quads themselves are immediate mode, so they are re-emitted every
            // frame — they just read the cached grid instead of resampling the terrain.
            const std::vector<float>& gH = projRefHeightGrid;
            for (int gy = 0; gy < G; ++gy) {
                for (int gx = 0; gx < G; ++gx) {
                    const float tx0 = projRefFlipX ? 1.0f - (float)gx / G : (float)gx / G;
                    const float tx1 = projRefFlipX ? 1.0f - (float)(gx + 1) / G : (float)(gx + 1) / G;
                    const float ty0 = projRefFlipZ ? 1.0f - (float)gy / G : (float)gy / G;
                    const float ty1 = projRefFlipZ ? 1.0f - (float)(gy + 1) / G : (float)(gy + 1) / G;
                    const float h00 = gH[(size_t)gy * GN + gx], h10 = gH[(size_t)gy * GN + gx + 1];
                    const float h11 = gH[(size_t)(gy + 1) * GN + gx + 1], h01 = gH[(size_t)(gy + 1) * GN + gx];
                    glTexCoord2f(tx0, ty0); glVertex3f(projRefCenter.x - halfW + (float)gx * (mw / G), h00, projRefCenter.y - halfH + (float)gy * (mh / G));
                    glTexCoord2f(tx1, ty0); glVertex3f(projRefCenter.x - halfW + (float)(gx + 1) * (mw / G), h10, projRefCenter.y - halfH + (float)gy * (mh / G));
                    glTexCoord2f(tx1, ty1); glVertex3f(projRefCenter.x - halfW + (float)(gx + 1) * (mw / G), h11, projRefCenter.y - halfH + (float)(gy + 1) * (mh / G));
                    glTexCoord2f(tx0, ty1); glVertex3f(projRefCenter.x - halfW + (float)gx * (mw / G), h01, projRefCenter.y - halfH + (float)(gy + 1) * (mh / G));
                }
            }
            glEnd();
            glDisable(GL_TEXTURE_2D);
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
            glEnable(GL_LIGHTING);
            if (roadToolMode == 3) {
                glColor3f(0.4f, 1.0f, 0.5f);
                glLineWidth(2.0f);
                glBegin(GL_LINE_LOOP);
                const float lift2 = 1.2f + projRefYOffset;
                glVertex3f(projRefCenter.x - halfW, terrain->getHeight(projRefCenter.x - halfW, projRefCenter.y - halfH) + lift2, projRefCenter.y - halfH);
                glVertex3f(projRefCenter.x + halfW, terrain->getHeight(projRefCenter.x + halfW, projRefCenter.y - halfH) + lift2, projRefCenter.y - halfH);
                glVertex3f(projRefCenter.x + halfW, terrain->getHeight(projRefCenter.x + halfW, projRefCenter.y + halfH) + lift2, projRefCenter.y + halfH);
                glVertex3f(projRefCenter.x - halfW, terrain->getHeight(projRefCenter.x - halfW, projRefCenter.y + halfH) + lift2, projRefCenter.y + halfH);
                glEnd();
                glLineWidth(1.0f);
            }
        }
    }

    // ── ROAD EDIT OVERLAY (in-world draft polyline + selection highlight) ──
    if (editMode && activeEditorMode == 5 && terrain->getRoadGraph()) {
        RoadGraph* rg = terrain->getRoadGraph();
        glUseProgram(0);
        glBindVertexArray(0);
        glDisableVertexAttribArray(0);
        glDisable(GL_LIGHTING);
        glDepthFunc(GL_LEQUAL);
        if (roadToolMode == 1 && rayHitTerrain) {
            // ── LIVE DRAW PREVIEW: smooth curve + road footprint + draggable points ──
            std::vector<glm::vec3> preview;
            if (!roadDraftPoints.empty()) {
                preview = roadDraftPoints;
                preview.push_back(lastRayHitPos); // tentative next point under the cursor
            }
            // if nothing drafted yet, still show a marker under the cursor
            if (!roadDraftPoints.empty() && preview.size() >= 2) {
                RoadSegment tempSeg;
                tempSeg.type = (RoadType)roadDrawType;
                tempSeg.width = roadDrawWidth;
                tempSeg.shoulderWidth = roadDrawShoulder;
                tempSeg.cornerRadius = roadDrawCornerRadius;
                for (const auto& p : preview) {
                    RoadPoint rp; rp.pos.x = p.x; rp.pos.y = p.z;
                    tempSeg.controlPoints.push_back(rp);
                }
                RoadSpline::evaluate(tempSeg, terrain->getRoadHeightSampler(), 0.5f);

                // Draw the centerline with a dark halo so it remains distinct on
                // both bright paper and dark water areas of the reference map.
                glColor3f(0.015f, 0.025f, 0.045f);
                glLineWidth(6.0f);
                glBegin(GL_LINE_STRIP);
                for (const auto& sp : tempSeg.splinePoints) glVertex3f(sp.x, sp.y + 0.15f, sp.z);
                glEnd();
                glColor3f(1.0f, 0.78f, 0.04f);
                glLineWidth(3.0f);
                glBegin(GL_LINE_STRIP);
                for (const auto& sp : tempSeg.splinePoints) glVertex3f(sp.x, sp.y + 0.15f, sp.z);
                glEnd();

                // road-width footprint (left & right edges) - shows the real scale
                if (tempSeg.rightVecs.size() >= 2) {
                    const float halfW = roadDrawWidth * 0.5f;
                    auto drawRoadEdges = [&]() {
                        glBegin(GL_LINE_STRIP);
                        for (size_t i = 0; i < tempSeg.splinePoints.size(); ++i) {
                            const glm::vec3& sp = tempSeg.splinePoints[i];
                            glVertex3f(sp.x + tempSeg.rightVecs[i].x * halfW,
                                       sp.y + 0.10f,
                                       sp.z + tempSeg.rightVecs[i].z * halfW);
                        }
                        glEnd();
                        glBegin(GL_LINE_STRIP);
                        for (size_t i = 0; i < tempSeg.splinePoints.size(); ++i) {
                            const glm::vec3& sp = tempSeg.splinePoints[i];
                            glVertex3f(sp.x - tempSeg.rightVecs[i].x * halfW,
                                       sp.y + 0.10f,
                                       sp.z - tempSeg.rightVecs[i].z * halfW);
                        }
                        glEnd();
                        const size_t endpointIndices[2] = {
                            0, tempSeg.splinePoints.size() - 1
                        };
                        for (size_t endpointIndex : endpointIndices) {
                            const glm::vec3& sp = tempSeg.splinePoints[endpointIndex];
                            const glm::vec3& right = tempSeg.rightVecs[endpointIndex];
                            glBegin(GL_LINES);
                            glVertex3f(sp.x - right.x * halfW, sp.y + 0.10f,
                                       sp.z - right.z * halfW);
                            glVertex3f(sp.x + right.x * halfW, sp.y + 0.10f,
                                       sp.z + right.z * halfW);
                            glEnd();
                        }
                    };
                    glColor3f(0.015f, 0.025f, 0.045f);
                    glLineWidth(5.0f);
                    drawRoadEdges();
                    glColor3f(0.05f, 1.0f, 0.95f);
                    glLineWidth(2.5f);
                    drawRoadEdges();
                    glLineWidth(1.0f);
                }
            }

            // control-point handles (draggable) + hover feedback
            glBegin(GL_QUADS);
            for (int i = 0; i < (int)roadDraftPoints.size(); ++i) {
                float dx = lastRayHitPos.x - roadDraftPoints[i].x;
                float dz = lastRayHitPos.z - roadDraftPoints[i].z;
                bool hover = (dx * dx + dz * dz) < 144.0f; // within 12m of cursor
                float y = terrain->getHeight(roadDraftPoints[i].x, roadDraftPoints[i].z) + 0.35f;
                if (i == draftDragIdx)      glColor3f(1.0f, 0.6f, 0.1f);
                else if (hover)             glColor3f(0.3f, 1.0f, 0.4f);
                else                        glColor3f(1.0f, 1.0f, 1.0f);
                const float s = (i == draftDragIdx) ? 3.5f : 2.6f;
                glVertex3f(roadDraftPoints[i].x - s, y, roadDraftPoints[i].z - s);
                glVertex3f(roadDraftPoints[i].x + s, y, roadDraftPoints[i].z - s);
                glVertex3f(roadDraftPoints[i].x + s, y, roadDraftPoints[i].z + s);
                glVertex3f(roadDraftPoints[i].x - s, y, roadDraftPoints[i].z + s);
            }
            glEnd();

            // cursor marker (where the next point / insertion will land)
            if (roadDraftPoints.size() >= 2) {
                float y = terrain->getHeight(lastRayHitPos.x, lastRayHitPos.z) + 0.25f;
                glColor3f(0.3f, 1.0f, 0.4f);
                glPointSize(8.0f);
                glBegin(GL_POINTS);
                glVertex3f(lastRayHitPos.x, y, lastRayHitPos.z);
                glEnd();
                glPointSize(1.0f);
            }
        }
        if (selectedRoadIdx >= 0 && selectedRoadIdx < (int)rg->segments.size()) {
            auto& seg = rg->segments[selectedRoadIdx];
            glColor3f(1.0f, 0.5f, 0.1f);
            glLineWidth(4.0f);
            glBegin(GL_LINE_STRIP);
            for (const auto& cp : seg.controlPoints) {
                glVertex3f(cp.pos.x, terrain->getHeight(cp.pos.x, cp.pos.y) + 0.6f, cp.pos.y);
            }
            glEnd();
            glLineWidth(1.0f);
            glPointSize(10.0f);
            glBegin(GL_POINTS);
            for (size_t i = 0; i < seg.controlPoints.size(); ++i) {
                bool selPt = (selectedRoadPointIdx >= 0 && (int)i == selectedRoadPointIdx);
                if (selPt) glColor3f(1.0f, 0.2f, 0.2f);
                else       glColor3f(0.4f, 0.95f, 0.4f);
                glVertex3f(seg.controlPoints[i].pos.x, terrain->getHeight(seg.controlPoints[i].pos.x, seg.controlPoints[i].pos.y) + 0.6f, seg.controlPoints[i].pos.y);
            }
            glEnd();
            glPointSize(1.0f);
        }
        glEnable(GL_LIGHTING);
        glDepthFunc(GL_LESS);
    }
    
    // 7. Character & Horse Shaders
    GameplayUtils::setupEntityShader(player->getShader(), cameraPosition, lightDir, lColor);

    // Frustum check for horse
    Vec3 hPos = horse->getPosition();
    glm::vec3 horsePos(hPos.x, hPos.y, hPos.z);
    bool horseInFrustum = camera->isSphereInFrustum(horsePos, 10.0f);
    if (horseInFrustum || player->isRiding()) {
        // Update Horse Shader (It has its own instance)
        GameplayUtils::setupEntityShader(horse->getShader(), cameraPosition, lightDir, lColor);
        horse->render(projection, view);
    }

    // Update Car Shader and render Car
    if (car) {
        Vec3 cPos = car->getPosition();
        glm::vec3 carPos(cPos.x, cPos.y, cPos.z);
        bool carInFrustum = camera->isSphereInFrustum(carPos, 15.0f);
        if (carInFrustum || player->isDriving()) {
            GameplayUtils::setupEntityShader(car->getShader(), cameraPosition, lightDir, lColor);
            car->render(projection, view, terrain);
        }
    }

    player->render(projection, view, lightDir, lColor, camera);
    recordRenderPhase(objectsRenderMs);
    gpuPhaseNext(); // objects -> water

    // ── BLIT SCENE COLOR TO REFRACTION FBO (0.05ms hardware blit - replaces entire second world render pass!) ──
    if (terrain->fbosReady && terrain->refrFBO) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, terrain->refrFBO);
        glBlitFramebuffer(0, 0, windowWidth, windowHeight, 0, 0, terrain->fboW, terrain->fboH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, windowWidth, windowHeight);
    }

    // 8. Render Water (transparent pass - after all opaque objects)
    terrain->renderWater(projection, view, cameraPosition, lightDir, lColor, 0, 0);
    recordRenderPhase(waterRenderMs);
    gpuPhaseNext(); // water -> post

    // --- PHASE 8.5: EDITOR HUD & GLOBAL STATUS OVERLAYS ---
    if (editMode || isSavingState) {
        // Set up 2D projection
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0, windowWidth, windowHeight, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);

        // Helper to draw text
        auto drawStr = [&](float x, float y, const std::string& text, float r = 0.8f, float g = 0.8f, float b = 0.8f) {
            glColor3f(r, g, b);
            glRasterPos2f(x, y);
            for (char c : text) {
#ifdef HAS_GLUT
                glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, c);
#endif
            }
        };

        auto drawRect = [&](float x, float y, float w, float h, float r, float g, float b, float a = 1.0f) {
            glColor4f(r, g, b, a);
            glBegin(GL_QUADS);
            glVertex2f(x, y);
            glVertex2f(x + w, y);
            glVertex2f(x + w, y + h);
            glVertex2f(x, y + h);
            glEnd();
        };

        auto drawRectOutline = [&](float x, float y, float w, float h, float r, float g, float b) {
            glColor3f(r, g, b);
            glLineWidth(1);
            glBegin(GL_LINE_LOOP);
            glVertex2f(x, y);
            glVertex2f(x + w, y);
            glVertex2f(x + w, y + h);
            glVertex2f(x, y + h);
            glEnd();
        };

        auto drawCircle2D = [&](float cx, float cy, float r, float red, float green, float blue, float alpha) {
            glColor4f(red, green, blue, alpha);
            glLineWidth(2);
            glBegin(GL_LINE_LOOP);
            for (int i = 0; i < 64; ++i) {
                float angle = (float)i * 2.0f * 3.14159265f / 64.0f;
                glVertex2f(cx + cos(angle) * r, cy + sin(angle) * r);
            }
            glEnd();
        };

        // Enable transparency
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        if (editMode) {

        // ─── 1. TOP MENU BAR (UNREAL STYLE) WITH MODE SWITCHER ───
        drawRect(0, 0, (float)windowWidth, 32, 0.08f, 0.08f, 0.08f, 1.0f); // Dark background
        drawRect(0, 31, (float)windowWidth, 1, 0.18f, 0.18f, 0.18f, 1.0f);  // Bottom line border
        if (windowWidth >= 1320) {
            drawStr(15, 20, "File      Edit      Window      Tools      Help", 0.7f, 0.7f, 0.7f);
        }
        if (windowWidth >= 1080) {
            drawStr((float)windowWidth - 260, 20, "Unreal Editor (Sousse Engine 5.4)", 0.5f, 0.5f, 0.5f);
        }
        drawRect((float)windowWidth - 45, 11, 10, 10, 0.1f, 0.8f, 0.1f, 1.0f); // Green status LED

        // Horizontally centered workspace mode switch tabs (fits any window width)
        float center = (float)windowWidth / 2.0f;
        std::string modeNames[] = { "GAME", "OBJECTS", "SCULPT", "PAINT", "WATER", "ROADS" };
        const float tabWidth = 110;
        const float tabGap = 8;
        const float tabStart = std::max(10.0f, center - (6 * tabWidth + 5 * tabGap) * 0.5f);

        for (int i = 0; i < 6; ++i) {
            float tx = tabStart + i * (tabWidth + tabGap);
            bool active = (activeEditorMode == i);
            if (active) {
                drawRect(tx, 4, tabWidth, 24, 0.89f, 0.36f, 0.13f, 0.9f);
                drawRectOutline(tx, 4, tabWidth, 24, 1.0f, 0.5f, 0.2f);
                drawStr(tx + 25, 20, modeNames[i], 1.0f, 1.0f, 1.0f);
            } else {
                drawRect(tx, 4, tabWidth, 24, 0.16f, 0.16f, 0.16f, 0.8f);
                drawRectOutline(tx, 4, tabWidth, 24, 0.25f, 0.25f, 0.25f);
                drawStr(tx + 25, 20, modeNames[i], 0.7f, 0.7f, 0.7f);
            }
        }

        // ─── 2. LEFT PANEL: TOOLS & DETAILS ───
        if (activeEditorMode != 5 || roadPanelsVisible) {
            float lpX = 10, lpY = 42, lpW = 320, lpH = (float)windowHeight - 52;
        drawRect(lpX, lpY, lpW, lpH, 0.11f, 0.11f, 0.11f, 0.95f); // Panel background
        drawRectOutline(lpX, lpY, lpW, lpH, 0.22f, 0.22f, 0.22f); // Panel border

        // Left Panel Header
        drawRect(lpX + 1, lpY + 1, lpW - 2, 28, 0.15f, 0.15f, 0.15f, 1.0f);
        
        if (activeEditorMode == 1) {
            drawStr(lpX + 15, lpY + 19, "ACTOR PLACEMENT", 0.9f, 0.5f, 0.1f);
            
            float assetStartY = lpY + 40;
            drawStr(lpX + 15, assetStartY, "PLACE NEW ACTOR (Click to select)", 0.9f, 0.5f, 0.1f);
            
            static int assetPage = 0;
            int assetsPerPage = 12; // Show more assets since there is space
            int totalPages = ((int)SousseMapData::discoveredAssets.size() + assetsPerPage - 1) / assetsPerPage;
            
            for (int i = 0; i < assetsPerPage; ++i) {
                int idx = assetPage * assetsPerPage + i;
                float btnY = assetStartY + 15 + i * 24;
                if (idx < (int)SousseMapData::discoveredAssets.size()) {
                    std::string fullPath = SousseMapData::discoveredAssets[idx];
                    std::string name = fullPath.substr(fullPath.find_last_of("/\\") + 1);
                    
                    bool isCurrent = (idx == selectedAssetIdx);
                    if (isCurrent) {
                        drawRect(lpX + 8, btnY - 2, lpW - 16, 20, 0.89f, 0.36f, 0.13f, 0.85f);
                        drawStr(lpX + 18, btnY + 12, name, 1.0f, 1.0f, 1.0f);
                    } else {
                        drawRect(lpX + 8, btnY - 2, lpW - 16, 20, 0.14f, 0.14f, 0.14f, 0.5f);
                        drawStr(lpX + 18, btnY + 12, name, 0.7f, 0.7f, 0.7f);
                    }
                    drawRectOutline(lpX + 8, btnY - 2, lpW - 16, 20, 0.22f, 0.22f, 0.22f);
                }
            }

            // Paging Controls
            float pageY = assetStartY + 15 + assetsPerPage * 24 + 10;
            drawRect(lpX + 15, pageY, 85, 20, 0.16f, 0.16f, 0.16f, 0.8f);
            drawRectOutline(lpX + 15, pageY, 85, 20, 0.25f, 0.25f, 0.25f);
            drawStr(lpX + 25, pageY + 14, "◀ Prev Page", 0.8f, 0.8f, 0.8f);

            drawRect(lpX + 110, pageY, 85, 20, 0.16f, 0.16f, 0.16f, 0.8f);
            drawRectOutline(lpX + 110, pageY, 85, 20, 0.25f, 0.25f, 0.25f);
            drawStr(lpX + 120, pageY + 14, "Next Page ▶", 0.8f, 0.8f, 0.8f);

            drawStr(lpX + 210, pageY + 14, "Page " + std::to_string(assetPage + 1) + "/" + std::to_string(totalPages), 0.6f, 0.6f, 0.6f);
        }
        else if (activeEditorMode == 2) {
            drawStr(lpX + 15, lpY + 19, "TERRAIN SCULPTING", 0.9f, 0.5f, 0.1f);
            
            drawStr(lpX + 15, lpY + 40, "ACTIVE SCULPT TOOL", 0.9f, 0.5f, 0.1f);
            std::string toolNames[] = { "Raise Terrain (LMB)", "Lower Terrain (LMB)", "Smooth Terrain (LMB)", "Erode Terrain (LMB)", "Angle Slope (LMB)", "Fit To Curved Road Mesh (LMB)", "Add Geometry + Smooth (LMB)" };
            for (int i = 0; i < 7; ++i) {
                float btnY = lpY + 45 + i * 22;
                bool isCurrent = (editorBrushMode == 1 + i);
                if (isCurrent) {
                    drawRect(lpX + 8, btnY - 2, lpW - 16, 20, 0.89f, 0.36f, 0.13f, 0.9f);
                    drawStr(lpX + 18, btnY + 12, toolNames[i], 1.0f, 1.0f, 1.0f);
                } else {
                    drawRect(lpX + 8, btnY - 2, lpW - 16, 20, 0.16f, 0.16f, 0.16f, 0.6f);
                    drawStr(lpX + 18, btnY + 12, toolNames[i], 0.7f, 0.7f, 0.7f);
                }
                drawRectOutline(lpX + 8, btnY - 2, lpW - 16, 20, 0.25f, 0.25f, 0.25f);
            }

            // Brush Settings Sliders
            float sliderStartY = lpY + 180;
            drawStr(lpX + 15, sliderStartY + 10, "BRUSH CONFIGURATION", 0.9f, 0.5f, 0.1f);
            
            float rNorm = glm::clamp((brushRadius - 5.0f) / 195.0f, 0.0f, 1.0f);
            drawStr(lpX + 15, sliderStartY + 30, "Radius: " + std::to_string((int)brushRadius) + "m", 0.8f, 0.8f, 0.8f);
            drawRect(lpX + 15, sliderStartY + 35, lpW - 30, 10, 0.06f, 0.06f, 0.06f, 1.0f);
            drawRectOutline(lpX + 15, sliderStartY + 35, lpW - 30, 10, 0.2f, 0.2f, 0.2f);
            drawRect(lpX + 15 + rNorm * (lpW - 40), sliderStartY + 32, 10, 16, 0.89f, 0.36f, 0.13f, 1.0f);

            float sNorm = glm::clamp((brushStrength - 0.5f) / 49.5f, 0.0f, 1.0f);
            drawStr(lpX + 15, sliderStartY + 62, "Strength: " + std::to_string((int)brushStrength), 0.8f, 0.8f, 0.8f);
            drawRect(lpX + 15, sliderStartY + 67, lpW - 30, 10, 0.06f, 0.06f, 0.06f, 1.0f);
            drawRectOutline(lpX + 15, sliderStartY + 67, lpW - 30, 10, 0.2f, 0.2f, 0.2f);
            drawRect(lpX + 15 + sNorm * (lpW - 40), sliderStartY + 64, 10, 16, 0.89f, 0.36f, 0.13f, 1.0f);

            float aNorm = glm::clamp(sculptAngleDegrees / 360.0f, 0.0f, 1.0f);
            drawStr(lpX + 15, sliderStartY + 94, "Direction: " + std::to_string((int)sculptAngleDegrees) + " deg", 0.8f, 0.8f, 0.8f);
            drawRect(lpX + 15, sliderStartY + 99, lpW - 30, 10, 0.06f, 0.06f, 0.06f, 1.0f);
            drawRectOutline(lpX + 15, sliderStartY + 99, lpW - 30, 10, 0.2f, 0.2f, 0.2f);
            drawRect(lpX + 15 + aNorm * (lpW - 40), sliderStartY + 96, 10, 16, 0.89f, 0.36f, 0.13f, 1.0f);

            float gNorm = glm::clamp(sculptGradeDegrees / 60.0f, 0.0f, 1.0f);
            drawStr(lpX + 15, sliderStartY + 126, "Grade: " + std::to_string((int)sculptGradeDegrees) + " deg", 0.8f, 0.8f, 0.8f);
            drawRect(lpX + 15, sliderStartY + 131, lpW - 30, 10, 0.06f, 0.06f, 0.06f, 1.0f);
            drawRectOutline(lpX + 15, sliderStartY + 131, lpW - 30, 10, 0.2f, 0.2f, 0.2f);
            drawRect(lpX + 15 + gNorm * (lpW - 40), sliderStartY + 128, 10, 16, 0.89f, 0.36f, 0.13f, 1.0f);

            // Subdivision detail for the Add Geometry brush. Level 1 makes each 22 m
            // cell four, level 2 makes it sixteen.
            drawStr(lpX + 15, sliderStartY + 145, "Detail: " + std::to_string(addGeomLevel) +
                    "x" + std::to_string(1 << addGeomLevel) + " per cell"
                    + (terrain->terrainManager && terrain->terrainManager->hasAddedGeometry()
                           ? " (added geometry present)" : ""),
                    0.8f, 0.8f, 0.8f);
            for (int lv = 1; lv <= 2; ++lv) {
                const bool on = (addGeomLevel == lv);
                drawRect(lpX + 15 + (lv - 1) * 45, sliderStartY + 149, 40, 14,
                         on ? 0.89f : 0.16f, on ? 0.36f : 0.16f, on ? 0.13f : 0.16f,
                         on ? 0.9f : 0.6f);
                drawStr(lpX + 26 + (lv - 1) * 45, sliderStartY + 160,
                        "L" + std::to_string(lv), 0.75f, 0.75f, 0.75f);
            }
            drawRectOutline(lpX + 15, sliderStartY + 149, 85, 14, 0.25f, 0.25f, 0.25f);

            // Global Actions Section
            float globalStartY = sliderStartY + 170;
            drawStr(lpX + 15, globalStartY + 10, "GLOBAL ACTIONS", 0.9f, 0.5f, 0.1f);
            
            // Button - highlight on hover could be cool, but standard UE orange button is great!
            drawRect(lpX + 15, globalStartY + 20, lpW - 30, 20, 0.89f, 0.36f, 0.13f, 1.0f);
            drawStr(lpX + 35, globalStartY + 34, "Run Global Erosion Pass", 1.0f, 1.0f, 1.0f);
            drawRectOutline(lpX + 15, globalStartY + 20, lpW - 30, 20, 0.25f, 0.25f, 0.25f);
        }
        else if (activeEditorMode == 3) {
            drawStr(lpX + 15, lpY + 19, "TERRAIN PAINTING", 0.9f, 0.5f, 0.1f);
            
            drawStr(lpX + 15, lpY + 40, "ACTIVE PAINT LAYER", 0.9f, 0.5f, 0.1f);
            std::string toolNames[] = { "Paint Road (Gravel)", "Paint River (Wet Mud)", "Erase Road/River" };
            for (int i = 0; i < 3; ++i) {
                float btnY = lpY + 45 + i * 25;
                bool isCurrent = (editorBrushMode == 8 + i);
                if (isCurrent) {
                    drawRect(lpX + 8, btnY - 2, lpW - 16, 20, 0.89f, 0.36f, 0.13f, 0.9f);
                    drawStr(lpX + 18, btnY + 12, toolNames[i], 1.0f, 1.0f, 1.0f);
                } else {
                    drawRect(lpX + 8, btnY - 2, lpW - 16, 20, 0.16f, 0.16f, 0.16f, 0.6f);
                    drawStr(lpX + 18, btnY + 12, toolNames[i], 0.7f, 0.7f, 0.7f);
                }
                drawRectOutline(lpX + 8, btnY - 2, lpW - 16, 20, 0.25f, 0.25f, 0.25f);
            }

            // Brush Settings Sliders
            float sliderStartY = lpY + 145;
            drawStr(lpX + 15, sliderStartY + 10, "BRUSH CONFIGURATION", 0.9f, 0.5f, 0.1f);
            
            float rNorm = glm::clamp((brushRadius - 5.0f) / 195.0f, 0.0f, 1.0f);
            drawStr(lpX + 15, sliderStartY + 30, "Radius: " + std::to_string((int)brushRadius) + "m", 0.8f, 0.8f, 0.8f);
            drawRect(lpX + 15, sliderStartY + 35, lpW - 30, 10, 0.06f, 0.06f, 0.06f, 1.0f);
            drawRectOutline(lpX + 15, sliderStartY + 35, lpW - 30, 10, 0.2f, 0.2f, 0.2f);
            drawRect(lpX + 15 + rNorm * (lpW - 40), sliderStartY + 32, 10, 16, 0.89f, 0.36f, 0.13f, 1.0f);

            float sNorm = glm::clamp((brushStrength - 0.5f) / 49.5f, 0.0f, 1.0f);
            drawStr(lpX + 15, sliderStartY + 62, "Strength: " + std::to_string((int)brushStrength), 0.8f, 0.8f, 0.8f);
            drawRect(lpX + 15, sliderStartY + 67, lpW - 30, 10, 0.06f, 0.06f, 0.06f, 1.0f);
            drawRectOutline(lpX + 15, sliderStartY + 67, lpW - 30, 10, 0.2f, 0.2f, 0.2f);
            drawRect(lpX + 15 + sNorm * (lpW - 40), sliderStartY + 64, 10, 16, 0.89f, 0.36f, 0.13f, 1.0f);
        }
        else if (activeEditorMode == 4) {
            // ─── WATER LEVEL EDITOR PANEL ─────────────────────────────
            drawStr(lpX + 15, lpY + 19, "WATER LEVEL", 0.3f, 0.7f, 1.0f);

            // Live value readout
            char wlBuf[64];
            snprintf(wlBuf, sizeof(wlBuf), "Sea Level Y: %.1f m", waterLevelAdjust);
            drawRect(lpX + 8, lpY + 38, lpW - 16, 24, 0.06f, 0.12f, 0.22f, 0.95f);
            drawRectOutline(lpX + 8, lpY + 38, lpW - 16, 24, 0.25f, 0.55f, 0.9f);
            drawStr(lpX + 18, lpY + 54, wlBuf, 0.4f, 0.85f, 1.0f);

            // Slider track (-80 to 0)
            float wlMin = -80.0f, wlMax = 0.0f;
            float wlNorm = glm::clamp((waterLevelAdjust - wlMin) / (wlMax - wlMin), 0.0f, 1.0f);
            drawStr(lpX + 15, lpY + 78, "DRAG SLIDER", 0.55f, 0.55f, 0.55f);
            drawRect(lpX + 15, lpY + 86, lpW - 30, 8, 0.06f, 0.10f, 0.18f, 1.0f);
            drawRectOutline(lpX + 15, lpY + 86, lpW - 30, 8, 0.2f, 0.4f, 0.65f);
            drawRect(lpX + 15, lpY + 86, (lpW - 30) * wlNorm, 8, 0.18f, 0.52f, 0.92f, 1.0f);
            float thumbX = lpX + 15 + wlNorm * (lpW - 40);
            drawRect(thumbX, lpY + 81, 12, 18, 0.89f, 0.36f, 0.13f, 1.0f);
            drawRectOutline(thumbX, lpY + 81, 12, 18, 1.0f, 0.6f, 0.3f);
            // Range labels
            drawStr(lpX + 15, lpY + 107, "-80m", 0.45f, 0.45f, 0.45f);
            drawStr(lpX + lpW - 40, lpY + 107, "0m", 0.45f, 0.45f, 0.45f);

            // Coarse step buttons (+/-0.5)
            float btnY = lpY + 120;
            drawRect(lpX + 15, btnY, 60, 22, 0.18f, 0.12f, 0.10f, 0.9f);
            drawRectOutline(lpX + 15, btnY, 60, 22, 0.5f, 0.25f, 0.15f);
            drawStr(lpX + 25, btnY + 15, "- 0.5m", 0.9f, 0.5f, 0.2f);

            drawRect(lpX + 85, btnY, 60, 22, 0.10f, 0.18f, 0.12f, 0.9f);
            drawRectOutline(lpX + 85, btnY, 60, 22, 0.2f, 0.5f, 0.25f);
            drawStr(lpX + 95, btnY + 15, "+ 0.5m", 0.4f, 0.9f, 0.5f);

            // Fine step buttons (+/-0.1)
            float fBtnY = lpY + 150;
            drawRect(lpX + 15, fBtnY, 60, 22, 0.14f, 0.10f, 0.10f, 0.85f);
            drawRectOutline(lpX + 15, fBtnY, 60, 22, 0.35f, 0.20f, 0.20f);
            drawStr(lpX + 25, fBtnY + 15, "- 0.1m", 0.8f, 0.4f, 0.3f);

            drawRect(lpX + 85, fBtnY, 60, 22, 0.10f, 0.14f, 0.10f, 0.85f);
            drawRectOutline(lpX + 85, fBtnY, 60, 22, 0.20f, 0.35f, 0.20f);
            drawStr(lpX + 95, fBtnY + 15, "+ 0.1m", 0.3f, 0.75f, 0.35f);

            // Reset button
            float resetY = lpY + 185;
            drawRect(lpX + 15, resetY, lpW - 30, 22, 0.20f, 0.08f, 0.08f, 0.9f);
            drawRectOutline(lpX + 15, resetY, lpW - 30, 22, 0.5f, 0.2f, 0.2f);
            drawStr(lpX + 80, resetY + 15, "Reset to -55.0m", 0.9f, 0.55f, 0.55f);

            // Push level to terrain shader every frame
            terrain->setWaterLevel(waterLevelAdjust);
        }
        else if (activeEditorMode == 5) {
            // ─── ROAD EDITOR PANEL ──────────────────────────────────────
            drawStr(lpX + 15, lpY + 19, "ROAD EDITOR", 1.0f, 0.6f, 0.1f);
            drawStr(lpX + 15, lpY + 37, "TOOL", 0.9f, 0.5f, 0.1f);
            std::string toolNames[] = {
                "1. Select Road (LMB)",
                "2. Draw Road (LMB)",
                "3. Edit Points (Drag)",
                "4. Reference Map"
            };
            for (int i = 0; i < 4; ++i) {
                float btnY = lpY + 45 + i * 26;
                bool isCur = (roadToolMode == i);
                if (isCur) {
                    drawRect(lpX + 8, btnY - 2, lpW - 16, 22, 0.89f, 0.36f, 0.13f, 0.9f);
                    drawStr(lpX + 18, btnY + 13, toolNames[i], 1.0f, 1.0f, 1.0f);
                } else {
                    drawRect(lpX + 8, btnY - 2, lpW - 16, 22, 0.16f, 0.16f, 0.16f, 0.6f);
                    drawStr(lpX + 18, btnY + 13, toolNames[i], 0.7f, 0.7f, 0.7f);
                }
                drawRectOutline(lpX + 8, btnY - 2, lpW - 16, 22, 0.25f, 0.25f, 0.25f);
            }
            if (roadToolMode == 1) {
                drawStr(lpX + 15, lpY + 131, "DRAW CONTROLS", 0.9f, 0.5f, 0.1f);
                drawStr(lpX + 15, lpY + 144, "CLICK = add point / start road", 0.72f, 0.72f, 0.72f);
                drawStr(lpX + 15, lpY + 158, "HOLD + DRAG = draw a continuous line", 0.72f, 0.72f, 0.72f);
                drawStr(lpX + 15, lpY + 172, "Enter/OK = commit, Esc/X = cancel, DEL PT = undo", 0.72f, 0.72f, 0.72f);
            } else if (roadToolMode != 3) {
                drawStr(lpX + 15, lpY + 131, "TIP", 0.9f, 0.5f, 0.1f);
                drawStr(lpX + 15, lpY + 145, "Press 4 to move/scale the projected map", 0.6f, 0.85f, 0.6f);
                drawStr(lpX + 15, lpY + 159, "Tab hides panels   R cycles PiP", 0.6f, 0.6f, 0.6f);
            }

            RoadGraph* rg = terrain->getRoadGraph();
            if (roadToolMode == 1) {
                // ─── NEW ROAD PROPERTY EDITOR (applied when you commit with Enter) ───
                drawStr(lpX + 15, lpY + 192, "NEW ROAD PROPERTIES", 0.9f, 0.5f, 0.1f);
                std::string typeNames6[] = {
                    "Colonial Avenue", "Urban Street", "Country Highway",
                    "Country Dirt", "Port", "Medina"
                };
                for (int i = 0; i < 6; ++i) {
                    float btnY = lpY + 215 + i * 22;
                    bool isCur = (roadDrawType == i);
                    if (isCur) {
                        drawRect(lpX + 8, btnY, lpW - 16, 20, 0.89f, 0.36f, 0.13f, 0.9f);
                        drawStr(lpX + 18, btnY + 14, typeNames6[i], 1.0f, 1.0f, 1.0f);
                    } else {
                        drawRect(lpX + 8, btnY, lpW - 16, 20, 0.13f, 0.13f, 0.13f, 0.6f);
                        drawStr(lpX + 18, btnY + 14, typeNames6[i], 0.7f, 0.7f, 0.7f);
                    }
                    drawRectOutline(lpX + 8, btnY, lpW - 16, 20, 0.25f, 0.25f, 0.25f);
                }
                std::ostringstream woss;
                woss << "Width (m): " << std::fixed << std::setprecision(1) << roadDrawWidth;
                drawStr(lpX + 15, lpY + 352, woss.str(), 0.8f, 0.8f, 0.8f);
                drawRect(lpX + 8, lpY + 358, 60, 24, 0.18f, 0.12f, 0.10f, 0.9f);
                drawRectOutline(lpX + 8, lpY + 358, 60, 24, 0.5f, 0.25f, 0.15f);
                drawStr(lpX + 27, lpY + 373, "- 0.5", 0.9f, 0.5f, 0.2f);
                drawRect(lpX + 78, lpY + 358, 60, 24, 0.10f, 0.18f, 0.12f, 0.9f);
                drawRectOutline(lpX + 78, lpY + 358, 60, 24, 0.2f, 0.5f, 0.25f);
                drawStr(lpX + 97, lpY + 373, "+ 0.5", 0.4f, 0.9f, 0.5f);
                std::ostringstream soss;
                soss << "Shoulder (m): " << std::fixed << std::setprecision(2) << roadDrawShoulder;
                drawStr(lpX + 15, lpY + 390, soss.str(), 0.8f, 0.8f, 0.8f);
                drawRect(lpX + 8, lpY + 396, 60, 24, 0.18f, 0.12f, 0.10f, 0.9f);
                drawRectOutline(lpX + 8, lpY + 396, 60, 24, 0.5f, 0.25f, 0.15f);
                drawStr(lpX + 27, lpY + 411, "- 0.25", 0.9f, 0.5f, 0.2f);
                drawRect(lpX + 78, lpY + 396, 60, 24, 0.10f, 0.18f, 0.12f, 0.9f);
                drawRectOutline(lpX + 78, lpY + 396, 60, 24, 0.2f, 0.5f, 0.25f);
                drawStr(lpX + 97, lpY + 411, "+ 0.25", 0.4f, 0.9f, 0.5f);
                drawStr(lpX + 15, lpY + 430, "CORNER ROUNDING", 0.8f, 0.65f, 0.35f);
                drawRect(lpX + 150, lpY + 428, 60, 22, 0.18f, 0.12f, 0.10f, 0.9f);
                drawRectOutline(lpX + 150, lpY + 428, 60, 22, 0.5f, 0.25f, 0.15f);
                drawStr(lpX + 164, lpY + 443, "- 2m", 0.9f, 0.5f, 0.2f);
                drawRect(lpX + 216, lpY + 428, 60, 22, 0.10f, 0.18f, 0.12f, 0.9f);
                drawRectOutline(lpX + 216, lpY + 428, 60, 22, 0.2f, 0.5f, 0.25f);
                drawStr(lpX + 230, lpY + 443, "+ 2m", 0.4f, 0.9f, 0.5f);
                std::string drawCurveText = roadDrawCornerRadius > 0.0f
                    ? std::to_string((int)roadDrawCornerRadius) + "m" : "auto";
                drawStr(lpX + 280, lpY + 443, drawCurveText, 0.8f, 0.8f, 0.8f);
                drawStr(lpX + 15, lpY + 445, "click-on-segment insert   drag freehand", 0.6f, 0.6f, 0.6f);
                drawStr(lpX + 15, lpY + 460, "Enter commit   Esc cancel   Backspace undo", 0.6f, 0.6f, 0.6f);
                drawStr(lpX + 15, lpY + 475, "Curve+width preview live   S/SHIFT straight", 0.55f, 0.55f, 0.55f);
                // Draw-tool option toggles
                {
                    float tY = lpY + 480;
                    if (roadStraightMode) {
                        drawRect(lpX + 8, tY, lpW - 16, 22, 0.30f, 0.25f, 0.10f, 0.9f);
                        drawStr(lpX + 140, tY + 15, "STRAIGHT LINE: ON   (click=off)", 0.8f, 1.0f, 0.6f);
                    } else {
                        drawRect(lpX + 8, tY, lpW - 16, 22, 0.15f, 0.15f, 0.15f, 0.9f);
                        drawStr(lpX + 140, tY + 15, "STRAIGHT LINE: off   (click=on)", 0.8f, 0.8f, 0.8f);
                    }
                    drawRectOutline(lpX + 8, tY, lpW - 16, 22, 0.35f, 0.3f, 0.15f);
                    drawStr(lpX + 18, tY + 15, "S:", 0.8f, 0.9f, 0.5f);
                }
                {
                    float tY = lpY + 502;
                    if (roadSnapToRoads) {
                        drawRect(lpX + 8, tY, lpW - 16, 22, 0.12f, 0.26f, 0.20f, 0.9f);
                        drawStr(lpX + 140, tY + 15, "SNAP TO ROADS: ON   (click=off)", 0.8f, 0.7f, 1.0f);
                    } else {
                        drawRect(lpX + 8, tY, lpW - 16, 22, 0.15f, 0.15f, 0.15f, 0.9f);
                        drawStr(lpX + 140, tY + 15, "SNAP TO ROADS: off   (click=on)", 0.8f, 0.8f, 0.8f);
                    }
                    drawRectOutline(lpX + 8, tY, lpW - 16, 22, 0.2f, 0.45f, 0.3f);
                    drawStr(lpX + 18, tY + 15, "J:", 0.8f, 0.6f, 1.0f);
                }
                // Drawing status + commit/cancel buttons
                std::ostringstream dss;
                dss << (roadDrawClosedLoop ? "ROUND ROAD: ON — click to turn off" : "ROUND ROAD: off — click to close into a loop");
                drawRect(lpX + 8, lpY + 524, lpW - 16, 20,
                         roadDrawClosedLoop ? 0.12f : 0.16f,
                         roadDrawClosedLoop ? 0.34f : 0.16f,
                         roadDrawClosedLoop ? 0.24f : 0.16f, 0.9f);
                drawRectOutline(lpX + 8, lpY + 524, lpW - 16, 20, 0.25f, 0.42f, 0.3f);
                drawStr(lpX + 20, lpY + 538, dss.str(), 0.78f, 0.9f, 0.75f);
                {
                    float oY = lpY + 548, thirdW = (lpW - 16 - 12) / 3.0f;
                    drawRect(lpX + 8, oY, thirdW, 24, 0.30f, 0.18f, 0.10f, 0.95f);
                    drawRectOutline(lpX + 8, oY, thirdW, 24, 0.55f, 0.35f, 0.2f);
                    drawStr(lpX + 8 + 22, oY + 17, "DEL PT", 0.9f, 1.0f, 0.8f);
                    drawRect(lpX + 8 + thirdW + 6, oY, thirdW, 24, 0.10f, 0.34f, 0.17f, 0.95f);
                    drawRectOutline(lpX + 8 + thirdW + 6, oY, thirdW, 24, 0.25f, 0.65f, 0.35f);
                    drawStr(lpX + 8 + thirdW + 6 + 15, oY + 17, "OK COMMIT", 0.85f, 0.9f, 1.0f);
                    drawRect(lpX + 8 + 2 * (thirdW + 6), oY, thirdW, 24, 0.32f, 0.13f, 0.11f, 0.95f);
                    drawRectOutline(lpX + 8 + 2 * (thirdW + 6), oY, thirdW, 24, 0.6f, 0.3f, 0.25f);
                    drawStr(lpX + 8 + 2 * (thirdW + 6) + 10, oY + 17, "CANCEL", 0.85f, 1.0f, 0.95f);
                }
            } else if (roadToolMode == 3) {
                std::string refName = std::string(roadRefPaths[roadRefIdx]);
                size_t slash = refName.find_last_of("/\\");
                if (slash != std::string::npos) refName = refName.substr(slash + 1);
                // ─── PROJECTED REFERENCE MAP PANEL (on the terrain, reference only) ───
                drawStr(lpX + 15, lpY + 190, "PROJECTED REFERENCE MAP", 0.9f, 0.5f, 0.1f);
                drawStr(lpX + 15, lpY + 204, refName, 0.6f, 0.75f, 0.55f);
                std::string visTxt = projRefVisible ? "PROJECTION: ON  (click to hide)" : "PROJECTION: OFF  (click to show)";
                if (projRefVisible) {
                    drawRect(lpX + 8, lpY + 210, lpW - 16, 22, 0.10f, 0.30f, 0.14f, 0.9f);
                } else {
                    drawRect(lpX + 8, lpY + 210, lpW - 16, 22, 0.16f, 0.16f, 0.16f, 0.9f);
                }
                drawRectOutline(lpX + 8, lpY + 210, lpW - 16, 22, 0.3f, 0.6f, 0.3f);
                drawStr(lpX + (lpW - (int)visTxt.size() * 7) / 2, lpY + 225, visTxt, 0.85f, 0.85f, 0.85f);

                const float cW = 140.0f, cGap = 12.0f;
                const float x1 = lpX + 8, x2 = lpX + 8 + cW + cGap;

                std::ostringstream mv;
                mv << "MOVE (X/Z 20m, Y 5m)  Y: " << std::fixed << std::setprecision(0) << projRefYOffset << "m";
                drawStr(lpX + 15, lpY + 236, mv.str(), 0.65f, 0.65f, 0.65f);
                drawRect(x1, lpY + 244, cW, 22, 0.16f, 0.16f, 0.14f, 0.9f);
                drawRectOutline(x1, lpY + 244, cW, 22, 0.35f, 0.35f, 0.3f);
                drawStr(lpX + 55, lpY + 259, "MOVE -X", 0.85f, 0.85f, 0.85f);
                drawRect(x2, lpY + 244, cW, 22, 0.14f, 0.16f, 0.16f, 0.9f);
                drawRectOutline(x2, lpY + 244, cW, 22, 0.3f, 0.35f, 0.35f);
                drawStr(lpX + 55 + cW + cGap, lpY + 259, "MOVE +X", 0.85f, 0.85f, 0.85f);

                drawRect(x1, lpY + 278, cW, 22, 0.16f, 0.16f, 0.14f, 0.9f);
                drawRectOutline(x1, lpY + 278, cW, 22, 0.35f, 0.35f, 0.3f);
                drawStr(lpX + 55, lpY + 293, "MOVE -Y", 0.85f, 0.85f, 0.85f);
                drawRect(x2, lpY + 278, cW, 22, 0.14f, 0.16f, 0.16f, 0.9f);
                drawRectOutline(x2, lpY + 278, cW, 22, 0.3f, 0.35f, 0.35f);
                drawStr(lpX + 55 + cW + cGap, lpY + 293, "MOVE +Y", 0.85f, 0.85f, 0.85f);

                drawRect(x1, lpY + 312, cW, 22, 0.16f, 0.16f, 0.14f, 0.9f);
                drawRectOutline(x1, lpY + 312, cW, 22, 0.35f, 0.35f, 0.3f);
                drawStr(lpX + 55, lpY + 327, "MOVE -Z", 0.85f, 0.85f, 0.85f);
                drawRect(x2, lpY + 312, cW, 22, 0.14f, 0.16f, 0.16f, 0.9f);
                drawRectOutline(x2, lpY + 312, cW, 22, 0.3f, 0.35f, 0.35f);
                drawStr(lpX + 55 + cW + cGap, lpY + 327, "MOVE +Z", 0.85f, 0.85f, 0.85f);

                std::ostringstream pss;
                pss << "SCALE  (" << std::fixed << std::setprecision(1) << projRefScale << "x)";
                drawStr(lpX + 15, lpY + 346, pss.str(), 0.65f, 0.65f, 0.65f);
                drawRect(x1, lpY + 354, cW, 22, 0.18f, 0.12f, 0.10f, 0.9f);
                drawRectOutline(x1, lpY + 354, cW, 22, 0.5f, 0.25f, 0.15f);
                drawStr(lpX + 62, lpY + 369, "- 0.1x", 0.9f, 0.5f, 0.2f);
                drawRect(x2, lpY + 354, cW, 22, 0.10f, 0.18f, 0.12f, 0.9f);
                drawRectOutline(x2, lpY + 354, cW, 22, 0.2f, 0.5f, 0.25f);
                drawStr(lpX + 62 + cW + cGap, lpY + 369, "+ 0.1x", 0.4f, 0.9f, 0.5f);

                drawStr(lpX + 15, lpY + 380, "FLIP", 0.65f, 0.65f, 0.65f);
                drawRect(x1, lpY + 388, cW, 22, 0.18f, 0.12f, 0.10f, 0.9f);
                drawRectOutline(x1, lpY + 388, cW, 22, 0.5f, 0.25f, 0.15f);
                drawStr(lpX + 55, lpY + 403, projRefFlipX ? "FLIP X (on)" : "FLIP X (off)", 0.9f, 0.6f, 0.4f);
                drawRect(x2, lpY + 388, cW, 22, 0.10f, 0.18f, 0.12f, 0.9f);
                drawRectOutline(x2, lpY + 388, cW, 22, 0.2f, 0.5f, 0.25f);
                drawStr(lpX + 55 + cW + cGap, lpY + 403, projRefFlipZ ? "FLIP Z (on)" : "FLIP Z (off)", 0.4f, 0.9f, 0.5f);

                std::ostringstream oss;
                oss << "OPACITY  (" << std::fixed << std::setprecision(1) << projRefOpacity << ")";
                drawStr(lpX + 15, lpY + 414, oss.str(), 0.65f, 0.65f, 0.65f);
                drawRect(x1, lpY + 422, cW, 22, 0.18f, 0.12f, 0.10f, 0.9f);
                drawRectOutline(x1, lpY + 422, cW, 22, 0.5f, 0.25f, 0.15f);
                drawStr(lpX + 62, lpY + 437, "- 0.1", 0.9f, 0.5f, 0.2f);
                drawRect(x2, lpY + 422, cW, 22, 0.10f, 0.18f, 0.12f, 0.9f);
                drawRectOutline(x2, lpY + 422, cW, 22, 0.2f, 0.5f, 0.25f);
                drawStr(lpX + 62 + cW + cGap, lpY + 437, "+ 0.1", 0.4f, 0.9f, 0.5f);

                drawRect(lpX + 8, lpY + 456, lpW - 16, 22, 0.13f, 0.13f, 0.13f, 0.9f);
                drawRectOutline(lpX + 8, lpY + 456, lpW - 16, 22, 0.4f, 0.4f, 0.4f);
                drawStr(lpX + (lpW - 46) / 2, lpY + 471, "RESET", 0.9f, 0.9f, 0.9f);
if (projSaveFlash > 0.0f) {
                    drawRect(lpX + 8, lpY + 494, lpW - 16, 22, 0.15f, 0.85f, 0.35f, 0.95f);
                    drawRectOutline(lpX + 8, lpY + 494, lpW - 16, 22, 0.4f, 1.0f, 0.6f);
                    drawStr(lpX + (lpW - 70) / 2, lpY + 509, "SAVE MAP PLACEMENT", 0.8f, 0.05f, 0.2f);
                    drawStr(lpX + 15, lpY + 530, "SAVED! -> projref_map.txt (next boot restores it)", 0.4f, 1.0f, 0.6f);
                } else {
                    drawRect(lpX + 8, lpY + 494, lpW - 16, 22, 0.10f, 0.26f, 0.16f, 0.9f);
                    drawRectOutline(lpX + 8, lpY + 494, lpW - 16, 22, 0.25f, 0.55f, 0.35f);
                    drawStr(lpX + (lpW - 70) / 2, lpY + 509, "SAVE MAP PLACEMENT", 0.8f, 0.9f, 0.15f);
                    drawStr(lpX + 15, lpY + 530, "saves center/scale/flip/opacity to projref_map.txt", 0.6f, 0.6f, 0.6f);
                }
                drawStr(lpX + 15, lpY + 545, "On launch this placement will be restored", 0.55f, 0.55f, 0.55f);
                {
                    const float mapBtnW = (lpW - 16 - 6) * 0.5f;
                    float mY = lpY + 556;
                    drawRect(lpX + 8, mY, mapBtnW, 22, 0.16f, 0.16f, 0.16f, 0.9f);
                    drawRectOutline(lpX + 8, mY, mapBtnW, 22, 0.35f, 0.35f, 0.35f);
                    drawStr(lpX + 8 + (mapBtnW - 77) / 2, mY + 15, "<  PREV MAP", 0.85f, 0.85f, 0.85f);
                    drawRect(lpX + 8 + mapBtnW + 6, mY, mapBtnW, 22, 0.16f, 0.16f, 0.16f, 0.9f);
                    drawRectOutline(lpX + 8 + mapBtnW + 6, mY, mapBtnW, 22, 0.35f, 0.35f, 0.35f);
                    drawStr(lpX + 8 + mapBtnW + 6 + (mapBtnW - 70) / 2, mY + 15, "NEXT MAP >", 0.85f, 0.85f, 0.85f);
                }
                {
                    float ppY = lpY + 586;
                    std::string pipTxt = roadRefVisible ? "PICTURE-IN-PICTURE: ON   (click=off)"
                                                        : "PICTURE-IN-PICTURE: off   (click=on)";
                    if (roadRefVisible) {
                        drawRect(lpX + 8, ppY, lpW - 16, 22, 0.10f, 0.26f, 0.30f, 0.9f);
                        drawRectOutline(lpX + 8, ppY, lpW - 16, 22, 0.2f, 0.5f, 0.6f);
                    } else {
                        drawRect(lpX + 8, ppY, lpW - 16, 22, 0.16f, 0.16f, 0.16f, 0.9f);
                        drawRectOutline(lpX + 8, ppY, lpW - 16, 22, 0.35f, 0.35f, 0.35f);
                    }
                    drawStr(lpX + (lpW - (int)pipTxt.size() * 7) / 2, ppY + 15, pipTxt, 0.85f, 0.85f, 0.85f);
                }
            } else if (rg) {
                drawStr(lpX + 15, lpY + 192, "SELECTED ROAD", 0.9f, 0.5f, 0.1f);
                if (selectedRoadIdx >= 0 && selectedRoadIdx < (int)rg->segments.size()) {
                    auto& seg = rg->segments[selectedRoadIdx];
                    std::string info = "id " + std::to_string(seg.id) + "  (" +
                                       std::to_string(seg.controlPoints.size()) + " pts, " +
                                       std::to_string((int)seg.width) + "m)";
                    drawStr(lpX + 15, lpY + 210, info, 0.85f, 0.85f, 0.85f);
                    drawStr(lpX + 15, lpY + 226, roadTypeLabel((int)seg.type), 0.85f, 0.6f, 0.3f);
                    drawStr(lpX + 15, lpY + 244, "TEXTURE (click to change)", 0.9f, 0.5f, 0.1f);
                    std::string typeNames6[] = {
                        "Colonial Avenue", "Urban Street", "Country Highway",
                        "Country Dirt", "Port", "Medina"
                    };
                    for (int i = 0; i < 6; ++i) {
                        float btnY = lpY + 260 + i * 22;
                        bool isCur = ((int)seg.type == i);
                        if (isCur) {
                            drawRect(lpX + 8, btnY, lpW - 16, 18, 0.89f, 0.36f, 0.13f, 0.9f);
                            drawStr(lpX + 18, btnY + 13, typeNames6[i], 1.0f, 1.0f, 1.0f);
                        } else {
                            drawRect(lpX + 8, btnY, lpW - 16, 18, 0.13f, 0.13f, 0.13f, 0.6f);
                            drawStr(lpX + 18, btnY + 13, typeNames6[i], 0.7f, 0.7f, 0.7f);
                        }
                        drawRectOutline(lpX + 8, btnY, lpW - 16, 18, 0.25f, 0.25f, 0.25f);
                    }
                    drawStr(lpX + 15, lpY + 405, "Selected point", 0.8f, 0.5f, 0.1f);
                    if (selectedRoadPointIdx >= 0) {
                        drawStr(lpX + 15, lpY + 422, "point " + std::to_string(selectedRoadPointIdx) +
                                   " selected", 0.72f, 0.72f, 0.72f);
                    } else {
                        drawStr(lpX + 15, lpY + 422, "none - click a point to grab it", 0.72f, 0.72f, 0.72f);
                    }
                    {
                        float wYb = lpY + 438;
                        drawStr(lpX + 15, lpY + 453, "WIDTH", 0.8f, 0.5f, 0.1f);
                        drawRect(lpX + 70, wYb, 60, 22, 0.18f, 0.12f, 0.10f, 0.9f);
                        drawRectOutline(lpX + 70, wYb, 60, 22, 0.5f, 0.25f, 0.15f);
                        drawStr(lpX + 82, wYb + 15, "- 10%", 0.9f, 0.5f, 0.2f);
                        drawRect(lpX + 136, wYb, 60, 22, 0.10f, 0.18f, 0.12f, 0.9f);
                        drawRectOutline(lpX + 136, wYb, 60, 22, 0.2f, 0.5f, 0.25f);
                        drawStr(lpX + 147, wYb + 15, "+ 10%", 0.4f, 0.9f, 0.5f);
                    }
                    {
                        // CORNER RADIUS slider - tracks the CORNER RADIUS input band
                        // (lpY + 468 .. +490) used by the editor input handler.
                        float cYb = lpY + 468;
                        const float trackX0 = lpX + 90.0f, trackW = 210.0f;
                        drawStr(lpX + 15, lpY + 483, "CORNER", 0.8f, 0.5f, 0.1f);
                        drawRect(trackX0, cYb + 6, trackW, 10, 0.06f, 0.06f, 0.06f, 1.0f);
                        drawRectOutline(trackX0, cYb + 6, trackW, 10, 0.25f, 0.25f, 0.25f);
                        float cNorm = glm::clamp(seg.cornerRadius / 30.0f, 0.0f, 1.0f);
                        drawRect(trackX0 + cNorm * (trackW - 10.0f), cYb + 3, 10.0f, 16.0f,
                                 0.89f, 0.36f, 0.13f, 1.0f);
                        // "auto" at the far left marks 0 = the automatic half-width radius.
                        drawStr(trackX0 + 2, cYb + 3, "auto", 0.55f, 0.55f, 0.55f);
                        std::string cVal = seg.cornerRadius > 0.0f
                            ? std::to_string((int)(seg.cornerRadius * 2.0f + 0.5f) / 2) + "m"
                            : std::string("auto");
                        drawStr(trackX0 + trackW - 34, cYb + 3, cVal, 0.9f, 0.5f, 0.2f);
                    }
                    {
                        float dYb = lpY + 498;
                        bool hasPoint = (selectedRoadPointIdx >= 0);
                        drawRect(lpX + 8, dYb, lpW - 16, 24, hasPoint ? 0.32f : 0.22f, 0.09f, 0.09f, 0.95f);
                        drawRectOutline(lpX + 8, dYb, lpW - 16, 24, 0.6f, 0.22f, 0.22f);
                        drawStr(lpX + (lpW - (hasPoint ? 130 : 118)) / 2, dYb + 17,
                                hasPoint ? "DELETE SELECTED POINT" : "DELETE SELECTED ROAD",
                                0.9f, 0.9f, 0.8f);
                    }
                } else {
                    drawStr(lpX + 15, lpY + 210, "None - click near a road (tool 1)", 0.6f, 0.6f, 0.6f);
                    drawStr(lpX + 15, lpY + 226, "Width +/- and delete buttons appear", 0.55f, 0.55f, 0.55f);
                    drawStr(lpX + 15, lpY + 240, "once a road is selected in the viewport", 0.55f, 0.55f, 0.55f);
                    drawStr(lpX + 15, lpY + 254, "also a per-road corner radius slider", 0.55f, 0.55f, 0.55f);
                }
            }
        }


        float saveBtnY = lpH - 35 + lpY;
        if (isSavingState) {
            double elapsed = glfwGetTime() - lastSaveTime;
            if (elapsed < 0.8) {
                float pulse = 0.5f + 0.5f * sin((float)glfwGetTime() * 12.0f);
                drawRect(lpX + 8, saveBtnY, lpW - 16, 26, 0.8f, 0.6f, 0.1f, 0.9f); // Dark Yellow
                drawRectOutline(lpX + 8, saveBtnY, lpW - 16, 26, 1.0f, 0.8f, 0.2f);
                drawStr(lpX + 115, saveBtnY + 17, "SAVING...", 1.0f, 1.0f, 1.0f);
                drawCircle2D(lpX + 100, saveBtnY + 13, 5.0f, 1.0f, 1.0f, 1.0f, pulse);
            }
            else if (elapsed < 3.0) {
                drawRect(lpX + 8, saveBtnY, lpW - 16, 26, 0.1f, 0.65f, 0.2f, 0.9f); // Bright Green
                drawRectOutline(lpX + 8, saveBtnY, lpW - 16, 26, 0.3f, 0.9f, 0.4f);
                drawStr(lpX + 90, saveBtnY + 17, "SAVED SUCCESSFULLY!", 1.0f, 1.0f, 1.0f);
            }
            else {
                isSavingState = false; // Reset state
                drawRect(lpX + 8, saveBtnY, lpW - 16, 26, 0.13f, 0.55f, 0.13f, 0.9f);
                drawRectOutline(lpX + 8, saveBtnY, lpW - 16, 26, 0.2f, 0.7f, 0.2f);
                drawStr(lpX + 100, saveBtnY + 17, "SAVE LEVEL LAYOUT", 1.0f, 1.0f, 1.0f);
            }
        } else {
            drawRect(lpX + 8, saveBtnY, lpW - 16, 26, 0.13f, 0.55f, 0.13f, 0.9f);
            drawRectOutline(lpX + 8, saveBtnY, lpW - 16, 26, 0.2f, 0.7f, 0.2f);
            drawStr(lpX + 100, saveBtnY + 17, "SAVE LEVEL LAYOUT", 1.0f, 1.0f, 1.0f);
        }
        } // end: left panel (hidden in road mode when roadPanelsVisible==false)


        // ─── 3. RIGHT PANEL: DETAILS (SELECTED ACTOR) ───
        if (activeEditorMode == 1) {
            float rpX = (float)windowWidth - 350, rpY = 42, rpW = 340, rpH = (float)windowHeight - 52;
            drawRect(rpX, rpY, rpW, rpH, 0.11f, 0.11f, 0.11f, 0.95f); // Panel background
            drawRectOutline(rpX, rpY, rpW, rpH, 0.22f, 0.22f, 0.22f); // Panel border

            // Right Panel Header
            drawRect(rpX + 1, rpY + 1, rpW - 2, 28, 0.15f, 0.15f, 0.15f, 1.0f);
            drawRect(rpX + 1, rpY + 28, rpW - 2, 1, 0.25f, 0.25f, 0.25f, 1.0f);
            drawStr(rpX + 15, rpY + 19, "DETAILS PANEL", 0.9f, 0.5f, 0.1f); // Orange header

            if (selectedPlacedIdx != -1 && selectedPlacedIdx < (int)SousseMapData::liveLandmarks.size()) {
                auto& landmark = SousseMapData::liveLandmarks[selectedPlacedIdx];
                
                // Actor Details
                drawStr(rpX + 15, rpY + 50, landmark.name, 1.0f, 1.0f, 1.0f);
                drawStr(rpX + 215, rpY + 50, "StaticMeshActor", 0.5f, 0.5f, 0.5f);
                
                // ─── 3.1 SLIDERS FOR TRANSFORM ───
                float dy = rpY + 75;
                drawRect(rpX + 8, dy, rpW - 16, 20, 0.18f, 0.18f, 0.18f, 1.0f);
                drawStr(rpX + 15, dy + 14, "▼  Interactive Transform Sliders", 0.9f, 0.9f, 0.9f);
                dy += 28;

                // Height Offset Slider (-15m to +15m)
                float hNorm = glm::clamp((landmark.yOffset + 15.0f) / 30.0f, 0.0f, 1.0f);
                drawStr(rpX + 15, dy + 10, "Height Offset: " + std::to_string((int)landmark.yOffset) + "m", 0.8f, 0.8f, 0.8f);
                drawRect(rpX + 15, dy + 15, rpW - 30, 8, 0.06f, 0.06f, 0.06f, 1.0f);
                drawRectOutline(rpX + 15, dy + 15, rpW - 30, 8, 0.2f, 0.2f, 0.2f);
                drawRect(rpX + 15 + hNorm * (rpW - 40), dy + 12, 10, 14, 0.17f, 0.42f, 0.77f, 1.0f); // Blue knob
                dy += 35;

                // Yaw Rotation Slider (0 to 360)
                float rotNorm = glm::clamp(landmark.angle / 360.0f, 0.0f, 1.0f);
                drawStr(rpX + 15, dy + 10, "Yaw Rotation: " + std::to_string((int)landmark.angle) + " deg", 0.8f, 0.8f, 0.8f);
                drawRect(rpX + 15, dy + 15, rpW - 30, 8, 0.06f, 0.06f, 0.06f, 1.0f);
                drawRectOutline(rpX + 15, dy + 15, rpW - 30, 8, 0.2f, 0.2f, 0.2f);
                drawRect(rpX + 15 + rotNorm * (rpW - 40), dy + 12, 10, 14, 0.22f, 0.55f, 0.23f, 1.0f); // Green knob
                dy += 35;

                // Scale Slider (0.1 to 5.0)
                float scNorm = glm::clamp((landmark.xScale - 0.1f) / 4.9f, 0.0f, 1.0f);
                drawStr(rpX + 15, dy + 10, "Scale Factor: " + std::to_string((int)landmark.xScale) + "x", 0.8f, 0.8f, 0.8f);
                drawRect(rpX + 15, dy + 15, rpW - 30, 8, 0.06f, 0.06f, 0.06f, 1.0f);
                drawRectOutline(rpX + 15, dy + 15, rpW - 30, 8, 0.2f, 0.2f, 0.2f);
                drawRect(rpX + 15 + scNorm * (rpW - 40), dy + 12, 10, 14, 0.76f, 0.23f, 0.13f, 1.0f); // Red knob
                dy += 45;

                // ─── 3.2 MATERIALS TEXTURE OVERRIDE (CLICKABLE LIST) ───
                drawRect(rpX + 8, dy, rpW - 16, 20, 0.18f, 0.18f, 0.18f, 1.0f);
                drawStr(rpX + 15, dy + 14, "▼  Blender Texture Paint Layers", 0.9f, 0.9f, 0.9f);
                dy += 28;

                // Texture layer selector buttons (Base / Blend)
                drawStr(rpX + 15, dy + 14, "Select Layer to paint:", 0.7f, 0.7f, 0.7f);
                dy += 20;

                // Layer 1 Button
                bool l1Active = (activeTextureLayer == 0);
                if (l1Active) {
                    drawRect(rpX + 15, dy, 145, 20, 0.89f, 0.36f, 0.13f, 0.9f); // Active orange
                    drawStr(rpX + 45, dy + 14, "Layer 1 (Base)", 1.0f, 1.0f, 1.0f);
                } else {
                    drawRect(rpX + 15, dy, 145, 20, 0.16f, 0.16f, 0.16f, 0.8f);
                    drawStr(rpX + 45, dy + 14, "Layer 1 (Base)", 0.7f, 0.7f, 0.7f);
                }
                drawRectOutline(rpX + 15, dy, 145, 20, 0.25f, 0.25f, 0.25f);

                // Layer 2 Button
                bool l2Active = (activeTextureLayer == 1);
                if (l2Active) {
                    drawRect(rpX + 175, dy, 150, 20, 0.89f, 0.36f, 0.13f, 0.9f);
                    drawStr(rpX + 205, dy + 14, "Layer 2 (Blend)", 1.0f, 1.0f, 1.0f);
                } else {
                    drawRect(rpX + 175, dy, 150, 20, 0.16f, 0.16f, 0.16f, 0.8f);
                    drawStr(rpX + 205, dy + 14, "Layer 2 (Blend)", 0.7f, 0.7f, 0.7f);
                }
                drawRectOutline(rpX + 175, dy, 150, 20, 0.25f, 0.25f, 0.25f);
                dy += 30;

                static int texPage = 0;
                int texPerPage = 6;
                int totalTexPages = ((int)SousseMapData::discoveredTextures.size() + texPerPage - 1) / texPerPage;

                for (int i = 0; i < texPerPage; ++i) {
                    int idx = texPage * texPerPage + i;
                    float btnY = dy + i * 24;
                    if (idx < (int)SousseMapData::discoveredTextures.size()) {
                        std::string fullPath = SousseMapData::discoveredTextures[idx];
                        std::string name = fullPath.substr(fullPath.find_last_of("/\\") + 1);
                        
                        bool isCurrent = (activeTextureLayer == 0) ? (landmark.customTexturePath == fullPath) : (landmark.customTexturePath2 == fullPath);
                        if (isCurrent) {
                            drawRect(rpX + 8, btnY - 2, rpW - 16, 20, 0.89f, 0.36f, 0.13f, 0.85f);
                            drawStr(rpX + 18, btnY + 12, name, 1.0f, 1.0f, 1.0f);
                        } else {
                            drawRect(rpX + 8, btnY - 2, rpW - 16, 20, 0.14f, 0.14f, 0.14f, 0.5f);
                            drawStr(rpX + 18, btnY + 12, name, 0.7f, 0.7f, 0.7f);
                        }
                        drawRectOutline(rpX + 8, btnY - 2, rpW - 16, 20, 0.22f, 0.22f, 0.22f);
                    }
                }

                // Material Reset / Page Controls
                float texPageY = dy + texPerPage * 24 + 10;
                drawRect(rpX + 15, texPageY, 150, 20, 0.22f, 0.22f, 0.22f, 0.9f);
                drawRectOutline(rpX + 15, texPageY, 150, 20, 0.3f, 0.3f, 0.3f);
                drawStr(rpX + 25, texPageY + 14, "Reset Default Texture", 0.9f, 0.9f, 0.9f);

                drawStr(rpX + 180, texPageY + 14, "Page " + std::to_string(texPage + 1) + "/" + std::to_string(totalTexPages), 0.6f, 0.6f, 0.6f);
                dy = texPageY + 30;

                // Texture Layer Blend Weight Slider
                float bNorm = glm::clamp(landmark.textureBlend, 0.0f, 1.0f);
                drawStr(rpX + 15, dy + 10, "Blender Layer Blend Weight: " + std::to_string((int)(landmark.textureBlend * 100)) + "%", 0.8f, 0.8f, 0.8f);
                drawRect(rpX + 15, dy + 15, rpW - 30, 8, 0.06f, 0.06f, 0.06f, 1.0f);
                drawRectOutline(rpX + 15, dy + 15, rpW - 30, 8, 0.2f, 0.2f, 0.2f);
                drawRect(rpX + 15 + bNorm * (rpW - 40), dy + 12, 10, 14, 0.89f, 0.36f, 0.13f, 1.0f); // Orange knob
                dy += 45;

                // ─── 3.3 ACTIONS (CLONE & DELETE) ───
                drawRect(rpX + 8, dy, rpW - 16, 20, 0.18f, 0.18f, 0.18f, 1.0f);
                drawStr(rpX + 15, dy + 14, "▼  Actor Actions", 0.9f, 0.9f, 0.9f);
                dy += 28;

                // Clone Button
                drawRect(rpX + 8, dy, rpW - 16, 26, 0.17f, 0.42f, 0.77f, 0.9f); // Blue
                drawRectOutline(rpX + 8, dy, rpW - 16, 26, 0.25f, 0.55f, 0.9f);
                drawStr(rpX + 110, dy + 17, "CLONE ACTOR", 1.0f, 1.0f, 1.0f);
                dy += 35;

                // Delete Button
                drawRect(rpX + 8, dy, rpW - 16, 26, 0.76f, 0.23f, 0.13f, 0.9f); // Red
                drawRectOutline(rpX + 8, dy, rpW - 16, 26, 0.9f, 0.35f, 0.25f);
                drawStr(rpX + 105, dy + 17, "DELETE ACTOR FROM LEVEL", 1.0f, 1.0f, 1.0f);

            } else {
                // Show level details if no actor is selected
                drawStr(rpX + 20, rpY + 60, "Click directly on any placed actor", 0.6f, 0.6f, 0.6f);
                drawStr(rpX + 20, rpY + 80, "in the viewport to select it.", 0.6f, 0.6f, 0.6f);
                
                float dy = rpY + 120;
                drawRect(rpX + 8, dy, rpW - 16, 20, 0.18f, 0.18f, 0.18f, 1.0f);
                drawStr(rpX + 15, dy + 14, "▼  Level Details", 0.9f, 0.9f, 0.9f);
                dy += 28;
                
                drawStr(rpX + 15, dy, "Total Level Landmark Actors: " + std::to_string(SousseMapData::liveLandmarks.size()), 0.7f, 0.7f, 0.7f); dy += 18;
                drawStr(rpX + 15, dy, "Active Terrain Chunk Width: " + std::to_string((int)SousseMapData::WORLD_ANCHOR.x), 0.7f, 0.7f, 0.7f); dy += 18;
            }
        }

        // ─── ROADS GRAPH TRANSFORM PANEL (RIGHT SIDE, mode 5) ───
        if (activeEditorMode == 5 && roadPanelsVisible) {
            const float rpX = (float)windowWidth - 350, rpY = 42, rpW = 340, rpH = (float)windowHeight - 52;
            const float colW = 150.0f, gap = 10.0f, smallColW = 96.0f;
            drawRect(rpX, rpY, rpW, rpH, 0.11f, 0.11f, 0.11f, 0.95f);
            drawRectOutline(rpX, rpY, rpW, rpH, 0.22f, 0.22f, 0.22f);
            drawRect(rpX + 1, rpY + 1, rpW - 2, 28, 0.15f, 0.15f, 0.15f, 1.0f);
            drawStr(rpX + 15, rpY + 19, "ROADS GRAPH TRANSFORM", 1.0f, 0.6f, 0.1f);

            float dy = rpY + 45;

            // Mirror
            drawStr(rpX + 15, dy + 6, "MIRROR the whole map", 0.8f, 0.8f, 0.8f);
            float mbY = dy + 22;
            drawRect(rpX + 15, mbY, colW, 26, 0.20f, 0.16f, 0.12f, 0.9f);
            drawRectOutline(rpX + 15, mbY, colW, 26, 0.5f, 0.3f, 0.2f);
            drawStr(rpX + 40, mbY + 17, "MIRROR X", 0.9f, 0.9f, 0.9f);
            drawRect(rpX + 15 + colW + gap, mbY, colW, 26, 0.20f, 0.16f, 0.12f, 0.9f);
            drawRectOutline(rpX + 15 + colW + gap, mbY, colW, 26, 0.5f, 0.3f, 0.2f);
            drawStr(rpX + 15 + colW + gap + 40, mbY + 17, "MIRROR Z", 0.9f, 0.9f, 0.9f);
            dy = mbY + 40;

            // Rotate
            drawStr(rpX + 15, dy + 6, "ROTATE the whole map (90 deg steps)", 0.8f, 0.8f, 0.8f);
            float rbY = dy + 22;
            const char* rotLabels[3] = { "ROT 90 <", "ROT 180", "ROT 90 >" };
            for (int i = 0; i < 3; ++i) {
                float bx = rpX + 15 + i * (smallColW + gap);
                drawRect(bx, rbY, smallColW, 26, 0.16f, 0.18f, 0.22f, 0.9f);
                drawRectOutline(bx, rbY, smallColW, 26, 0.3f, 0.4f, 0.5f);
                drawStr(bx + 12, rbY + 17, rotLabels[i], 0.85f, 0.85f, 0.85f);
            }
            dy = rbY + 40;

            // Scale
            std::ostringstream scoss;
            scoss << "SCALE whole map   x" << std::fixed << std::setprecision(2) << roadTransformScale;
            drawStr(rpX + 15, dy + 6, scoss.str(), 0.8f, 0.8f, 0.8f);
            float scY = dy + 22;
            drawRect(rpX + 15, scY, colW, 26, 0.16f, 0.12f, 0.10f, 0.9f);
            drawRectOutline(rpX + 15, scY, colW, 26, 0.4f, 0.25f, 0.2f);
            drawStr(rpX + 45, scY + 17, "SCALE -5%", 0.85f, 0.85f, 0.85f);
            drawRect(rpX + 15 + colW + gap, scY, colW, 26, 0.10f, 0.16f, 0.12f, 0.9f);
            drawRectOutline(rpX + 15 + colW + gap, scY, colW, 26, 0.25f, 0.45f, 0.3f);
            drawStr(rpX + 15 + colW + gap + 45, scY + 17, "SCALE +5%", 0.85f, 0.85f, 0.85f);
            float applyY = scY + 32;
            drawRect(rpX + 15, applyY, rpW - 30, 26, 0.17f, 0.30f, 0.50f, 0.9f);
            drawRectOutline(rpX + 15, applyY, rpW - 30, 26, 0.35f, 0.55f, 0.75f);
            drawStr(rpX + 95, applyY + 17, "APPLY SCALE", 1.0f, 1.0f, 1.0f);
            float resetY = applyY + 32;
            drawRect(rpX + 15, resetY, rpW - 30, 26, 0.22f, 0.10f, 0.10f, 0.9f);
            drawRectOutline(rpX + 15, resetY, rpW - 30, 26, 0.5f, 0.25f, 0.25f);
            drawStr(rpX + 105, resetY + 17, "RESET SCALE 1.0", 0.9f, 0.9f, 0.9f);
            dy = resetY + 40;

            // Connect
            std::ostringstream coss;
            coss << "CONNECT ROADS   tolerance " << (int)roadConnectTolerance << "m";
            drawStr(rpX + 15, dy + 6, coss.str(), 0.8f, 0.8f, 0.8f);
            float cbY = dy + 22;
            drawRect(rpX + 15, cbY, rpW - 30, 26, 0.30f, 0.45f, 0.18f, 0.9f);
            drawRectOutline(rpX + 15, cbY, rpW - 30, 26, 0.55f, 0.7f, 0.35f);
            drawStr(rpX + 80, cbY + 17, "CONNECT ROADS", 1.0f, 1.0f, 1.0f);
            float tolY = cbY + 32;
            drawRect(rpX + 15, tolY, colW, 26, 0.16f, 0.16f, 0.14f, 0.9f);
            drawRectOutline(rpX + 15, tolY, colW, 26, 0.35f, 0.35f, 0.3f);
            drawStr(rpX + 40, tolY + 17, "TOL -5m", 0.85f, 0.85f, 0.85f);
            drawRect(rpX + 15 + colW + gap, tolY, colW, 26, 0.14f, 0.16f, 0.16f, 0.9f);
            drawRectOutline(rpX + 15 + colW + gap, tolY, colW, 26, 0.3f, 0.35f, 0.35f);
            drawStr(rpX + 15 + colW + gap + 40, tolY + 17, "TOL +5m", 0.85f, 0.85f, 0.85f);
            dy = tolY + 40;

            // Move
            std::ostringstream moss;
            moss << "MOVE the whole map   step " << (int)roadMoveStep << "m";
            drawStr(rpX + 15, dy + 6, moss.str(), 0.8f, 0.8f, 0.8f);
            float mY1 = dy + 22;
            drawRect(rpX + 15, mY1, colW, 26, 0.16f, 0.16f, 0.14f, 0.9f);
            drawRectOutline(rpX + 15, mY1, colW, 26, 0.35f, 0.35f, 0.3f);
            drawStr(rpX + 45, mY1 + 17, "MOVE -X", 0.85f, 0.85f, 0.85f);
            drawRect(rpX + 15 + colW + gap, mY1, colW, 26, 0.14f, 0.16f, 0.16f, 0.9f);
            drawRectOutline(rpX + 15 + colW + gap, mY1, colW, 26, 0.3f, 0.35f, 0.35f);
            drawStr(rpX + 15 + colW + gap + 45, mY1 + 17, "MOVE +X", 0.85f, 0.85f, 0.85f);
            float mY2 = mY1 + 32;
            drawRect(rpX + 15, mY2, colW, 26, 0.16f, 0.16f, 0.14f, 0.9f);
            drawRectOutline(rpX + 15, mY2, colW, 26, 0.35f, 0.35f, 0.3f);
            drawStr(rpX + 45, mY2 + 17, "MOVE -Z", 0.85f, 0.85f, 0.85f);
            drawRect(rpX + 15 + colW + gap, mY2, colW, 26, 0.14f, 0.16f, 0.16f, 0.9f);
            drawRectOutline(rpX + 15 + colW + gap, mY2, colW, 26, 0.3f, 0.35f, 0.35f);
            drawStr(rpX + 15 + colW + gap + 45, mY2 + 17, "MOVE +Z", 0.85f, 0.85f, 0.85f);
            float mY3 = mY2 + 32;
            drawRect(rpX + 15, mY3, colW, 26, 0.16f, 0.12f, 0.12f, 0.9f);
            drawRectOutline(rpX + 15, mY3, colW, 26, 0.4f, 0.25f, 0.25f);
            drawStr(rpX + 40, mY3 + 17, "STEP -5m", 0.85f, 0.85f, 0.85f);
            drawRect(rpX + 15 + colW + gap, mY3, colW, 26, 0.12f, 0.14f, 0.16f, 0.9f);
            drawRectOutline(rpX + 15 + colW + gap, mY3, colW, 26, 0.25f, 0.3f, 0.35f);
            drawStr(rpX + 15 + colW + gap + 40, mY3 + 17, "STEP +5m", 0.85f, 0.85f, 0.85f);
            dy = mY3 + 34;

            drawStr(rpX + 15, dy + 4, "F2 saves the transformed roads", 0.55f, 0.55f, 0.55f);
        }

        if (activeEditorMode == 5 && !roadPanelsVisible) {
            std::string hint = "Panels hidden (TAB shows them).  Tools: 1 Select   2 Draw (drag draws, Enter commit)   3 Edit Pts    T type    Del delete    R reference";
            drawStr(windowWidth / 2.0f - 400, (float)windowHeight - 42, hint.c_str(), 0.65f, 0.65f, 0.65f);
        }

        // ─── REFERENCE IMAGE OVERLAY (PiP) in road edit mode ───
        if (activeEditorMode == 5) {
            if (roadRefVisible && roadRefTex) {
                std::string refNameSub = std::string(roadRefPaths[roadRefIdx]);
                size_t rslash = refNameSub.find_last_of("/\\");
                if (rslash != std::string::npos) refNameSub = refNameSub.substr(rslash + 1);
                float rw = 300.0f;
                float rh = (roadRefH > 0) ? rw * ((float)roadRefH / (float)roadRefW) : 200.0f;
                float rx = (float)windowWidth - rw - 12.0f;
                float ry = 40.0f;
                drawRect(rx - 6, ry - 26, rw + 12, rh + 32, 0.04f, 0.04f, 0.04f, 0.9f);
                drawRectOutline(rx - 6, ry - 26, rw + 12, rh + 32, 0.5f, 0.5f, 0.5f);
                std::string title = std::string("REF MAP ") + std::to_string(roadRefIdx + 1) + "/" +
                                     std::to_string(roadRefPathCount) + "  " + refNameSub +
                                     "  (R toggle, [ ] cycle)";
                drawStr(rx, ry - 16, title, 0.85f, 0.85f, 0.85f);
                glEnable(GL_TEXTURE_2D);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D,
                              (roadToolMode == 1 && roadRefDrawTex) ? roadRefDrawTex : roadRefTex);
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
                glColor4f(1.0f, 1.0f, 1.0f, 0.85f);
                glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 0.0f); glVertex2f(rx, ry);
                glTexCoord2f(1.0f, 0.0f); glVertex2f(rx + rw, ry);
                glTexCoord2f(1.0f, 1.0f); glVertex2f(rx + rw, ry + rh);
                glTexCoord2f(0.0f, 1.0f); glVertex2f(rx, ry + rh);
                glEnd();
                glDisable(GL_TEXTURE_2D);
            }
        }

        // ─── 4. 2D PROJECTED BRUSH SIZE CIRCLE (BLENDER STYLE) ───
        if ((activeEditorMode == 2 || activeEditorMode == 3) && lastRayHitPos.y > -900.0f) {
            float aspectRatio = (float)windowWidth / (float)windowHeight;
            glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspectRatio, 0.5f, 10000.0f);
            glm::mat4 view = camera->getViewMatrix();
            
            glm::vec4 screenCenter = proj * view * glm::vec4(lastRayHitPos, 1.0f);
            glm::vec4 screenOffset = proj * view * glm::vec4(lastRayHitPos + glm::vec3(brushRadius, 0.0f, 0.0f), 1.0f);
            
            if (screenCenter.w > 0.0f && screenOffset.w > 0.0f) {
                float cx = (screenCenter.x / screenCenter.w * 0.5f + 0.5f) * windowWidth;
                float cy = (1.0f - (screenCenter.y / screenCenter.w * 0.5f + 0.5f)) * windowHeight;
                
                float ox = (screenOffset.x / screenOffset.w * 0.5f + 0.5f) * windowWidth;
                float oy = (1.0f - (screenOffset.y / screenOffset.w * 0.5f + 0.5f)) * windowHeight;
                
                float rPixels = sqrt((ox - cx)*(ox - cx) + (oy - cy)*(oy - cy));
                
                if (rPixels > 1.0f && rPixels < 1500.0f) {
                    // Orange circle outline
                    drawCircle2D(cx, cy, rPixels, 1.0f, 0.5f, 0.0f, 0.8f);
                    
                    // Subtle semi-transparent orange fill
                    glColor4f(1.0f, 0.5f, 0.0f, 0.12f);
                    glBegin(GL_TRIANGLE_FAN);
                    glVertex2f(cx, cy);
                    for (int i = 0; i <= 64; ++i) {
                        float angle = (float)i * 2.0f * 3.14159265f / 64.0f;
                        glVertex2f(cx + cos(angle) * rPixels, cy + sin(angle) * rPixels);
                    }
                    glEnd();
                }
            }
        }
        } // Close inner if (editMode)

        // ─── GLOBAL STATUS NOTIFICATION OVERLAY ───
        if (isSavingState) {
            double elapsed = glfwGetTime() - lastSaveTime;
            if (elapsed < 3.0) {
                float opacity = 1.0f;
                if (elapsed > 2.5) {
                    opacity = 1.0f - (float)((elapsed - 2.5) / 0.5); // Fade out in last 0.5 seconds
                }
                
                // Draw a sleek HUD overlay at the top center of the screen
                float bannerW = 280;
                float bannerH = 36;
                float bannerX = ((float)windowWidth - bannerW) / 2.0f;
                float bannerY = 45; // Below the menu bar
                
                if (elapsed < 0.8) {
                    // Saving state
                    drawRect(bannerX, bannerY, bannerW, bannerH, 0.15f, 0.15f, 0.15f, 0.85f * opacity);
                    drawRectOutline(bannerX, bannerY, bannerW, bannerH, 0.8f, 0.6f, 0.1f);
                    
                    float pulse = 0.5f + 0.5f * sin((float)glfwGetTime() * 12.0f);
                    drawCircle2D(bannerX + 25, bannerY + 18, 6.0f, 0.8f, 0.6f, 0.1f, pulse * opacity);
                    drawStr(bannerX + 50, bannerY + 22, "Saving world changes...", 0.9f, 0.9f, 0.9f);
                } else {
                    // Saved state
                    drawRect(bannerX, bannerY, bannerW, bannerH, 0.1f, 0.15f, 0.1f, 0.85f * opacity);
                    drawRectOutline(bannerX, bannerY, bannerW, bannerH, 0.2f, 0.7f, 0.3f);
                    
                    drawCircle2D(bannerX + 25, bannerY + 18, 6.0f, 0.2f, 0.8f, 0.3f, opacity);
                    drawStr(bannerX + 50, bannerY + 22, "Saved level successfully!", 0.8f, 1.0f, 0.8f);
                }
            } else {
                isSavingState = false; // Done
            }
        }

        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
    }

    // 9. Cleanup
    glDisable(GL_LIGHTING);
    glUseProgram(0);

    if (activeGpuTimerSlot >= 0) {
        glEndQuery(GL_TIME_ELAPSED);
        gpuTimerPending[activeGpuTimerSlot] = true;
    }

    recordRenderPhase(renderPostMs);
    gpuPhaseNext(); // post: closes the last phase query
    if (profileRender) {
        static int profileFrames = 0;
        static double accumulatedSkyMs = 0.0;
        static double accumulatedPreTerrainMs = 0.0;
        static double accumulatedTerrainMs = 0.0;
        static double accumulatedObjectsMs = 0.0;
        static double accumulatedWaterMs = 0.0;
        static double accumulatedPostMs = 0.0;
        accumulatedSkyMs += skyRenderMs;
        accumulatedPreTerrainMs += preTerrainRenderMs;
        accumulatedTerrainMs += terrainRenderMs;
        accumulatedObjectsMs += objectsRenderMs;
        accumulatedWaterMs += waterRenderMs;
        accumulatedPostMs += renderPostMs;
        if (++profileFrames >= 60) {
            const double scale = 1.0 / profileFrames;
            std::cout << "[RENDER PERF] avg ms: sky=" << accumulatedSkyMs * scale
                      << " setup=" << accumulatedPreTerrainMs * scale
                      << " terrain=" << accumulatedTerrainMs * scale
                      << " objects=" << accumulatedObjectsMs * scale
                      << " water=" << accumulatedWaterMs * scale
                      << " post=" << accumulatedPostMs * scale << '\n';
            profileFrames = 0;
            accumulatedSkyMs = accumulatedPreTerrainMs = 0.0;
            accumulatedTerrainMs = accumulatedObjectsMs = accumulatedWaterMs = accumulatedPostMs = 0.0;
        }
    }
}
void Game::performSave() {
    isSavingState = true;
    lastSaveTime = glfwGetTime();
    
    SousseMapData::saveLayout("assets/saves/city.txt");
    if (terrain) {
        terrain->saveTerrain();
        terrain->saveRoadsToJSON("assets/roads/roads_manual.json");
    }
    std::cout << "[Editor] Saved level layout, terrain and road graph!\n";
}

// Persists the projected reference map placement & scale (center, size, flip, opacity, height, image index)
void Game::saveProjRefMap() {
    const std::string path = "assets/roads/projref_map.txt";
    {
        // Preserve the previous placement to projref_map.txt.bak_old before overwriting
        const std::string bakPath = path + ".bak_old";
        std::ifstream oldf(path);
        if (oldf.good()) {
            std::string old((std::istreambuf_iterator<char>(oldf)), std::istreambuf_iterator<char>());
            oldf.close();
            std::ofstream bak(bakPath, std::ios::trunc);
            if (bak) { bak << old; bak.close(); }
        }
    }
    std::ofstream f(path, std::ios::trunc);
    if (!f) {
        std::cerr << "[Road] FAILED to open " << path << " for writing\n";
        return;
    }
    f << "centerX " << projRefCenter.x << "\n"
      << "centerZ " << projRefCenter.y << "\n"
      << "mapW " << projRefMapW << "\n"
      << "scale " << projRefScale << "\n"
      << "flipX " << (projRefFlipX ? 1 : 0) << "\n"
      << "flipZ " << (projRefFlipZ ? 1 : 0) << "\n"
      << "opacity " << projRefOpacity << "\n"
      << "yOffset " << projRefYOffset << "\n"
      << "idx " << roadRefIdx << "\n";
    f.close();
    projSaveFlash = 1.6f; // visual feedback: highlight the button + show "SAVED" line
    projSavedCenter = projRefCenter;
    projSavedScale = projRefScale;
    projSavedFlipX = projRefFlipX;
    projSavedFlipZ = projRefFlipZ;
    projSavedOpacity = projRefOpacity;
    projSavedYOffset = projRefYOffset;
    projSavedIdx = roadRefIdx;
    projSavedMapW = projRefMapW;
    projLastSave = glfwGetTime();
    std::cout << "[Road] Projection placement saved to " << path
              << " (center " << projRefCenter.x << ", " << projRefCenter.y
              << ", scale " << projRefScale << "x)\n";
}

void Game::commitRoadDraft() {
    RoadGraph* rg = terrain ? terrain->getRoadGraph() : nullptr;
    if (rg && roadDraftPoints.size() >= 2) {
        uint32_t nextId = 0;
        for (const auto& s : rg->segments) nextId = std::max(nextId, s.id + 1);
        RoadSegment ns;
        ns.id = nextId;
        ns.type = (RoadType)roadDrawType;
        ns.width = roadDrawWidth;
        ns.shoulderWidth = roadDrawShoulder;
        ns.cornerRadius = roadDrawCornerRadius;

        // -- Closed-loop detection -----------------------------------------
        // When the last draft point coincides with the first (user snapped
        // back to the start), mark this as a closed ring road so the mesh
        // builder emits a seamless loop: no seam kink, no texture squash,
        // no end caps.
        //
        // This must stay in step with the auto-detect in RoadSpline::evaluate.
        // It used to accept any road whose two ends finished within 15 m of each
        // other, which quietly promoted ordinary L-shaped streets to rings — and a
        // closed segment is excluded from endpoint clustering, so the road then
        // connected to nothing at all. Only treat it as a loop when the author
        // actually landed back on the first point.
        const float loopThresh = glm::max(1.0f, ns.width * 0.5f);
        const glm::vec3& dFront = roadDraftPoints.front();
        const glm::vec3& dBack  = roadDraftPoints.back();
        float dLoop = std::sqrt((dBack.x - dFront.x) * (dBack.x - dFront.x) +
                                (dBack.z - dFront.z) * (dBack.z - dFront.z));
        bool isClosedLoop = ((roadDrawClosedLoop && roadDraftPoints.size() >= 3) ||
                             (roadDraftPoints.size() >= 4 && dLoop < loopThresh));

        if (isClosedLoop) {
            // Snap the back point exactly onto the front so the mesh seam is pixel-perfect
            roadDraftPoints.back() = glm::vec3(dFront.x, dBack.y, dFront.z);
            ns.closed = true;
            std::cout << "[Road] Detected closed loop (gap=" << dLoop << "m, thresh=" << loopThresh << "m)\n";
        }

        // Ramer-Douglas-Peucker simplification (2D x/z) so freehand drags don't bake
        // in every 5m sample — keeps drawn roads smooth with far fewer control points.
        auto simplifyDraft = [&](const std::vector<glm::vec3>& pts, float tol) {
            if (pts.size() <= 2) return pts;
            std::vector<bool> keep(pts.size(), false);
            keep.front() = keep.back() = true;
            std::vector<std::pair<int, int>> stack;
            stack.push_back({ 0, (int)pts.size() - 1 });
            while (!stack.empty()) {
                int a = stack.back().first, b = stack.back().second;
                stack.pop_back();
                if (b - a < 2) continue;
                float maxD = 0.0f; int at = -1;
                for (int i = a + 1; i < b; ++i) {
                    float d = distPointToPolylineSegment(pts[i].x, pts[i].z,
                                                         pts[a].x, pts[a].z, pts[b].x, pts[b].z);
                    if (d > maxD) { maxD = d; at = i; }
                }
                if (maxD > tol && at >= 0) {
                    keep[at] = true;
                    stack.push_back({ a, at });
                    stack.push_back({ at, b });
                }
            }
            std::vector<glm::vec3> out;
            for (size_t i = 0; i < pts.size(); ++i) if (keep[i]) out.push_back(pts[i]);
            return out;
        };
        // tolerance scales with road width so narrow medina alleys keep their shape
        float tol = glm::max(1.2f, ns.width * 0.15f);
        std::vector<glm::vec3> simp = simplifyDraft(roadDraftPoints, tol);
        if (isClosedLoop && simp.size() >= 3) {
            const glm::vec3& first = simp.front();
            const glm::vec3& last = simp.back();
            const float seam = glm::length(glm::vec2(last.x - first.x, last.z - first.z));
            if (seam > 0.01f) simp.push_back(first);
            ns.closed = true;
        }
        const int skipped = (int)roadDraftPoints.size() - (int)simp.size();
        for (const auto& p : simp) {
            RoadPoint rp; rp.pos.x = p.x; rp.pos.y = p.z;
            ns.controlPoints.push_back(rp);
        }
        rg->segments.push_back(ns);
        selectedRoadIdx = (int)rg->segments.size() - 1;
        selectedRoadPointIdx = -1;
        roadGraphDirty = true;
        roadDraftPoints.clear();
        roadDrawClosedLoop = false;
        draftDragIdx = -1;
        draftInsertNewly = false;
        std::cout << "[Road] Committed road id=" << ns.id
                  << " (" << ns.controlPoints.size() << " control points"
                  << (ns.closed ? ", CLOSED LOOP" : "")
                  << (skipped > 0 ? ", auto-simplified " + std::to_string(skipped) + " pts" : "") << ")\n";
    } else {
        std::cout << "[Road] Need at least 2 draft points to commit\n";
    }
}

void Game::cancelRoadDraft() {
    if (!roadDraftPoints.empty()) {
        roadDraftPoints.clear();
        draftDragIdx = -1;
        std::cout << "[Road] Drawing cancelled (" << roadDraftPoints.size() << " draft points cleared)\n";
    }
}

bool Game::loadProjRefMap() {
    const std::string path = "assets/roads/projref_map.txt";
    std::ifstream f(path);
    if (!f.is_open()) return false;
    float cx = projRefCenter.x, cz = projRefCenter.y, mw = 0.0f, sc = 1.0f, op = 0.85f, yo = 0.0f;
    int flX = 0, flZ = 0, idx = 0;
    bool haveMw = false;
    std::string key;
    while (f >> key) {
        if (key == "centerX") f >> cx;
        else if (key == "centerZ") f >> cz;
        else if (key == "mapW") { f >> mw; haveMw = true; }
        else if (key == "scale") f >> sc;
        else if (key == "flipX") f >> flX;
        else if (key == "flipZ") f >> flZ;
        else if (key == "opacity") f >> op;
        else if (key == "yOffset") f >> yo;
        else if (key == "idx") f >> idx;
    }
    f.close();
    projRefCenter.x = cx;
    projRefCenter.y = cz;
    projRefScale = sc;
    projRefFlipX = (flX != 0);
    projRefFlipZ = (flZ != 0);
    projRefOpacity = glm::clamp(op, 0.1f, 1.0f);
    projRefYOffset = yo;
    roadRefIdx = (idx >= 0 && idx < roadRefPathCount) ? idx : 0;
    if (haveMw) {
        projRefMapW = mw;
        projSavedCenter = projRefCenter;
        projSavedScale = projRefScale;
        projSavedFlipX = projRefFlipX;
        projSavedFlipZ = projRefFlipZ;
        projSavedOpacity = projRefOpacity;
        projSavedYOffset = projRefYOffset;
        projSavedIdx = roadRefIdx;
        projSavedMapW = projRefMapW;
        projLastSave = glfwGetTime();
        std::cout << "[Road] Projection placement restored from " << path
                  << " (center " << projRefCenter.x << ", " << projRefCenter.y
                  << ", scale " << projRefScale << "x, opacity " << projRefOpacity << ")\n";
        return true;
    }
    return false;
}

Camera& Game::getCamera() {
    return *camera;
}

void Game::keyDown(int key) {
    // --- GLOBAL EDITOR KEYS ---
    if (key == GLFW_KEY_F1) {
        activeEditorMode = (activeEditorMode == 0) ? 1 : 0;
        editMode = (activeEditorMode != 0);
        std::cout << "[Editor] Toggle Editor Mode: " << (editMode ? "ACTIVE" : "INACTIVE") << " (Mode: " << activeEditorMode << ")\n";
        return;
    }
    if (key == GLFW_KEY_F2) {
        performSave();
        return;
    }
    if (key == GLFW_KEY_F3) {
        player->setPosition(Vec3(4275.0f, -46.5f, -835.0f));
        camera->setAngles(355.0f, 10.0f);
        camera->reset();
        std::cout << "[Teleport] Place Pichon / Square de la Marine (Postcard View)\n";
        return;
    }
    if (key == GLFW_KEY_F4) {
        player->setPosition(Vec3(4240.0f, -46.5f, -600.0f));
        camera->setAngles(300.0f, 12.0f);
        camera->reset();
        std::cout << "[Teleport] Cours de la Marine Waterfront Boulevard\n";
        return;
    }
    if (key == GLFW_KEY_F5) {
        player->setPosition(Vec3(3038.0f, -10.0f, 466.0f));
        camera->setAngles(42.0f, 15.0f);
        camera->reset();
        std::cout << "[Teleport] Kasbah Hill Overlook (Panoramic Vista to Port)\n";
        return;
    }
    if (key == GLFW_KEY_F6) {
        player->setPosition(Vec3(-435.0f, 0.0f, 1315.0f));
        camera->setAngles(0.0f, 10.0f);
        camera->reset();
        std::cout << "[Teleport] Train Station Countryside\n";
        return;
    }

    if (editMode) {
        // Handle Editor specific inputs

        // ── Road Edit mode keys ──
        if (activeEditorMode == 5) {
            if (key == GLFW_KEY_TAB) {
                roadPanelsVisible = !roadPanelsVisible;
                std::cout << "[Road] Side panels " << (roadPanelsVisible ? "shown (Tab hides to free the viewport for clicks)" : "hidden (Tab shows them again)") << "\n";
                return;
            }
            if (key == GLFW_KEY_R) {
                roadRefVisible = !roadRefVisible;
                std::cout << "[Road] Reference image " << (roadRefVisible ? "shown" : "hidden") << " (R toggles)\n";
                return;
            }
            if (key == GLFW_KEY_LEFT_BRACKET || key == GLFW_KEY_RIGHT_BRACKET) {
                int dir = (key == GLFW_KEY_LEFT_BRACKET) ? -1 : 1;
                int next = (roadRefIdx + dir + roadRefPathCount) % roadRefPathCount;
                if (next != roadRefIdx) {
                    roadRefIdx = next;
                    roadRefLoaded = false;   // force reload next frame
                    std::cout << "[Road] Reference map -> " << roadRefPaths[roadRefIdx] << "\n";
                }
                return;
            }
            if (key == GLFW_KEY_1) { if (roadToolMode == 1) { roadDraftPoints.clear(); draftDragIdx = -1; } roadToolMode = 0; std::cout << "[Road] Tool: Select Road\n"; return; }
            if (key == GLFW_KEY_2) { if (roadToolMode != 1) draftDragIdx = -1; roadToolMode = 1; std::cout << "[Road] Tool: Draw Road (LMB click=add/grab point, drag on edge=insert, drag=draw, Enter commit, Esc cancel)\n"; return; }
            if (key == GLFW_KEY_3) { if (roadToolMode == 1) { roadDraftPoints.clear(); draftDragIdx = -1; } roadToolMode = 2; std::cout << "[Road] Tool: Edit Points (click grab/insert, drag move, Del remove)\n"; return; }
            if (key == GLFW_KEY_4) { if (roadToolMode == 1) { roadDraftPoints.clear(); draftDragIdx = -1; } roadToolMode = 3; std::cout << "[Road] Tool: Reference Map (drag to move, panel for scale/flip)\n"; return; }
            // (in Draw tool) Del / Backspace pops the last draft point (undo)
            if ((key == GLFW_KEY_DELETE || key == GLFW_KEY_BACKSPACE) && roadToolMode == 1 && !roadDraftPoints.empty()) {
                roadDraftPoints.pop_back();
                draftDragIdx = -1;
                std::cout << "[Road] Removed last draft point (" << roadDraftPoints.size() << " left)\n";
                return;
            }
            // (in Select tool) '=' / '-' +/- 10% scales the selected road's width ("scale roads")
            if ((key == GLFW_KEY_EQUAL || key == GLFW_KEY_MINUS) && roadToolMode == 0) {
                RoadGraph* rgw = terrain->getRoadGraph();
                if (rgw && selectedRoadIdx >= 0 && selectedRoadIdx < (int)rgw->segments.size()) {
                    float f = (key == GLFW_KEY_EQUAL) ? 1.1f : 1.0f / 1.1f;
                    auto& seg = rgw->segments[selectedRoadIdx];
                    seg.width = glm::clamp(seg.width * f, 2.0f, 20.0f);
                    roadGraphDirty = true;
                    std::cout << "[Road] Selected width -> " << seg.width << "m (id=" << seg.id << ")\n";
                } else {
                    std::cout << "[Road] Select a road first (tool 1), then '='/' -' scales its width\n";
                }
                return;
            }
            if (key == GLFW_KEY_T) {
                if (roadToolMode == 1) {
                    roadDrawType = (roadDrawType + 1) % 6;
                    roadDrawWidth = roadDefaultWidthFor((RoadType)roadDrawType);
                    roadDrawShoulder = roadDefaultShoulderFor((RoadType)roadDrawType);
                    std::cout << "[Road] Next road type -> " << roadTypeLabel(roadDrawType)
                              << " (w " << roadDrawWidth << "m, s " << roadDrawShoulder << "m)\n";
                    return;
                }
                RoadGraph* rg = terrain->getRoadGraph();
                if (rg && selectedRoadIdx >= 0 && selectedRoadIdx < (int)rg->segments.size()) {
                    auto& seg = rg->segments[selectedRoadIdx];
                    seg.type = (RoadType)((((int)seg.type) + 1) % 6);
                    seg.width = roadDefaultWidthFor(seg.type);
                    seg.shoulderWidth = roadDefaultShoulderFor(seg.type);
                    roadGraphDirty = true;
                    std::cout << "[Road] Type -> " << roadTypeLabel((int)seg.type) << "\n";
                } else {
                    std::cout << "[Road] Select a road first (tool 1)\n";
                }
                return;
            }
            if (key == GLFW_KEY_ENTER) {
                commitRoadDraft();
                return;
            }
            if (key == GLFW_KEY_ESCAPE) {
                cancelRoadDraft();
                selectedRoadPointIdx = -1;
                return;
            }
            if (key == GLFW_KEY_DELETE || key == GLFW_KEY_BACKSPACE) {
                RoadGraph* rg = terrain->getRoadGraph();
                if (rg && selectedRoadIdx >= 0 && selectedRoadIdx < (int)rg->segments.size()) {
                    if (selectedRoadPointIdx >= 0 &&
                        (int)rg->segments[selectedRoadIdx].controlPoints.size() > 2) {
                        rg->segments[selectedRoadIdx].controlPoints.erase(
                            rg->segments[selectedRoadIdx].controlPoints.begin() + selectedRoadPointIdx);
                        selectedRoadPointIdx = -1;
                        std::cout << "[Road] Removed control point\n";
                    } else {
                        rg->segments.erase(rg->segments.begin() + selectedRoadIdx);
                        selectedRoadIdx = -1;
                        selectedRoadPointIdx = -1;
                        std::cout << "[Road] Deleted selected road\n";
                    }
                    roadGraphDirty = true;
                }
                return;
            }
            // Straight-line (roadStraightMode) and Snap (roadSnapToRoads) are BUTTON-ONLY
            // (draw panel). The S/J key handlers were removed because S conflicts with the
            // player movement key (fall-through to the player switch below) and J is now
            // redundant. Shift+LMB still gives one-shot straight when held.
        }

        if (key == GLFW_KEY_B) {
            editorBrushMode = (editorBrushMode + 1) % 11;
            std::string modeNames[] = {
                "Select / Move", "Sculpt Raise", "Sculpt Lower", "Sculpt Smooth", "Sculpt Erode (Hydraulic)", "Sculpt Angle Slope",
                "Fit Terrain Under Roads", "Add Geometry + Smooth", "Paint Road (Gravel)", "Paint River (Wet Mud)", "Paint Erase"
            };
            std::cout << "[Brush] Mode changed to: " << modeNames[editorBrushMode] << "\n";
            return;
        }
        if (key == GLFW_KEY_N) {
            GameEditorUtils::adjustBrushParam(brushRadius, -5.0f,
                GameEditorUtils::BrushConfig::RADIUS_MIN, GameEditorUtils::BrushConfig::RADIUS_MAX, "Radius");
            return;
        }
        if (key == GLFW_KEY_M) {
            GameEditorUtils::adjustBrushParam(brushRadius, 5.0f,
                GameEditorUtils::BrushConfig::RADIUS_MIN, GameEditorUtils::BrushConfig::RADIUS_MAX, "Radius");
            return;
        }
        if (key == GLFW_KEY_G) {
            GameEditorUtils::adjustBrushParam(brushStrength, -0.5f,
                GameEditorUtils::BrushConfig::STRENGTH_MIN, GameEditorUtils::BrushConfig::STRENGTH_MAX, "Strength");
            return;
        }
        if (key == GLFW_KEY_H) {
            GameEditorUtils::adjustBrushParam(brushStrength, 0.5f,
                GameEditorUtils::BrushConfig::STRENGTH_MIN, GameEditorUtils::BrushConfig::STRENGTH_MAX, "Strength");
            return;
        }

        if (key == GLFW_KEY_L) {
            if (!SousseMapData::discoveredAssets.empty()) {
                selectedAssetIdx = (selectedAssetIdx + 1) % SousseMapData::discoveredAssets.size();
                std::cout << "[Editor] Cycled active model to: " << SousseMapData::discoveredAssets[selectedAssetIdx] << "\n";
            }
            return;
        }
        if (key == GLFW_KEY_ENTER || key == GLFW_KEY_P) {
            if (selectedAssetIdx >= 0 && selectedAssetIdx < (int)SousseMapData::discoveredAssets.size()) {
                glm::vec3 playerPos = glm::vec3(player->getPosition().x, player->getPosition().y, player->getPosition().z);
                glm::vec3 dir = player->getForwardDirection();
                glm::vec3 placementPos = playerPos + dir * 15.0f;
                placementPos.y = terrain->getHeight(placementPos.x, placementPos.z);
                
                SousseMapData::Landmark landmark;
                std::string path = SousseMapData::discoveredAssets[selectedAssetIdx];
                std::string fn = path.substr(path.find_last_of("/\\") + 1);
                landmark.name = fn.substr(0, fn.find_last_of('.')) + "_" + std::to_string(SousseMapData::liveLandmarks.size());
                landmark.localOffset = glm::vec2(placementPos.x, placementPos.z) - SousseMapData::WORLD_ANCHOR;
                landmark.yOffset = 0.0f;
                landmark.bakedY = placementPos.y;
                landmark.xScale = previewScale;
                landmark.yScale = previewScale;
                landmark.zScale = previewScale;
                landmark.angle = previewAngle;
                landmark.placed = true;
                landmark.customTexturePath = "";
                
                SousseMapData::liveLandmarks.push_back(landmark);
                selectedPlacedIdx = (int)SousseMapData::liveLandmarks.size() - 1;
                SousseMapData::layoutDirty = true;
                terrain->markBatchesDirty();
                std::cout << "[Editor] Placed model: " << landmark.name << " at " << placementPos.x << ", " << placementPos.z << "\n";
            }
            return;
        }
        if (key == GLFW_KEY_LEFT_BRACKET) {
            if (!SousseMapData::liveLandmarks.empty()) {
                int startIdx = selectedPlacedIdx;
                do {
                    selectedPlacedIdx = (selectedPlacedIdx - 1 + SousseMapData::liveLandmarks.size()) % SousseMapData::liveLandmarks.size();
                } while (selectedPlacedIdx >= 0 && selectedPlacedIdx < (int)SousseMapData::liveLandmarks.size() && !SousseMapData::liveLandmarks[selectedPlacedIdx].placed && selectedPlacedIdx != startIdx);
                std::cout << "[Editor] Selected placed landmark index: " << selectedPlacedIdx << "\n";
            }
            return;
        }
        if (key == GLFW_KEY_RIGHT_BRACKET) {
            if (!SousseMapData::liveLandmarks.empty()) {
                int startIdx = selectedPlacedIdx;
                do {
                    selectedPlacedIdx = (selectedPlacedIdx + 1) % SousseMapData::liveLandmarks.size();
                } while (selectedPlacedIdx >= 0 && selectedPlacedIdx < (int)SousseMapData::liveLandmarks.size() && !SousseMapData::liveLandmarks[selectedPlacedIdx].placed && selectedPlacedIdx != startIdx);
                std::cout << "[Editor] Selected placed landmark index: " << selectedPlacedIdx << "\n";
            }
            return;
        }
        if (key == GLFW_KEY_COMMA) {
            previewScale = std::max(0.1f, previewScale - 0.1f);
            return;
        }
        if (key == GLFW_KEY_PERIOD) {
            previewScale += 0.1f;
            return;
        }
        if (key == GLFW_KEY_R) {
            previewAngle = fmod(previewAngle + 15.0f, 360.0f);
            return;
        }

        // Selected object manipulations
        if (selectedPlacedIdx != -1 && selectedPlacedIdx < (int)SousseMapData::liveLandmarks.size()) {
            auto& landmark = SousseMapData::liveLandmarks[selectedPlacedIdx];
            if (key == GLFW_KEY_UP) {
                landmark.localOffset.y += 1.0f;
                landmark.bakedY = -1e9; // Force re-snapping Y height on next render
                SousseMapData::layoutDirty = true;
                terrain->markBatchesDirty();
                return;
            }
            if (key == GLFW_KEY_DOWN) {
                landmark.localOffset.y -= 1.0f;
                landmark.bakedY = -1e9;
                SousseMapData::layoutDirty = true;
                terrain->markBatchesDirty();
                return;
            }
            if (key == GLFW_KEY_LEFT) {
                landmark.localOffset.x -= 1.0f;
                landmark.bakedY = -1e9;
                SousseMapData::layoutDirty = true;
                terrain->markBatchesDirty();
                return;
            }
            if (key == GLFW_KEY_RIGHT) {
                landmark.localOffset.x += 1.0f;
                landmark.bakedY = -1e9;
                SousseMapData::layoutDirty = true;
                terrain->markBatchesDirty();
                return;
            }
            if (key == GLFW_KEY_PAGE_UP) {
                landmark.yOffset += 0.25f;
                SousseMapData::layoutDirty = true;
                terrain->markBatchesDirty();
                return;
            }
            if (key == GLFW_KEY_PAGE_DOWN) {
                landmark.yOffset -= 0.25f;
                SousseMapData::layoutDirty = true;
                terrain->markBatchesDirty();
                return;
            }
            if (key == GLFW_KEY_HOME) {
                landmark.angle = fmod(landmark.angle + 5.0f, 360.0f);
                SousseMapData::layoutDirty = true;
                terrain->markBatchesDirty();
                return;
            }
            if (key == GLFW_KEY_END) {
                landmark.angle = fmod(landmark.angle - 5.0f + 360.0f, 360.0f);
                SousseMapData::layoutDirty = true;
                terrain->markBatchesDirty();
                return;
            }
            if (key == GLFW_KEY_KP_ADD || key == GLFW_KEY_EQUAL) {
                landmark.xScale *= 1.1f;
                landmark.yScale *= 1.1f;
                landmark.zScale *= 1.1f;
                SousseMapData::layoutDirty = true;
                terrain->markBatchesDirty();
                return;
            }
            if (key == GLFW_KEY_KP_SUBTRACT || key == GLFW_KEY_MINUS) {
                landmark.xScale *= 0.9f;
                landmark.yScale *= 0.9f;
                landmark.zScale *= 0.9f;
                SousseMapData::layoutDirty = true;
                terrain->markBatchesDirty();
                return;
            }
            if (key == GLFW_KEY_BACKSPACE || key == GLFW_KEY_DELETE) {
                landmark.placed = false;
                std::cout << "[Editor] Deleted landmark: " << landmark.name << "\n";
                selectedPlacedIdx = -1;
                SousseMapData::layoutDirty = true;
                terrain->markBatchesDirty();
                return;
            }
            if (key == GLFW_KEY_C) {
                auto clone = landmark;
                glm::vec3 playerPos = glm::vec3(player->getPosition().x, player->getPosition().y, player->getPosition().z);
                glm::vec3 dir = player->getForwardDirection();
                glm::vec3 placementPos = playerPos + dir * 15.0f;
                placementPos.y = terrain->getHeight(placementPos.x, placementPos.z);
                
                clone.name = clone.name + "_copy";
                clone.localOffset = glm::vec2(placementPos.x, placementPos.z) - SousseMapData::WORLD_ANCHOR;
                clone.bakedY = placementPos.y;
                clone.placed = true;
                
                SousseMapData::liveLandmarks.push_back(clone);
                selectedPlacedIdx = (int)SousseMapData::liveLandmarks.size() - 1;
                SousseMapData::layoutDirty = true;
                terrain->markBatchesDirty();
                std::cout << "[Editor] Cloned landmark to: " << clone.name << "\n";
                return;
            }
            if (key == GLFW_KEY_K) {
                if (!SousseMapData::discoveredTextures.empty()) {
                    selectedTextureIdx = (selectedTextureIdx + 1) % SousseMapData::discoveredTextures.size();
                    landmark.customTexturePath = SousseMapData::discoveredTextures[selectedTextureIdx];
                    SousseMapData::layoutDirty = true;
                    terrain->markBatchesDirty();
                    std::cout << "[Editor] Cycled custom texture to: " << landmark.customTexturePath << "\n";
                }
                return;
            }
            if (key == GLFW_KEY_U) {
                landmark.customTexturePath = "";
                SousseMapData::layoutDirty = true;
                terrain->markBatchesDirty();
                std::cout << "[Editor] Reset landmark custom texture to default.\n";
                return;
            }
        }
    }

    switch (key) {
        case GLFW_KEY_W: player->keyDown('z'); break;
        case GLFW_KEY_S: player->keyDown('s'); break;
        case GLFW_KEY_A: player->keyDown('q'); break;
        case GLFW_KEY_D: player->keyDown('d'); break;
        case GLFW_KEY_E:
        {
            Vec3 playerPos = player->getPosition();
            float interactionDistanceSq = 5.0f * 5.0f;

            if (player->isDriving()) {
                player->keyDown('e');
            } else if (player->isRiding()) {
                player->keyDown('e');
            } else {
                // Try mounting horse
                if (horse) {
                    Vec3 horsePos = horse->getPosition();
                    float distSq = (playerPos.x - horsePos.x) * (playerPos.x - horsePos.x) + 
                                   (playerPos.z - horsePos.z) * (playerPos.z - horsePos.z);
                    if (distSq < interactionDistanceSq) {
                        horse->mountCharacter(player);
                        break;
                    }
                }
                // Try mounting car
                if (car) {
                    Vec3 carPos = car->getPosition();
                    float distSq = (playerPos.x - carPos.x) * (playerPos.x - carPos.x) + 
                                   (playerPos.z - carPos.z) * (playerPos.z - carPos.z);
                    if (distSq < interactionDistanceSq) {
                        car->mountCharacter(player);
                        player->startEnteringCar(car);
                    }
                }
            }
        }
        break;

    // ...

        case GLFW_KEY_ESCAPE:
        break;
        default:
            // Transmettre toutes les autres touches de mouvement au joueur
            // Utilisez 'z', 's', 'q', 'd', '<'
            if (key >= 65 && key <= 90) { // Si c'est une lettre majuscule
                 player->keyDown(tolower(key)); 
            } else {
                 player->keyDown(key); 
            }
            break;
    
    }
    
}

void Game::keyUp(int key) {
    switch (key) {
        case GLFW_KEY_W: player->keyUp('z'); break;
        case GLFW_KEY_S: player->keyUp('s'); break;
        case GLFW_KEY_A: player->keyUp('q'); break;
        case GLFW_KEY_D: player->keyUp('d'); break;
        case GLFW_KEY_E: player->keyUp('e'); break;
        default:
            if (key >= 65 && key <= 90) { // Capital letters
                player->keyUp(tolower(key));
            } else {
                player->keyUp(key);
            }
            break;
    }
}
