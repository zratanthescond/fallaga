#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include "TerrainChunk.h"
#include "TerrainRefinement.h"
#include "TerrainUndo.h"
#include "Shader.h"
#include "RoadSystem.h"

class TerrainManager {
public:
    TerrainManager(const std::string& hmPath, const std::string& pmPath, float minX, float maxX, float minZ, float maxZ, float minH, float maxH);
    ~TerrainManager();
    void update(const glm::vec3& camPos);
    void render(const glm::mat4& proj, const glm::mat4& view, const glm::vec3& camPos, const glm::vec3& lightDir, const glm::vec3& lightColor, float waterLevel, const glm::vec4& clipPlane = glm::vec4(0.0f, -1.0f, 0.0f, 100000.0f), float maxDrawDistance = 6000.0f);
    float getHeightAt(float x, float z) const;
    void exportToOBJ(const std::string& filename, int downsample = 4) const;
    glm::vec3 getNormalAt(float x, float z) const;
    float getRoadAt(float x, float z) const;
    float getRoadGeometryAt(float x, float z) const;
    float getRiverAt(float x, float z) const;
    GLuint getHeightmapTexture() const { return heightmapTexID; }
    GLuint getRoadMask() const { return roadMask; }
    void setEnvironmentMapTexture(GLuint texture, const glm::vec4& worldRect,
                                  bool hasCover = false) {
        environmentMapTexture = texture;
        environmentMapWorldRect = worldRect;
        environmentMapHasCover = hasCover;
    }
    void setBiomeMapTexture(GLuint texture, const glm::vec4& worldRect,
                            bool available = false) {
        biomeMapTexture = texture;
        biomeMapWorldRect = worldRect;
        hasBiomeMap = available;
    }
    // Apply/remove road shaping in the visual and physics heightfields. Passing
    // null offsets removes tracked road-bed and mesh-conformance changes.
    void applyRoadBedHeightfield(const float* offsets, int offsetW, int offsetH);
    void setRoadGeometryMask(const unsigned char* mask, int maskW, int maskH);
    void sculpt(float wx, float wz, float radius, float speed, int mode, float dt,
                float directionDegrees = 0.0f, float gradeDegrees = 20.0f,
                int falloffMode = 0, float hardness = 0.0f);
    // Continuous brush stroke. Remembers the previous pointer position and samples
    // the whole segment so the frame's strength is spread along the path instead of
    // being applied in full at each frame's endpoint. The affected cells of every
    // sample are unioned and the physics/GPU/chunk update runs once per frame.
    // Call endSculptStroke() when the button is released.
    void sculptStroke(float toX, float toZ, float radius, float speed, int mode, float dt,
                      float directionDegrees = 0.0f, float gradeDegrees = 20.0f,
                      int falloffMode = 0, float hardness = 0.0f);
    void endSculptStroke();
    // Straightens a shoreline between two world-space endpoints and reshapes both
    // sides around the configured sea level. halfWidth is the blend distance in metres.
    bool straightenShoreline(float x1, float z1, float x2, float z2,
                              float halfWidth, float waterLevel);
    void conformTerrainToRoadMesh(float wx, float wz, float radius, float speed, float dt,
                                  const std::vector<RoadVertex>& roadVertices,
                                  const std::vector<uint32_t>& roadIndices,
                                  bool wholeNetwork = false,
                                  float bermWidth = 30.0f);
    void paint(float wx, float wz, float radius, float speed, int layerIdx, float dt);
    void saveTerrain();
    void erodeGlobal();

