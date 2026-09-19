#include "ObjectModel.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <limits>
#include <cmath>
#include <map>
#include <assimp/Exporter.hpp>
#define GLM_ENABLE_EXPERIMENTAL
// 🌟 FIX: STB_IMAGE_IMPLEMENTATION moved to stb_impl.cpp 🌟
#include <stb_image.h>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h> 
#include <assimp/config.h>
#include "Vec3.h"
#include "Shader.h"
#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp> // For glm::normalize
bool ObjModel::rayTriangleIntersect(
     const Vec3& rayOrigin,
     const Vec3& rayDir,
     const Vec3& v0, 
     const Vec3& v1, 
     const Vec3& v2,
     float& outT) const {
     const float EPSILON = 0.0000001f;

     Vec3 edge1 = v1 - v0;
     Vec3 edge2 = v2 - v0;

     Vec3 h = rayDir.cross(edge2); 

     float a = edge1.dot( h);

     if (a > -EPSILON && a < EPSILON) { return false; }

     float f = 1.0f / a;
     Vec3 s = rayOrigin - v0;

     float u = f * s.dot( h);

     if (u < 0.0f || u > 1.0f) { return false; }

     Vec3 q = s.cross( edge1);

     float v = f * rayDir.dot( q);

     if (v < 0.0f || u + v > 1.0f) { return false; }

     float t = f * edge2.dot( q);

     if (t > EPSILON) {
         outT = t;
         return true;
     }

     return false;
}

// FIXED: Iterates over all vertices in all meshes for min/max Y.
void ObjModel::getMinMaxY(float& minY, float& maxY) const {
    if (meshes.empty()) {
        minY = 0.0f;
        maxY = 0.0f;
        return;
    }

    minY = std::numeric_limits<float>::max();
    maxY = -std::numeric_limits<float>::max();

    for (const auto& mesh : meshes) {
        const aiMesh* aimesh = mesh.assimpMesh;
        if (!aimesh || !aimesh->HasPositions()) continue;

        for (unsigned int i = 0; i < aimesh->mNumVertices; ++i) {
            float y = aimesh->mVertices[i].y;
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
        }
    }
    
    // Safety check for models with zero vertices in all meshes
    if (minY == std::numeric_limits<float>::max()) {
        minY = 0.0f;
        maxY = 0.0f;
    }
}

// ===============================
// Texture Loading (Kept)
// ===============================
void ObjModel::loadTexture(const std::string& textureFilename, unsigned int& textureID) {
    if (textureFilename.empty()) {
        textureID = 0;
        return;
    }

    // Try primary path
    std::string path1 = basepath + textureFilename;
    int width, height, nrChannels;
    unsigned char* data = stbi_load(path1.c_str(), &width, &height, &nrChannels, 0);
    std::string loadedPath = path1;

    // Fallback: extract basename and look in local "textures/" or model base directory
    if (!data) {
        std::string basename = textureFilename;
        size_t lastSlashPos = textureFilename.find_last_of("/\\");
        if (lastSlashPos != std::string::npos) {
            basename = textureFilename.substr(lastSlashPos + 1);
        }
        
        std::string path2 = basepath + "textures/" + basename;
        data = stbi_load(path2.c_str(), &width, &height, &nrChannels, 0);
        loadedPath = path2;

        if (!data) {
            std::string path3 = basepath + basename;
            data = stbi_load(path3.c_str(), &width, &height, &nrChannels, 0);
            loadedPath = path3;
        }
    }

    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (data) {
        GLenum format = (nrChannels == 1) ? GL_RED : (nrChannels == 3 ? GL_RGB : GL_RGBA);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        std::cout << "Texture loaded successfully: " << loadedPath << std::endl;
    } else {
        std::cerr << "Failed to load texture (tried fallbacks): " << textureFilename << std::endl;
        textureID = 0;
    }
    stbi_image_free(data);
}

void ObjModel::loadTextureFromMemory(const aiTexture* embeddedTexture, unsigned int& textureID) {
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    int width, height, nrChannels;
    unsigned char* data = nullptr;

    if (embeddedTexture->mHeight == 0) {
        // Compressed format (png, jpg) - most common for FBX
        data = stbi_load_from_memory(reinterpret_cast<unsigned char*>(embeddedTexture->pcData), 
                                     embeddedTexture->mWidth, &width, &height, &nrChannels, 0);
    } else {
        // Uncompressed format (raw RGBA)
        data = stbi_load_from_memory(reinterpret_cast<unsigned char*>(embeddedTexture->pcData), 
                                     embeddedTexture->mWidth * embeddedTexture->mHeight, &width, &height, &nrChannels, 0);
    }

    if (data) {
        GLenum format = (nrChannels == 1) ? GL_RED : (nrChannels == 3 ? GL_RGB : GL_RGBA);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        std::cout << "Embedded texture loaded successfully from memory, channels: " << nrChannels << std::endl;
        stbi_image_free(data);
    } else {
        std::cerr << "Failed to load embedded texture from memory." << std::endl;
        textureID = 0;
    }
}


