#include <GL/glew.h>
#include <GLFW/glfw3.h> // GLFW headers
#include <GL/glut.h>   // GLUT headers
#include "ObjectModel.h"
#include "Camera.h"
#include "Character.h"
#include "Terrain.h"
#include "Car.h"
#include <cmath>
#include <cstdlib>
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

    // While driving, the camera must follow the CAR. The character's own
    // velocity is not driven by the car, so it is stale (leftover walking
    // velocity, usually zero) and useless for camera alignment.
    Car* car = target->isDrivingOrTransitioning() ? target->getMountedCar() : nullptr;

    // 1. Update angles and distance based on mouse input
    angleAroundTarget += mouseDeltaX * CAMERA_SENSITIVITY;
    pitch -= mouseDeltaY * CAMERA_SENSITIVITY;
    
    // Clamp pitch to prevent flipping
    if (pitch > CAMERA_PITCH_MAX) pitch = CAMERA_PITCH_MAX;
    if (pitch < CAMERA_PITCH_MIN) pitch = CAMERA_PITCH_MIN;

    // Contain the pitch while driving. An on-foot camera left near-overhead or
    // almost level makes the car undrivable, and reset() never changes pitch.
    if (car) {
        if (pitch > 42.0f) pitch = 42.0f;
        if (pitch < 3.0f) pitch = 3.0f;
    }

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

    // Driving pull-back. FOV is fixed at 45 degrees, so distance is the only
    // way to see far enough ahead at speed. A manual scroll suppresses it for a
    // couple of seconds so it does not fight the player.
    if (car) {
        if (mouseScroll != 0.0f) zoomHold = 2.0f;
        else if (zoomHold > 0.0f) zoomHold -= deltaTime;

        float wanted = glm::clamp(7.0f + car->getSpeed() * 0.30f, 7.0f, 20.0f);
        if (zoomHold <= 0.0f) {
            distance += (wanted - distance) * (1.0f - std::exp(-2.5f * deltaTime));
        }
    }

    // 2. Auto-Alignment (RDR2 Camera feel) - Disabled in Editor Mode
    if (mouseDeltaX != 0.0f || mouseDeltaY != 0.0f) {
        timeSinceLastMouseMovement = 0.0f;
    } else {
        timeSinceLastMouseMovement += deltaTime;
    }

    Vec3 vel = target->getVelocity();
    float horizontalSpeed = sqrt(vel.x * vel.x + vel.z * vel.z);
    float alignAngle = std::atan2(vel.x, vel.z) * 180.0f / 3.14159265f;
    bool haveAlignSource = horizontalSpeed > 0.5f;

    if (car) {
        // Align to the car's heading, not its velocity, so the camera stays
        // behind the car while reversing instead of swinging to the front.
        glm::vec3 cf = car->getForwardVector();
        // The orbit angle is measured from the car's forward vector: with the
        // camera position formula below, this places the camera behind it.
        alignAngle = std::atan2(cf.x, cf.z) * 180.0f / 3.14159265f;
        horizontalSpeed = car->getSpeed();
        haveAlignSource = true;
    }

    // Driving uses a short manual-look hold and an exponential return so the
    // chase camera stays behind the car through corners, including at rest.
    float idleNeeded = car ? 0.10f : 1.2f;

    // Only align if moving, mouse has been quiet, and NOT in edit mode
    const bool shouldAlign = car || horizontalSpeed > 0.5f;
    if (!isEditMode && haveAlignSource && shouldAlign &&
        timeSinceLastMouseMovement > idleNeeded) {
        float angleDiff = alignAngle - angleAroundTarget;
        while (angleDiff > 180.0f) angleDiff -= 360.0f;
        while (angleDiff < -180.0f) angleDiff += 360.0f;
        
        // Slowly align camera behind player
        float alignRate = car ? 8.0f : ((target->isRiding() || target->isDrivingOrTransitioning()) ? 0.8f : 1.2f);
        angleAroundTarget += angleDiff * (1.0f - std::exp(-alignRate * deltaTime));
    }

    if (std::getenv("FALLAGA_TEST_MODE") && car) {
        static int camDbg = 0;
        if (++camDbg % 20 == 0) {
            float d = alignAngle - angleAroundTarget;
            while (d > 180.0f) d -= 360.0f;
            while (d < -180.0f) d += 360.0f;
            std::cout << "[CAMDGB] spd=" << horizontalSpeed
                      << " alignAngle=" << alignAngle
                      << " camAngle=" << angleAroundTarget
                      << " diff=" << d
                      << " dist=" << distance
                      << " pitch=" << pitch
                      << " idle=" << timeSinceLastMouseMovement
                      << " haveAlign=" << haveAlignSource << "\n";
        }
    }

    // 3. Calculate the new camera's goal position
    calculateCameraPosition();

    // 4. Smooth the camera's position for a cinematic feel
    updateSmoothing(deltaTime);
}

void Camera::calculateCameraPosition() {
    // The seated character anchor can lag the car by one update. Use the car's
    // actual chassis position for both camera placement and look-at framing.
    Car* car = target->isDrivingOrTransitioning() ? target->getMountedCar() : nullptr;
    Vec3 targetPos = target->getPosition();
    if (car) {
        const Vec3 carPos = car->getPosition();
        targetPos = Vec3(carPos.x, carPos.y, carPos.z);
    }

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
        // Ground surface, not raw terrain: on a roadbed the rendered surface sits
        // above the raw heightfield, and a raw-height clamp sinks the camera
        // through the road.
        terrainY = terrainModel->getGroundSurfaceHeight(position.x, position.z);
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
    Car* car = target && target->isDrivingOrTransitioning()
        ? target->getMountedCar() : nullptr;
    const float smoothingRate = car ? 10.0f : CAMERA_SMOOTHING;
    float smoothingFactor = 1.0f - exp(-smoothingRate * deltaTime);

    // Smoothly move the camera from its current position to the new goal position
    smoothedPosition.x += (position.x - smoothedPosition.x) * smoothingFactor;
    smoothedPosition.y += (position.y - smoothedPosition.y) * smoothingFactor;
    smoothedPosition.z += (position.z - smoothedPosition.z) * smoothingFactor;
    
    // Calculate the goal target look-at position
    Vec3 targetPos = target->getPosition();
    if (car) {
        const Vec3 carPos = car->getPosition();
        targetPos = Vec3(carPos.x, carPos.y, carPos.z);
    }
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
    Car* car = target->isDrivingOrTransitioning() ? target->getMountedCar() : nullptr;
    Vec3 targetPos = target->getPosition();
    if (car) {
        const Vec3 carPos = car->getPosition();
        targetPos = Vec3(carPos.x, carPos.y, carPos.z);
    }
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
