#pragma once
#include "Character.h"
#include "Camera.h"
#include "Terrain.h"
#include "Horse.h"
#include "Sky.h"
#include "WaterSimulation.h"

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
    
    bool editMode = false;
    int activeEditorMode = 0; // 0 = Game, 1 = Actor Edit, 2 = Terrain Sculpt, 3 = Terrain Paint
    int selectedAssetIdx = 0;
    int selectedPlacedIdx = -1;
    int selectedTextureIdx = 0;
    int activeTextureLayer = 0; // 0 = Layer 1, 1 = Layer 2
    float previewScale = 1.0f;
    float previewAngle = 0.0f;
    int editorBrushMode = 0; // 0=Select/Move, 1=Sculpt Raise, 2=Sculpt Lower, 3=Sculpt Smooth, 4=Sculpt Erode, 5=Paint Road, 6=Paint River, 7=Paint Erase
    float brushRadius = 30.0f;
    float brushStrength = 5.0f;
    glm::vec3 lastRayHitPos = glm::vec3(0.0f, -1000.0f, 0.0f);
    bool rayHitTerrain = false;
    float waterLevelAdjust = -48.5f; // Real-time water level, default = WATER_BASE
    double lastSaveTime = -100.0;
    bool isSavingState = false;
    void performSave();

    Character* player;
    Camera* camera;
    Terrain* terrain;
    Horse* horse;
    Sky* sky;
    class Car* car;
    WaterSimulation waterSim;
    int windowWidth;
    int windowHeight;
    double lastFrameTime;
    double deltaTime;
    double totalTime;
};