// ===============================
// Material Processing (FIXED: Added color extraction)
// ===============================
void ObjModel::processMaterials() {
    std::cout << "[ObjModel] processMaterials for " << basepath << ": materials=" << scene->mNumMaterials << ", embeddedTextures=" << scene->mNumTextures << std::endl;
    materials.resize(scene->mNumMaterials);

    for (unsigned int i = 0; i < scene->mNumMaterials; i++) {
        aiMaterial* material = scene->mMaterials[i];
        Material& currentMtl = materials[i]; // Use a reference to easily update 
        
        // 1. Get Material Name
        aiString name;
        if (material->Get(AI_MATKEY_NAME, name) == AI_SUCCESS) {
            currentMtl.name = name.C_Str();
        } else {
            currentMtl.name = "default_material_" + std::to_string(i);
        }
        
        // 2. Get and Load Texture (Diffuse Map)
        aiString texturePath;
        if (material->GetTexture(aiTextureType_DIFFUSE, 0, &texturePath) == AI_SUCCESS) {
            std::cout << "  Material " << i << " (" << currentMtl.name << ") has diffuse texture: " << texturePath.C_Str() << std::endl;
            const aiTexture* embeddedTexture = scene->GetEmbeddedTexture(texturePath.C_Str());
            if (embeddedTexture) {
                std::cout << "  Detected EMBEDDED texture in ObjModel. Loading from memory..." << std::endl;
                loadTextureFromMemory(embeddedTexture, currentMtl.textureID);
            } else {
                loadTexture(texturePath.C_Str(), currentMtl.textureID); 
            }
        } else {
            currentMtl.textureID = 0;
        }

        // 3. Extract Material Colors and Properties (NEW)
        aiColor3D color;
        float fValue;
        
        // Ambient Color (Ka)
        if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_AMBIENT, color)) {
            currentMtl.ambient[0] = color.r;
            currentMtl.ambient[1] = color.g;
            currentMtl.ambient[2] = color.b;
        }

        // Diffuse Color (Kd)
        if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_DIFFUSE, color)) {
            currentMtl.diffuse[0] = color.r;
            currentMtl.diffuse[1] = color.g;
            currentMtl.diffuse[2] = color.b;
        }

        // Specular Color (Ks)
        if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_SPECULAR, color)) {
            currentMtl.specular[0] = color.r;
            currentMtl.specular[1] = color.g;
            currentMtl.specular[2] = color.b;
        }

        // Shininess (Ns)
        if (AI_SUCCESS == material->Get(AI_MATKEY_SHININESS, fValue)) {
            currentMtl.shininess = fValue;
            // Assimp uses "power of 2" shininess, but OpenGL uses 0-128. 
            // We'll cap it just in case, though 0-100 is common for .mtl.
            currentMtl.shininess = std::min(currentMtl.shininess, 128.0f);
        }
    }
}
glm::vec3 ObjModel::getNormalAt(float x, float z) {
    
    // --- Sample Points ---
    // Use a small offset to sample the height change around the point (x, z)
    // Adjust 'SAMPLE_OFFSET' based on the scale and resolution of your terrain grid.
    const float SAMPLE_OFFSET = 0.1f; 

    // 1. Get the height at the surrounding points
    // NOTE: This assumes you have a public getHeightAt(float x, float z) method.
    float hL = getHeightAt(x - SAMPLE_OFFSET, z); // Left
    float hR = getHeightAt(x + SAMPLE_OFFSET, z); // Right
    float hD = getHeightAt(x, z - SAMPLE_OFFSET); // Down (Back)
    float hU = getHeightAt(x, z + SAMPLE_OFFSET); // Up (Forward)

    // 2. Create the two tangent vectors that define the slope at (x, z)
    
    // Vector pointing from Left to Right (Tangent on X-axis)
    // The Y-component represents the height difference.
    glm::vec3 tangentX = glm::vec3(2.0f * SAMPLE_OFFSET, hR - hL, 0.0f); 

    // Vector pointing from Back to Forward (Tangent on Z-axis)
    // The Y-component represents the height difference.
    glm::vec3 tangentZ = glm::vec3(0.0f, hU - hD, 2.0f * SAMPLE_OFFSET);

    // 3. Calculate the normal vector (Normal is perpendicular to both tangents)
    // The order of the cross product matters for the direction (Up vs. Down).
    // Using cross(tangentX, tangentZ) gives the standard upward-pointing normal.
    glm::vec3 normal = glm::cross(tangentX, tangentZ);

    // 4. Normalize the result
    if (glm::length2(normal) > 1e-6) { // Safety check against a zero vector (perfectly flat area)
        return glm::normalize(normal);
    }

    // Default to a vertical normal if the area is perfectly flat or calculation failed
    return glm::vec3(0.0f, 1.0f, 0.0f);
}
// ===============================
// Scene Traversal (Kept)
// ===============================
void ObjModel::processNode(aiNode* node) {
    // Process all the node's meshes
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        
        Mesh newMesh;
        newMesh.assimpMesh = mesh;
        newMesh.materialIndex = mesh->mMaterialIndex;
        newMesh.name = mesh->mName.C_Str();

        // Calculate AABB for this mesh
        newMesh.minBound = glm::vec3(std::numeric_limits<float>::max());
        newMesh.maxBound = glm::vec3(-std::numeric_limits<float>::max());
        if (mesh->HasPositions()) {
            for (unsigned int j = 0; j < mesh->mNumVertices; j++) {
                newMesh.minBound.x = std::min(newMesh.minBound.x, mesh->mVertices[j].x);
                newMesh.minBound.y = std::min(newMesh.minBound.y, mesh->mVertices[j].y);
                newMesh.minBound.z = std::min(newMesh.minBound.z, mesh->mVertices[j].z);
                newMesh.maxBound.x = std::max(newMesh.maxBound.x, mesh->mVertices[j].x);
                newMesh.maxBound.y = std::max(newMesh.maxBound.y, mesh->mVertices[j].y);
                newMesh.maxBound.z = std::max(newMesh.maxBound.z, mesh->mVertices[j].z);
            }
        }

        // Extract indices (faces) from Assimp's face structure
        for (unsigned int j = 0; j < mesh->mNumFaces; j++) {
            aiFace face = mesh->mFaces[j];
            for (unsigned int k = 0; k < face.mNumIndices; k++) {
                newMesh.indices.push_back(face.mIndices[k]);
            }
        }
        
        meshes.push_back(newMesh);
    }
    
    // Recurse for all children
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        processNode(node->mChildren[i]);
    }
}


