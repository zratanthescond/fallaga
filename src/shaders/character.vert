#version 330 core

// ----------------------------------------------------
// Input Attributes from AnimatedModel.cpp
// ----------------------------------------------------
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in ivec4 aBoneIDs;   // Bone IDs (integers)
layout (location = 4) in vec4 aWeights;    // Bone Weights (floats)

// ----------------------------------------------------
// Uniforms (from Character::render)
// ----------------------------------------------------
uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;

// CRITICAL UNIFORM: Bone transformation array
#define MAX_BONES 100
uniform mat4 gBones[MAX_BONES]; 

// ----------------------------------------------------
// Output
// ----------------------------------------------------
out vec2 TexCoords;
out vec3 Normal;
out vec3 FragPos;

// Function to calculate the final bone transform for the vertex
mat4 getFinalBoneTransform()
{
    mat4 boneTransform = mat4(0.0);
    
    // We loop through the 4 bones influencing this vertex
    for(int i = 0 ; i < 4 ; i++)
    {
        // If the weight is greater than 0, apply the bone's influence
        if(aBoneIDs[i] >= 0)
        {
            mat4 boneMatrix = gBones[aBoneIDs[i]];
            boneTransform += boneMatrix * aWeights[i];
        }
    }
    return boneTransform;
}

void main()
{
    // Calculate the final transformation matrix for this vertex
    mat4 finalBoneTransform = getFinalBoneTransform();

    // 1. Calculate the final position after skeletal animation
    vec4 localPosition = finalBoneTransform * vec4(aPos, 1.0);
    
    // 2. Calculate the final position in world space
    FragPos = vec3(model * localPosition);
    
    // 3. Transform the position to clip space
    gl_Position = projection * view * model * localPosition;

    // 4. Transform the normal for lighting calculation
    Normal = mat3(transpose(inverse(model))) * vec3(finalBoneTransform * vec4(aNormal, 0.0));
    
    TexCoords = aTexCoords;
}