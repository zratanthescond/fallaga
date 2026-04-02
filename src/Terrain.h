#ifndef TERRAIN_H
#define TERRAIN_H

#pragma once
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include "ObjectModel.h"
#include "HeightmapTerrain.h"
#include "Shader.h"

struct Monument { char id; float x,z,w,h; const char* name; };
struct Street   { float x1,z1,x2,z2,width; };

// A named street polyline: chain of (x,z) points with a width
struct StreetPolyline {
    std::string name;
    float width;
    std::vector<glm::vec2> points; // (x, z) waypoints
};

class Terrain {
public:
    Shader* oceanShader   = nullptr;
    Shader* terrainShader = nullptr;

    Terrain();
    ~Terrain();

    void render(const glm::mat4& projection, const glm::mat4& view,
                const glm::vec3& camPos,     const glm::vec3& lightDir) const;

    void render(Shader& /*ignored*/,
                const glm::mat4& projection, const glm::mat4& view,
                const glm::vec3& camPos,     const glm::vec3& lightDir) const {
        render(projection, view, camPos, lightDir);
    }

    Shader& getShader()     const { return *oceanShader; }
    void    printDebugMap() const {}

    float     getHeight  (float x, float z) const;
    glm::vec3 getNormalAt(float x, float z) const;

    enum class Zone { SEA, PORT, MEDINA, WADI, VILLE_NOUVELLE, PLAIN, BEACH, KASBAH };
    Zone getZoneAt(float x, float z) const;
    bool pointInPolygon(float x, float z, const std::vector<glm::vec2>& polygon) const;

private:
    HeightmapTerrain* heightmapTerrain = nullptr;
    ObjModel* trainStationModel = nullptr;

    unsigned int waterVAO = 0, waterVBO = 0, waterEBO = 0;
    int          waterIndexCount = 0;
    unsigned int flatNormalTex   = 0;
    unsigned int mapTexture      = 0;  // 1899 Sousse map projected onto terrain

    // Street rendering
    unsigned int streetVAO = 0, streetVBO = 0;
    int          streetVertexCount = 0;
    Shader*      streetShader = nullptr;

    std::vector<Monument>       monuments;
    std::vector<Street>         streets;
    std::vector<StreetPolyline> streetPolylines;

    void buildWaterMesh();
    void initStreets();
    void buildStreetMesh();
    void renderStreets(const glm::mat4& proj, const glm::mat4& view,
                       const glm::vec3& lightDir) const;
    float distToSegment(float px, float pz,
                        float x1, float z1, float x2, float z2) const;
};

#endif