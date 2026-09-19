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
#include <sstream>
#include <iomanip>
#include "Shader.h"
#include "AudioSystem.h"
#include "Car.h"
#include "SousseMapData.h"
#include "TextureEngine.h"
#include "GameEditorUtils.h"
#include "GameplayUtils.h"

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

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
    double currentTime = glfwGetTime();
   
    deltaTime = currentTime - lastFrameTime;
    lastFrameTime = currentTime;
    totalTime += deltaTime;
   // std::cout << "Delta Time: " << deltaTime << " seconds\n";
    player->update(camera, static_cast<float>(deltaTime), terrain);
    camera->update(mouseDeltaX, mouseDeltaY, mouseScroll, static_cast<float>(deltaTime), editMode);
    horse->update(static_cast<float>(deltaTime), terrain);
    if (car) {
        car->update(static_cast<float>(deltaTime), terrain, static_cast<float>(totalTime), camera);
    }
    
    // Periodically update terrain (e.g. check for heightmap file changes)
    {
        auto camPos = camera->getPosition();
        terrain->update(glm::vec3(camPos.x, camPos.y, camPos.z));
    }

    // Flow physics for the sea/wadis, bounded to the connected ocean/river
    // mask so it can only redistribute water it already has - never spread
    // onto new land (see WaterSimulation::update).
    waterSim.update(static_cast<float>(deltaTime));
    if (terrain) {
        terrain->updateWaterMapTexture(waterSim.getTextureData());
    }

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
                float center = (float)windowWidth / 2.0f;
                if (leftMouseClicked) {
                    if (mouseX >= center - 300 && mouseX <= center - 190) {
                        activeEditorMode = 0;
                        editMode = false;
                        std::cout << "[Editor Mode] Switched to Game Mode\n";
                    }
                    else if (mouseX >= center - 180 && mouseX <= center - 70) {
                        activeEditorMode = 1;
                        editMode = true;
                        editorBrushMode = 0;
                        std::cout << "[Editor Mode] Switched to Actor Edit Mode\n";
                    }
                    else if (mouseX >= center - 60 && mouseX <= center + 50) {
                        activeEditorMode = 2;
                        editMode = true;
                        editorBrushMode = 1;
                        std::cout << "[Editor Mode] Switched to Terrain Sculpt Mode\n";
                    }
                    else if (mouseX >= center + 60 && mouseX <= center + 170) {
                        activeEditorMode = 3;
                        editMode = true;
                        editorBrushMode = 4;
                        std::cout << "[Editor Mode] Switched to Terrain Paint Mode\n";
                    }
                    else if (mouseX >= center + 180 && mouseX <= center + 290) {
                        activeEditorMode = 4;
                        editMode = true;
                        editorBrushMode = 0;
                        std::cout << "[Editor Mode] Switched to Water Level Mode\n";
                    }
                }
                clickedUI = true;
            }

            // Check Left Panel Clicks
            if (!clickedUI && mouseX >= lpX && mouseX <= lpX + lpW && mouseY >= lpY && mouseY <= lpY + lpH) {
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
                        for (int i = 0; i < 4; ++i) { // 4 tools: Raise, Lower, Smooth, Erode
                            float btnY = lpY + 45 + i * 25;
                            if (leftMouseClicked && mouseY >= btnY && mouseY <= btnY + 20) {
                                editorBrushMode = 1 + i; // 1 = Raise, 2 = Lower, 3 = Smooth, 4 = Erode
                                std::cout << "[Editor UI] Sculpt tool: " << editorBrushMode << "\n";
                            }
                        }

                        // Brush Settings (Radius & Strength) - Aligned with Y coordinates lpY + 180 and lpY + 212
                        GameEditorUtils::updateBrushSliders(mouseX, mouseY, lpX, lpY, lpW, brushRadius, brushStrength);

                        // Run Global Erosion Button Click (Y range lpY + 265 to lpY + 285)
                        if (leftMouseClicked && mouseY >= lpY + 265 && mouseY <= lpY + 285 && mouseX >= lpX + 15 && mouseX <= lpX + lpW - 15) {
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
                                editorBrushMode = 5 + i; // 5 = Paint Road, 6 = Paint River, 7 = Erase
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
                    float saveBtnY = lpH - 35 + lpY;
                    if (leftMouseClicked && mouseY >= saveBtnY && mouseY <= saveBtnY + 26) {
                        performSave();
                    }
                }
            }
            // Check Right Panel Clicks (Details)
            else if (selectedPlacedIdx != -1 && mouseX >= rpX && mouseX <= rpX + rpW && mouseY >= rpY && mouseY <= rpY + rpH) {
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
                    if (editorBrushMode == 0) { // Select / Move
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
                    else if (editorBrushMode >= 1 && editorBrushMode <= 4) { // Sculpt modes (1=Raise, 2=Lower, 3=Smooth, 4=Erode)
                        if (leftMouseDown && terrain->terrainManager) {
                            terrain->terrainManager->sculpt(lastRayHitPos.x, lastRayHitPos.z, brushRadius, brushStrength, editorBrushMode, static_cast<float>(deltaTime));
                            waterSim.updateTerrainArea(terrain->terrainManager, lastRayHitPos.x, lastRayHitPos.z, brushRadius);
                            terrain->updateWaterMapTexture(waterSim.getTextureData());
                        }
                    }
                    else if (editorBrushMode >= 5 && editorBrushMode <= 7) { // Paint modes (5=Paint Road, 6=Paint River, 7=Erase)
                        if (leftMouseDown && terrain->terrainManager) {
                            int paintIdx = (editorBrushMode == 5) ? 1 : ((editorBrushMode == 6) ? 2 : 0);
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
                    exit(1);
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
                    exit(1);
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
                    exit(1);
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
                    exit(1);
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
                    exit(0);
                } else if (now - stateTimer > 10.0) {
                    std::cerr << "[TEST] ERROR: Character failed to dismount from the horse!\n";
                    exit(1);
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
    terrain->renderOpaque(projection, view, cameraPosition, lightDir, lColor, selectedPlacedIdx, (activeEditorMode == 1), selectedAssetIdx, previewAngle, previewScale, false);
    
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
        drawStr(15, 20, "File      Edit      Window      Tools      Help", 0.7f, 0.7f, 0.7f);
        drawStr((float)windowWidth - 260, 20, "Unreal Editor (Sousse Engine 5.4)", 0.5f, 0.5f, 0.5f);
        drawRect((float)windowWidth - 45, 11, 10, 10, 0.1f, 0.8f, 0.1f, 1.0f); // Green status LED

        // Horizontally centered workspace mode switch tabs
        float center = (float)windowWidth / 2.0f;
        std::string modeNames[] = { "GAME", "OBJECTS", "SCULPT", "PAINT", "WATER" };
        float tabOffsets[] = { -300, -180, -60, 60, 180 };
        float tabWidth = 110;

        for (int i = 0; i < 5; ++i) {
            float tx = center + tabOffsets[i];
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
            std::string toolNames[] = { "Raise Terrain (LMB)", "Lower Terrain (LMB)", "Smooth Terrain (LMB)", "Erode Terrain (LMB)" };
            for (int i = 0; i < 4; ++i) {
                float btnY = lpY + 45 + i * 25;
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
            float sliderStartY = lpY + 155;
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

            // Global Actions Section
            float globalStartY = sliderStartY + 90;
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
                bool isCurrent = (editorBrushMode == 5 + i);
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
            drawStr(lpX + 80, resetY + 15, "Reset to -43.0m", 0.9f, 0.55f, 0.55f);

            // Push level to terrain shader every frame
            terrain->setWaterLevel(waterLevelAdjust);
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
}
void Game::performSave() {
    isSavingState = true;
    lastSaveTime = glfwGetTime();
    
    SousseMapData::saveLayout("assets/saves/city.txt");
    if (terrain) {
        terrain->saveTerrain();
    }
    std::cout << "[Editor] Saved level layout and terrain modifications!\n";
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
        if (key == GLFW_KEY_B) {
            editorBrushMode = (editorBrushMode + 1) % 8;
            std::string modeNames[] = {
                "Select / Move", "Sculpt Raise", "Sculpt Lower", "Sculpt Smooth", "Sculpt Erode (Hydraulic)",
                "Paint Road (Gravel)", "Paint River (Wet Mud)", "Paint Erase"
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
