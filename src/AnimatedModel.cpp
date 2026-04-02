#include "AnimatedModel.h"
#include <glm/gtc/type_ptr.hpp>
#include <assimp/quaternion.h>
#include <assimp/vector3.h>
#include <assimp/anim.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp> 
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <vector>
#include <stddef.h> // For offsetof

// Assuming stbi_image.h is included and implemented elsewhere.
#include "stb_image.h" // Ensure this header is in your project directory

void loadTexture(const std::string& textureFilename, unsigned int& textureID, const std::string& basepath) {
    std::string fullPath = basepath + textureFilename;
    
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    // Set texture wrapping and filtering parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    int width, height, nrChannels;
    // FBX textures often need to be flipped vertically
    stbi_set_flip_vertically_on_load(true);
    unsigned char *data = stbi_load(fullPath.c_str(), &width, &height, &nrChannels, 0);
    
    if (data) {
        GLenum format = (nrChannels == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
    } else {
        std::cerr << "Failed to load texture at: " << fullPath << std::endl;
    }
    stbi_image_free(data);
}

// --- UTILITY FUNCTIONS ---

// Converts Assimp's aiMatrix4x4 (Row-Major) to GLM's glm::mat4 (Column-Major)
glm::mat4 AssimpToGLMMat4(const aiMatrix4x4& from)
{
    glm::mat4 to;
    // Copy Assimp's Row 1 (a) to GLM's Column 0
    to[0][0] = from.a1; to[1][0] = from.a2; to[2][0] = from.a3; to[3][0] = from.a4;
    // Copy Assimp's Row 2 (b) to GLM's Column 1
    to[0][1] = from.b1; to[1][1] = from.b2; to[2][1] = from.b3; to[3][1] = from.b4;
    // Copy Assimp's Row 3 (c) to GLM's Column 2
    to[0][2] = from.c1; to[1][2] = from.c2; to[2][2] = from.c3; to[3][2] = from.c4;
    // Copy Assimp's Row 4 (d) to GLM's Column 3
    to[0][3] = from.d1; to[1][3] = from.d2; to[2][3] = from.d3; to[3][3] = from.d4;
    
    return to; 
}


// --- Global Utility Function in AnimatedModel.cpp ---

aiNodeAnim* findNodeAnim(const aiAnimation* animation, const std::string& nodeName)
{
    // Helper lambda to strip prefixes and suffixes for comparison.
    // This allows the global function to perform the logic without relying on 
    // the private class method getCanonicalBoneName.
    auto getCanonicalName = [](const std::string& name) -> std::string {
        std::string canonical = name;

        // 1. Strip FBX Helper Suffixes: _$AssimpFbx$_Translation etc.
        size_t fbx_pos = canonical.find("_$AssimpFbx$_");
        if (fbx_pos != std::string::npos) {
            canonical = canonical.substr(0, fbx_pos);
        }

        // 2. Strip Prefix: mixamorig:
        size_t mix_pos = canonical.find("mixamorig:");
        if (mix_pos == 0) {
            canonical = canonical.substr(10); // Strip "mixamorig:" (10 characters)
        }

        // 3. Fallback: Check for generic colon prefix
        size_t colon_pos = canonical.find_last_of(':');
        if (colon_pos != std::string::npos && colon_pos < canonical.length() - 1) {
            canonical = canonical.substr(colon_pos + 1);
        }

        return canonical;
    };


    // 1. Calculate the canonical name of the node we are currently traversing.
    // This handles the complex name from the scene graph.
    std::string canonicalNodeName = getCanonicalName(nodeName); 

    for (unsigned int i = 0; i < animation->mNumChannels; i++)
    {
        aiNodeAnim* nodeAnim = animation->mChannels[i];
        std::string animChannelName = nodeAnim->mNodeName.C_Str();

        // 2. Calculate the canonical name of the animation channel.
        std::string canonicalChannelName = getCanonicalName(animChannelName);

        // 3. Compare the stripped, canonical names.
        if (canonicalChannelName == canonicalNodeName) {
           // std::cout << "Found a match: " << canonicalChannelName << " == " << canonicalNodeName << std::endl;
            // Found a match (e.g., 'Hips' == 'Hips')
            return nodeAnim;
        }

        // Fallback: Check if the original names happen to match exactly (rare but safe)
        if (animChannelName == nodeName) {
            return nodeAnim;
        }
    }
    
    // WARNING: Removed the std::cerr output to prevent lag.
    return nullptr;
}
glm::vec3 AssimpToGLMVec3(const aiVector3D& from) {
    return glm::vec3(from.x, from.y, from.z);
}

glm::quat AssimpToGLMQuat(const aiQuaternion& from) {
    return glm::quat(from.w, from.x, from.y, from.z);
}

// --- INTERPOLATION HELPERS (REQUIRED FOR SMOOTH MOVEMENT) ---

unsigned int AnimatedModel::GetPositionIndex(float animationTime, const aiNodeAnim* nodeAnim) {
    for (unsigned int i = 0; i < nodeAnim->mNumPositionKeys - 1; i++) {
        if (animationTime < (float)nodeAnim->mPositionKeys[i + 1].mTime)
            return i;
    }
    return 0; 
}

unsigned int AnimatedModel::GetRotationIndex(float animationTime, const aiNodeAnim* nodeAnim) {
    for (unsigned int i = 0; i < nodeAnim->mNumRotationKeys - 1; i++) {
        if (animationTime < (float)nodeAnim->mRotationKeys[i + 1].mTime)
            return i;
    }
    return 0;
}

unsigned int AnimatedModel::GetScalingIndex(float animationTime, const aiNodeAnim* nodeAnim) {
    for (unsigned int i = 0; i < nodeAnim->mNumScalingKeys - 1; i++) {
        if (animationTime < (float)nodeAnim->mScalingKeys[i + 1].mTime)
            return i;
    }
    return 0;
}

glm::mat4 AnimatedModel::InterpolatePosition(float animationTime, const aiNodeAnim* nodeAnim) {
    if (nodeAnim->mNumPositionKeys == 1)
        return glm::translate(glm::mat4(1.0f), AssimpToGLMVec3(nodeAnim->mPositionKeys[0].mValue));

    unsigned int pIndex0 = GetPositionIndex(animationTime, nodeAnim);
    unsigned int pIndex1 = pIndex0 + 1;
    
    float startTime = (float)nodeAnim->mPositionKeys[pIndex0].mTime;
    float endTime = (float)nodeAnim->mPositionKeys[pIndex1].mTime;
    float factor = (animationTime - startTime) / (endTime - startTime);

    aiVector3D start = nodeAnim->mPositionKeys[pIndex0].mValue;
    aiVector3D end = nodeAnim->mPositionKeys[pIndex1].mValue;
    glm::vec3 finalPos = glm::mix(AssimpToGLMVec3(start), AssimpToGLMVec3(end), factor);

    return glm::translate(glm::mat4(1.0f), finalPos);
}

glm::mat4 AnimatedModel::InterpolateRotation(float animationTime, const aiNodeAnim* nodeAnim) {
    if (nodeAnim->mNumRotationKeys == 1) {
        auto rotation = AssimpToGLMQuat(nodeAnim->mRotationKeys[0].mValue);
        return glm::toMat4(rotation);
    }

    unsigned int rIndex0 = GetRotationIndex(animationTime, nodeAnim);
    unsigned int rIndex1 = rIndex0 + 1;
    
    float startTime = (float)nodeAnim->mRotationKeys[rIndex0].mTime;
    float endTime = (float)nodeAnim->mRotationKeys[rIndex1].mTime;
    float factor = (animationTime - startTime) / (endTime - startTime);
    
    aiQuaternion start = nodeAnim->mRotationKeys[rIndex0].mValue;
    aiQuaternion end = nodeAnim->mRotationKeys[rIndex1].mValue;

    glm::quat finalRot = glm::slerp(AssimpToGLMQuat(start), AssimpToGLMQuat(end), factor);
    finalRot = glm::normalize(finalRot);

    return glm::toMat4(finalRot);
}

glm::mat4 AnimatedModel::InterpolateScaling(float animationTime, const aiNodeAnim* nodeAnim) {
    if (nodeAnim->mNumScalingKeys == 1)
        return glm::scale(glm::mat4(1.0f), AssimpToGLMVec3(nodeAnim->mScalingKeys[0].mValue));

    unsigned int sIndex0 = GetScalingIndex(animationTime, nodeAnim);
    unsigned int sIndex1 = sIndex0 + 1;

    float startTime = (float)nodeAnim->mScalingKeys[sIndex0].mTime;
    float endTime = (float)nodeAnim->mScalingKeys[sIndex1].mTime;
    float factor = (animationTime - startTime) / (endTime - startTime);

    aiVector3D start = nodeAnim->mScalingKeys[sIndex0].mValue;
    aiVector3D end = nodeAnim->mScalingKeys[sIndex1].mValue;
    glm::vec3 finalScale = glm::mix(AssimpToGLMVec3(start), AssimpToGLMVec3(end), factor);

    return glm::scale(glm::mat4(1.0f), finalScale);
}

void AnimatedModel::setAnimationCyclic(const std::string& animName, bool isCyclic)
{
    if (m_AnimationMap.count(animName)) {
        m_IsCyclicMap[animName] = isCyclic;
    } else {
        std::cerr << "WARNING: Cannot set cyclic status for unknown animation: " << animName << std::endl;
    }
}
// --- ANIMATEDMODEL IMPLEMENTATION ---
bool AnimatedModel::loadAnimation(const std::string& animFilename, const std::string& animName)
{
    if (m_AnimationMap.count(animName)) {
        std::cerr << "WARNING: Animation name '" << animName << "' already exists." << std::endl;
        return false;
    }
    
    // 1. Create a new Importer instance to own the memory of this specific animation scene
    Assimp::Importer* animImporter = new Assimp::Importer();
    
    // 2. Load the animation file
    const aiScene* animScene = animImporter->ReadFile(animFilename, 
        aiProcess_LimitBoneWeights | // Only standard processing needed for animation extraction
        aiProcess_Triangulate 
    );
    
    if (!animScene || /*animScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE ||*/ !animScene->mRootNode) {
        std::cerr << "ASSIMP FAILED TO LOAD ANIMATION FILE: " << animImporter->GetErrorString() << std::endl;
        delete animImporter;
        return false;
    }

    // 3. Extract the animation data
    if (animScene->HasAnimations() && animScene->mNumAnimations > 0)
    {
        // We only care about the FIRST animation in the file, which usually contains the motion.
        const aiAnimation* anim = animScene->mAnimations[0];

        // 4. Store the importer and the animation clip pointer
        m_AnimationImporters[animName] = animImporter; 
        m_AnimationMap[animName] = anim;
        m_IsCyclicMap[animName] = true; // Assuming m_IsCyclicMap is a member map of the AnimatedModel class
        // 5. If this is the first animation loaded, set it as the current one.
        if (m_CurrentAnimationName.empty()) {
            m_CurrentAnimationName = animName;
            float ticksPerSecond = anim->mTicksPerSecond != 0 ? anim->mTicksPerSecond : 25.0f;
            m_Duration = (float)anim->mDuration / ticksPerSecond;
        }

        std::cout << "Successfully loaded animation: " << animName << std::endl;
        return true;
    }
    else
    {
        std::cerr << "ERROR: Animation file " << animFilename << " contains no animation data." << std::endl;
        delete animImporter;
        return false;
    }
}
AnimatedModel::AnimatedModel(const std::string& rigFilename) : m_Scene(nullptr), m_BoneCounter(0)
{
    size_t lastSlash = rigFilename.find_last_of("/\\");
    m_BasePath = (lastSlash == std::string::npos) ? "" : rigFilename.substr(0, lastSlash + 1);

    // CRITICAL: Load the mesh file (containing the rig/bones)
    m_Scene = m_Importer.ReadFile(rigFilename, 
        aiProcess_Triangulate | 
        aiProcess_GenSmoothNormals | 
        aiProcess_CalcTangentSpace | 
        aiProcess_FlipUVs
    );

    if (!m_Scene || m_Scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !m_Scene->mRootNode) {
        std::cerr << "ASSIMP FAILED TO LOAD RIGGED MESH: " << m_Importer.GetErrorString() << std::endl;
        return;
    }

    m_GlobalInverseTransform = AssimpToGLMMat4(m_Scene->mRootNode->mTransformation.Inverse());
    m_FinalBoneMatrices.resize(MAX_BONES);
      for (int i = 0; i < MAX_BONES; i++) {
    m_FinalBoneMatrices[i] = glm::mat4(1.0f);
    // Load mesh data (which populates m_BoneInfoMap)
    
  
}
processNode(m_Scene->mRootNode);
    // Initialize current animation to nothing
    m_CurrentAnimationName = ""; 
    m_AnimationTime = 0.0f;
    m_Duration = 0.0f;


    
//     std::cout << "\n--- RIG BONE NAMES (m_BoneInfoMap) ---" << std::endl;
// if (m_BoneInfoMap.empty()) {
//     std::cout << "ERROR: m_BoneInfoMap is EMPTY. Rig import failed." << std::endl;
// } else {
//     for (const auto& pair : m_BoneInfoMap) {
//         std::cout << "Rig Bone Name: '" << pair.first << "'" << std::endl;
//     }
// }
// std::cout << "--------------------------------------" << std::endl;
}

AnimatedModel::~AnimatedModel()
{
    glDeleteVertexArrays(1, &m_MeshData.VAO);
    glDeleteBuffers(1, &m_MeshData.VBO);
    glDeleteBuffers(1, &m_MeshData.EBO);
    if (m_MeshData.textureID) glDeleteTextures(1, &m_MeshData.textureID);
    // CRITICAL CLEANUP: Delete all separate Importer instances
   for (auto const& pair : m_AnimationImporters) {
        // 'pair.second' est le pointeur Assimp::Importer*
        delete pair.second;
    }
    m_AnimationImporters.clear();
    // The main m_Importer is a class member and is automatically cleaned up.
}

// --- MODEL PROCESSING FUNCTIONS ---

void AnimatedModel::processNode(aiNode* node)
{
    // Process all meshes located at the current node
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        aiMesh* mesh = m_Scene->mMeshes[node->mMeshes[i]];
        processMesh(mesh);
    }
    // Recurse on the node's children
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        processNode(node->mChildren[i]);
    }
}
void AnimatedModel::loadTextureFromMemory(const aiTexture* embeddedTexture, unsigned int& textureID) {
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

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
        GLenum format = (nrChannels == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        stbi_image_free(data);
    }
}
void AnimatedModel::processMesh(aiMesh* mesh)
{
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;

    // Process vertices, normals, UVs
    for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
        Vertex vertex;
        vertex.Position = glm::vec3(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z);
        if (mesh->HasNormals()) {
            vertex.Normal = glm::vec3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);
        }
        if (mesh->mTextureCoords[0]) {
            vertex.TexCoords = glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y);
        }
        
        // Initialize bone IDs/weights to default values (-1/0.0)
        for(int j = 0; j < MAX_BONE_INFLUENCE; ++j) {
            vertex.m_BoneIDs[j] = -1;
            vertex.m_Weights[j] = 0.0f;
        }
        vertices.push_back(vertex);
    }
    
    // Process indices
    for (unsigned int j = 0; j < mesh->mNumFaces; j++) {
        aiFace face = mesh->mFaces[j];
        for (unsigned int k = 0; k < face.mNumIndices; k++) {
            indices.push_back(face.mIndices[k]);
        }
    }
    m_MeshData.indices = indices;
    
    // Extract bone data
    if (mesh->HasBones()) {
        extractBoneWeightForVertices(vertices, mesh);
    }
    // Inside AnimatedModel::processMesh
