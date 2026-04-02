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
#include "Shader.h"

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

Game::Game() : lastFrameTime(0.0), deltaTime(0.0) {
    player = new Character();
    terrain = new Terrain(); // Initialize terrain first
    
    // DEBUG: Print the map to console on startup
    std::cout << "\n=== TERRAIN ZONE MAP (Ascii) ===\n";
    terrain->printDebugMap();
    std::cout << "================================\n\n";

    camera = new Camera(player);
    horse= new Horse();
    sky = new Sky();
    lastFrameTime = glfwGetTime();
    totalTime = 0.0f;
}

Game::~Game() {
    delete player;
    delete camera;
    delete terrain;
    delete sky;
}
void Game::setup(int width, int height) {
    windowWidth = width;
    windowHeight = height;
    // ... other initial setup for Game
}

void Game::update( float mouseDeltaX, float mouseDeltaY, float mouseScroll) {
    double currentTime = glfwGetTime();
   
    deltaTime = currentTime - lastFrameTime;
    lastFrameTime = currentTime;
    totalTime += deltaTime;
   // std::cout << "Delta Time: " << deltaTime << " seconds\n";
    player->update(camera, static_cast<float>(deltaTime), terrain);
    camera->update( mouseDeltaX, mouseDeltaY, mouseScroll);
    horse->update(static_cast<float>(deltaTime), terrain);
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
        glm::radians(camera->distance), 
        aspectRatio, 
        0.5f, 1000.0f
    );
    glm::mat4 view = camera->getViewMatrix();

    // 3. Render Sky (behind everything)
    // 120 seconds for a full day cycle
    float sunAngle = totalTime * (2.0f * 3.14159f / 120.0f);
    glm::vec3 lightDir = glm::normalize(glm::vec3(cos(sunAngle), sin(sunAngle), 0.2f));
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
    if (sunTilt > 0.1f) {
        lColor = glm::mix(glm::vec3(1.0, 0.5, 0.2), glm::vec3(1.0, 1.0, 1.0), glm::smoothstep(0.1f, 0.4f, sunTilt));
    } else if (sunTilt > -0.1f) {
        lColor = glm::mix(glm::vec3(0.05, 0.1, 0.2), glm::vec3(1.0, 0.5, 0.2), glm::smoothstep(-0.1f, 0.1f, sunTilt));
    } else {
        lColor = glm::vec3(0.05, 0.1, 0.2); // Night light
    }

    GLfloat lightPosFixed[] = { lightDir.x, lightDir.y, lightDir.z, 0.0f }; // 0.0 for directional
    GLfloat lightColorFixed[] = { lColor.r, lColor.g, lColor.b, 1.0f };
    GLfloat ambientColorFixed[] = { lColor.r * 0.2f, lColor.g * 0.2f, lColor.b * 0.2f, 1.0f };

    glLightfv(GL_LIGHT0, GL_POSITION, lightPosFixed);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, lightColorFixed);
    glLightfv(GL_LIGHT0, GL_AMBIENT, ambientColorFixed);

    // 5. Render Terrain, Trees, and Rocks
    Shader& OceanShader = terrain->getShader();
    
    // 6. Character & Horse Shaders
    auto cameraPosCustom = camera->getPosition();
    glm::vec3 cameraPosition = glm::vec3(cameraPosCustom.x, cameraPosCustom.y, cameraPosCustom.z);
    
    // Update Character Shader
    Shader& characterShader = player->getShader();
    characterShader.use();
    characterShader.setVec3("viewPos", cameraPosition);
    characterShader.setVec3("lightPos", lightDir * 1000.0f);
    characterShader.setVec3("lightColor", lColor);
    
    // Update Horse Shader (It has its own instance)
    Shader& horseShader = horse->getShader(); 
    horseShader.use();
    horseShader.setVec3("viewPos", cameraPosition);
    horseShader.setVec3("lightPos", lightDir * 1000.0f);
    horseShader.setVec3("lightColor", lColor);

    // Update Ocean Shader
    OceanShader.use();
    OceanShader.setVec3("lightDir", lightDir);
    
    terrain->render(OceanShader, projection, view, cameraPosition, lightDir);
    horse->render(projection, view);
    player->render(projection, view);
    
    // 7. Cleanup
    glDisable(GL_LIGHTING);
    glUseProgram(0);
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
        case GLFW_KEY_E:
       {
            Vec3 playerPos = player->getPosition();
            Vec3 horsePos = horse->getPosition();
            float distSq = (playerPos.x - horsePos.x) * (playerPos.x - horsePos.x) + 
                           (playerPos.z - horsePos.z) * (playerPos.z - horsePos.z);
            float interactionDistanceSq = 5.0f * 5.0f;

            if (!player->isRiding() && horse && distSq < interactionDistanceSq) {
                // Monture : on monte directement, on n'a pas besoin d'enregistrer la touche
                horse->mountCharacter(player);
                // NE PAS appeler player->keyDown('e') ici.
                
            } else if (player->isRiding()) {
                // Démontage : on enregistre la touche pour que Character::update puisse le gérer
                player->keyDown('e'); 
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
    }
}
