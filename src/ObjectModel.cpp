#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "ObjectModel.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
#include <limits> // For numeric_limits
#include <cmath>  // For std::abs
#include <map>    // For std::map
#include "Vec3.h"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// ===============================
// Utility Functions
// ===============================

// Ray-triangle intersection (Möller–Trumbore)
bool ObjModel::rayTriangleIntersect(
     const Vec3& rayOrigin,
     const Vec3& rayDir,
     const Vec3& v0, 
     const Vec3& v1, 
     const Vec3& v2,
    float& outT) const {
    // A small value to prevent division by zero or floating-point errors
    const float EPSILON = 0.0000001f;

    // Calculate vectors for triangle edges
    Vec3 edge1 = { v1.x - v0.x, v1.y - v0.y, v1.z - v0.z };
    Vec3 edge2 = { v2.x - v0.x, v2.y - v0.y, v2.z - v0.z };

    // Calculate the cross product of the ray direction and edge2
    Vec3 h = { rayDir.y * edge2.z - rayDir.z * edge2.y,
               rayDir.z * edge2.x - rayDir.x * edge2.z,
               rayDir.x * edge2.y - rayDir.y * edge2.x };

    // Calculate the determinant
    float a = edge1.x * h.x + edge1.y * h.y + edge1.z * h.z;

    // Check if the ray is parallel to the triangle
    if (a > -EPSILON && a < EPSILON) {
        return false;
    }

    float f = 1.0f / a;
    Vec3 s = { rayOrigin.x - v0.x, rayOrigin.y - v0.y, rayOrigin.z - v0.z };

    // Calculate the barycentric coordinate u
    float u = f * (s.x * h.x + s.y * h.y + s.z * h.z);

    // Check if the intersection point is outside the triangle
    if (u < 0.0f || u > 1.0f) {
        return false;
    }

    // Calculate the cross product of s and edge1
    Vec3 q = { s.y * edge1.z - s.z * edge1.y,
               s.z * edge1.x - s.x * edge1.z,
               s.x * edge1.y - s.y * edge1.x };

    // Calculate the barycentric coordinate v
    float v = f * (rayDir.x * q.x + rayDir.y * q.y + rayDir.z * q.z);

    // Check if the intersection point is outside the triangle
    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }

    // Calculate the distance `t`
    float t = f * (edge2.x * q.x + edge2.y * q.y + edge2.z * q.z);

    // Check if the intersection is in front of the ray's origin
    if (t > EPSILON) {
        outT = t;
        return true;
    }

    return false;
}

void ObjModel::getMinMaxY(float& minY, float& maxY) const {
    // If the vector of vertices is empty, there is no data to check
    if (temp_vertices.empty()) {
        minY = 0.0f;
        maxY = 0.0f;
        return;
    }

    // Initialize minY and maxY with the first vertex's y-coordinate
    minY = temp_vertices[0].y;
    maxY = temp_vertices[0].y;

    // Iterate through the remaining vertices to find the true min and max
    for (size_t i = 1; i < temp_vertices.size(); ++i) {
        minY = std::min(minY, temp_vertices[i].y);
        maxY = std::max(maxY, temp_vertices[i].y);
    }
}

// Parse a vertex string "v/vt/vn"
void ObjModel::parseVertexString(const std::string& vertStr, int& v_idx, int& vt_idx, int& vn_idx) {
    std::stringstream vss(vertStr);
    std::string token;
    v_idx = vt_idx = vn_idx = -1;

    if (std::getline(vss, token, '/'))
        if (!token.empty()) v_idx = std::stoi(token) - 1;

    if (std::getline(vss, token, '/'))
        if (!token.empty()) vt_idx = std::stoi(token) - 1;

    if (std::getline(vss, token, '/'))
        if (!token.empty()) vn_idx = std::stoi(token) - 1;
}

// ===============================
// Texture Loading
// ===============================
void ObjModel::loadTexture(const std::string& textureFilename, GLuint& textureID) {
    if (textureFilename.empty()) {
        textureID = 0;
        return;
    }

    std::string fullPath = basepath + textureFilename;

    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    int width, height, nrChannels;
    unsigned char* data = stbi_load(fullPath.c_str(), &width, &height, &nrChannels, 0);

    if (data) {
        GLenum format = (nrChannels == 1) ? GL_RED : (nrChannels == 3 ? GL_RGB : GL_RGBA);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        std::cout << "Texture loaded successfully: " << fullPath << std::endl;
    } else {
        std::cerr << "Failed to load texture: " << fullPath << std::endl;
        textureID = 0;
    }
    stbi_image_free(data);
}

