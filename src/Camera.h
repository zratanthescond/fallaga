#pragma once

#include "Vec3.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// RDR2-like camera parameters
const float CAMERA_MIN_DISTANCE = 5.0f;
const float CAMERA_MAX_DISTANCE = 300.0f;
const float CAMERA_PITCH_MIN = 0.5f;
const float CAMERA_PITCH_MAX = 80.0f;
const float CAMERA_SENSITIVITY = 0.5f;
const float CAMERA_ZOOM_SENSITIVITY = 0.50f;
const float CAMERA_SMOOTHING = 5.0f; 

class Character; // Forward declaration
class ObjModel; // Forward declaration

class Camera {
public:
    Camera(Character* target);
    ~Camera();
    void setTerrain(ObjModel* terrain); // New setter
    glm::mat4 getViewMatrix();
    // RDR2-like update method with mouse input
    void update(float mouseDeltaX, float mouseDeltaY, float mouseScroll);

    // Apply the camera view to the OpenGL matrix stack
  //  void apply() const;

    // Get the camera's current position and direction
    Vec3 getPosition() const;
    Vec3 getForward() const;
    Vec3 getRight() const;

private:
    Character* target;
    ObjModel* terrainModel = nullptr; // Dependency pointer


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
    void updateSmoothing();
    public:
    float distance; // Current distance from the target
};