// ===============================
// Model Loader (Main Constructor)
// ===============================
ObjModel::ObjModel(const std::string& filename) : scene(nullptr), displayList(0), filename(filename) {
    std::cout << "Trying to load model with Assimp: " << filename << std::endl;
    std::cout << "Calling Assimp::Importer::ReadFile..." << std::endl;

    size_t lastSlash = filename.find_last_of("/\\");
    basepath = (lastSlash == std::string::npos) ? "" : filename.substr(0, lastSlash + 1);
 // importer.SetPropertyString(AI_CONFIG_PP_LMW_MATERIAL_PATH, basepath);
    scene = importer.ReadFile(filename, 
        aiProcess_Triangulate |      // Mandatory: ensures all faces are triangles
        aiProcess_GenSmoothNormals | // Generates smooth normals if none are provided
        aiProcess_CalcTangentSpace | // Calculates tangents/bitangents
        aiProcess_FlipUVs           // Useful for most OpenGL applications
        // aiProcess_PreTransformVertices   // REMOVED: Too slow for large terrains
    );
    std::cout << "Assimp::Importer::ReadFile finished." << std::endl;
    
    // Check for errors
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cerr << "ASSIMP FAILED TO LOAD: " << importer.GetErrorString() << std::endl;
        createFallbackCube();
        return;
    }
    
    // 1. Process Materials/Textures/Colors
    std::cout << "Processing materials..." << std::endl;
    processMaterials();
    std::cout << "Materials processed." << std::endl;
    
    // 2. Process Nodes/Meshes
    std::cout << "Processing nodes..." << std::endl;
    processNode(scene->mRootNode);
    std::cout << "Nodes processed." << std::endl;

    // 3. REMOVED: Inefficient temporary vertex list population
    // The physics/raycasting now iterates over the mesh data directly.

    std::cout << "Successfully loaded model with Assimp. Total meshes: " << meshes.size() << std::endl;

    // 4. Setup OpenGL Display List
    setupDisplayList();

    // 5. Setup GPU Buffers (VBO/VAO)
    setupGPUBuffers();
}

