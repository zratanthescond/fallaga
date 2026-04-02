#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "Terrain.h"
#include <glm/gtc/matrix_transform.hpp>
#include "PhysicsSystem.h"
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <vector>
#include <algorithm>
#include <iostream>
#include <stb_image.h>

static constexpr float WATER_BASE = 1.5f;

static unsigned int buildNormalMap(int sz = 512)
{
    const float PI2 = 6.28318530718f;
    auto H = [&](float u, float v) {
        return 0.40f*sinf(u*PI2*2+v*PI2*1)  + 0.25f*sinf(u*PI2*3-v*PI2*4)
             + 0.15f*sinf(u*PI2*7+v*PI2*5)  + 0.10f*sinf(u*PI2*9-v*PI2*8)
             + 0.06f*sinf(u*PI2*13+v*PI2*11)+ 0.04f*sinf(u*PI2*19-v*PI2*17);
    };
    std::vector<unsigned char> px(sz*sz*3);
    float eps = 1.0f/sz;
    for (int y=0;y<sz;++y) for (int x=0;x<sz;++x) {
        float u=(float)x/sz, v=(float)y/sz;
        float dX=(H(u+eps,v)-H(u-eps,v))/(2*eps);
        float dZ=(H(u,v+eps)-H(u,v-eps))/(2*eps);
        float len=sqrtf(dX*dX+1+dZ*dZ);
        int i=(y*sz+x)*3;
        px[i+0]=(unsigned char)std::clamp((int)(128+127*(-dX/len)),0,255);
        px[i+1]=(unsigned char)std::clamp((int)(128+127*(1.0f/len)),0,255);
        px[i+2]=(unsigned char)std::clamp((int)(128+127*(-dZ/len)),0,255);
    }
    unsigned int tid;
    glGenTextures(1,&tid);
    glBindTexture(GL_TEXTURE_2D,tid);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,sz,sz,0,GL_RGB,GL_UNSIGNED_BYTE,px.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D,0);
    return tid;
}

Terrain::Terrain()
{
    terrainShader = new Shader("src/shaders/terrain.vert", "src/shaders/terrain.frag");
    oceanShader   = new Shader("src/shaders/ocean.vert",   "src/shaders/ocean.frag");
    streetShader  = new Shader("src/shaders/street.vert",  "src/shaders/street.frag");

    // NEW REAL-WORLD MAX HEIGHT FOR SOUSSE FROM DEM DATA = 59.9f
    heightmapTerrain = new HeightmapTerrain(
        "assets/terrain/heightmap.png",
        -1250.0f, 1250.0f,
        -2500.0f, 2500.0f,
         0.0f,    59.9f
    );

    trainStationModel = new ObjModel("assets/moniment/trainStation.obj");

    buildWaterMesh();
    flatNormalTex = buildNormalMap(512);
}

Terrain::~Terrain()
{
    if (waterVAO)   glDeleteVertexArrays(1, &waterVAO);
    if (waterVBO)   glDeleteBuffers(1, &waterVBO);
    if (waterEBO)   glDeleteBuffers(1, &waterEBO);
    if (streetVAO)  glDeleteVertexArrays(1, &streetVAO);
    if (streetVBO)  glDeleteBuffers(1, &streetVBO);
    delete heightmapTerrain;
    delete trainStationModel;
    delete terrainShader;
    delete oceanShader;
    delete streetShader;
}

void Terrain::initStreets() {}
void Terrain::buildStreetMesh() {}
void Terrain::renderStreets(const glm::mat4& proj, const glm::mat4& view, const glm::vec3& lightDir) const {}
float Terrain::distToSegment(float px, float pz, float x1, float z1, float x2, float z2) const { return 0.0f; }

