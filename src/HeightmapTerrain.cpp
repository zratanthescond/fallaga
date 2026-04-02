#include "HeightmapTerrain.h"
#include <stb_image.h>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <algorithm>
#include <iostream>

HeightmapTerrain::HeightmapTerrain(const char* heightmapPath,
                                   float worldMinX_, float worldMaxX_,
                                   float worldMinZ_, float worldMaxZ_,
                                   float minHeight_, float maxHeight_)
    : worldMinX(worldMinX_), worldMaxX(worldMaxX_),
      worldMinZ(worldMinZ_), worldMaxZ(worldMaxZ_),
      minHeight(minHeight_),  maxHeight(maxHeight_)
{
    if (!loadPNG(heightmapPath)) {
        std::cerr << "[HeightmapTerrain] FAILED to load: " << heightmapPath << std::endl;
        // Fallback: 4-vertex flat plane so we at least see something
        gridW = gridH = 2;
        heights = { minHeight_, minHeight_, minHeight_, minHeight_ };
    }
    buildMesh();
}

HeightmapTerrain::~HeightmapTerrain()
{
    if (VAO) glDeleteVertexArrays(1, &VAO);
    if (VBO) glDeleteBuffers(1, &VBO);
    if (EBO) glDeleteBuffers(1, &EBO);
    if (heightmapTex) glDeleteTextures(1, &heightmapTex);
}

bool HeightmapTerrain::loadPNG(const char* path)
{
    int w, h, ch;
    stbi_set_flip_vertically_on_load(false); // row 0 = top = North

    // Try 16-bit first
    unsigned short* data16 = stbi_load_16(path, &w, &h, &ch, 1);
    if (data16) {
        gridW = w; gridH = h;
        heights.resize(w * h);
        for (int i = 0; i < w * h; ++i)
            heights[i] = minHeight + (data16[i] / 65535.0f) * (maxHeight - minHeight);

        glGenTextures(1, &heightmapTex);
        glBindTexture(GL_TEXTURE_2D, heightmapTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R16, w, h, 0, GL_RED, GL_UNSIGNED_SHORT, data16);

        stbi_image_free(data16);
        std::cout << "[HeightmapTerrain] Loaded 16-bit: " << path
                  << " (" << w << "x" << h << ") elev "
                  << minHeight << " .. " << maxHeight << std::endl;
        return true;
    }

    // Fallback: load as 8-bit
    unsigned char* data8 = stbi_load(path, &w, &h, &ch, 1);
    if (!data8) return false;

    gridW = w; gridH = h;
    heights.resize(w * h);
    for (int i = 0; i < w * h; ++i)
        heights[i] = minHeight + (data8[i] / 255.0f) * (maxHeight - minHeight);

    // Upload as R8 texture, the ocean shader will still work
    glGenTextures(1, &heightmapTex);
    glBindTexture(GL_TEXTURE_2D, heightmapTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, data8);

    stbi_image_free(data8);
    std::cout << "[HeightmapTerrain] Loaded 8-bit: " << path
              << " (" << w << "x" << h << ") elev "
              << minHeight << " .. " << maxHeight << std::endl;
    return true;
}

float HeightmapTerrain::sampleNorm(float ux, float uz) const
{
    float fx = std::clamp(ux, 0.0f, 1.0f) * (gridW - 1);
    float fz = (1.0f - std::clamp(uz, 0.0f, 1.0f)) * (gridH - 1); // row0=North
    int x0 = (int)fx, x1 = std::min(x0+1, gridW-1);
    int z0 = (int)fz, z1 = std::min(z0+1, gridH-1);
    float tx = fx - x0, tz = fz - z0;
    return heights[z0*gridW+x0]*(1-tx)*(1-tz)
         + heights[z0*gridW+x1]*   tx *(1-tz)
         + heights[z1*gridW+x0]*(1-tx)*   tz
         + heights[z1*gridW+x1]*   tx *   tz;
}

float HeightmapTerrain::getHeightAt(float x, float z) const
{
    return sampleNorm((x - worldMinX) / (worldMaxX - worldMinX),
                      (z - worldMinZ) / (worldMaxZ - worldMinZ));
}