if (m_Scene->HasMaterials()) {
        aiMaterial* material = m_Scene->mMaterials[mesh->mMaterialIndex];
        aiString str;

        // Check for Diffuse Textures
        if (material->GetTexture(aiTextureType_DIFFUSE, 0, &str) == AI_SUCCESS) {
            std::cout << "Found Texture String in FBX: " << str.C_Str() << std::endl;

            // Check if the texture is embedded (Assimp uses '*' followed by an index)
            const aiTexture* embeddedTexture = m_Scene->GetEmbeddedTexture(str.C_Str());
            
            if (embeddedTexture) {
                std::cout << "Detected EMBEDDED texture. Loading from memory..." << std::endl;
                loadTextureFromMemory(embeddedTexture, m_MeshData.textureID);
            } else {
                std::cout << "Detected EXTERNAL texture. Loading from file: " << str.C_Str() << std::endl;
                loadTexture(str.C_Str(), m_MeshData.textureID, m_BasePath);
            }
        } else {
            std::cout << "WARNING: No Diffuse texture found for mesh material index: " << mesh->mMaterialIndex << std::endl;
        }
    }
    // Setup GL Buffers
    setupMeshGL(vertices, indices); 
    
    // Load Texture (assuming loadTexture implementation exists)
    // You may need to call loadTexture here if you have texture data
}