ObjModel::~ObjModel()
{
    if (displayList) {
        glDeleteLists(displayList, 1);
        displayList = 0;
    }
    for (auto& mesh : meshes) {
        if (mesh.VBO) { glDeleteBuffers(1, &mesh.VBO); }
        if (mesh.EBO) { glDeleteBuffers(1, &mesh.EBO); }
        if (mesh.instanceVBO) { glDeleteBuffers(1, &mesh.instanceVBO); }
        if (mesh.VAO) { glDeleteVertexArrays(1, &mesh.VAO); }
    }
}

// ===============================
// Rendering (FIXED: Applies material colors)
// ===============================
void ObjModel::setupDisplayList() {
    displayList = glGenLists(1);
    glNewList(displayList, GL_COMPILE);

    // --- FIX: Force Opaque State ---
    glDisable(GL_BLEND);         // Ensure transparency is OFF for the terrain
    glEnable(GL_DEPTH_TEST);     // Ensure the depth buffer is used correctly
    glDepthMask(GL_TRUE);        // Allow writing to the depth buffer

    glShadeModel(GL_SMOOTH); 

    for (const auto& mesh : meshes) {
        const Material& mtl = getMaterial(mesh.materialIndex);

        glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mtl.ambient);
        glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mtl.diffuse);
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mtl.specular);
        glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, mtl.shininess);

        if (mtl.textureID) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, mtl.textureID);
            
            // --- FIX: Force Texture to be Opaque ---
            // GL_REPLACE ignores the underlying vertex color/alpha
            // Use GL_MODULATE if you want lighting to affect the texture
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        } else {
            glDisable(GL_TEXTURE_2D);
        }

        glBegin(GL_TRIANGLES);
        const aiMesh* aimesh = mesh.assimpMesh;
        for (unsigned int index : mesh.indices) {
            if (aimesh->HasNormals()) {
                const aiVector3D& n = aimesh->mNormals[index];
                glNormal3f(n.x, n.y, n.z);
            }
            if (aimesh->HasTextureCoords(0)) {
                const aiVector3D& t = aimesh->mTextureCoords[0][index]; 
                glTexCoord2f(t.x, t.y); 
            }
            const aiVector3D& v = aimesh->mVertices[index];
            glVertex3f(v.x, v.y, v.z);
        }
        glEnd();
    }

    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);

    glEndList();
}

void ObjModel::createFallbackCube()
{
    // ... (Fallback cube code: kept as is, but glColor is less important if using lighting) ...
    displayList = glGenLists(1);
    glNewList(displayList, GL_COMPILE);

    // Apply a default material for the fallback cube
    GLfloat fallbackAmbient[] = {0.1f, 0.1f, 0.1f, 1.0f};
    GLfloat fallbackSpecular[] = {0.5f, 0.5f, 0.5f, 1.0f};
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, fallbackAmbient);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, fallbackSpecular);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 32.0f);


    glBegin(GL_QUADS);
    // Front face
    glNormal3f(0.0f, 0.0f, 1.0f);
    glColor3f(1.0f, 0.0f, 0.0f); // Red (Diffuse fallback if GL_COLOR_MATERIAL is on)
    glVertex3f(-1.0f, -1.0f,1.0f);
    glVertex3f( 1.0f, -1.0f,1.0f);
    glVertex3f( 1.0f,1.0f,1.0f);
    glVertex3f(-1.0f,1.0f,1.0f);
    // Back face
    glNormal3f(0.0f, 0.0f, -1.0f);
    glColor3f(0.0f, 1.0f, 0.0f); // Green
    glVertex3f(-1.0f, -1.0f, -1.0f);
    glVertex3f(-1.0f,1.0f, -1.0f);
    glVertex3f( 1.0f,1.0f, -1.0f);
    glVertex3f( 1.0f, -1.0f, -1.0f);
    // Left face
    glNormal3f(-1.0f, 0.0f, 0.0f);
    glColor3f(0.0f, 0.0f, 1.0f); // Blue
    glVertex3f(-1.0f, -1.0f, -1.0f);
    glVertex3f(-1.0f, -1.0f,1.0f);
    glVertex3f(-1.0f,1.0f,1.0f);
    glVertex3f(-1.0f,1.0f, -1.0f);
    // Right face
    glNormal3f(1.0f, 0.0f, 0.0f);
    glColor3f(1.0f, 1.0f, 0.0f); // Yellow
    glVertex3f(1.0f, -1.0f, -1.0f);
    glVertex3f(1.0f,1.0f, -1.0f);
    glVertex3f(1.0f,1.0f,1.0f);
    glVertex3f(1.0f, -1.0f,1.0f);
    // Top face
    glNormal3f(0.0f, 1.0f, 0.0f);
    glColor3f(1.0f, 0.0f, 1.0f); // Magenta
    glVertex3f(-1.0f,1.0f, -1.0f);
    glVertex3f(-1.0f,1.0f,1.0f);
    glVertex3f( 1.0f,1.0f,1.0f);
    glVertex3f( 1.0f,1.0f, -1.0f);
    // Bottom face
    glNormal3f(0.0f, -1.0f, 0.0f);
    glColor3f(0.0f, 1.0f, 1.0f); // Cyan
    glVertex3f(-1.0f, -1.0f, -1.0f);
    glVertex3f( 1.0f, -1.0f, -1.0f);
    glVertex3f( 1.0f, -1.0f,1.0f);
    glVertex3f(-1.0f, -1.0f,1.0f);
    glEnd();

    glEndList();
}


