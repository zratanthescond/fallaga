// GLEW must be included before GLFW
#include <GL/glew.h>
#include <iostream>
#include <GLFW/glfw3.h>
#include <GL/glut.h>
#include <direct.h> // For _chdir

#include <sys/stat.h> // For struct stat
#include <vector>
#include <string>



#include "Game.h"
#include "Camera.h"
#include "Character.h"
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

    // --- CRITICAL FIX FOR FULL-SCREEN ---
    GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);

    // Set global dimensions to monitor resolution
    WINDOW_WIDTH = mode->width;
    WINDOW_HEIGHT = mode->height;
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
        // --- Calculate mouse deltas for camera update ---
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);

        if (firstMouse) {
            lastX = xpos;
            lastY = ypos;
            firstMouse = false;
        }

        float xoffset = static_cast<float>(xpos - lastX);
        float yoffset = static_cast<float>(ypos - lastY);
        lastX = xpos;
        lastY = ypos;

        // --- Update and Render the game ---
        game->update(xoffset, yoffset, static_cast<float>(scrollOffset));
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        game->render();
        
        // Reset scroll offset for the next frame
        scrollOffset = 0.0;

        // Swap buffers and poll for events
        glfwSwapBuffers(window);
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