void AnimatedModel::extractBoneWeightForVertices(std::vector<Vertex>& vertices, aiMesh* mesh)
{
    for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
    {
        int boneID = -1;
        std::string rawBoneName = mesh->mBones[boneIndex]->mName.C_Str();
        std::string canonicalBoneName = getCanonicalBoneName(rawBoneName); 

        if (m_BoneInfoMap.find(canonicalBoneName) == m_BoneInfoMap.end())
        {
            if (m_BoneCounter >= MAX_BONES) continue;

            BoneInfo newBoneInfo;
            newBoneInfo.id = m_BoneCounter;
            newBoneInfo.offsetMatrix = AssimpToGLMMat4(mesh->mBones[boneIndex]->mOffsetMatrix); 
            m_BoneInfoMap[canonicalBoneName] = newBoneInfo; 
            boneID = m_BoneCounter;
            m_BoneCounter++;
        }
        else
        {
            boneID = m_BoneInfoMap[canonicalBoneName].id;
        }

        for (unsigned int weightIndex = 0; weightIndex < mesh->mBones[boneIndex]->mNumWeights; ++weightIndex)
        {
            aiVertexWeight weight = mesh->mBones[boneIndex]->mWeights[weightIndex];
            int vertexID = weight.mVertexId;
            float weightValue = weight.mWeight;
            
            for (int i = 0; i < MAX_BONE_INFLUENCE; i++) {
                if (vertices[vertexID].m_BoneIDs[i] == -1) {
                    vertices[vertexID].m_BoneIDs[i] = boneID;
                    vertices[vertexID].m_Weights[i] = weightValue;
                    break;
                }
            }
        }
    }

    // --- CRITICAL ADDITION: WEIGHT NORMALIZATION ---
    for (auto& vertex : vertices) {
        float totalWeight = 0.0f;
        for (int i = 0; i < MAX_BONE_INFLUENCE; i++) {
            if (vertex.m_BoneIDs[i] != -1) {
                totalWeight += vertex.m_Weights[i];
            }
        }
        
        // Normalize if the sum isn't 1.0 (prevents mesh collapsing)
        if (totalWeight > 0.0f) {
            for (int i = 0; i < MAX_BONE_INFLUENCE; i++) {
                vertex.m_Weights[i] /= totalWeight;
            }
        }
    }

    for (unsigned int i = 0; i < vertices.size(); i++) {
        float totalWeight = 0.0f;
        for (int j = 0; j < MAX_BONE_INFLUENCE; j++) {
            if (vertices[i].m_BoneIDs[j] != -1) {
                totalWeight += vertices[i].m_Weights[j];
            }
        }

        // If the weight doesn't equal 1.0, redistribute it
        if (totalWeight > 0.0f) {
            for (int j = 0; j < MAX_BONE_INFLUENCE; j++) {
                vertices[i].m_Weights[j] /= totalWeight;
            }
        }
    }
}

