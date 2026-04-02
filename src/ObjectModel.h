// --- ObjModel.h (File 2: Place this in C:\Users\honco\OneDrive\Bureau\fallaga\src\ObjectModel.h) ---

#pragma once

#include <string>
#include <vector>
#include <map>
#include <GL/glew.h>
#include "Vec3.h"
#include <glm/glm.hpp>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>

// Forward declarations are no longer needed since we include the full headers.

struct Material {
    std::string name;
    unsigned int textureID = 0;
    float ambient[4] = {0.2f, 0.2f, 0.2f, 1.0f}; // Ka
    float diffuse[4] = {0.8f, 0.8f, 0.8f, 1.0f}; // Kd
    float specular[4] = {0.0f, 0.0f, 0.0f, 1.0f}; // Ks
    float shininess = 0.0f; // Ns
};

// Simplified structs (keep only if actually used by legacy methods, otherwise remove)
struct Vec2 {
    float u, v;
    Vec2(float u = 0.0f, float v = 0.0f) : u(u), v(v) {}
};
struct Face {
    int v[3];   // Vertex indices
    int vt[3];  // Texture coordinate indices
    int vn[3];  // Normal indices
};


struct Mesh {
    unsigned int materialIndex;
    std::vector<unsigned int> indices; 
    const aiMesh* assimpMesh; 
    glm::vec3 minBound; // AABB Min
    glm::vec3 maxBound; // AABB Max
};

class ObjModel {
public:
    // **Declarations only! No {} body here.**
    ObjModel(const std::string& filename);
    ~ObjModel();

    void render() const;
    void getMinMaxY(float& minY, float& maxY) const;
    float getHeightAt(float x, float z) const;
    
    // Check const correctness here: 
    // Your CPP implementation of rayTriangleIntersect is CONST, so the declaration must be:
    bool rayTriangleIntersect(const Vec3& rayOrigin, const Vec3& rayDir,
                              const Vec3& v0, const Vec3& v1, const Vec3& v2,
                              float& outT) const;
                              
    // CHECK: This function is declared but implementation is missing. 
    // If you don't use it, remove this line to avoid future linker errors.
    void computeVertexNormals(const std::vector<Face>& faces); 
    
    // Member variables (keep these, as they are used by the .cpp functions)
    std::vector<Vec3> temp_vertices;
    std::vector<Vec3> temp_normals;
    std::vector<Vec2> temp_texcoords;
    std::vector<glm::vec3> vertices;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec3> colors;
    std::vector<float> alphas;
    std::vector<Face> temp_faces; 
    glm::vec3 ObjModel::getNormalAt(float x, float z);
private:
    // Private declarations (must also match the .cpp file)
    void processMaterials();
    void processNode(aiNode* node);
    void setupDisplayList(); 
    void createFallbackCube();
    // Use GLuint for OpenGL ID if that is the correct type for your system/API
    void loadTexture(const std::string& textureFilename, unsigned int& textureID); 
    
    std::vector<Material> materials;
    std::string basepath; 
    std::vector<Mesh> meshes;
    unsigned int displayList;
    Assimp::Importer importer; 
    const aiScene* scene;
};