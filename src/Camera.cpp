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

void Camera::setTerrain(Terrain* terrain) {
    terrainModel = terrain;
}
glm::mat4 Camera::getViewMatrix()
{
    // Use the smoothed camera position and target (from Vec3)
    glm::vec3 cameraPos(smoothedPosition.x, smoothedPosition.y, smoothedPosition.z); 
    glm::vec3 cameraTarget(smoothedTarget.x, smoothedTarget.y, smoothedTarget.z); 
    glm::vec3 worldUp(0.0f, 1.0f, 0.0f); // World Up is always Y
    return glm::lookAt(cameraPos, cameraTarget, worldUp);
}
void Camera::update(float mouseDeltaX, float mouseDeltaY, float mouseScroll, float deltaTime, bool isEditMode) {
    if (!target) return;

    // 1. Update angles and distance based on mouse input
    angleAroundTarget += mouseDeltaX * CAMERA_SENSITIVITY;
    pitch -= mouseDeltaY * CAMERA_SENSITIVITY;
    
    // Clamp pitch to prevent flipping
    if (pitch > CAMERA_PITCH_MAX) pitch = CAMERA_PITCH_MAX;
    if (pitch < CAMERA_PITCH_MIN) pitch = CAMERA_PITCH_MIN;

    // Handle camera zoom limits dynamically (closer when aiming)
    float minZoom = CAMERA_MIN_DISTANCE;
    float maxZoom = CAMERA_MAX_DISTANCE;
    if (isEditMode) {
        minZoom = 2.0f;
        maxZoom = 400.0f; // Massive zoom out in editor
    } else if (target->isAiming()) {
        minZoom = 1.5f;
        maxZoom = 10.0f;
        // Smoothly zoom in when aiming
        distance = glm::mix(distance, 2.5f, 4.0f * deltaTime);
    } else if (target->isRiding() || target->isDrivingOrTransitioning()) {
        minZoom = 6.0f;
        maxZoom = 30.0f;
    } else {
        minZoom = 4.0f;
        maxZoom = (distance > 20.0f) ? CAMERA_MAX_DISTANCE : 20.0f;
    }

    distance -= mouseScroll * CAMERA_ZOOM_SENSITIVITY * (isEditMode ? 4.0f : 1.0f); // Faster zoom in editor
    if (distance > maxZoom) distance = maxZoom;
    if (distance < minZoom) distance = minZoom;

    // 2. Auto-Alignment (RDR2 Camera feel) - Disabled in Editor Mode
    if (mouseDeltaX != 0.0f || mouseDeltaY != 0.0f) {
        timeSinceLastMouseMovement = 0.0f;
    } else {
        timeSinceLastMouseMovement += deltaTime;
    }

    Vec3 vel = target->getVelocity();
    float horizontalSpeed = sqrt(vel.x * vel.x + vel.z * vel.z);
    
    // Only align if moving, mouse has been quiet for 1.2 seconds, and NOT in edit mode
    if (!isEditMode && horizontalSpeed > 0.5f && timeSinceLastMouseMovement > 1.2f) {
        // Find movement angle in degrees
        float targetAngle = std::atan2(vel.x, vel.z) * 180.0f / 3.14159265f;
        
        float angleDiff = targetAngle - angleAroundTarget;
        while (angleDiff > 180.0f) angleDiff -= 360.0f;
        while (angleDiff < -180.0f) angleDiff += 360.0f;
        
        // Slowly align camera behind player
        float alignRate = (target->isRiding() || target->isDrivingOrTransitioning()) ? 0.8f : 1.2f;
        angleAroundTarget += angleDiff * alignRate * deltaTime;
    }

    // 3. Calculate the new camera's goal position
    calculateCameraPosition();

    // 4. Smooth the camera's position for a cinematic feel
    updateSmoothing(deltaTime);
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
    
    // Core head/shoulder height offset in world space (1.3 units up)
    Vec3 lookTargetPos = targetPos;
    lookTargetPos.y += 1.3f;

    // Base camera goal position
    Vec3 basePos;
    basePos.x = lookTargetPos.x - offsetX;
    basePos.y = lookTargetPos.y + verticalDistance;
    basePos.z = lookTargetPos.z - offsetZ;

    // Over-the-shoulder framing calculation (RDR2 Camera style)
    // Compute horizontal right vector (perpendicular to target-to-camera direction)
    Vec3 rightVec(cos(radAngleAroundTarget), 0.0f, -sin(radAngleAroundTarget));
    
    float shoulderOffset = 0.6f;
    float lookAtOffset = -0.3f;
    
    if (target->isRiding() || target->isDrivingOrTransitioning()) {
        shoulderOffset = 0.0f; // Center camera on horse or car
        lookAtOffset = 0.0f;
    } else if (target->isAiming()) {
        shoulderOffset = 0.9f; // Larger shift when aiming
        lookAtOffset = -0.4f;
    }

    // Apply right offsets to both position and target to keep player on the left side of screen
    position = basePos + rightVec * shoulderOffset;
    
    float terrainY = 0.0f;
    if (terrainModel) {
        terrainY = terrainModel->getHeight(position.x, position.z);
    }
    
    // Define a minimum height offset above the terrain
    float minCameraY = terrainY + 0.5f;

    // Clamp the camera's goal Y position to this minimum
    if (position.y < minCameraY) {
        position.y = minCameraY;
    }
}

