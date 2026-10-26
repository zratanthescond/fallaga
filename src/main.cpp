// GLEW must be included before GLFW
#include <GL/glew.h>
#include <iostream>
#include <GLFW/glfw3.h>
#include <GL/glut.h>
#include <direct.h> // For _chdir

#include <sys/stat.h> // For struct stat
#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>
#include <stb_image_write.h>



#include "Game.h"
#include "Camera.h"
#include "Character.h"
#include "Car.h"
#include "Terrain.h"

// Seeds the standard 5-road test network (A..E, world coords) used by the
// harnesses that need a real road graph.
static void seedTestRoadNetwork(RoadGraph* g) {
    if (!g || !g->segments.empty()) return;
    RoadSegment a; a.id = 101; a.width = 12.0f; a.type = RoadType::UrbanStreet;
    a.controlPoints.push_back(RoadPoint{ glm::vec2(4190.0f, -505.0f) });
    a.controlPoints.push_back(RoadPoint{ glm::vec2(4280.0f, -540.0f) });
    a.controlPoints.push_back(RoadPoint{ glm::vec2(4370.0f, -600.0f) });

    RoadSegment b; b.id = 102; b.width = 7.0f; b.type = RoadType::CountryDirt;
    b.controlPoints.push_back(RoadPoint{ glm::vec2(4190.0f, -505.0f) }); // shared endpoint -> junction
    b.controlPoints.push_back(RoadPoint{ glm::vec2(4190.0f, -440.0f) });

    RoadSegment c; c.id = 103; c.width = 6.0f; c.type = RoadType::CountryDirt;
    c.controlPoints.push_back(RoadPoint{ glm::vec2(4480.0f, -700.0f) });
    c.controlPoints.push_back(RoadPoint{ glm::vec2(4540.0f, -720.0f) });

    RoadSegment e; e.id = 105; e.width = 8.0f; e.type = RoadType::ColonialAvenue;
    e.controlPoints.push_back(RoadPoint{ glm::vec2(4235.0f, -462.0f) });
    e.controlPoints.push_back(RoadPoint{ glm::vec2(4235.0f, -640.0f) }); // snapped onto 101 after evaluate

    RoadSegment d; d.id = 104; d.width = 10.0f; d.type = RoadType::UrbanStreet;
    d.controlPoints.push_back(RoadPoint{ glm::vec2(4500.0f, -500.0f) });
    d.controlPoints.push_back(RoadPoint{ glm::vec2(4590.0f, -470.0f) });
    d.controlPoints.push_back(RoadPoint{ glm::vec2(4650.0f, -550.0f) });
    d.controlPoints.push_back(RoadPoint{ glm::vec2(4600.0f, -630.0f) });
    d.controlPoints.push_back(RoadPoint{ glm::vec2(4510.0f, -600.0f) });
    d.controlPoints.push_back(RoadPoint{ glm::vec2(4503.0f, -503.0f) }); // revisit start -> closed loop

    g->segments.push_back(a);
    g->segments.push_back(b);
    g->segments.push_back(c);
    g->segments.push_back(e);
    g->segments.push_back(d);
}


// --- Function Prototypes ---
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset); 
void setup_opengl(int width, int height); // Updated to accept dimensions

// --- Globals (Initial values are placeholders for fullscreen setup) ---
int WINDOW_WIDTH = 800;  // Will be overwritten by monitor resolution
int WINDOW_HEIGHT = 600; // Will be overwritten by monitor resolution
Game* game; // Global game object

// Mouse state variables
double lastX = 400.0, lastY = 300.0;
bool firstMouse = true;
double scrollOffset = 0.0;

// Helper to change directory to project root
void fixWorkingDirectory() {
    struct stat info;
    std::string path = "assets";
    
    // Check up to 5 levels up
    for (int i = 0; i < 5; ++i) {
        if (stat(path.c_str(), &info) == 0 && (info.st_mode & S_IFDIR)) {
            std::cout << "Found assets directory! Working directory set." << std::endl;
            return;
        }
        std::cout << "Assets not found, moving up..." << std::endl;
        if (_chdir("..") != 0) {
            std::cerr << "Failed to change directory!" << std::endl;
            break;
        }
    }
    std::cerr << "CRITICAL: Could not find assets directory!" << std::endl;
}