    // ── Sculpt undo/redo ───────────────────────────────────────────────────────
    // Journaled transactions. A continuous stroke opens a transaction on its first
    // frame and leaves it open; endSculptStroke() commits it when the button is
    // released, so one undo covers a whole stroke rather than a frame. Discrete
    // operations (shoreline straighten, global erosion) open and commit their own.
    // Any edit calls beginEditTransaction(); doing so while one is already open is
    // a no-op, which is what lets the tools nest without a second entry point.
    void beginEditTransaction();
    void commitEditTransaction();
    void abortEditTransaction();
    bool undo();                 // true if something was undone
    bool redo();                 // true if something was redone
    bool canUndo() const { return undoStack.canUndo(); }
    bool canRedo() const { return undoStack.canRedo(); }
    std::size_t undoDepth() const { return undoStack.undoCount(); }
    std::size_t redoDepth() const { return undoStack.redoCount(); }
    void clearUndoHistory() { undoStack.clear(); }
    void updateGPUHeightmapTexture();
    // Upload only a rectangle of the heightmap texture. Without it every brush
    // frame re-normalised and re-uploaded the whole native grid (8129^2 floats in
    // the worst case) for a change covering a few hundred texels.
    void updateGPUHeightmapTextureRect(int minX, int minZ, int maxX, int maxZ);
    // FALLAGA_ROADGAP: coarse signature of the rendered heightfield. Used to trace which
    // stage of a tool stroke raises terrain back, since the brush, the roadbed deform and
    // the conform all write the same array.
    void logHeightfieldSignature(const char* stage) const;

    // Accessors for road system integration
    const float* getHeightsData() const { return heights.data(); }
    int getGridW() const { return gridW; }
    int getGridH() const { return gridH; }
    // Bumped whenever `heights` may have reallocated. Consumers that cache
    // getHeightsData() (e.g. Terrain's HeightSampler) must re-init when this
    // changes; otherwise their pointer is left dangling by the reallocation.
    unsigned int getHeightRevision() const { return heightRevision; }
    unsigned int getHeightmapRevision() const { return heightmapRevision; }
    bool hasOpenTerrainEdit() const { return editTxnActive; }
    int getVisualGridW() const { return visualW; }
    int getVisualGridH() const { return visualH; }
    float getWorldMinX() const { return worldMinX; }
    float getWorldMaxX() const { return worldMaxX; }
    float getWorldMinZ() const { return worldMinZ; }
    float getWorldMaxZ() const { return worldMaxZ; }
    float getMinHeight() const { return minHeight; }
    float getMaxHeight() const { return maxHeight; }
    // Rendered-terrain heightfield. Roads are sampled from getHeightsData(), so
    // anything comparing the two (road/terrain conformance checks) must read this
    // instead to see the surface the GPU actually draws.
    const std::vector<float>& getVisualHeightsData() const { return visualHeights; }

    // ── Chunk LOD ──────────────────────────────────────────────────────────────
    // Distance-selected geometric level per chunk (0 = full detail, up to 3 = 1/8
    // samples per edge). Chunks holding, or adjacent to, editor-added geometry stay
    // at LOD0, and a chunk bordering a different level is rebuilt with a skirt that
    // closes the seam. Selection runs in update(), once per frame with the GL context
    // current; a chunk is only re-meshed when its level actually changes.
    void updateChunkLOD(const glm::vec3& camPos);
    void rebuildChunk(TerrainChunk* chunk);
    bool chunkHasFineGeometry(const TerrainChunk* chunk) const;

