#include <GL/glew.h>
#include <GLFW/glfw3.h> // GLFW headers
#include <GL/glut.h>   // GLUT headers
#include "ObjectModel.h"
#include "Camera.h"
#include "Character.h"
#include "Terrain.h"
#include <cmath>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp> // Ensure this is included here as well
Camera::Camera(Character* target) 
    : target(target), 
      distance(2.0f), 
      angleAroundTarget(0.0f), 
      pitch(10.0f),
      lastX(400.0f), lastY(300.0f), firstMouse(true),
      position(target->getPosition()),
      smoothedPosition(target->getPosition()),
      smoothedTarget(target->getPosition()),
      terrainModel(nullptr) { // Initialize to null
    // Initial camera position is calculated in the constructor
   // calculateCameraPosition();  // DEFER this until update or when terrain is set
}

// REMOVED GLOBAL OBJECT MODEL
// ObjModel* terrainModel = new ObjModel("assets/terrain/sousseMap.obj");

Camera::~Camera() {
    // Do NOT delete terrainModel here, it's owned by Terrain class
}

void Camera::setTerrain(ObjModel* terrain) {
    terrainModel = terrain;
}
glm::mat4 Camera::getViewMatrix()
{
    // Use the smoothed camera position and target (from Vec3)
glm::vec3 cameraPos(smoothedPosition.x, smoothedPosition.y, smoothedPosition.z); 
 glm::vec3 cameraTarget(smoothedTarget.x, smoothedTarget.y, smoothedTarget.z); 
glm::vec3 worldUp(0.0f, 1.0f, 0.0f); // World Up is always Y
// The GLM function that calculates the View Matrix:
return glm::lookAt(cameraPos, cameraTarget, worldUp);
}
void Camera::update(float mouseDeltaX, float mouseDeltaY, float mouseScroll) {
    if (!target) return;

    // 1. Update angles and distance based on mouse input
    angleAroundTarget += mouseDeltaX * CAMERA_SENSITIVITY;
    pitch -= mouseDeltaY * CAMERA_SENSITIVITY;
    
    // Clamp pitch to prevent flipping
    if (pitch > CAMERA_PITCH_MAX) pitch = CAMERA_PITCH_MAX;
    if (pitch < CAMERA_PITCH_MIN) pitch = CAMERA_PITCH_MIN;

    // Handle camera zoom based on scroll wheel
    distance -= mouseScroll * CAMERA_ZOOM_SENSITIVITY;
    if (distance > CAMERA_MAX_DISTANCE) distance = CAMERA_MAX_DISTANCE;
    if (distance < CAMERA_MIN_DISTANCE) distance = CAMERA_MIN_DISTANCE;

    // 2. Calculate the new camera's goal position
    calculateCameraPosition();

    // 3. Smooth the camera's position for a cinematic feel
    updateSmoothing();
}

void Camera::calculateCameraPosition() {
    // The target is the character's position
    Vec3 targetPos = target->getPosition();

    // Convert angles to radians for trigonometric functions
    float radAngleAroundTarget = angleAroundTarget * 3.14159265f / 180.0f;
    float radPitch = pitch * 3.14159265f / 180.0f;

    // Calculate position based on a sphere around the target
    float horizontalDistance = distance * cos(radPitch);
    float verticalDistance = distance * sin(radPitch);

    float offsetX = horizontalDistance * sin(radAngleAroundTarget);
    float offsetZ = horizontalDistance * cos(radAngleAroundTarget);
    
    // This is the camera's goal position
    position.x = targetPos.x - offsetX;
    position.y = targetPos.y + verticalDistance;
    position.z = targetPos.z - offsetZ;
    float terrainY = 0.0f;
    if (terrainModel) {
        terrainY = terrainModel->getHeightAt(position.x, position.z);
    }
    
    // 2. Define a minimum height offset above the terrain (e.g., 0.5 units)
    float minCameraY = terrainY + CAMERA_PITCH_MIN;

    // 3. Clamp the camera's goal Y position to this minimum
    if (position.y < minCameraY) {
        position.y = minCameraY;
    }
}

void Camera::updateSmoothing() {
    // The core of the RDR2-like feel.
    // We smoothly interpolate the camera's position towards its goal.
    float smoothingFactor = 1.0f - exp(-CAMERA_SMOOTHING * 0.016f); // Using a time-based factor (assuming ~60fps)

    // Smoothly move the camera from its current position to the new goal position
    smoothedPosition.x += (position.x - smoothedPosition.x) * smoothingFactor;
    smoothedPosition.y += (position.y - smoothedPosition.y) * smoothingFactor;
    smoothedPosition.z += (position.z - smoothedPosition.z) * smoothingFactor;
    
    // The target itself can also be smoothed
    Vec3 targetPos = target->getPosition();
    smoothedTarget.x += (targetPos.x - smoothedTarget.x) * smoothingFactor;
    smoothedTarget.y += (targetPos.y - smoothedTarget.y) * smoothingFactor;
    smoothedTarget.z += (targetPos.z - smoothedTarget.z) * smoothingFactor;
}

// void Camera::apply() const {
//     glMatrixMode(GL_MODELVIEW);
//     glLoadIdentity();
    
//     // Use the smoothed position and target for the gluLookAt call
//     // This provides the damped, cinematic feel.
//     gluLookAt(
//         smoothedPosition.x, smoothedPosition.y, smoothedPosition.z,
//         smoothedTarget.x, smoothedTarget.y, smoothedTarget.z,
//         0.0f, 1.0f, 0.0f
//     );
// }

Vec3 Camera::getForward() const {
    Vec3 forward = smoothedTarget - smoothedPosition;
    forward.normalize();
    return forward;
}

Vec3 Camera::getRight() const {
    Vec3 forward = getForward();
    Vec3 up(0.0f, 1.0f, 0.0f);
    Vec3 right = forward.cross(up);
    right.normalize();
    return right;
}

Vec3 Camera::getPosition() const {
    return smoothedPosition;
}

// You would need to call the update() method in your main game loop
// and provide the mouse delta values. For example, in your main loop:
//
// double currentX, currentY;
// glfwGetCursorPos(window, &currentX, &currentY);
//
// // Calculate mouse movement
// float deltaX = currentX - lastX;
// float deltaY = currentY - lastY;
// lastX = currentX;
// lastY = currentY;
//
// // Call the camera update function
// myCamera->update(deltaX, deltaY, mouseScrollValue);
//
// // Call the apply function to set the view matrix
// myCamera->apply();