void Terrain::buildWaterMesh()
{
    std::vector<glm::vec3> verts;
    std::vector<unsigned int> idx;
    int W = 180, H = 180;
    float startX = -1250.0f, endX = 1250.0f;
    float startZ = -2500.0f, endZ = 2500.0f;

    for(int i=0; i<H; ++i) {
        float fz = (float)i/(H-1);
        float z = startZ + fz*(endZ - startZ);
        for(int j=0; j<W; ++j) {
            float fx = (float)j/(W-1);
            float x = startX + fx*(endX - startX);
            verts.push_back({x, WATER_BASE, z});
        }
    }
    for(int i=0; i<H-1; ++i) {
        for(int j=0; j<W-1; ++j) {
            int tl=i*W+j, tr=tl+1, bl=tl+W, br=tl+W+1;
            idx.push_back(tl); idx.push_back(bl); idx.push_back(tr);
            idx.push_back(tr); idx.push_back(bl); idx.push_back(br);
        }
    }
    waterIndexCount = (int)idx.size();
    glGenVertexArrays(1, &waterVAO);
    glGenBuffers(1, &waterVBO);
    glGenBuffers(1, &waterEBO);
    glBindVertexArray(waterVAO);
    glBindBuffer(GL_ARRAY_BUFFER, waterVBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(glm::vec3), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, waterEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size()*sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glBindVertexArray(0);
}

void Terrain::render(const glm::mat4& proj, const glm::mat4& view, const glm::vec3& cam, const glm::vec3& light) const
{
    if (heightmapTerrain && terrainShader) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);

        terrainShader->use();
        terrainShader->setMat4("model",      glm::mat4(1.0f));
        terrainShader->setMat4("view",       view);
        terrainShader->setMat4("projection", proj);
        terrainShader->setVec3("lightDir",   light);
        terrainShader->setVec3("viewPos",    cam);
        terrainShader->setFloat("minHeight", heightmapTerrain->minHeight);
        terrainShader->setFloat("maxHeight", heightmapTerrain->maxHeight);

        heightmapTerrain->render();
        glUseProgram(0);
    }

    if (trainStationModel) {
        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
        glEnable(GL_NORMALIZE);
        glDisable(GL_COLOR_MATERIAL);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        
        glPushMatrix();
        static float stationY = -1e9;
        float tx = -73.0f, tz = 247.0f;
        if (stationY < -1e8) {
            Vec3 pos(tx, 0, tz);
            PhysicsSystem::snapToGround(pos, this, 0.1f);
            stationY = pos.y;
        }
        glTranslatef(tx, stationY, tz);
        glScalef(20.0f, 20.0f, 20.0f);
        trainStationModel->render();
        glPopMatrix();

        glDisable(GL_NORMALIZE);
        glEnable(GL_COLOR_MATERIAL);
    }

    if (!waterVAO || !oceanShader) return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    oceanShader->use();
    oceanShader->setMat4 ("projection", proj);
    oceanShader->setMat4 ("view",       view);
    oceanShader->setMat4 ("model",      glm::mat4(1.0f));
    oceanShader->setVec3 ("viewPos",    cam);
    oceanShader->setVec3 ("lightDir",   light);
    oceanShader->setFloat("time",       (float)glfwGetTime());
    oceanShader->setFloat("waveBase",   WATER_BASE);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, flatNormalTex);
    oceanShader->setInt("normalMap", 0);

    if (heightmapTerrain) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, heightmapTerrain->getTextureID());
        oceanShader->setInt("heightmapTex", 1);
        oceanShader->setFloat("minElev", heightmapTerrain->minHeight);
        oceanShader->setFloat("maxElev", heightmapTerrain->maxHeight);
        oceanShader->setFloat("worldMinX", heightmapTerrain->worldMinX);
        oceanShader->setFloat("worldMaxX", heightmapTerrain->worldMaxX);
        oceanShader->setFloat("worldMinZ", heightmapTerrain->worldMinZ);
        oceanShader->setFloat("worldMaxZ", heightmapTerrain->worldMaxZ);
        oceanShader->setInt("useHeightmap", 1);
    } else {
        oceanShader->setInt("useHeightmap", 0);
    }

    glBindVertexArray(waterVAO);
    glDrawElements(GL_TRIANGLES, waterIndexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glUseProgram(0);
}

float Terrain::getHeight(float x, float z) const
{
    float h = heightmapTerrain ? heightmapTerrain->getHeightAt(x, z) : 0.0f;
    return std::max(h, WATER_BASE);
}

glm::vec3 Terrain::getNormalAt(float x, float z) const
{
    if (heightmapTerrain) return heightmapTerrain->getNormalAt(x, z);
    return glm::vec3(0,1,0);
}

Terrain::Zone Terrain::getZoneAt(float x, float z) const { return Zone::PLAIN; }
bool Terrain::pointInPolygon(float x, float z, const std::vector<glm::vec2>& polygon) const { return false; }