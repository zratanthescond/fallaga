#ifndef CAMERA_H
#define CAMERA_H
#include <map>
#include <GL/glut.h>
#include "Vec3.h" 

// Forward declaration to break the circular dependency
class Character; 

class Camera {
public:
    Camera(Character* target);
    void update();
    void apply();
    void handleMouse(double xpos, double ypos);
    
    // Add these methods to allow the character to move relative to the camera
    Vec3 getForward() const;
    Vec3 getRight() const;
    Vec3 getPosition() const;

private:
    Character* target;
    float distance;
    float yaw;
    float pitch;
    Vec3 position;
    Vec3 targetPos;
    double lastX;
    double lastY;
    bool firstMouse;
};

#endif