void AnimatedModel::setupMeshGL(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices) 
{
    glGenVertexArrays(1, &m_MeshData.VAO);
    glGenBuffers(1, &m_MeshData.VBO);
    glGenBuffers(1, &m_MeshData.EBO);

    glBindVertexArray(m_MeshData.VAO);
    
    glBindBuffer(GL_ARRAY_BUFFER, m_MeshData.VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), &vertices[0], GL_STATIC_DRAW);  
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_MeshData.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);

    // Position (location = 0)
    glEnableVertexAttribArray(0);   
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    
    // Normal (location = 1)
    glEnableVertexAttribArray(1);   
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));
    
    // Texture Coordinates (location = 2)
    glEnableVertexAttribArray(2);   
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));

    // Bone IDs (location = 3) - Use Integer Pointer for GL_INT
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, MAX_BONE_INFLUENCE, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, m_BoneIDs));

    // Bone Weights (location = 4)
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, MAX_BONE_INFLUENCE, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, m_Weights));

    glBindVertexArray(0);
}


// --- ANIMATION LOGIC ---

// Refactored: Accepts animation clip name
void AnimatedModel::updateAnimation(const std::string& animName, float deltaTime)
{
    // If the requested animation isn't in the map, stick to the current one or stop.
    if (m_AnimationMap.count(animName)) {
        if (m_CurrentAnimationName != animName) {
            m_CurrentAnimationName = animName;
            m_AnimationTime = 0.0f; // Reset time to start the new animation
            
            // Update duration based on the new clip
            const aiAnimation* newAnim = m_AnimationMap.at(m_CurrentAnimationName);
            float ticksPerSecond = newAnim->mTicksPerSecond != 0 ? newAnim->mTicksPerSecond : 25.0f;
            m_Duration = (float)newAnim->mDuration / ticksPerSecond; 
        }
    } else if (m_CurrentAnimationName.empty()) {
        return; // No animation available
    }
    
    const aiAnimation* animation = m_AnimationMap.at(m_CurrentAnimationName);
    bool isCyclic = m_IsCyclicMap.count(m_CurrentAnimationName) ? m_IsCyclicMap.at(m_CurrentAnimationName) : true;
    float ticksPerSecond = animation->mTicksPerSecond != 0 ? animation->mTicksPerSecond : 25.0f;
    
    m_AnimationTime += deltaTime * ticksPerSecond;
    
   if (isCyclic) {
        // Cyclic: Loop back to start (original logic)
        m_AnimationTime = fmod(m_AnimationTime, (float)animation->mDuration); 
    } else {
        // Single-Shot: Clamp the time to the animation duration
        if (m_AnimationTime > (float)animation->mDuration) {
            m_AnimationTime = (float)animation->mDuration; // Stop at the last frame
            m_AnimationIsFinished = true; // Mark animation as finished
        }else{
            m_AnimationIsFinished = false; // Animation is still playing
        }
    }

    glm::mat4 rootTransform(1.0f); 

    // Pass the correct animation clip pointer to the recursive function
    calculateBoneTransform(m_Scene->mRootNode, rootTransform, animation);
}