int main() {
    fixWorkingDirectory();
    // 1. Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return -1;
    }
    // Target the current stable desktop OpenGL API. Keep the compatibility
    // profile until the remaining immediate-mode editor overlays are replaced.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24); 
    glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE); 

    // --- CRITICAL PERFORMANCE SCALE FOR FULL-SCREEN ---
    GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);

    // Cap full-screen resolution to 1920x1080 maximum to optimize fillrate on high-DPI (e.g. 4K) displays
    WINDOW_WIDTH = std::min(mode->width, 1920);
    WINDOW_HEIGHT = std::min(mode->height, 1080);
    // --- END FULL-SCREEN FIX ---

    // 2. Create a Window and OpenGL context
    // Pass the monitor handle to make it fullscreen
    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "RDR2 Prototype", primaryMonitor, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    // 3. Initialize GLEW
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW\n";
        return -1;
    }
    GLint glMajor = 0, glMinor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &glMajor);
    glGetIntegerv(GL_MINOR_VERSION, &glMinor);
    if (glMajor < 4 || (glMajor == 4 && glMinor < 6)) {
        std::cerr << "OpenGL 4.6 is required; driver created "
                  << glMajor << '.' << glMinor << ".\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }
    std::cout << "[OpenGL] vendor=" << reinterpret_cast<const char*>(glGetString(GL_VENDOR))
              << " renderer=" << reinterpret_cast<const char*>(glGetString(GL_RENDERER))
              << " version=" << reinterpret_cast<const char*>(glGetString(GL_VERSION))
              << " GLSL=" << reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION))
              << '\n';

    // --- Prefer low-latency presentation; allow users to restore VSync ---
    const char* vsyncEnv = std::getenv("FALLAGA_VSYNC");
    const int swapInterval = vsyncEnv ? (std::atoi(vsyncEnv) != 0 ? 1 : 0) : 0;
    glfwSwapInterval(swapInterval);
    std::cout << "[Display] " << WINDOW_WIDTH << "x" << WINDOW_HEIGHT
              << " @ " << mode->refreshRate << " Hz, vsync=" << swapInterval << '\n';

    // --- RENDER LOADING SCREEN ---
    // Clear screen to a "Loading" color (e.g., Dark Grey) or text if possible
    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glfwSwapBuffers(window);
    glfwPollEvents(); // Ensure window updates
    // ----------------------------

    // Initialize GLUT 
    int argc = 0;
    char** argv = nullptr;
    glutInit(&argc, argv);
    
    // Set callbacks
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback); 

    // Hide and capture cursor
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    
    // Create the game instance
    game = new Game();
    game->setup(WINDOW_WIDTH, WINDOW_HEIGHT);
    
    // Initial OpenGL state setup (pass final dimensions)
    setup_opengl(WINDOW_WIDTH, WINDOW_HEIGHT);
    
    // Main game loop
    while (!glfwWindowShouldClose(window)) {
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);

        if (firstMouse) {
            lastX = xpos;
            lastY = ypos;
            firstMouse = false;
        }

        float xoffset = 0.0f;
        float yoffset = 0.0f;

        if (game->editMode) {
            // Free mouse cursor unless holding Right Mouse Button (RMB) to look around
            if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                xoffset = static_cast<float>(xpos - lastX);
                yoffset = static_cast<float>(ypos - lastY);
            } else {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            }
        } else {
            // Always lock mouse cursor in play mode
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            xoffset = static_cast<float>(xpos - lastX);
            yoffset = static_cast<float>(ypos - lastY);
        }

        lastX = xpos;
        lastY = ypos;

        static bool posOverridden = false;
        if (!posOverridden) {
            const char* pposEnv = std::getenv("FALLAGA_PLAYER_POS");
            if (pposEnv) {
                float px = 0, py = 0, pz = 0;
                if (sscanf(pposEnv, "%f,%f,%f", &px, &py, &pz) == 3) {
                    game->player->setPosition(Vec3(px, py, pz));
                    game->camera->reset();
                }
            }
            const char* camAngleEnv = std::getenv("FALLAGA_CAM_ANGLE");
            if (camAngleEnv) {
                float ang = 0, pt = 10.0f, dst = -1.0f;
                int n = sscanf(camAngleEnv, "%f,%f,%f", &ang, &pt, &dst);
                if (n >= 1) {
                    game->camera->setAngles(ang, pt);
                    if (n >= 3 && dst > 0.0f) {
                        game->camera->distance = dst;
                        game->camera->reset();
                    }
                }
            }
            const char* carPosEnv = std::getenv("FALLAGA_CAR_POS");
            if (carPosEnv) {
                float cx = 0, cy = 0, cz = 0;
                if (sscanf(carPosEnv, "%f,%f,%f", &cx, &cy, &cz) == 3) {
                    game->car->setPosition(Vec3(cx, cy, cz));
                }
            }
            // Editor state, so panels and brushes can be inspected without clicking
            // the tabs. FALLAGA_EDITOR_MODE selects the tab (1=Objects 2=Sculpt
            // 3=Paint 4=Water 5=Roads) and FALLAGA_BRUSH the tool within it.
            if (const char* edEnv = std::getenv("FALLAGA_EDITOR_MODE")) {
                const int m = atoi(edEnv);
                if (m >= 0 && m < 6) {
                    game->activeEditorMode = m;
                    game->editMode = (m != 0);
                    game->editorBrushMode = (m == 2) ? 1 : (m == 3) ? 8 : 0;
                }
            }
            if (const char* brEnv = std::getenv("FALLAGA_BRUSH")) {
                const int b = atoi(brEnv);
                if (b >= 0 && b <= 10) game->editorBrushMode = b;
            }
            posOverridden = true;
        }

        static double lastSwapEnd = 0.0;
        double t_gap = (lastSwapEnd > 0.0) ? (glfwGetTime() - lastSwapEnd) * 1000.0 : 0.0;

        // --- Update and Render the game ---
        game->roadShiftHeld = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                               glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
        double t0 = glfwGetTime();
        game->update(xoffset, yoffset, static_cast<float>(scrollOffset));
        double t1 = glfwGetTime();
        game->render();
        double t2 = glfwGetTime();
        
        // Reset scroll offset for the next frame
        scrollOffset = 0.0;

        // DEV CAPTURE: FALLAGA_SHOT=<frame> grabs the framebuffer and quits.
        // Temporary scaffolding for the terrain-texturing pass; remove when done.
        // DEV CAPTURE & TEST HARNESS:
        {
            static const char* shotEnv = std::getenv("FALLAGA_SHOT");
            static long shotFrame = shotEnv ? atol(shotEnv) : -1;
            static const char* sceneFboAbEnv = std::getenv("FALLAGA_SCENEFBO_AB");
            static const char* carSeqEnv = std::getenv("FALLAGA_CAR_SEQUENCE");
            static const char* roadProjEnv = std::getenv("FALLAGA_ROAD_PROJECT");
            static long frameNo = 0;
            ++frameNo;

            if (roadProjEnv && frameNo == 1) {
                game->editMode = true;
                game->activeEditorMode = 5;
                game->roadToolMode = 3;
                game->roadPanelsVisible = true;
                const char* refIdxEnv = std::getenv("FALLAGA_REFIDX");
                if (refIdxEnv) game->roadRefIdx = atoi(refIdxEnv);
                game->camera->setAngles(120.0f, 40.0f);
                game->camera->distance = 500.0f;
                game->camera->reset();
            }

            auto saveCapture = [&](const char* outPath) {
                int fw = 0, fh = 0;
                glfwGetFramebufferSize(window, &fw, &fh);
                std::vector<unsigned char> px((size_t)fw * fh * 3);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadBuffer(GL_BACK);
                glReadPixels(0, 0, fw, fh, GL_RGB, GL_UNSIGNED_BYTE, px.data());
                std::vector<unsigned char> flip((size_t)fw * fh * 3);
                for (int y = 0; y < fh; ++y)
                    memcpy(&flip[(size_t)y * fw * 3], &px[(size_t)(fh - 1 - y) * fw * 3], (size_t)fw * 3);
                stbi_write_png(outPath, fw, fh, 3, flip.data(), fw * 3);
                std::cout << "[SHOT] wrote " << outPath << "\n";
            };

            // ── DRAW-ROAD HARNESS: preview → commit → captured frames ──
            static const char* rdrawEnv2 = std::getenv("FALLAGA_RDRAW");
            if (rdrawEnv2 && frameNo == 1) {
                    game->editMode = true;
                    game->activeEditorMode = 5;
                    game->roadToolMode = 1;
                    game->roadDraftPoints.clear();
                    game->roadPanelsVisible = false;
                    game->camera->setAngles(120.0f, 40.0f);
                    game->camera->distance = 500.0f;
                    game->camera->reset();
                    std::cout << "[RDRAW] draw tool armed\n";
            }
            if (rdrawEnv2 && frameNo == 2) {
                    for (int i = 0; i < 25; ++i) {
                        float x = 4190.0f + i * 15.0f, z = -505.0f - i * 3.0f;
                        float h = game->terrain->getHeight(x, z);
                        game->roadDraftPoints.push_back(glm::vec3(x, h, z));
                    }
                    std::cout << "[RDRAW] pushed 25 draft points (straight drag)\n";
            }
            if (rdrawEnv2 && frameNo == 4) saveCapture("scratch/draw_draft.png");
            if (rdrawEnv2 && frameNo == 5) { game->keyDown(GLFW_KEY_ENTER); std::cout << "[RDRAW] enter committed\n"; }
            if (rdrawEnv2 && frameNo == 30) saveCapture("scratch/draw_done.png");
            if (rdrawEnv2 && frameNo == 31) { game->keyDown(GLFW_KEY_F2); std::cout << "[RDRAW] manual roads JSON saved\n"; }
            if (rdrawEnv2 && frameNo == 35) { std::cout << "[RDRAW] quit\n"; glfwSetWindowShouldClose(window, GLFW_TRUE); }

            // ── DRAW PANEL RENDER VERIFY (status + OK COMMIT / X CANCEL buttons) ──
            static const char* drawPanelEnv = std::getenv("FALLAGA_DRAWPANEL");
            if (drawPanelEnv && frameNo == 1) {
                game->editMode = true;
                game->activeEditorMode = 5;
                game->roadToolMode = 1;
                game->roadPanelsVisible = true;
                game->camera->setAngles(120.0f, 40.0f);
                game->camera->distance = 500.0f;
                game->camera->reset();
            }
            if (drawPanelEnv && frameNo == 2) {
                const float pts[3][2] = { { 4190.0f, -505.0f }, { 4260.0f, -540.0f }, { 4330.0f, -585.0f } };
                for (int i = 0; i < 3; ++i) {
                    float h = game->terrain->getHeight(pts[i][0], pts[i][1]);
                    game->roadDraftPoints.push_back(glm::vec3(pts[i][0], h, pts[i][1]));
                }
            }
            if (drawPanelEnv && frameNo == 4) saveCapture("scratch/draw_panel.png");
            if (drawPanelEnv && frameNo == 5) { game->keyDown(GLFW_KEY_S); game->keyDown(GLFW_KEY_J); }   // straight ON, snap OFF
            if (drawPanelEnv && frameNo == 6) saveCapture("scratch/draw_panel2.png"); // toggled state
            if (drawPanelEnv && frameNo == 7) { game->keyDown(GLFW_KEY_2); }          // re-enter Draw tool -> must NOT wipe draft
            if (drawPanelEnv && frameNo == 8) { std::cout << "[DRAWPANEL] draft after re-key-2: " << game->roadDraftPoints.size() << " pts\n";
                                                glfwSetWindowShouldClose(window, GLFW_TRUE); }

            // ── PROJECTION SAVE/RESTORE HARNESS ──
            static const char* projSaveEnv = std::getenv("FALLAGA_PROJSAVE");
            if (projSaveEnv && frameNo == 1) {
                game->editMode = true;
                game->activeEditorMode = 5;
                game->roadToolMode = 3;
                game->roadPanelsVisible = true;
                game->projRefVisible = true;
                game->camera->setAngles(120.0f, 40.0f);
                game->camera->distance = 500.0f;
                game->camera->reset();
                std::cout << "[PROJSAVE] armed\n";
            }
            if (projSaveEnv && frameNo == 3) {
                game->projRefCenter = glm::vec2(4400.0f, -700.0f);
                game->projRefScale = 1.35f;
                game->projRefFlipX = true;
                game->projRefOpacity = 0.6f;
                game->projRefYOffset = 3.0f;
                game->roadRefIdx = 1;
                game->saveProjRefMap();
            }
            if (projSaveEnv && frameNo == 4) saveCapture("scratch/projsave_flash.png"); // flash still active

            // ── SELECT/EDIT PANEL VERIFY (width +/- and delete buttons on selected road) ──
            static const char* panel0Env = std::getenv("FALLAGA_PANEL0");
            if (panel0Env && frameNo == 1) {
                game->editMode = true;
                game->activeEditorMode = 5;
                game->roadToolMode = 0;
                game->roadPanelsVisible = true;
                game->camera->setAngles(120.0f, 40.0f);
                game->camera->distance = 500.0f;
                game->camera->reset();
            }
            if (panel0Env && frameNo == 2) {
                RoadGraph* g = game->terrain->getRoadGraph();
                if (g && g->segments.empty()) {
                    RoadSegment s;
                    s.id = 999;
                    s.width = 12.0f;
                    s.type = RoadType::UrbanStreet;
                    s.controlPoints.push_back(RoadPoint{ glm::vec2(4190.0f, -505.0f) });
                    s.controlPoints.push_back(RoadPoint{ glm::vec2(4260.0f, -540.0f) });
                    s.controlPoints.push_back(RoadPoint{ glm::vec2(4330.0f, -585.0f) });
                    g->segments.push_back(s);
                }
                game->selectedRoadIdx = 0;
                game->selectedRoadPointIdx = 1;
                std::cout << "[PANEL0] selected road 0, point 1\n";
            }
            if (panel0Env && frameNo == 4) saveCapture("scratch/panel0.png");
            if (panel0Env && frameNo == 5) glfwSetWindowShouldClose(window, GLFW_TRUE);

            // ── ROAD NETWORK MESH VERIFY (junction patch + closed ring + end caps) ──
            static const char* meshNetEnv = std::getenv("FALLAGA_MESHNET");
            if (meshNetEnv && frameNo == 1) {
                game->editMode = true;
                game->activeEditorMode = 5;
                game->roadToolMode = 0;
                game->roadPanelsVisible = true;
                game->player->setPosition(Vec3(4350.0f, -46.5f, -560.0f));
                game->camera->setAngles(0.0f, 62.0f);
                game->camera->distance = 420.0f;
                game->camera->reset();
            }
            if (meshNetEnv && frameNo == 2 && game->terrain) {
                RoadGraph* g = game->terrain->getRoadGraph();
                if (g && g->segments.empty()) {
                    seedTestRoadNetwork(g);
                    game->projRefVisible = false;
                    game->terrain->rebuildRoadMesh();
                    // Snap branch E's back endpoint onto road 101's evaluated spline
                    // (mid-edge) so the T-junction is a real connection test.
                    {
                        const RoadSegment& ar = g->segments[0];
                        const int k = (int)ar.splinePoints.size() / 2;
                        RoadSegment& er = g->segments[3];
                        er.controlPoints[1].pos = glm::vec2(ar.splinePoints[k].x, ar.splinePoints[k].z);
                        std::cout << "[MESHNET] T snap onto road101 spline idx " << k
                                  << " -> (" << ar.splinePoints[k].x << "," << ar.splinePoints[k].z << ")\n";
                    }
                    game->terrain->rebuildRoadMesh();
                    std::cout << "[MESHNET] rebuilt with " << g->segments.size()
                              << " segments, tris=" << game->terrain->getRoadMeshBuilder()->getTriangleCount() << "\n";
                }
            }
            if (meshNetEnv && frameNo == 4) {
                const char* dv = std::getenv("FALLAGA_DEBUGVIEW");
                saveCapture(dv ? "scratch/meshnet_dbg.png" : "scratch/meshnet.png");
            }
            if (meshNetEnv && frameNo == 5) glfwSetWindowShouldClose(window, GLFW_TRUE);

            // ── SAVE/LOAD ROUND-TRIP HARNESS (prove drawn roads persist) ──
            static const char* roundEnv = std::getenv("FALLAGA_ROADROUND");
            if (roundEnv && frameNo == 2 && game->terrain) {
                RoadGraph* g = game->terrain->getRoadGraph();
                if (g && g->segments.empty()) {
                    seedTestRoadNetwork(g);
                    game->terrain->rebuildRoadMesh();
                    game->terrain->saveRoadsToJSON("assets/roads/roads_manual.json");
                    std::cout << "[ROUND] seeded 5 roads and saved to roads_manual.json\n";
                }
            }
            if (roundEnv && frameNo == 3) glfwSetWindowShouldClose(window, GLFW_TRUE);

            // ── AUTO-SAVE PERSISTENCE HARNESS (no button: move/scale then quit, verify file) ──
            static const char* autoSaveEnv = std::getenv("FALLAGA_AUTOSAVE");
            if (autoSaveEnv && frameNo == 1) {
                game->editMode = true;
                game->activeEditorMode = 5;
                game->roadToolMode = 3;
                game->roadPanelsVisible = true;
                game->projRefVisible = true;
                game->camera->setAngles(120.0f, 40.0f);
                game->camera->distance = 500.0f;
                game->camera->reset();
            }
            if (autoSaveEnv && frameNo == 3) {
                game->projRefCenter = glm::vec2(4530.0f, -620.0f);
                game->projRefScale = 1.6f;
                game->projRefOpacity = 0.55f;
                game->projRefYOffset = -4.0f;
                game->roadRefIdx = 2;
                std::cout << "[AUTOSAVE] placement changed without pressing any button\n";
            }
            if (autoSaveEnv && frameNo == 90) {
                saveCapture("scratch/autosave_done.png");
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }

            if (carSeqEnv && (atoi(carSeqEnv) == 1 || atoi(carSeqEnv) == 2)) {
                // Keep the entry camera setup for the animation, then let it
                // follow the moving car during sequence 2's controlled drive.
                if (frameNo == 1) {
                    game->camera->setAngles(110.0f, 16.0f);
                    game->camera->distance = 5.5f;
                    game->camera->reset();
                }

                static int enterFrame = -1;
                static int driveStartFrame = -1;
                static int exitStartFrame = -1;

                if (frameNo == 5) {
                    glm::vec3 outsidePos = game->car->getDriverOutsideWorldPosition();
                    game->player->setPosition(Vec3(outsidePos.x, outsidePos.y, outsidePos.z));
                } else if (frameNo == 10) {
                    game->keyDown(GLFW_KEY_E);
                    enterFrame = frameNo;
                }

                if (enterFrame > 0 && frameNo == enterFrame + 25) {
                    saveCapture("scratch/car_seq_1_entering_door.png");
                }
                if (enterFrame > 0 && frameNo == enterFrame + 50) {
                    saveCapture("scratch/car_seq_2_entering_step_in.png");
                }

                if (game->player->isDriving() && driveStartFrame < 0) {
                    driveStartFrame = frameNo;
                    std::cout << "[TEST] Reached DrivingCar at frame " << frameNo << "\n";
                    if (atoi(carSeqEnv) == 2) {
                        game->player->keys['w'] = true;
                        std::cout << "[TEST] Starting controlled 3-second drive\n";
                    }
                }

                if (driveStartFrame > 0 && frameNo == driveStartFrame + 10) {
                    saveCapture("scratch/car_seq_3_seated_driving.png");
                }
                if (atoi(carSeqEnv) == 1 && driveStartFrame > 0 &&
                    frameNo == driveStartFrame + 15) {
                    std::cout << "[TEST] Triggering dismount at frame " << frameNo << "\n";
                    game->keyDown(GLFW_KEY_E);
                    exitStartFrame = frameNo;
                }

                if (atoi(carSeqEnv) == 2 && driveStartFrame > 0 &&
                    frameNo == driveStartFrame + 180) {
                    game->player->keys['w'] = false;
                    std::cout << "[TEST] Controlled drive finished at frame "
                              << frameNo << "\n";
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
                }

                if (atoi(carSeqEnv) == 2 && driveStartFrame < 0 && frameNo > 360) {
                    std::cout << "[TEST] Timed out before controlled drive began\n";
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
                }

                if (exitStartFrame > 0 && frameNo == exitStartFrame + 25) {
                    saveCapture("scratch/car_seq_4_exiting_step_out.png");
                }

                if (exitStartFrame > 0 && !game->player->isDrivingOrTransitioning()) {
                    saveCapture("scratch/car_seq_5_exited_idle.png");
                    std::cout << "[TEST] Dismount completed, player in Idle at frame " << frameNo << "\n";
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
                }
            } else if (sceneFboAbEnv && shotFrame > 0 && frameNo == shotFrame) {
                // In-process A/B for the scene target: separate runs are not pixel-stable,
                // so the frame just rendered (through the target) is captured, then the
                // target is disabled and the same frame is re-rendered straight to the
                // screen. The two must match if the composite is truly identity.
                const char* sideA = std::getenv("FALLAGA_AB_A");
                const char* sideB = std::getenv("FALLAGA_AB_B");
                saveCapture(sideA ? sideA : "scratch/ab_fbo.png");
                // Same frame with SSAO disabled: the only pixel-stable measure of what the
                // AO pass actually changed (separate runs are not comparable).
                const bool prevSsao = game->ssaoEnabled;
                game->ssaoEnabled = false;
                game->render();
                saveCapture("scratch/ab_nossao.png");
                game->ssaoEnabled = prevSsao;
                game->sceneFboEnabled = false;
                game->render();
                saveCapture(sideB ? sideB : "scratch/ab_direct.png");
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            } else if (shotFrame > 0 && frameNo == shotFrame) {
                const char* out = std::getenv("FALLAGA_SHOT_PATH");
                saveCapture(out ? out : "scratch/frame.png");
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
        }

        static double accUpdate = 0.0, accRender = 0.0, accSwap = 0.0, accGap = 0.0;
        accUpdate += (t1 - t0) * 1000.0;
        accRender += (t2 - t1) * 1000.0;
        accGap += t_gap;

        // Swap buffers and poll for events
        double t_swap_start = glfwGetTime();
        glfwSwapBuffers(window);
        double t_swap_end = glfwGetTime();
        accSwap += (t_swap_end - t_swap_start) * 1000.0;
        lastSwapEnd = t_swap_end;

        // Update FPS in window title
        {
            static double lastFpsTime = glfwGetTime();
            static int fpsFrameCount = 0;
            fpsFrameCount++;
            double currentFpsTime = glfwGetTime();
            if (currentFpsTime - lastFpsTime >= 0.5) {
                double fps = fpsFrameCount / (currentFpsTime - lastFpsTime);
                double ms = (currentFpsTime - lastFpsTime) * 1000.0 / fpsFrameCount;
                double avgUp = accUpdate / fpsFrameCount;
                double avgRen = accRender / fpsFrameCount;
                double avgSwp = accSwap / fpsFrameCount;
                double avgGap = accGap / fpsFrameCount;
                char title[160];
                snprintf(title, sizeof(title), "RDR2 Prototype - %.1f FPS (%.2f ms) [Up:%.1f, Ren:%.1f, Swp:%.1f, Gap:%.1f]", fps, ms, avgUp, avgRen, avgSwp, avgGap);
                glfwSetWindowTitle(window, title);
                static int fpsPrintCount = 0;
                if (++fpsPrintCount % 2 == 0) {
                    std::cout << "[PERF] " << title << "\n";
                }
                fpsFrameCount = 0;
                accUpdate = 0.0; accRender = 0.0; accSwap = 0.0; accGap = 0.0;
                lastFpsTime = currentFpsTime;
            }
        }

        glfwPollEvents();

        // The FALLAGA_TEST_MODE harness requests shutdown through this flag
        // instead of calling exit() mid-frame, so the Game destructor (and any
        // GL/audio teardown) still runs.
        if (game->isQuitRequested()) break;
    }

    // 6. Cleanup
    const int exitCode = game->getRequestedExitCode();
    delete game;
    glfwDestroyWindow(window);
    glfwTerminate();
    return exitCode;
}

