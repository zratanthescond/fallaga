Created At: 2026-05-26T17:51:58Z
Completed At: 2026-05-26T17:51:58Z
File Path: `file:///c:/Users/honco/OneDrive/Bureau/fallaga/src/Game.cpp`
Total Lines: 653
Total Bytes: 29879
Showing lines 1 to 653
The following code has been modified to include a line number before every line, in the format: <line_number>: <original_line>. Please note that any changes targeting the original code should remove the line number, colon, and leading space.
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <GL/glut.h>
#include "Game.h"
#include "Character.h"
#include "Weapon.h"
#include "Camera.h"
#include "Terrain.h"
#include "Horse.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include "Shader.h"
#include "CollisionSystem.h"
#include "AudioSystem.h"
#include <fstream>

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

Game::Game() : lastFrameTime(0.0), deltaTime(0.0), totalTime(0.0) {
    AudioSystem::getInstance().init();
    player = new Character();
    terrain = new Terrain();
    camera = new Camera(player);
    particleSystem = new ParticleSystem();
    horse = new Horse();
    sky = new Sky();
    
    // Spawn player's primary car
    car = new Car();
    float gh = terrain->getHeight(10.0f, 10.0f);
    car->setPosition(Vec3(10.0f, gh + 1.0f, 10.0f));
    
    // Spawn traffic (Cars)
    // Some roaming, some parked
    npcCars.push_back(new Car(Car::State::ROAMING));
    npcCars.back()->setPosition(Vec3(-435, 12, 1315));
    
    npcCars.push_back(new Car(Car::State::ROAMING));
    npcCars.back()->setPosition(Vec3(-450, 12, 1330));
    npcCars.back()->setState(Car::State::ROAMING); // redundant but clear

    npcCars.push_back(new Car(Car::State::PARKED));
    npcCars.back()->setPosition(Vec3(-400, 12, 1280));

    npcC
<truncated 29465 bytes>
); break;
        case GLFW_KEY_A: player->keyUp('q'); break;
        case GLFW_KEY_D: player->keyUp('d'); break;
    }
}

void Game::mouseButton(int button, int action, double x, double y) {
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        isRightMouseDown = (action == GLFW_PRESS);
    }
}

void Game::setupFBO(int width, int height) {
    if (sceneFBO) {
        glDeleteFramebuffers(1, &sceneFBO);
        glDeleteTextures(1, &sceneColorTex);
        glDeleteTextures(1, &sceneDepthTex);
    }

    glGenFramebuffers(1, &sceneFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);

    // Color texture
    glGenTextures(1, &sceneColorTex);
    glBindTexture(GL_TEXTURE_2D, sceneColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneColorTex, 0);

    // Depth texture
    glGenTextures(1, &sceneDepthTex);
    glBindTexture(GL_TEXTURE_2D, sceneDepthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, sceneDepthTex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << std::endl;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}


The above content shows the entire, complete file contents of the requested file.
