#pragma once
#include "Character.h"
#include "Camera.h"
#include "Terrain.h"

class Game {
public:
    Game();
    ~Game();
    
    void update();
    void render();
    void keyDown(int key);
    void keyUp(int key);
    
    Camera& getCamera();
    
private:
    Character* player;
    Camera* camera;
    Terrain* terrain;
    
    float lastFrameTime;
    float deltaTime;
};
