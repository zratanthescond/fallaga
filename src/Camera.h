#pragma once

#include "Vec3.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// RDR2-like camera parameters
const float CAMERA_MIN_DISTANCE = 2.0f;
const float CAMERA_MAX_DISTANCE = 3500.0f;
const float CAMERA_PITCH_MIN = 0.5f;
const float CAMERA_PITCH_MAX = 89.5f;
const float CAMERA_SENSITIVITY = 0.5f;
const float CAMERA_ZOOM_SENSITIVITY = 0.50f;
const float CAMERA_SMOOTHING = 5.0f; 

class Character; // Forward declaration
class Terrain; // Forward declaration

class Camera {
public:
    Camera(Character* target);
    ~Camera();
    void setTerrain(Terrain* terrain); // New setter
    Terrain* getTerrain() const { return terrainModel; } // New getter
    glm::mat4 getViewMatrix();
    // RDR2-like update method with mouse input and deltaTime
    void update(float mouseDeltaX, float mouseDeltaY, float mouseScroll, float deltaTime, bool isEditMode = false);
    void reset();
    void setAngles(float angle, float p) { angleAroundTarget = angle; pitch = p; reset(); }

    // Apply the camera view to the OpenGL matrix stack
  //  void apply() const;

    // Get the camera's current position and direction
    Vec3 getPosition() const;
    Vec3 getForward() const;
    Vec3 getRight() const;

private:
    Character* target;
    Terrain* terrainModel = nullptr; // Dependency pointer
    float timeSinceLastMouseMovement = 0.0f; // Track mouse activity for auto-alignment


    //float distance; // Current distance from the target
    float angleAroundTarget; // Horizontal rotation
    float pitch; // Vertical rotation
    
    // Smooth position and target
    Vec3 smoothedPosition;
    Vec3 smoothedTarget;

    // Camera vectors
    Vec3 position;
    Vec3 forward;
    Vec3 up;
    Vec3 right;
    
    // Internal state for mouse handling
    float lastX, lastY;
    bool firstMouse;

    // Utility functions for calculation and smoothing
    void calculateCameraPosition();
    void updateSmoothing(float deltaTime);

    glm::vec4 frustumPlanes[6];

    public:
    void updateFrustum(const glm::mat4& proj, const glm::mat4& view);
    bool isPointInFrustum(const glm::vec3& p) const;
    bool isSphereInFrustum(const glm::vec3& center, float radius) const;

    float distance; // Current distance from the target
    void applyRecoil(float pitchAmount, float yawAmount) {
        // Recoil stub — add actual camera shake here
        pitch -= pitchAmount * 0.1f;
        angleAroundTarget += yawAmount * 0.1f;
    }
};