void Camera::updateSmoothing(float deltaTime) {
    // The core of the RDR2-like feel.
    // We smoothly interpolate the camera's position towards its goal.
    float smoothingFactor = 1.0f - exp(-CAMERA_SMOOTHING * deltaTime);

    // Smoothly move the camera from its current position to the new goal position
    smoothedPosition.x += (position.x - smoothedPosition.x) * smoothingFactor;
    smoothedPosition.y += (position.y - smoothedPosition.y) * smoothingFactor;
    smoothedPosition.z += (position.z - smoothedPosition.z) * smoothingFactor;
    
    // Calculate the goal target look-at position
    Vec3 targetPos = target->getPosition();
    targetPos.y += 1.3f; // head/shoulder level
    
    float radAngleAroundTarget = angleAroundTarget * 3.14159265f / 180.0f;
    Vec3 rightVec(cos(radAngleAroundTarget), 0.0f, -sin(radAngleAroundTarget));
    
    float lookAtOffset = -0.3f;
    if (target->isRiding() || target->isDrivingOrTransitioning()) {
        lookAtOffset = 0.0f;
    } else if (target->isAiming()) {
        lookAtOffset = -0.4f;
    }
    
    Vec3 targetGoal = targetPos + rightVec * lookAtOffset;

    // Smoothly move the look-at target
    smoothedTarget.x += (targetGoal.x - smoothedTarget.x) * smoothingFactor;
    smoothedTarget.y += (targetGoal.y - smoothedTarget.y) * smoothingFactor;
    smoothedTarget.z += (targetGoal.z - smoothedTarget.z) * smoothingFactor;
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

void Camera::reset() {
    if (!target) return;
    
    // Calculate unsmoothed positions immediately
    calculateCameraPosition();
    smoothedPosition = position;
    
    // Calculate target goal immediately
    Vec3 targetPos = target->getPosition();
    targetPos.y += 1.3f; // head/shoulder level
    
    float radAngleAroundTarget = angleAroundTarget * 3.14159265f / 180.0f;
    Vec3 rightVec(cos(radAngleAroundTarget), 0.0f, -sin(radAngleAroundTarget));
    
    float lookAtOffset = -0.3f;
    if (target->isRiding() || target->isDrivingOrTransitioning()) {
        lookAtOffset = 0.0f;
    } else if (target->isAiming()) {
        lookAtOffset = -0.4f;
    }
    
    Vec3 targetGoal = targetPos + rightVec * lookAtOffset;
    smoothedTarget = targetGoal;
}

void Camera::updateFrustum(const glm::mat4& proj, const glm::mat4& view) {
    glm::mat4 VP = proj * view;
    glm::mat4 transVP = glm::transpose(VP);
    
    frustumPlanes[0] = transVP[3] + transVP[0]; // Left
    frustumPlanes[1] = transVP[3] - transVP[0]; // Right
    frustumPlanes[2] = transVP[3] + transVP[1]; // Bottom
    frustumPlanes[3] = transVP[3] - transVP[1]; // Top
    frustumPlanes[4] = transVP[3] + transVP[2]; // Near
    frustumPlanes[5] = transVP[3] - transVP[2]; // Far

    // Normalize planes
    for (int i = 0; i < 6; ++i) {
        float len = glm::length(glm::vec3(frustumPlanes[i]));
        if (len > 0.0001f) {
            frustumPlanes[i] /= len;
        }
    }
}

bool Camera::isPointInFrustum(const glm::vec3& p) const {
    for (int i = 0; i < 6; ++i) {
        if (glm::dot(glm::vec3(frustumPlanes[i]), p) + frustumPlanes[i].w < 0.0f) {
            return false;
        }
    }
    return true;
}

bool Camera::isSphereInFrustum(const glm::vec3& center, float radius) const {
    for (int i = 0; i < 6; ++i) {
        if (glm::dot(glm::vec3(frustumPlanes[i]), center) + frustumPlanes[i].w < -radius) {
            return false;
        }
    }
    return true;
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