glm::vec3 HeightmapTerrain::getNormalAt(float x, float z) const
{
    const float e = 4.0f;
    float hL = getHeightAt(x-e, z), hR = getHeightAt(x+e, z);
    float hD = getHeightAt(x, z-e), hU = getHeightAt(x, z+e);
    // X increases East, Z increases North, Y is up
    // dH/dX = (hR-hL)/(2e), dH/dZ = (hU-hD)/(2e)
    // Normal = (-dH/dX, 1, -dH/dZ) then normalise
    glm::vec3 n = glm::normalize(glm::vec3(hL-hR, 2.0f*e, hD-hU));
    return (n.y < 0.0f) ? -n : n;
}

void HeightmapTerrain::buildMesh()
{
    // Use the full 512x512 resolution for the mesh so detail is not lost
    const int RW = gridW;
    const int RH = gridH;

    struct Vertex { glm::vec3 pos, normal; glm::vec2 uv; };
    std::vector<Vertex>       verts;
    std::vector<unsigned int> idx;
    verts.reserve(RW * RH);
    idx  .reserve((RW-1)*(RH-1)*6);

    // ── Step sizes in normalised UV space ───────────────────────────────
    float dux = 1.0f / (RW - 1);  // one pixel in U
    float duz = 1.0f / (RH - 1);  // one pixel in V

    for (int row = 0; row < RH; ++row) {
        for (int col = 0; col < RW; ++col) {
            float ux = col * dux;
            float uz = row * duz;

            float wx = worldMinX + ux * (worldMaxX - worldMinX);
            float wz = worldMinZ + uz * (worldMaxZ - worldMinZ);
            float wy = sampleNorm(ux, uz);

            // ── Accurate per-vertex normals via central differences ────
            // Sample 1 pixel away in UV space on each side
            float hL = sampleNorm(std::max(0.0f, ux - dux), uz);
            float hR = sampleNorm(std::min(1.0f, ux + dux), uz);
            // uz increases southward (row 0=North), so row-1=more-North=higher-Z
            // hD = sample at uz-duz = one row to the North (+Z direction)
            // hU = sample at uz+duz = one row to the South (-Z direction)
            float hNorth = sampleNorm(ux, std::min(1.0f, uz + duz)); // +Z
            float hSouth = sampleNorm(ux, std::max(0.0f, uz - duz)); // -Z

            float stepX = 2.0f * dux * (worldMaxX - worldMinX);
            float stepZ = 2.0f * duz * (worldMaxZ - worldMinZ);

            // Normal = (-dH/dX, scale, -dH/dZ) where scale normalises slope
            // dH/dX: hR-hL over stepX,  dH/dZ: hNorth-hSouth over stepZ
            glm::vec3 norm = glm::normalize(glm::vec3(
                (hL - hR) / stepX,   // -dH/dX
                1.0f,
                (hSouth - hNorth) / stepZ  // -dH/dZ
            ));
            if (norm.y < 0.0f) norm = -norm;

            verts.push_back({ glm::vec3(wx, wy, wz), norm, glm::vec2(ux, uz) });
        }
    }

    for (int row = 0; row < RH-1; ++row)
        for (int col = 0; col < RW-1; ++col) {
            unsigned int tl = row*RW+col, tr=tl+1, bl=tl+RW, br=bl+1;
            idx.push_back(tl); idx.push_back(bl); idx.push_back(tr);
            idx.push_back(tr); idx.push_back(bl); idx.push_back(br);
        }
    indexCount = (int)idx.size();

    glGenVertexArrays(1,&VAO); glGenBuffers(1,&VBO); glGenBuffers(1,&EBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER,VBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(Vertex), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size()*sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)offsetof(Vertex,pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)offsetof(Vertex,normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)offsetof(Vertex,uv));
    glBindVertexArray(0);

    std::cout << "[HeightmapTerrain] Mesh: " << RW << "x" << RH
              << ", " << indexCount/3 << " triangles" << std::endl;
}

void HeightmapTerrain::render() const
{
    if (!VAO || indexCount == 0) return;
    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}
