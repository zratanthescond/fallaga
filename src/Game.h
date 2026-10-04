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
    int editorBrushMode = 0; // 0=Select; 1-5 sculpt; 6=Paint Road; 7=Paint River; 8=Paint Erase
    float brushRadius = 30.0f;
    float brushStrength = 5.0f;
    float sculptAngleDegrees = 20.0f;
    float sculptGradeDegrees = 20.0f;
    glm::vec3 lastRayHitPos = glm::vec3(0.0f, -1000.0f, 0.0f);
    bool rayHitTerrain = false;
    float waterLevelAdjust = -55.0f; // Real-time sea level
    // ── Road Edit mode state ──
    int roadToolMode = 0;              // 0=Select Road, 1=Draw Road, 2=Edit Points
    int selectedRoadIdx = -1;
    int selectedRoadPointIdx = -1;
    int draftDragIdx = -1;             // draw-tool: index of draft point being moved (>=0 while dragging)
    bool draftInsertNewly = false;     // draw-tool: the dragged point was freshly inserted (edge split)
    bool roadScalingSelected = false;  // select-tool: scaling selected road with = / - keys
    bool roadGraphDirty = false;
    std::vector<glm::vec3> roadDraftPoints;
    float roadTransformScale = 1.0f;   // whole-map scale factor (applied via button)
    float roadConnectTolerance = 25.0f; // connect-roads snap/merge tolerance (m)
    float roadMoveStep = 20.0f;        // whole-map translate step (m), 5..200
    // Draw-tool options (tool 2)
    bool roadStraightMode = false;     // axis-lock (0/45/90) every new point to the previous one
    bool roadShiftHeld = false;        // Shift held (straight on that click)
    bool roadSnapToRoads = true;       // snap new draft points to existing road endpoints
    float roadSnapTolerance = 12.0f;   // max distance to snap onto an existing road endpoint (m)
    bool roadPanelsVisible = true;     // Tab hides/shows side panels so viewport is clickable
    // Properties applied to the NEXT road drawn in Draw mode
    int roadDrawType = 1;              // default UrbanStreet
    float roadDrawWidth = 8.0f;        // m
    float roadDrawShoulder = 1.5f;     // m
    float roadDrawCornerRadius = 0.0f;  // m; 0 uses the road-width-based automatic curve
    bool roadDrawClosedLoop = false;   // close the next drawn road into a continuous ring
    // Reference image overlay (loaded in road edit mode)
    bool roadRefVisible = true;
    bool roadRefLoaded = false;
    unsigned int roadRefTex = 0;
    unsigned int roadRefDrawTex = 0;
    int roadRefW = 0, roadRefH = 0;
    int roadRefIdx = 0;              // which reference map file is shown
    // Projected reference map (drawn on the terrain surface, reference only)
    bool projRefVisible = true;
    bool projRefInitialized = false;
    bool projRefFlipX = false;
    bool projRefFlipZ = false;
    float projRefScale = 1.0f;       // uniform scale factor
    float projRefOpacity = 0.85f;     // 0..1 blend over terrain
    float projRefMapW = 3000.0f;     // base world width (from road bbox once)
    glm::vec2 projRefCenter = glm::vec2(0.0f);
    float projRefYOffset = 0.0f;     // extra lift above terrain (m), +/- via panel
    double lastSaveTime = -100.0;
    bool isSavingState = false;
    void performSave();
    void saveProjRefMap();
    bool loadProjRefMap();
    void commitRoadDraft();
    void cancelRoadDraft();
    // Graceful shutdown request. The scripted FALLAGA_TEST_MODE harness used to
    // call exit() straight from Game::update(), which skips every destructor and
    // any pending save. main() polls this and leaves its loop instead.
    void requestQuit(int exitCode) { quitRequested = true; requestedExitCode = exitCode; }
    bool isQuitRequested() const { return quitRequested; }
    int getRequestedExitCode() const { return requestedExitCode; }
    // Keeps the HUD layout, UI hit-testing and raycast normalisation in step with
    // the framebuffer after a resize; the size is otherwise only known at setup().
    void setWindowSize(int w, int h) { windowWidth = w; windowHeight = h; }
    float projSaveFlash = 0.0f;   // seconds remaining of "SAVED" highlight on the map-panel button
    // Auto-save snapshot: last placement that was persisted to disk
    glm::vec2 projSavedCenter = glm::vec2(0.0f);
    float projSavedScale = 1.0f;
    float projSavedOpacity = 0.85f;
    float projSavedYOffset = 0.0f;
    bool projSavedFlipX = false;
    bool projSavedFlipZ = false;
    int projSavedIdx = 0;
    float projSavedMapW = 3000.0f;   // must match projRefMapW default so the first frame never spuriously saves
    double projLastSave = -100.0;

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

private:
    bool quitRequested = false;
    int requestedExitCode = 0;
    // Cache for the reference map draped over the terrain (road edit mode only).
    // Rebuilding it costs 1089 terrain->getHeight() queries per frame; the grid is
    // valid only while the terrain heights and the map's placement are unchanged,
    // so the inputs are remembered alongside the heights they produced.
    std::vector<float> projRefHeightGrid;
    std::size_t projRefHeightGridCount = 0;   // 0 = cache empty (GN never changes)
    unsigned int projRefHeightGridHeightRev = 0;
    std::uint64_t projRefHeightGridSurfaceRev = 0;
    glm::vec2 projRefHeightGridCenter = glm::vec2(0.0f);
    float projRefHeightGridMapW = 0.0f;
    float projRefHeightGridScale = 0.0f;
    bool projRefHeightGridFlipX = false;
    bool projRefHeightGridFlipZ = false;
    float projRefHeightGridYOffset = 0.0f;
    // GPU timer queries. Members (not function-local statics) so the destructor can
    // delete them; they used to be glGen'd and never freed.
    unsigned int gpuTimerQueries[4] = {0, 0, 0, 0};
    // Per-phase GPU timers: [slot][phase]. Results are read two frames after the
    // slot is used, so a slot is never reused before its queries have completed and
    // reading them cannot stall the pipeline. Phase order follows the render order:
    // sky, terrain, objects, water, post.
    static constexpr int kGpuPhaseSlots = 4;
    static constexpr int kGpuPhaseCount = 5;
    unsigned int gpuPhaseQueries[kGpuPhaseSlots][kGpuPhaseCount] = {};
    bool gpuPhasePending[kGpuPhaseSlots] = {};
    double gpuPhaseAccum[kGpuPhaseCount] = {};
    int gpuPhaseSampleCount = 0;
};