void ObjModel::render() const {
    if (displayList) glCallList(displayList);
}

// ===============================
// Height Query (Kept the correct Assimp adaptation)
// ===============================
float ObjModel::getHeightAt(float x, float z) const {
    float maxHeight = -std::numeric_limits<float>::max();
    Vec3 rayOrigin = { x, 1000.0f, z };
    Vec3 rayDir = { 0.0f, -1.0f, 0.0f };
    
    // Iterate over ALL meshes
    for (const auto& mesh : meshes) {
        // Optimization: Skip meshes whose AABB doesn't contain (x, z)
        if (x < mesh.minBound.x || x > mesh.maxBound.x || z < mesh.minBound.z || z > mesh.maxBound.z) {
            continue;
        }

        const aiMesh* aimesh = mesh.assimpMesh;
        if (!aimesh || !aimesh->HasPositions()) continue;

        // Iterate through indices 3 at a time (since they are guaranteed to be triangles)
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            unsigned int idx0 = mesh.indices[i];
            unsigned int idx1 = mesh.indices[i+1];
            unsigned int idx2 = mesh.indices[i+2];

            const aiVector3D& av0 = aimesh->mVertices[idx0];
            const aiVector3D& av1 = aimesh->mVertices[idx1];
            const aiVector3D& av2 = aimesh->mVertices[idx2];
            
            Vec3 v0 = {av0.x, av0.y, av0.z};
            Vec3 v1 = {av1.x, av1.y, av1.z};
            Vec3 v2 = {av2.x, av2.y, av2.z};

            float t = 0.0f;
            if (rayTriangleIntersect(rayOrigin, rayDir, v0, v1, v2, t)) {
                float intersectionY = rayOrigin.y + t * rayDir.y;
                if (intersectionY > maxHeight) {
                    maxHeight = intersectionY;
                }
            }
        }
    }
    
    return (maxHeight == -std::numeric_limits<float>::max()) ? 0.0f : maxHeight;
}

// ===============================
// Modern Shader-Based Rendering
// ===============================
void ObjModel::render(Shader& shader, unsigned int overrideTextureID) const {
    for (const auto& mesh : meshes) {
        const Material& mtl = getMaterial(mesh.materialIndex);
        
        shader.setVec3("diffuseColor", glm::vec3(mtl.diffuse[0], mtl.diffuse[1], mtl.diffuse[2]));
        
        unsigned int texID = overrideTextureID ? overrideTextureID : mtl.textureID;
        if (texID) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texID);
            shader.setInt("texture_diffuse1", 0);
            shader.setBool("hasTexture", true);
        } else {
            shader.setBool("hasTexture", false);
        }
        
        if (mtl.normalMapID) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, mtl.normalMapID);
            shader.setInt("texture_normal1", 1);
            shader.setBool("hasNormalMap", true);
        } else {
            shader.setBool("hasNormalMap", false);
        }
        
        // Use GPU buffers if available, else fallback to display list per-mesh
        if (mesh.VAO != 0) {
            glBindVertexArray(mesh.VAO);
            glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
        } else {
            // Fallback: immediate mode per mesh
            const aiMesh* aimesh = mesh.assimpMesh;
            if (!aimesh) continue;
            
            glBegin(GL_TRIANGLES);
            for (unsigned int index : mesh.indices) {
                if (aimesh->HasNormals()) {
                    const aiVector3D& n = aimesh->mNormals[index];
                    glNormal3f(n.x, n.y, n.z);
                }
                if (aimesh->HasTextureCoords(0)) {
                    const aiVector3D& t = aimesh->mTextureCoords[0][index];
                    glTexCoord2f(t.x, t.y);
                }
                const aiVector3D& v = aimesh->mVertices[index];
                glVertex3f(v.x, v.y, v.z);
            }
            glEnd();
        }
    }
}

