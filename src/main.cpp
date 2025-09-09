// GLEW must be included before GLFW
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <GL/glut.h>
#include <iostream>

#include "Game.h"
#include "Camera.h"
#include "Character.h"

// --- Function Prototypes ---
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
void setup_opengl();

// --- Globals ---
const int WINDOW_WIDTH = 800;
const int WINDOW_HEIGHT = 600;
Game* game; // Global game object

int main() {
    // 1. Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return -1;
    }

    // 2. Create a Window and OpenGL context
    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "RDR2 Prototype", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    // 3. Initialize GLEW (must be done after creating a context)
    glewExperimental = GL_TRUE; // Enable modern OpenGL features
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW\n";
        return -1;
    }

    int argc = 0;
    char** argv = nullptr;
    glutInit(&argc, argv);
    
    // Set callbacks
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, mouse_callback);

    // Hide and capture cursor
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    
    // Create the game instance
    game = new Game();
    
    // Initial OpenGL state setup
    setup_opengl();
    
    // Get initial mouse position
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    game->getCamera().handleMouse(xpos, ypos);

    // The Game Loop
    while (!glfwWindowShouldClose(window)) {
        // Update and Render the game
        game->update();
        game->render();
        
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
void setup_opengl() {
    glClearColor(0.5f, 0.7f, 1.0f, 1.0f); // Light blue background
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    GLfloat light_position[] = { 1.0f, 1.0f, 1.0f, 0.0f };
    glLightfv(GL_LIGHT0, GL_POSITION, light_position);

    // Set projection matrix once at the start
    framebuffer_size_callback(nullptr, WINDOW_WIDTH, WINDOW_HEIGHT);
}

// Called when the window is resized
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    // Using legacy gluPerspective for projection
    gluPerspective(45.0, (double)width / (double)height, 0.1, 1000.0);
    glMatrixMode(GL_MODELVIEW);
}

// Called when a keyboard key is pressed/released
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS) {
        game->keyDown(key);
    } else if (action == GLFW_RELEASE) {
        game->keyUp(key);
    }
}

// Called when the mouse moves
void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    game->getCamera().handleMouse(xpos, ypos);
}
