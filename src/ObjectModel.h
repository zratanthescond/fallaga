#pragma once
#include <string>
#include <vector>
#include <map>
#include <GL/glew.h>
#include "Vec3.h"

// Simplified structs for reading OBJ files
struct Vec2 {
    float u, v;
    Vec2(float u = 0.0f, float v = 0.0f) : u(u), v(v) {}
};

struct Face {
    int v[3];   // Vertex indices
    int vt[3];  // Texture coordinate indices
    int vn[3];  // Normal indices
};

// New Material structure to hold properties from the MTL file
struct Material {
    std::string name;
    GLuint textureID = 0; // OpenGL texture ID
};

class ObjModel {
public:
    ObjModel(const std::string& filename);
    ~ObjModel();

    void render() const;
    void getMinMaxY(float& minY, float& maxY) const;
    bool rayTriangleIntersect(const Vec3& rayOrigin, const Vec3& rayDir,
                          const Vec3& v0, const Vec3& v1, const Vec3& v2,
                          float& outT) const;
    // Method to get the height of the terrain at a given (x, z) coordinate
    float getHeightAt(float x, float z) const;
   void computeVertexNormals(std::vector<Face>& faces);
    std::vector<Vec3> temp_vertices;
    std::vector<Vec3> temp_normals;
    std::vector<Vec2> temp_texcoords;
    
    // Store faces temporarily, but now they are grouped by material in the constructor
    std::vector<Face> temp_faces;  // New function to compute normals if missing
private:
    // This function now needs to accept the map of faces to materials
    void setupBuffers(const std::map<std::string, std::vector<Face>>& materialFaces);
    void createFallbackCube();

    // Data read from the OBJ file
   
    
    // Data structures for materials and textures
    std::vector<Material> materials;
    std::string basepath; // Store the directory of the OBJ file

    // Functions for loading materials and textures
    void loadTexture(const std::string& textureFilename, GLuint& textureID);
    void loadMtl(const std::string& mtlFilename);
    void parseVertexString(const std::string& vertStr, int& v_idx, int& vt_idx, int& vn_idx); // New helper function
   /// void computeVertexNormals(const std::vector<Face>& faces); // New function to compute normals if missing
    // Legacy OpenGL Display List
    GLuint displayList;
};