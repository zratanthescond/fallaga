#pragma once
#include "Character.h"
#include "Camera.h"
#include "Terrain.h"
#include "Horse.h"
#include "Sky.h"

class Game {
public:
    Game();
    ~Game();
    void setup(int width, int height);
    void update( float mouseDeltaX, float mouseDeltaY, float mouseScroll);
    void render();
    void keyDown(int key);
    void keyUp(int key);
    
    Camera& getCamera();
    
private:
    Character* player;
    Camera* camera;
    Terrain* terrain;
    Horse* horse;
    Sky* sky;
    int windowWidth;
    int windowHeight;
    double lastFrameTime;
    double deltaTime;
    double totalTime;
};
