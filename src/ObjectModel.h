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

// Forward declarations
class Shader;

struct Material {
    std::string name;
    unsigned int textureID = 0;
    unsigned int normalMapID = 0;
    unsigned int specularMapID = 0;
    float ambient[4] = {0.2f, 0.2f, 0.2f, 1.0f}; // Ka
    float diffuse[4] = {0.8f, 0.8f, 0.8f, 1.0f}; // Kd
    float specular[4] = {0.0f, 0.0f, 0.0f, 1.0f}; // Ks
    float shininess = 32.0f; // Ns
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
    std::string name;       // Mesh name from FBX (e.g. "body", "FL_wheel")
    std::string nodeName;   // Owning FBX node name (often carries part identity)
    glm::vec3 minBound; // AABB Min
    glm::vec3 maxBound; // AABB Max
    unsigned int VAO = 0;
    unsigned int VBO = 0;
    unsigned int EBO = 0;
    unsigned int instanceVBO = 0; // VBO for Per-Instance Model Matrices
    unsigned int indexCount = 0;
    glm::mat4 nodeTransform = glm::mat4(1.0f); // Absolute transform from Blender node hierarchy
    mutable size_t lastUploadedSize = 0; // Track uploaded size to prevent redundant uploads
};

class ObjModel {
public:
    // **Declarations only! No {} body here.**
    ObjModel(const std::string& filename);
    ~ObjModel();

    void render() const; // Legacy (not recommended)
    void render(Shader& shader, unsigned int overrideTextureID = 0) const; // Modern
    void renderMesh(int meshIndex, Shader& shader) const; // Single mesh with per-mesh model matrix
    void renderInstanced(Shader& shader, const std::vector<glm::mat4>& instanceMatrices, bool forceUpdate = false) const; 
    int getMeshCount() const { return (int)meshes.size(); }
    std::string getMeshName(int index) const { return (index >= 0 && index < (int)meshes.size()) ? meshes[index].name : ""; }
    std::string getMeshNodeName(int index) const { return (index >= 0 && index < (int)meshes.size()) ? meshes[index].nodeName : ""; }
    glm::vec3 getMeshCenter(int index) const {
        if (index < 0 || index >= (int)meshes.size()) return glm::vec3(0.0f);
        return (meshes[index].minBound + meshes[index].maxBound) * 0.5f;
    }
    glm::vec3 getMeshMinBound(int index) const {
        return (index >= 0 && index < (int)meshes.size()) ? meshes[index].minBound : glm::vec3(0.0f);
    }
    glm::vec3 getMeshMaxBound(int index) const {
        return (index >= 0 && index < (int)meshes.size()) ? meshes[index].maxBound : glm::vec3(0.0f);
    }
    void getMinMaxY(float& minY, float& maxY) const;
    float getHeightAt(float x, float z) const;
    bool getNodeTransform(const std::string& name, glm::mat4& outTransform) const;
    const Material& getMaterial(unsigned int index) const {
        if (index >= materials.size()) {
            static Material fallbackMtl;
            return fallbackMtl;
        }
        return materials[index];
    }

    const aiNode* findNode(const std::string& name) const {
        if (!scene || !scene->mRootNode) return nullptr;
        return scene->mRootNode->FindNode(name.c_str());
    }

    glm::mat4 getNodeAbsoluteTransform(const aiNode* node) const {
        glm::mat4 transform(1.0f);
        const aiNode* curr = node;
        while (curr) {
            aiMatrix4x4 t = curr->mTransformation;
            glm::mat4 mat;
            mat[0][0] = t.a1; mat[0][1] = t.b1; mat[0][2] = t.c1; mat[0][3] = t.d1;
            mat[1][0] = t.a2; mat[1][1] = t.b2; mat[1][2] = t.c2; mat[1][3] = t.d2;
            mat[2][0] = t.a3; mat[2][1] = t.b3; mat[2][2] = t.c3; mat[2][3] = t.d3;
            mat[3][0] = t.a4; mat[3][1] = t.b4; mat[3][2] = t.c4; mat[3][3] = t.d4;
            transform = mat * transform;
            curr = curr->mParent;
        }
        return transform;
    }
    
    // Check const correctness here: 
    // Your CPP implementation of rayTriangleIntersect is CONST, so the declaration must be:
    bool rayTriangleIntersect(const Vec3& rayOrigin, const Vec3& rayDir,
                              const Vec3& v0, const Vec3& v1, const Vec3& v2,
                              float& outT) const;
                              
    bool checkSphereCollision(const glm::vec3& centerLocal, float radiusLocal, glm::vec3& outNormalLocal, float& outDepthLocal) const;
    
    // NEW: Collision test with automatic world-to-local space transform
    // This is the one to use! It handles all coordinate space conversions automatically.
    bool checkSphereCollisionWorld(
        const glm::vec3& centerWorld,        // Car position in world space
        float radiusWorld,                   // Car radius
        const glm::mat4& modelMatrix,        // Wall's model matrix
        glm::vec3& outNormalWorld,           // Output collision normal in world space
        float& outDepthWorld) const;         // Output penetration depth
                               
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
    glm::vec3 getNormalAt(float x, float z);
private:
    // Private declarations (must also match the .cpp file)
    void processMaterials();
    void processNode(aiNode* node);
    void setupDisplayList(); 
    void setupGPUBuffers(); // New modern approach
    void createFallbackCube();
    // Use GLuint for OpenGL ID if that is the correct type for your system/API
    void loadTexture(const std::string& textureFilename, unsigned int& textureID); 
    void loadTextureFromMemory(const aiTexture* embeddedTexture, unsigned int& textureID); 
    
    std::vector<Material> materials;
    std::string basepath; 
    std::string filename;
    std::vector<Mesh> meshes;
    unsigned int displayList;
    Assimp::Importer importer; 
    const aiScene* scene;
};