void ObjModel::renderMesh(int meshIndex, Shader& shader) const {
    if (meshIndex < 0 || meshIndex >= (int)meshes.size()) return;
    const auto& mesh = meshes[meshIndex];
    const Material& mtl = getMaterial(mesh.materialIndex);
    
    shader.setVec3("diffuseColor", glm::vec3(mtl.diffuse[0], mtl.diffuse[1], mtl.diffuse[2]));
    
    if (mtl.textureID) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, mtl.textureID);
        shader.setInt("texture_diffuse1", 0);
        shader.setBool("hasTexture", true);
    } else {
        shader.setBool("hasTexture", false);
    }
    
    if (mtl.normalMapID) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, mtl.normalMapID);
        shader.setInt("texture_normal1", 1);
        shader.setBool("hasNormalMap", true);
    } else {
        shader.setBool("hasNormalMap", false);
    }
    
    if (mesh.VAO != 0) {
        glBindVertexArray(mesh.VAO);
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    } else {
        const aiMesh* aimesh = mesh.assimpMesh;
        if (!aimesh) return;
        
        glBegin(GL_TRIANGLES);
        for (unsigned int index : mesh.indices) {
            if (aimesh->HasNormals()) {
                const aiVector3D& n = aimesh->mNormals[index];
                glNormal3f(n.x, n.y, n.z);
            }
            if (aimesh->HasTextureCoords(0)) {
                const aiVector3D& t = aimesh->mTextureCoords[0][index];
                glTexCoord2f(t.x, t.y);
            }
            const aiVector3D& v = aimesh->mVertices[index];
            glVertex3f(v.x, v.y, v.z);
        }
        glEnd();
    }
}

