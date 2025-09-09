#ifndef CHARACTER_H
#define CHARACTER_H
#include <map>
#include <GL/glut.h>
#include "Vec3.h" // Include the external Vec3.h header
#include "Camera.h"
#include "ObjectModel.h" // Include the ObjectModel header

class Character {
public:
    Character();
    ~Character(); // Add destructor

    void update(Camera* camera, float deltaTime, ObjModel* terrainModel); // character logic
    void render(); // draw character
    Vec3 getPosition() const { return position; }
    void keyDown(unsigned char key);
    void keyUp(unsigned char key);
    
private:
    Vec3 position;
    float speed = 0.5f; // movement speed
    std::map<unsigned char, bool> keys; // track pressed keys
    ObjModel* model; // 3D model of the character
};
#endif