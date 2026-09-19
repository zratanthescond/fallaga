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
     glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
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

    // --- ENABLE VSYNC ---
    glfwSwapInterval(1); 

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
            posOverridden = true;
        }

        static double lastSwapEnd = 0.0;
        double t_gap = (lastSwapEnd > 0.0) ? (glfwGetTime() - lastSwapEnd) * 1000.0 : 0.0;

        // --- Update and Render the game ---
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
            static const char* carSeqEnv = std::getenv("FALLAGA_CAR_SEQUENCE");
            static long frameNo = 0;
            ++frameNo;

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

            if (carSeqEnv && atoi(carSeqEnv) == 1) {
                // Ensure camera is locked at a cinematic angle looking directly at the driver side
                game->camera->setAngles(110.0f, 16.0f);
                game->camera->distance = 5.5f;
                game->camera->reset();

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
                }

                if (driveStartFrame > 0 && frameNo == driveStartFrame + 10) {
                    saveCapture("scratch/car_seq_3_seated_driving.png");
                }
                if (driveStartFrame > 0 && frameNo == driveStartFrame + 15) {
                    std::cout << "[TEST] Triggering dismount at frame " << frameNo << "\n";
                    game->keyDown(GLFW_KEY_E);
                    exitStartFrame = frameNo;
                }

                if (exitStartFrame > 0 && frameNo == exitStartFrame + 25) {
                    saveCapture("scratch/car_seq_4_exiting_step_out.png");
                }

                if (exitStartFrame > 0 && !game->player->isDrivingOrTransitioning()) {
                    saveCapture("scratch/car_seq_5_exited_idle.png");
                    std::cout << "[TEST] Dismount completed, player in Idle at frame " << frameNo << "\n";
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
                }
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
    }

    // 6. Cleanup
    delete game;
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

// Initial OpenGL state setup
void setup_opengl(int width, int height) {
    glClearColor(0.5f, 0.7f, 1.0f, 1.0f); 
    glEnable(GL_DEPTH_TEST);
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