void ObjModel::renderInstanced(Shader& shader, const std::vector<glm::mat4>& instanceMatrices, bool forceUpdate) const {
    if (instanceMatrices.empty()) return;

    bool isGrassPack = (filename.find("grass") != std::string::npos ||
                        filename.find("Grass") != std::string::npos) &&
                       (meshes.size() > 1);

    if (isGrassPack) {
        static const int NUM_GRASS_VARIANTS = 8;
        static std::vector<int> grassMeshIndices;
        if (grassMeshIndices.empty()) {
            int totalMeshes = (int)meshes.size();
            int step = std::max(1, totalMeshes / NUM_GRASS_VARIANTS);
            for (int i = 0; i < NUM_GRASS_VARIANTS && i * step < totalMeshes; ++i) {
                grassMeshIndices.push_back(i * step);
            }
            std::cout << "[GrassOptim] Selected " << grassMeshIndices.size() 
                      << " representative meshes from " << totalMeshes << " total." << std::endl;
        }

        static size_t lastGrassTotalCount = 0;
        bool grassNeedsUpdate = forceUpdate || (instanceMatrices.size() != lastGrassTotalCount);

        if (grassNeedsUpdate) {
            // Group instances into the 8 buckets using spatial hash
            static std::vector<std::vector<glm::mat4>> meshInstances;
            meshInstances.resize(grassMeshIndices.size());
            for (auto& v : meshInstances) v.clear();

            for (const auto& mat : instanceMatrices) {
                glm::vec3 pos(mat[3]);
                unsigned int hash = (unsigned int)(abs(pos.x * 12.9898f + pos.z * 78.233f + pos.y * 43.123f));
                int bucket = hash % (int)grassMeshIndices.size();
                meshInstances[bucket].push_back(mat);
            }

            for (size_t bIdx = 0; bIdx < grassMeshIndices.size(); ++bIdx) {
                const auto& matricesForMesh = meshInstances[bIdx];
                int mIdx = grassMeshIndices[bIdx];
                const auto& mesh = meshes[mIdx];
                if (mesh.VAO == 0) continue;

                auto& mutableMesh = const_cast<Mesh&>(mesh);
                if (matricesForMesh.empty()) {
                    mutableMesh.lastUploadedSize = 0;
                    continue;
                }

                glBindVertexArray(mesh.VAO);

                if (mutableMesh.instanceVBO == 0) {
                    glGenBuffers(1, &mutableMesh.instanceVBO);
                    glBindBuffer(GL_ARRAY_BUFFER, mutableMesh.instanceVBO);
                    glBufferData(GL_ARRAY_BUFFER, matricesForMesh.size() * sizeof(glm::mat4), matricesForMesh.data(), GL_STATIC_DRAW);
                    for (unsigned int i = 0; i < 4; i++) {
                        glEnableVertexAttribArray(3 + i);
                        glVertexAttribPointer(3 + i, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4), (void*)(i * sizeof(glm::vec4)));
                        glVertexAttribDivisor(3 + i, 1);
                    }
                } else {
                    glBindBuffer(GL_ARRAY_BUFFER, mutableMesh.instanceVBO);
                    glBufferData(GL_ARRAY_BUFFER, matricesForMesh.size() * sizeof(glm::mat4), matricesForMesh.data(), GL_STATIC_DRAW);
                }
                mutableMesh.lastUploadedSize = matricesForMesh.size();
            }
            lastGrassTotalCount = instanceMatrices.size();
        }

        shader.setBool("isInstanced", true);

        for (size_t bIdx = 0; bIdx < grassMeshIndices.size(); ++bIdx) {
            int mIdx = grassMeshIndices[bIdx];
            const auto& mesh = meshes[mIdx];
            if (mesh.VAO == 0 || mesh.instanceVBO == 0 || mesh.lastUploadedSize == 0) continue;

            const Material& mtl = getMaterial(mesh.materialIndex);
            shader.setVec3("diffuseColor", glm::vec3(mtl.diffuse[0], mtl.diffuse[1], mtl.diffuse[2]));

            if (mtl.textureID) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, mtl.textureID);
                shader.setBool("hasTexture", true);
            } else {
                shader.setBool("hasTexture", false);
            }

            glBindVertexArray(mesh.VAO);
            glDrawElementsInstanced(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, 0, (GLsizei)mesh.lastUploadedSize);
            glBindVertexArray(0);
        }

        shader.setBool("isInstanced", false);

    } else {
        shader.setBool("isInstanced", true);

        for (const auto& mesh : meshes) {
            if (mesh.VAO == 0) continue;

            auto& mutableMesh = const_cast<Mesh&>(mesh);
            glBindVertexArray(mesh.VAO);

            if (mutableMesh.instanceVBO == 0) {
                glGenBuffers(1, &mutableMesh.instanceVBO);
                glBindBuffer(GL_ARRAY_BUFFER, mutableMesh.instanceVBO);
                glBufferData(GL_ARRAY_BUFFER, instanceMatrices.size() * sizeof(glm::mat4), instanceMatrices.data(), GL_STATIC_DRAW);
                for (unsigned int i = 0; i < 4; i++) {
                    glEnableVertexAttribArray(3 + i);
                    glVertexAttribPointer(3 + i, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4), (void*)(i * sizeof(glm::vec4)));
                    glVertexAttribDivisor(3 + i, 1);
                }
                mutableMesh.lastUploadedSize = instanceMatrices.size();
            } else if (forceUpdate || instanceMatrices.size() != mutableMesh.lastUploadedSize) {
                glBindBuffer(GL_ARRAY_BUFFER, mutableMesh.instanceVBO);
                glBufferData(GL_ARRAY_BUFFER, instanceMatrices.size() * sizeof(glm::mat4), instanceMatrices.data(), GL_STATIC_DRAW);
                mutableMesh.lastUploadedSize = instanceMatrices.size();
            }

            const Material& mtl = getMaterial(mesh.materialIndex);
            shader.setVec3("diffuseColor", glm::vec3(mtl.diffuse[0], mtl.diffuse[1], mtl.diffuse[2]));

            if (mtl.textureID) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, mtl.textureID);
                shader.setBool("hasTexture", true);
            } else {
                shader.setBool("hasTexture", false);
            }

            glDrawElementsInstanced(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, 0, (GLsizei)mutableMesh.lastUploadedSize);
            glBindVertexArray(0);
        }

        shader.setBool("isInstanced", false);
    }
}