    // ── Sculpt "Add Geometry" tool ─────────────────────────────────────────────
    // Adds mesh resolution inside the brush footprint so smoothing has vertices to
    // work with. `level` is the subdivision exponent for the visual-grid cells the
    // brush touches (1 = each ~22 m cell becomes 4, 2 = 16). Raises the level and
    // seeds the new fine vertices on the existing surface; never lowers a cell below
    // the level it already has. Returns the number of cells that gained geometry.
    int addGeometry(float wx, float wz, float radius, int level);
    // Smooths using the fine grid, so a refined area actually gains a smoother
    // surface rather than being limited by the base grid. Falls back to the normal
    // grid smooth anywhere without added geometry.
    void smoothFine(float wx, float wz, float radius, float strength, float dt,
                    int falloffMode = 0, float hardness = 0.0f);
    // True once any cell has added geometry (for the editor readout).
    bool hasAddedGeometry() const { return refinement.any(); }
    // Refined-surface access, exposed so the behaviour can be tested without a GL
    // context (the mesh rebuild itself needs one, this does not).
    static constexpr int kRefineScale = TerrainRefinement::kScale; // fine samples per cell
    float refinedHeightAt(int fgx, int fgz, float fallback) const {
        return refinement.heightAt(fgx, fgz, fallback);
    }
    int getRefineLevel(int cellX, int cellZ) const { return refinement.levelAt(cellX, cellZ); }
    int getRefinedDim() const { return refinement.dim(); }
    // Refinement level lookup in the shape TerrainChunk wants.
    std::function<int(int, int)> refineLevelLookup() const {
        return [this](int gx, int gz) { return refinement.levelAt(gx, gz); };
    }
    TerrainChunk::HeightFn refinedHeightLookup() const {
        return [this](int fgx, int fgz, float fallback) {
            return refinement.heightAt(fgx, fgz, fallback);
        };
    }
    // Rebuilds only the chunks overlapping a visual-grid box. Needs a GL context;
    // the height maths above does not.
    void rebuildChunksInBox(int minCellX, int maxCellX, int minCellZ, int maxCellZ);

private:
    void loadHeightmap(const std::string& path);
    // Pending dirty rectangle of the heightmap texture, in native grid pixels.
    // -1 in any field means "nothing pending".
    int hmDirtyMinX = -1, hmDirtyMinZ = -1, hmDirtyMaxX = -1, hmDirtyMaxZ = -1;
    std::vector<float> hmScratch; // reused scratch for the normalized upload

    void setupTextures(const std::string& pmPath);
    void erode(int numDroplets, float brushX, float brushZ, float radius, bool isLocal);
    // Mirrors a sculpt edit box into the physics mirror, the native heightmap and
    // the GPU texture, then rebuilds the chunks that overlap it. Shared by sculpt()
    // and sculptStroke() so a long stroke pays for the mirror/upload/rebuild once.
    void finalizeSculpt(int minX, int maxX, int minZ, int maxZ);
    bool sculptStrokeActive = false;
    float sculptStrokeLastX = 0.0f, sculptStrokeLastZ = 0.0f;
    // Flatten brush (mode 9): the height sampled where the stroke began. Held for the
    // whole stroke so the pad stays at a fixed level instead of creeping as the brush
    // moves, and cleared by endSculptStroke() so the next stroke re-anchors.
    float sculptFlattenAnchorHeight = 0.0f;
    bool sculptFlattenAnchorValid = false;

    // ── Edit journal (see TerrainUndo.h) ───────────────────────────────────────
    // Capture-before-write. The stamp arrays deduplicate a sample across the many
    // frames of one stroke: the first capture of a sample wins, so the record holds
    // its true pre-stroke value. `editTxnActive` is true between begin and commit.
    void captureBaseRegion(int minX, int maxX, int minZ, int maxZ);
    void captureBaseAt(std::size_t idx);
    void captureLayerAt(std::size_t idx);
    void captureFineBox(int fMinX, int fMaxX, int fMinZ, int fMaxZ);
    void captureCellsAndFineBox(int cMinX, int cMaxX, int cMinZ, int cMaxZ);
    void resetCapture();
    // Swaps a record's fine samples / subdivision levels with the live refinement,
    // filling `inverse`, then leaves the record's regions on `inverse`.
    void applyFineAndCells(const TerrainEditRecord& rec, TerrainEditRecord& inverse);
    // Applies one record (base + layers + fine + cells) and rebuilds the affected
    // meshes. Used by both undo() and redo().
    void applyEditRecord(TerrainEditRecord& rec, TerrainEditRecord& inverse);

