#include "TileRoadRenderer.h"

#include <algorithm>
#include <chrono>
#include <iostream>

#include "Shader.h"

namespace roadgrid {

namespace {

// Tile grid bounds to build. The whole 2048x2048 grid is 4 M cells and almost all of
// it is empty, so building "everything" would be pure waste; this is the region the
// existing road network occupies, with margin to paint outside it.
constexpr int kBuildMinX = 800;
constexpr int kBuildMinY = 400;
constexpr int kBuildMaxX = 1700;
constexpr int kBuildMaxY = 1500;

float normalY(const TileVertex& v) { return v.kind < 0.5f ? 1.0f : -1.0f; }

} // namespace

TileRoadRenderer::~TileRoadRenderer() {
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

void TileRoadRenderer::rebuildAll(const RoadGrid& grid) {
    tileMesh_.clear();
    tileMesh_.build(grid, kBuildMinX, kBuildMinY, kBuildMaxX, kBuildMaxY);

    // Convert to the road vertex layout the street shader already understands.
    const std::vector<TileVertex>& src = tileMesh_.vertices();
    vertices_.clear();
    vertices_.reserve(src.size());
    for (const TileVertex& t : src) {
        RoadVertex v;
        v.position = glm::vec3(t.x, t.y, t.z);
        // A tile road surface always faces up. A curb vertex points DOWN (normal.y < 0)
        // purely as a flag: the lip and the top are vertical or up-facing, so lighting
        // would rather see the real normal, but this is the one spare channel that lets
        // phase 4 blend curb material without a new attribute. The vertex shader
        // normalises it for lighting, and a face-down quad would be culled anyway.
        v.normal = glm::vec3(0.0f, normalY(t), 0.0f);
        // uv.x carries the curb-to-centre gradient and is used by the marking bands,
        // which are switched off for tile roads (markMask 0), so the value is free to
        // be reused by phase 4.
        v.uv = glm::vec2(t.edge, 0.0f);
        v.roadType = (float)t.type;
        v.surfacePriority = 0.0f;
        v.markMask = 0.0f; // no lane paint on tile roads yet
        // Surface texture coordinates are derived from the world position in the shader
        // for tile roads, so these two fields are free: pack the edge gradient and the
        // baked AO here for phase 4 to read.
        v.metricUV = glm::vec2(t.edge, t.ao);
        vertices_.push_back(v);
    }
    indices_ = tileMesh_.indices();
    indexCount_ = (int)indices_.size();
    upload();
}

void TileRoadRenderer::update(const RoadGrid& grid) {
    // The data layer already tracks which chunks changed; nothing dirty means there is
    // nothing to do, so this is safe to call every frame.
    if (grid.dirtyChunks().empty()) return;

    const auto t0 = std::chrono::steady_clock::now();
    // Phase 3 rebuilds the whole visible region in one mesh. That is correct but not yet
    // cheap; per-block rebuilding is a phase 8 item and the block bookkeeping below is
    // what it will hang off.
    rebuildAll(grid);
    const auto t1 = std::chrono::steady_clock::now();
    lastBuildMs_ = std::chrono::duration<float, std::milli>(t1 - t0).count();

    std::cout << "[TileRoad] rebuilt: " << vertices_.size() << " verts, "
              << (indexCount_ / 3) << " tris, " << tileMesh_.surfaceQuads()
              << " surface quads, " << tileMesh_.curbQuads() << " curb quads, "
              << lastBuildMs_ << " ms" << std::endl;

    // Consume the dirty flags after a successful rebuild, so a failure retries.
    const_cast<RoadGrid&>(grid).clearDirty();
    ++blocksBuilt_;
}

void TileRoadRenderer::upload() {
    if (vao_ == 0) glGenVertexArrays(1, &vao_);
    if (vbo_ == 0) glGenBuffers(1, &vbo_);
    if (ebo_ == 0) glGenBuffers(1, &ebo_);
    if (vao_ == 0 || vbo_ == 0 || ebo_ == 0) {
        std::cerr << "[TileRoad] Failed to allocate GPU buffers" << std::endl;
        return;
    }

    glBindVertexArray(vao_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(vertices_.size() * sizeof(RoadVertex)),
                 vertices_.empty() ? nullptr : vertices_.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 (GLsizeiptr)(indices_.size() * sizeof(std::uint32_t)),
                 indices_.empty() ? nullptr : indices_.data(), GL_STATIC_DRAW);

    // Exactly the attribute locations RoadMeshBuilder uses, so the same street shader
    // serves both meshes with no second program.
    glEnableVertexAttribArray(0); // aPos
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RoadVertex),
                          (void *)offsetof(RoadVertex, position));
    glEnableVertexAttribArray(1); // aNormal
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(RoadVertex),
                          (void *)offsetof(RoadVertex, normal));
    glEnableVertexAttribArray(2); // aUV
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(RoadVertex),
                          (void *)offsetof(RoadVertex, uv));
    glEnableVertexAttribArray(3); // aType
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(RoadVertex),
                          (void *)offsetof(RoadVertex, roadType));
    glEnableVertexAttribArray(4); // aMarkMask
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(RoadVertex),
                          (void *)offsetof(RoadVertex, markMask));
    glEnableVertexAttribArray(5); // aSurfacePriority
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(RoadVertex),
                          (void *)offsetof(RoadVertex, surfacePriority));
    glEnableVertexAttribArray(6); // aMetricUV
    glVertexAttribPointer(6, 2, GL_FLOAT, GL_FALSE, sizeof(RoadVertex),
                          (void *)offsetof(RoadVertex, metricUV));

    glBindVertexArray(0);
}

void TileRoadRenderer::render(Shader& shader) const {
    if (vao_ == 0 || indexCount_ == 0) return;
    shader.use();
    // The tile mesh is already in world space, so the identity model is correct.
    shader.setMat4("model", glm::mat4(1.0f));

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    // Draw after the spline ribbons and bias towards the camera, so where the two
    // systems overlap while both exist the tile road wins the depth test instead of
    // z-fighting with it.
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-6.0f, -6.0f);

    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    glDisable(GL_POLYGON_OFFSET_FILL);
}

} // namespace roadgrid