// Initial OpenGL state setup
void setup_opengl(int width, int height) {
    glClearColor(0.5f, 0.7f, 1.0f, 1.0f); 
    glEnable(GL_DEPTH_TEST);
    // The engine renders most content two-sided (terrain chunk skirts, the water
    // plane, the sky dome) and its geometry is not authored with a consistent
    // winding order, so culling is deliberately off. It used to be disabled ad hoc
    // inside render passes with no matching re-enable, which left the state
    // implicit and order-dependent; declaring it once here makes it explicit.
    glDisable(GL_CULL_FACE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    
    // Ensure smooth shading is active
    glShadeModel(GL_SMOOTH); 
    
    // Define stronger light colors
    GLfloat light_ambient[] = { 0.5f, 0.5f, 0.5f, 1.0f }; 
    GLfloat light_diffuse[] = { 1.0f, 1.0f, 1.0f, 1.0f }; 
    GLfloat light_specular[] = { 0.1f, 0.1f, 0.1f, 1.0f };
    
    glLightfv(GL_LIGHT0, GL_AMBIENT, light_ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, light_diffuse);
    glLightfv(GL_LIGHT0, GL_SPECULAR, light_specular);
    
    // CRITICAL: Ensure the light is positioned correctly
    GLfloat light_position[] = { 100.0f, 500.0f, 100.0f, 0.0f }; 
    glLightfv(GL_LIGHT0, GL_POSITION, light_position);
    
    // Call the callback once with the full screen dimensions
    framebuffer_size_callback(nullptr, width, height); 
}

// Called when the window is resized (now also called at startup with full dimensions)
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);

    // The HUD layout, UI hit-testing and raycast normalisation all read the size from
    // Game, which otherwise only learned it once in Game::setup(). This callback also
    // runs once from setup_opengl() — before/around Game's construction — so `game`
    // must be checked for null before it is dereferenced.
    if (game) game->setWindowSize(width, height);

    // Note: The legacy glMatrixMode/gluPerspective calls remain commented out
    // as per the previous fix, delegating matrix creation to Game::render().
}

// Called when a keyboard key is pressed/released
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_ESCAPE) {
            // Allow ESC to close the full-screen window
            glfwSetWindowShouldClose(window, GLFW_TRUE); 
        } else {
            game->keyDown(key);
        }
    } else if (action == GLFW_RELEASE) {
        game->keyUp(key);
    }
}

// Mouse position is handled in the main loop
void mouse_callback(GLFWwindow* window, double xpos, double ypos) {}

// Scroll input is captured here
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    scrollOffset = yoffset;
}
