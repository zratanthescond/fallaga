#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <GL/glut.h>
#include "Character.h"
#include "Camera.h"
#include "ObjectModel.h"
#include <iostream>

Character::Character() {
    position = Vec3(0, 0, 0);
    model = new ObjModel("assets/character01/2nrtbod1out.obj");
}

Character::~Character() {
    delete model;
}

void Character::update(Camera* camera, float deltaTime, ObjModel* terrainModel) {
    Vec3 moveDir(0,0,0);

    if (keys['z']|| keys['Z']) moveDir += camera->getForward();
    if (keys['s']|| keys['S']) moveDir -= camera->getForward();
    if (keys['q']|| keys['Q']) moveDir -= camera->getRight();
    if (keys['d']|| keys['D']) moveDir += camera->getRight();

    if (moveDir.length() > 0.0f) {
        moveDir.normalize();
        position += moveDir * speed * deltaTime;
    }
    float terrainHeight = terrainModel->getHeightAt(position.x, position.z);
    std::cout << "Character position: (" << position.x << ", " << position.y << ", " << position.z << "), Terrain height: " << terrainHeight << std::endl;
    // Set the character's y position to be on top of the terrain
    // Add a small offset to prevent z-fighting with the ground
    position.y = terrainHeight + 0.1f; 
}

void Character::render() {
    glPushMatrix();
    glTranslatef(position.x, position.y, position.z);
    glScalef(0.01f, 0.01f, 0.01f);
glRotatef(-90.0f, 1.0f, 0.0f, 0.0f);
    if (model) {
        model->render();
    }
    glPopMatrix();
}

void Character::keyDown(unsigned char key) {
    keys[key] = true;

    if (key == 27) { // ESC to quit
        exit(0);
    }
}

void Character::keyUp(unsigned char key) {
    keys[key] = false;
}