// ===============================
// MTL Loader
// ===============================
void ObjModel::loadMtl(const std::string& mtlFilename) {
    std::string fullPath = basepath + mtlFilename;
    std::ifstream file(fullPath);
    if (!file.is_open()) {
        std::cerr << "Failed to load MTL: " << fullPath << std::endl;
        return;
    }

    std::string line;
    Material currentMaterial;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "newmtl") {
            if (!currentMaterial.name.empty()) materials.push_back(currentMaterial);
            iss >> currentMaterial.name;
            currentMaterial.textureID = 0;
        } else if (prefix == "map_Kd") {
            std::string texturePath;
            iss >> texturePath;
            loadTexture(texturePath, currentMaterial.textureID);
        }
    }
    if (!currentMaterial.name.empty()) materials.push_back(currentMaterial);
}

// ===============================
// Normal Computation (Fix)
// ===============================
void ObjModel::computeVertexNormals(std::vector<Face>& faces) {
    std::vector<Vec3> vertexNormals(temp_vertices.size(), Vec3(0,0,0));

    for (auto& face : faces) {
        const Vec3& v0 = temp_vertices[face.v[0]];
        const Vec3& v1 = temp_vertices[face.v[1]];
        const Vec3& v2 = temp_vertices[face.v[2]];

        Vec3 edge1 = v1 - v0;
        Vec3 edge2 = v2 - v0;
        Vec3 normal = edge1.cross(edge2);
        normal.normalize();

        for (int i = 0; i < 3; i++) {
            vertexNormals[face.v[i]] = vertexNormals[face.v[i]] + normal;
            face.vn[i] = face.v[i]; // assign normal index = vertex index
        }
    }

    temp_normals.resize(temp_vertices.size());
    for (size_t i = 0; i < vertexNormals.size(); i++){
          Vec3 n=vertexNormals[i];
    n.normalize();
        temp_normals[i] = n;
    }
  
}

// ===============================
// OBJ Loader
// ===============================
ObjModel::ObjModel(const std::string& filename) : displayList(0) {
    std::cout << "Trying to load OBJ: " << filename << std::endl;

    size_t lastSlash = filename.find_last_of("/\\");
    basepath = (lastSlash == std::string::npos) ? "" : filename.substr(0, lastSlash + 1);

    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to load OBJ: " << filename << std::endl;
        createFallbackCube();
        return;
    }

    std::string line, mtlFile, currentMtlName;
    std::map<std::string, std::vector<Face>> materialFaces;

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "v") {
            Vec3 v; iss >> v.x >> v.y >> v.z; temp_vertices.push_back(v);
        } else if (prefix == "vn") {
            Vec3 n; iss >> n.x >> n.y >> n.z; temp_normals.push_back(n);
        } else if (prefix == "vt") {
            Vec2 t; iss >> t.u >> t.v; temp_texcoords.push_back(t);
        } else if (prefix == "mtllib") {
            iss >> mtlFile; loadMtl(mtlFile);
        } else if (prefix == "usemtl") {
            iss >> currentMtlName;
        } else if (prefix == "f") {
            Face f;
            for (int i = 0; i < 3; i++) {
                std::string vertStr; iss >> vertStr;
                int v_idx, vt_idx, vn_idx;
                parseVertexString(vertStr, v_idx, vt_idx, vn_idx);
                f.v[i]  = v_idx;
                f.vt[i] = vt_idx;
                f.vn[i] = vn_idx;
            }
            materialFaces[currentMtlName].push_back(f);
        }
    }

    std::cout << "Successfully loaded model with " << temp_vertices.size()
              << " vertices and " << materialFaces.size() << " materials." << std::endl;

    // ✅ Fix: compute normals if missing
    for (auto& [mtlName, faces] : materialFaces) {
        if (temp_normals.empty()) {
            std::cout << "No normals in OBJ, computing smooth normals...\n";
            computeVertexNormals(faces);
        }
    }

    setupBuffers(materialFaces);
}

ObjModel::~ObjModel()
{
    if (displayList) {
        glDeleteLists(displayList, 1);
        displayList = 0;
    }
}