    TerrainUndoStack undoStack;
    bool editTxnActive = false;
    int txnStamp = 0;                       // increments per transaction; stamps are "in this txn?"
    std::vector<int32_t> baseStamp;         // per visual sample, sized visualHeights.size()
    std::vector<int32_t> layerStamp;        // per visual sample
    std::vector<uint32_t> txnBaseIdx;
    std::vector<float> txnBaseBefore;
    std::vector<uint32_t> txnLayerIdx;
    std::vector<float> txnRoadOffBefore;
    std::vector<float> txnRoadConBefore;
    std::unordered_map<uint32_t, float> txnFineBefore;
    std::unordered_map<uint32_t, unsigned char> txnFinePresentBefore;
    std::unordered_map<uint32_t, unsigned char> txnCellBefore;
    std::vector<float> heights;
    std::vector<float> visualHeights;
    std::vector<float> physicsVisualHeights; // authoritative for height queries (visual grid res)
    unsigned int heightRevision = 0;         // see getHeightRevision()
    unsigned int heightmapRevision = 0;      // increments only when heightmap.png is reloaded
    std::vector<float> appliedRoadOffsets;
    std::vector<float> appliedRoadConformOffsets;
    std::vector<unsigned char> roadMaskData;
    bool hasRoadGeometryMask = false;
    std::vector<unsigned char> roadGeometryMaskData;
    int roadGeometryMaskW = 0, roadGeometryMaskH = 0;

    // Subdivision and its fine height field. Free of OpenGL so the logic is testable
    // without a context; see TerrainRefinement.h for why subdivision alone cannot
    // change the surface.
    TerrainRefinement refinement;

    // ── Added geometry (sculpt "Add Geometry" tool) ────────────────────────────
    // Subdivision is grid-aligned rather than per-cell, so a refined vertex has an
    // unambiguous global position and neighbouring chunks agree on it without any
    // cross-chunk stitching. refineLevel is one byte per visual-grid CELL (0 = no
    // added geometry). refinedHeights/Vertices hold the fine vertices inside
    // subdivided cells, keyed densely by fine-grid position; they start on the
    // bilinear surface, so turning subdivision on cannot change the silhouette --
    // only sculpting the fine vertices does.
    // nothing to stitch.
    std::vector<unsigned char> refineLevel;  // (visualW-1) x (visualH-1)
    std::vector<float> refinedHeights;       // (visualW-1)*4+1 square, kNaN = use base
    std::vector<unsigned char> refinedPresent;
    int refinedDim = 0;
    float refinedSample(int fgx, int fgz, float fallback) const;
    void setRefinedSample(int fgx, int fgz, float value);
    bool anyRefinement() const;
    int refineLevelAt(int cellX, int cellZ) const;

    int gridW=0, gridH=0;
    int visualW=0, visualH=0;
    int roadW=0, roadH=0;
    int chunkCols=0, chunkRows=0; // chunk grid dimensions (chunks are pushed row-major)
    // Per-chunk "holds editor-added geometry" flags, cached against the refinement's
    // level revision so the LOD pass does not rescan the refinement grid every frame.
    std::vector<unsigned char> chunkFineCache;
    unsigned int chunkFineCacheRev = 0xFFFFFFFFu;
    float worldMinX, worldMaxX, worldMinZ, worldMaxZ, minHeight, maxHeight;
    std::vector<TerrainChunk*> chunks;
    GLuint terrainLayerArray=0, terrainNormalArray=0, terrainDispArray=0, terrainRoughArray=0,
           portMask=0, roadMask=0, roadGeometryMask=0, heightmapTexID=0,
           albedoTex=0, splatmap2Tex=0, terrainSplatTex=0;
    GLuint environmentMapTexture = 0;
    glm::vec4 environmentMapWorldRect{0.0f};
    bool environmentMapHasCover = false;
    GLuint biomeMapTexture = 0;
    glm::vec4 biomeMapWorldRect{0.0f};
    bool hasBiomeMap = false;
    bool hasRoadMask = false;
    int activePaintLayerIdx = 2;
    Shader* terrainShader = nullptr;
    std::string hmFilePath;
    time_t lastHmModTime = 0;
    const int CHUNK_SIZE = 129;
};
