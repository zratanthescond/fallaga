#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <GL/glut.h>
#include "Game.h"
#include "Character.h"
#include "Camera.h"
#include "Terrain.h"

Game::Game() : lastFrameTime(0.0), deltaTime(0.0f) {
    player = new Character();
    camera = new Camera(player);
    terrain = new Terrain();
    
    lastFrameTime = glfwGetTime();
}

Game::~Game() {
    delete player;
    delete camera;
    delete terrain;
}

void Game::update() {
    double currentTime = glfwGetTime();
    deltaTime = static_cast<float>(currentTime - lastFrameTime);
    lastFrameTime = currentTime;
    
    player->update(camera, deltaTime, terrain->getModel());
    camera->update();
}

void Game::render() {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    
    // Clear the screen and depth buffer
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Set up the ModelView matrix for rendering
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Apply camera transformation
    camera->apply();
    
    // Render terrain first (largest object)
    terrain->render();
    
    // Render player last
    player->render();
}

Camera& Game::getCamera() {
    return *camera;
}

void Game::keyDown(int key) {
    switch (key) {
        case GLFW_KEY_W: player->keyDown('z'); break;
        case GLFW_KEY_S: player->keyDown('s'); break;
        case GLFW_KEY_A: player->keyDown('q'); break;
        case GLFW_KEY_D: player->keyDown('d'); break;
        case GLFW_KEY_ESCAPE: exit(0);
    }
}

void Game::keyUp(int key) {
    switch (key) {
        case GLFW_KEY_W: player->keyUp('z'); break;
        case GLFW_KEY_S: player->keyUp('s'); break;
        case GLFW_KEY_A: player->keyUp('q'); break;
        case GLFW_KEY_D: player->keyUp('d'); break;
    }
}
