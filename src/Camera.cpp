#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <GL/glut.h>
#include "Camera.h"
#include "Character.h"
#include <cmath>
#include <iostream> // For debugging
#include"Vec3.h"
// Assuming these are implemented in Character.cpp
// Vec3 Character::getPosition() const { return position; }

Camera::Camera(Character* target)
    : target(target), distance(10.0f), yaw(0.0f), pitch(20.0f),
      lastX(400.0f), lastY(300.0f), firstMouse(true)
{
    // The initial target position is set here
    if (target) {
        targetPos = target->getPosition();
    }
}

void Camera::handleMouse(double xpos, double ypos) {
    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = static_cast<float>(xpos - lastX);
    float yoffset = static_cast<float>(lastY - ypos); // Reversed since y-coordinates go from bottom to top
    lastX = xpos;
    lastY = ypos;

    float sensitivity = 0.1f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    yaw   += xoffset;
    pitch += yoffset;

    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -10.0f) pitch = -10.0f;
}

void Camera::update() {
    if (!target) return;

    targetPos = target->getPosition();

    // Convert angles to radians for trigonometric functions
    float radYaw   = yaw * 3.14159265f / 180.0f;
    float radPitch = pitch * 3.14159265f / 180.0f;

    // Calculate camera position based on distance, yaw, and pitch from the target
    position.x = targetPos.x + distance * cos(radPitch) * sin(radYaw);
    position.y = targetPos.y + distance * sin(radPitch);
    position.z = targetPos.z + distance * cos(radPitch) * cos(radYaw);
}

void Camera::apply() {
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    
    // Calculate the camera's up vector
    Vec3 up(0.0f, 1.0f, 0.0f);
    
    // Call gluLookAt to set the camera's view
    gluLookAt(
        position.x, position.y, position.z,
        targetPos.x, targetPos.y, targetPos.z,
        up.x, up.y, up.z
    );
}

Vec3 Camera::getForward() const {
    float radYaw = yaw * 3.14159265f / 180.0f;
    float radPitch = pitch * 3.14159265f / 180.0f;
    
    Vec3 forward;
    forward.x = cos(radPitch) * sin(radYaw);
    forward.y = sin(radPitch);
    forward.z = cos(radPitch) * cos(radYaw);
    forward.normalize();
    return forward;
}

Vec3 Camera::getRight() const {
    Vec3 forward = getForward();
    Vec3 up(0.0f, 1.0f, 0.0f);
    
    Vec3 right;
    right.x = forward.y * up.z - forward.z * up.y;
    right.y = forward.z * up.x - forward.x * up.z;
    right.z = forward.x * up.y - forward.y * up.x;
    right.normalize();
    return right;
}

Vec3 Camera::getPosition() const {
    return position;
}