// ===============================
// Rendering
// ===============================
void ObjModel::setupBuffers(const std::map<std::string, std::vector<Face>>& materialFaces) {
    displayList = glGenLists(1);
    glNewList(displayList, GL_COMPILE);

    glShadeModel(GL_SMOOTH); // ✅ enable smooth shading

    for (const auto& pair : materialFaces) {
        const auto& faces = pair.second;

        GLuint currentTextureID = 0;
        for (const auto& mtl : materials)
            if (mtl.name == pair.first) currentTextureID = mtl.textureID;

        if (currentTextureID) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, currentTextureID);
        } else {
            glDisable(GL_TEXTURE_2D);
        }

        glBegin(GL_TRIANGLES);
        for (const auto& face : faces) {
            for (int i = 0; i < 3; i++) {
                if (face.vn[i] != -1 && face.vn[i] < temp_normals.size()) {
                    const auto& n = temp_normals[face.vn[i]];
                    glNormal3f(n.x, n.y, n.z);
                }
                if (face.vt[i] != -1 && face.vt[i] < temp_texcoords.size()) {
                    const auto& t = temp_texcoords[face.vt[i]];
                    glTexCoord2f(t.u, t.v);
                }
                if (face.v[i] != -1 && face.v[i] < temp_vertices.size()) {
                    const auto& v = temp_vertices[face.v[i]];
                    glVertex3f(v.x, v.y, v.z);
                }
            }
        }
        glEnd();
    }

    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);

    glEndList();
}

void ObjModel::createFallbackCube()
{
    displayList = glGenLists(1);
    glNewList(displayList, GL_COMPILE);

    glBegin(GL_QUADS);
    // Front face
    glColor3f(1.0f, 0.0f, 0.0f); // Red
    glVertex3f(-1.0f, -1.0f,  1.0f);
    glVertex3f( 1.0f, -1.0f,  1.0f);
    glVertex3f( 1.0f,  1.0f,  1.0f);
    glVertex3f(-1.0f,  1.0f,  1.0f);
    // Back face
    glColor3f(0.0f, 1.0f, 0.0f); // Green
    glVertex3f(-1.0f, -1.0f, -1.0f);
    glVertex3f(-1.0f,  1.0f, -1.0f);
    glVertex3f( 1.0f,  1.0f, -1.0f);
    glVertex3f( 1.0f, -1.0f, -1.0f);
    // Left face
    glColor3f(0.0f, 0.0f, 1.0f); // Blue
    glVertex3f(-1.0f, -1.0f, -1.0f);
    glVertex3f(-1.0f, -1.0f,  1.0f);
    glVertex3f(-1.0f,  1.0f,  1.0f);
    glVertex3f(-1.0f,  1.0f, -1.0f);
    // Right face
    glColor3f(1.0f, 1.0f, 0.0f); // Yellow
    glVertex3f(1.0f, -1.0f, -1.0f);
    glVertex3f(1.0f,  1.0f, -1.0f);
    glVertex3f(1.0f,  1.0f,  1.0f);
    glVertex3f(1.0f, -1.0f,  1.0f);
    // Top face
    glColor3f(1.0f, 0.0f, 1.0f); // Magenta
    glVertex3f(-1.0f,  1.0f, -1.0f);
    glVertex3f(-1.0f,  1.0f,  1.0f);
    glVertex3f( 1.0f,  1.0f,  1.0f);
    glVertex3f( 1.0f,  1.0f, -1.0f);
    // Bottom face
    glColor3f(0.0f, 1.0f, 1.0f); // Cyan
    glVertex3f(-1.0f, -1.0f, -1.0f);
    glVertex3f( 1.0f, -1.0f, -1.0f);
    glVertex3f( 1.0f, -1.0f,  1.0f);
    glVertex3f(-1.0f, -1.0f,  1.0f);
    glEnd();

    glEndList();
}

void ObjModel::render() const {
    if (displayList) glCallList(displayList);
}

// ===============================
// Height Query
// ===============================
float ObjModel::getHeightAt(float x, float z) const {
    float maxHeight = -std::numeric_limits<float>::max();
    Vec3 rayOrigin = { x, 1000.0f, z };
    Vec3 rayDir = { 0.0f, -1.0f, 0.0f };

    for (const auto& face : temp_faces) {
        if (face.v[0] == -1 || face.v[1] == -1 || face.v[2] == -1) continue;

        const Vec3& v0 = temp_vertices[face.v[0]];
        const Vec3& v1 = temp_vertices[face.v[1]];
        const Vec3& v2 = temp_vertices[face.v[2]];

        float t = 0.0f;
        if (rayTriangleIntersect(rayOrigin, rayDir, v0, v1, v2, t)) {
            std::cout << "Intersection at t = " << t << std::endl;
            float intersectionY = rayOrigin.y + t * rayDir.y;
            if (intersectionY > maxHeight) maxHeight = intersectionY;
        }
    }
      std::cout << "Height at (" << x << ", " << z << ") = " << maxHeight << std::endl;
    return (maxHeight == -std::numeric_limits<float>::max()) ? 0.0f : maxHeight;
}