void ObjModel::setupGPUBuffers() {
    // Setup modern GPU buffers for each mesh
    for (auto& mesh : meshes) {
        const aiMesh* aimesh = mesh.assimpMesh;
        if (!aimesh || !aimesh->HasPositions()) continue;
        
        struct GPUVertex {
            glm::vec3 position;
            glm::vec3 normal;
            glm::vec2 texcoord;
            glm::vec3 tangent;
        };
        
        std::vector<GPUVertex> gpuVerts(aimesh->mNumVertices);
        for (unsigned int i = 0; i < aimesh->mNumVertices; ++i) {
            gpuVerts[i].position = glm::vec3(aimesh->mVertices[i].x, aimesh->mVertices[i].y, aimesh->mVertices[i].z);
            gpuVerts[i].normal = aimesh->HasNormals() ? glm::vec3(aimesh->mNormals[i].x, aimesh->mNormals[i].y, aimesh->mNormals[i].z) : glm::vec3(0, 1, 0);
            gpuVerts[i].texcoord = aimesh->HasTextureCoords(0) ? glm::vec2(aimesh->mTextureCoords[0][i].x, aimesh->mTextureCoords[0][i].y) : glm::vec2(0);
            gpuVerts[i].tangent = aimesh->HasTangentsAndBitangents() ? glm::vec3(aimesh->mTangents[i].x, aimesh->mTangents[i].y, aimesh->mTangents[i].z) : glm::vec3(1, 0, 0);
        }
        
        glGenVertexArrays(1, &mesh.VAO);
        glGenBuffers(1, &mesh.VBO);
        glGenBuffers(1, &mesh.EBO);
        
        glBindVertexArray(mesh.VAO);
        
        glBindBuffer(GL_ARRAY_BUFFER, mesh.VBO);
        glBufferData(GL_ARRAY_BUFFER, gpuVerts.size() * sizeof(GPUVertex), gpuVerts.data(), GL_STATIC_DRAW);
        
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(unsigned int), mesh.indices.data(), GL_STATIC_DRAW);
        mesh.indexCount = (unsigned int)mesh.indices.size();
        
        // Position
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GPUVertex), (void*)0);
        // Normal
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GPUVertex), (void*)offsetof(GPUVertex, normal));
        // TexCoord
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(GPUVertex), (void*)offsetof(GPUVertex, texcoord));
        // Tangent
        glEnableVertexAttribArray(7);
        glVertexAttribPointer(7, 3, GL_FLOAT, GL_FALSE, sizeof(GPUVertex), (void*)offsetof(GPUVertex, tangent));
        
        glBindVertexArray(0);
    }
}

bool ObjModel::getNodeTransform(const std::string& name, glm::mat4& outTransform) const {
    const aiNode* node = findNode(name);
    if (!node) return false;
    outTransform = getNodeAbsoluteTransform(node);
    return true;
}

bool ObjModel::checkSphereCollision(const glm::vec3& centerLocal, float radiusLocal, glm::vec3& outNormalLocal, float& outDepthLocal) const {
    // Simple AABB-sphere collision per mesh
    for (const auto& mesh : meshes) {
        glm::vec3 closest = glm::clamp(centerLocal, mesh.minBound, mesh.maxBound);
        glm::vec3 diff = centerLocal - closest;
        float distSq = glm::dot(diff, diff);
        if (distSq < radiusLocal * radiusLocal) {
            float dist = sqrtf(distSq);
            outNormalLocal = (dist > 0.001f) ? diff / dist : glm::vec3(0, 1, 0);
            outDepthLocal = radiusLocal - dist;
            return true;
        }
    }
    return false;
}

bool ObjModel::checkSphereCollisionWorld(const glm::vec3& centerWorld, float radiusWorld, const glm::mat4& modelMatrix, glm::vec3& outNormalWorld, float& outDepthWorld) const {
    glm::mat4 invModel = glm::inverse(modelMatrix);
    glm::vec3 centerLocal = glm::vec3(invModel * glm::vec4(centerWorld, 1.0f));
    
    // Approximate local radius (assumes uniform scale)
    float scaleApprox = glm::length(glm::vec3(modelMatrix[0]));
    float radiusLocal = (scaleApprox > 0.001f) ? radiusWorld / scaleApprox : radiusWorld;
    
    glm::vec3 normalLocal;
    float depthLocal;
    if (checkSphereCollision(centerLocal, radiusLocal, normalLocal, depthLocal)) {
        // Transform normal back to world space
        outNormalWorld = glm::normalize(glm::vec3(glm::transpose(invModel) * glm::vec4(normalLocal, 0.0f)));
        outDepthWorld = depthLocal * scaleApprox;
        return true;
    }
    return false;
}

void ObjModel::computeVertexNormals(const std::vector<Face>& faces) {
    // Stub — vertex normals are computed by Assimp
}
