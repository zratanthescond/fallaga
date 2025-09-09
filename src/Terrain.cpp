#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <GL/glut.h>
#include "Terrain.h"
#include "ObjectModel.h"
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iostream>

Terrain::Terrain() : treeModel(nullptr), rockModel(nullptr), terrainModel(nullptr) {
    srand(static_cast<unsigned>(time(nullptr)));
    
    // Create models by loading from files
    terrainModel = new ObjModel("assets/terrain/untitled.obj");
    treeModel = new ObjModel("assets/Tree_02/Tree.obj");
    rockModel = new ObjModel("assets/Rock1/Rock1.obj");

    // Generate random trees
    trees.reserve(20);
    for (int i = 0; i < 20; i++) {
        Tree t;
        t.x = static_cast<float>((rand() % 100) - 50);
        t.z = static_cast<float>((rand() % 100) - 50);
        t.y = getHeight(t.x, t.z);
        trees.push_back(t);
    }

    // Generate random rocks
    rocks.reserve(10);
    for (int i = 0; i < 10; i++) {
        Rock r;
        r.x = static_cast<float>((rand() % 100) - 50);
        r.z = static_cast<float>((rand() % 100) - 50);
        r.y = getHeight(r.x, r.z);
        r.size = static_cast<float>((rand() % 5 + 2) / 10.0f);
        rocks.push_back(r);
    }
}

Terrain::~Terrain() {
    delete treeModel;
    delete rockModel;
    delete terrainModel;
}

// NOTE: We no longer need this function. The rendering will happen in the `render()` method.
// void Terrain::createDisplayLists() {}

float Terrain::getHeight(float x, float z) const {
    ObjModel* model = terrainModel; // Use the member variable directly
    if (!model) {
        std::cerr << "Error: Terrain model not available for height check." << std::endl;
        return 0.0f;
    }

    // Get the maximum Y-coordinate of the terrain model to ensure the ray starts above it.
    float minY, maxY;
    model->getMinMaxY(minY, maxY);
    
    // Set the ray origin to be slightly above the highest point of the model.
    // This is a much more robust solution than a fixed value like 1000.0f.
    const float rayStartHeight = maxY + 1.0f; 
    Vec3 rayOrigin = { x, rayStartHeight, z };
    Vec3 rayDir = { 0.0f, -1.0f, 0.0f };

    float maxHeight = -std::numeric_limits<float>::max();
    float t = 0.0f;

    for (const auto& face : model->temp_faces) {
        // It's crucial to check that the indices are valid before accessing the vector.
        int v0_idx = face.v[0] - 1;
        int v1_idx = face.v[1] - 1;
        int v2_idx = face.v[2] - 1;

        if (v0_idx < 0 || v1_idx < 0 || v2_idx < 0 || 
            v0_idx >= model->temp_vertices.size() || v1_idx >= model->temp_vertices.size() || v2_idx >= model->temp_vertices.size()) {
            continue;
        }

        const Vec3& v0 = model->temp_vertices[v0_idx];
        const Vec3& v1 = model->temp_vertices[v1_idx];
        const Vec3& v2 = model->temp_vertices[v2_idx];

        if (model->rayTriangleIntersect(rayOrigin, rayDir, v0, v1, v2, t)) {
            float intersectionY = rayOrigin.y + t * rayDir.y;
            if (intersectionY > maxHeight) {
                maxHeight = intersectionY;
            }
        }
    }

    // Debug output to help you see the result
    std::cout << "Height at (" << x << ", " << z << ") = " << maxHeight << std::endl;
    
    return (maxHeight == -std::numeric_limits<float>::max()) ? 0.0f : maxHeight;
}

void Terrain::render() const {
    glPushMatrix();
  //  glTranslatef(0.0f, -1.5f, 0.0f);
   // glScalef(50.0f, 50.0f, 50.0f);
    terrainModel->render();
    glPopMatrix();
    
    for (const auto& t : trees) {
        glPushMatrix();
        glTranslatef(t.x, t.y, t.z);
        glScalef(1.0f, 1.0f, 1.0f);
        treeModel->render();
        glPopMatrix();
    }
    
    for (const auto& r : rocks) {
        glPushMatrix();
        glTranslatef(r.x, r.y, r.z);
        glScalef(r.size, r.size, r.size);
        rockModel->render();
        glPopMatrix();
    }
}