#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h> 
#include <map>
#include <vector>
#include <string>
#include "Shader.h" // Include the Shader class

// CRITICAL FIX: Ensure all required Assimp headers are present
#include <assimp/anim.h> // Necessary for aiAnimation and aiNodeAnim types

#define MAX_BONE_INFLUENCE 4
#define MAX_BONES 100 // Must match the shader uniform array size

struct BoneInfo {
    int id;                
    glm::mat4 offsetMatrix;    
};

struct Vertex {
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec2 TexCoords;
    int m_BoneIDs[MAX_BONE_INFLUENCE] = {-1, -1, -1, -1};
    float m_Weights[MAX_BONE_INFLUENCE] = {0.0f, 0.0f, 0.0f, 0.0f};
};

struct MeshData {
    GLuint VAO, VBO, EBO;
    std::vector<unsigned int> indices;
    unsigned int textureID = 0; 
};

// --- Assimp Helper (These signatures use types defined above) ---
// Note: aiMatrix4x4 and aiNodeAnim are defined by the includes above.
glm::mat4 AssimpToGLMMat4(const aiMatrix4x4& from);
aiNodeAnim* findNodeAnim(const aiAnimation* animation, const std::string& boneName);


class AnimatedModel
{
public:
    AnimatedModel(const std::string& filename);
    ~AnimatedModel();

    // MODIFIED: Accepts the name of the animation clip to play
    void updateAnimation(const std::string& animName, float deltaTime);
    
    void draw();
    void setBoneUniforms(Shader& shader);
    float getAnimationDuration() const;
   bool loadAnimation(const std::string& animFilename, const std::string& animName);
   void setAnimationCyclic(const std::string& animName, bool isCyclic);
   bool isAnimationFinished() const { return m_AnimationIsFinished; }
private:
void loadTextureFromMemory(const aiTexture* embeddedTexture, unsigned int& textureID);
    
    glm::quat AssimpToGLMQuat(const aiQuaternion& quat) {
        return glm::quat(quat.w, quat.x, quat.y, quat.z);
    }

    glm::vec3 AssimpToGLMVec3(const aiVector3D& vec) {
        return glm::vec3(vec.x, vec.y, vec.z);
    }

    // --- Animation State ---
bool m_AnimationIsFinished = false;
    std::map<std::string, bool> m_IsCyclicMap;
    Assimp::Importer m_Importer;
    const aiScene* m_Scene;
    std::string m_BasePath;
    glm::mat4 m_GlobalInverseTransform;
    MeshData m_MeshData;
    
    std::string getCanonicalBoneName(const std::string& nodeName) const;
  
    // --- New Animation State & Data for Multi-Clip Support ---
    std::map<std::string, const aiAnimation*> m_AnimationMap;
    std::string m_CurrentAnimationName;
    std::map<std::string, Assimp::Importer*> m_AnimationImporters; 
    
    // Animation State
    float m_AnimationTime = 0.0f;
    float m_Duration = 0.0f; // Stores the duration of the current clip

    // Bone Data
    std::vector<glm::mat4> m_FinalBoneMatrices;
    std::map<std::string, BoneInfo> m_BoneInfoMap;
    int m_BoneCounter = 0;

    // --- Interpolation Helpers ---
    unsigned int GetPositionIndex(float animationTime, const aiNodeAnim* nodeAnim);
    unsigned int GetRotationIndex(float animationTime, const aiNodeAnim* nodeAnim);
    unsigned int GetScalingIndex(float animationTime, const aiNodeAnim* nodeAnim);
    
    // These functions use aiVector3D/aiQuaternion, which are defined in the includes.
    glm::mat4 InterpolatePosition(float animationTime, const aiNodeAnim* nodeAnim);
    glm::mat4 InterpolateRotation(float animationTime, const aiNodeAnim* nodeAnim);
    glm::mat4 InterpolateScaling(float animationTime, const aiNodeAnim* nodeAnim);
    
    // --- Mesh Processing ---
    void processNode(aiNode* node);
    void processMesh(aiMesh* mesh);
    void setupMeshGL(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices);
    void extractBoneWeightForVertices(std::vector<Vertex>& vertices, aiMesh* mesh);
    
    // --- Animation Calculation ---
    // MODIFIED: Now requires the specific animation clip to calculate transforms
    void calculateBoneTransform(const aiNode* node, glm::mat4 parentTransform, const aiAnimation* animation);
};