// --- NEW Helper Function (Must be implemented in AnimatedModel.cpp) ---

std::string AnimatedModel::getCanonicalBoneName(const std::string& nodeName) const
{
    std::string canonicalName = nodeName;

    // 1. Strip FBX Helper Suffixes: _$AssimpFbx$_Translation etc.
    // The name we want is the base bone name before this suffix.
    size_t fbx_pos = canonicalName.find("_$AssimpFbx$_");
    if (fbx_pos != std::string::npos) {
        canonicalName = canonicalName.substr(0, fbx_pos);
    }
    
    // 2. Strip Prefix: mixamorig:
    size_t mix_pos = canonicalName.find("mixamorig:");
    if (mix_pos == 0) { // Check if it starts at the beginning
        canonicalName = canonicalName.substr(10); // Strip "mixamorig:" (10 chars)
    }

    // 3. Fallback: Check for generic colon prefix if the above failed
    size_t colon_pos = canonicalName.find_last_of(':');
    if (colon_pos != std::string::npos) {
        canonicalName = canonicalName.substr(colon_pos + 1); 
    }
    
    return canonicalName;
}
// Refactored: Accepts animation clip pointer
void AnimatedModel::calculateBoneTransform(const aiNode* node, glm::mat4 parentTransform, const aiAnimation* animation)
{
    std::string nodeName = node->mName.C_Str();
    // This debug line can be removed once it works:
    // std::cout << "Animation Node Name: '" << nodeName << "'" << std::endl; 
    glm::mat4 nodeTransform = AssimpToGLMMat4(node->mTransformation); 
    
    // 1. Check for Animation Data using the full node name
    if (aiNodeAnim* boneNodeAnim = findNodeAnim(animation, nodeName))
    {
        // Decompose the rig's original base transform for this bone
        aiVector3D rigScaling;
        aiQuaternion rigRotation;
        aiVector3D rigPosition;
        node->mTransformation.Decompose(rigScaling, rigRotation, rigPosition);

        // Interpolate only the animation's rotation (discard position to lock them to the mesh)
        glm::mat4 animRot = InterpolateRotation(m_AnimationTime, boneNodeAnim);

        std::string canonical = getCanonicalBoneName(nodeName);
        bool isRootPose = (canonical == "Hips" || canonical == "Root" || canonical == "Pelvis");

        // CRITICAL Z-UP EXPORT FIX: 
        // If the specific animation was exported lying flat (Z-up), we fix it directly on the root bone!
        // This avoids pitching the entire model into the floor geometry since the root bone pivot is kept at its base height.
        if (isRootPose && (m_CurrentAnimationName == "Idle" || m_CurrentAnimationName == "rideIdle")) {
            animRot = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)) * animRot;
        }

        glm::mat4 finalPos = glm::translate(glm::mat4(1.0f), AssimpToGLMVec3(rigPosition));
        glm::mat4 finalScale = glm::scale(glm::mat4(1.0f), AssimpToGLMVec3(rigScaling));
        nodeTransform = finalPos * animRot * finalScale;
    }

    glm::mat4 globalTransform = parentTransform * nodeTransform;

    // 2. Look up the bone using the Canonical Name
    std::string canonicalName = getCanonicalBoneName(nodeName);

    if (m_BoneInfoMap.count(canonicalName))
    {
        int boneID = m_BoneInfoMap.at(canonicalName).id;
        glm::mat4 offsetMatrix = m_BoneInfoMap.at(canonicalName).offsetMatrix;
        
        // Final matrix = GlobalInverse * GlobalBoneTransform * BoneOffset
        m_FinalBoneMatrices[boneID] = m_GlobalInverseTransform * globalTransform * offsetMatrix;
    }
    
    // 3. Recurse for children
    for (unsigned int i = 0; i < node->mNumChildren; ++i)
    {
        calculateBoneTransform(node->mChildren[i], globalTransform, animation);
    }
}

// --- RENDERING & UTILITY ---

void AnimatedModel::setBoneUniforms(Shader& shader)
{
    glUniformMatrix4fv(shader.getUniformLocation("gBones"), 
                        m_BoneCounter, 
                        GL_FALSE, 
                        glm::value_ptr(m_FinalBoneMatrices[0]));
                        
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_MeshData.textureID);
    shader.setInt("ourTexture", 0);
}

void AnimatedModel::draw()
{
    glBindVertexArray(m_MeshData.VAO);
    glDrawElements(GL_TRIANGLES, (GLsizei)m_MeshData.indices.size(), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

float AnimatedModel::getAnimationDuration() const { 
    if (m_AnimationMap.count(m_CurrentAnimationName)) {
        const aiAnimation* anim = m_AnimationMap.at(m_CurrentAnimationName);
        float ticksPerSecond = anim->mTicksPerSecond != 0 ? anim->mTicksPerSecond : 25.0f;
        return (float)anim->mDuration / ticksPerSecond; 
    }
    return 0.0f; 
}