#define WIN32_LEAN_AND_MEAN
 #define GLM_ENABLE_EXPERIMENTAL
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp> // Pour glm::pi

#include "Horse.h"
#include "Shader.h"
#include "AnimatedModel.h" 
#include "Character.h" // Nécessaire pour l'état du cavalier
#include "Terrain.h" // NEW: Use procedural terrain
#include "Camera.h"
#include "PhysicsSystem.h" 
#include <glm/gtx/norm.hpp>
// IMPORTANT : Assurez-vous que Horse.h contient les membres suivants :
// AnimatedModel* model;
// Shader* horseShader;
// Vec3 position;
// float m_RotationAngle;
// glm::vec3 m_ForwardDirection;
// std::string m_CurrentAnimation;
// Character* m_MountedCharacter = nullptr;
// const float m_WalkSpeed = 15.0f;
// const float m_RunSpeed = 40.0f; 
// const float HEIGHT_UPDATE_INTERVAL = 0.1f;
// float m_HeightUpdateTimer = 0.0f; 


// --- Constructeur ---
Horse::Horse() {
    // Initialisation
    position = Vec3(10, 0, 10); // Position initiale arbitraire
    velocity = Vec3(0, 0, 0); 
    m_RotationAngle = 0.0f;
    m_ForwardDirection = glm::vec3(0.0f, 0.0f, -1.0f);
    m_MountedCharacter = nullptr; 

    // 1. Initialiser le shader (Réutilisation du shader du personnage pour l'éclairage)
    horseShader = new Shader("src/shaders/character.vert", "src/shaders/character.frag"); 
    
    // 2. Charger le modèle FBX du cheval
    // REMPLACER PAR LE CHEMIN DE VOTRE CHEVAL RIGÉ
    model = new AnimatedModel("assets/horses/Horse.fbx"); 

    // 3. Charger les clips d'animation
    if (model) {
        // REMPLACER PAR LES NOMS DE CLIPS CORRESPONDANTS À VOTRE MODÈLE
    ((AnimatedModel*)model)->loadAnimation("assets/horses/HorseIdle.fbx", "Idle");
    ((AnimatedModel*)model)->loadAnimation("assets/horses/HorseWalk.fbx", "Walk");
    ((AnimatedModel*)model)->loadAnimation("assets/horses/horseRun.fbx", "Run");
        m_CurrentAnimation = "Idle";
    }
}

// --- Destructeur ---
Horse::~Horse() {
    delete model;
    delete horseShader;
}
/**
 * @brief Utility function to calculate the horse's horizontal forward vector 
 * based on its current yaw rotation (m_RotationAngle and m_ForwardDirection).
 * Assumes the horse's default forward is (0, 0, -1) before rotation.
 */
/**
 * @brief Calculates the horse's current forward vector based on m_CurrentYaw.
 * This vector is used for both position update and terrain alignment calculation.
 */
glm::vec3 Horse::getHorizontalForward() const {
    // Calculate the forward vector from the current smooth yaw
    // sin(yaw) for X, -cos(yaw) for Z (assuming -Z is default forward)
    return glm::vec3(std::sin(m_CurrentYaw), 0.0f, -std::cos(m_CurrentYaw));
}
// --- Mise à Jour pour le Cheval Non Monté ---
// Cette fonction gère l'animation de base (Idle) et les mises à jour non liées au mouvement du joueur.
void Horse::update(float deltaTime, Terrain* terrain) {
    
    // 1. Update Height and Terrain Alignment (Physics & Gravity)
    PhysicsSystem::applyGravity(position, velocity, deltaTime, terrain, 0.1f);

    if (terrain) {

        // Terrain Alignment Logic (Pitch/Roll Matrix Calculation)
        glm::vec3 terrainNormal = terrain->getNormalAt(position.x, position.z);
        if (terrainNormal.y < 0.0f) { terrainNormal = -terrainNormal; } 
        
        glm::vec3 horizForward = getHorizontalForward(); 
        
        glm::vec3 right = glm::cross(horizForward, terrainNormal); 
        if (glm::length2(right) < 1e-6) { right = glm::vec3(1.0f, 0.0f, 0.0f); }
        right = glm::normalize(right);
        
        glm::vec3 newForward = glm::cross(terrainNormal, right);
        newForward = glm::normalize(newForward);
        
        m_PitchRollMatrix = glm::mat4(1.0f);
        m_PitchRollMatrix[0] = glm::vec4(right, 0.0f);
        m_PitchRollMatrix[1] = glm::vec4(terrainNormal, 0.0f);
        m_PitchRollMatrix[2] = glm::vec4(newForward, 0.0f);
    }
    
    // 2. Animation (IDLE) : S'applique UNIQUEMENT si le cheval n'est PAS monté.
    if (!m_MountedCharacter && model) {
        m_CurrentAnimation = "Idle";
        ((AnimatedModel*)model)->updateAnimation(m_CurrentAnimation, deltaTime);
    }
}
// --- Mise à Jour du Mouvement (Appelée par Character::update) ---
void Horse::updateMovement(Camera* camera, float deltaTime, Terrain* terrain) {
    if (!m_MountedCharacter) return;

 bool isShiftPressed = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) ||
                          (GetAsyncKeyState(VK_RSHIFT) & 0x8000);
    float horseSpeed = isShiftPressed ? m_RunSpeed : m_WalkSpeed;
    
    // 1. Calculate Horizontal Camera Vectors
    glm::vec3 cameraForward = glm::vec3(camera->getForward().x, 0.0f, camera->getForward().z);
    glm::vec3 cameraRight = glm::vec3(camera->getRight().x, 0.0f, camera->getRight().z);

    if (glm::length2(cameraForward) > 0.0f) cameraForward = glm::normalize(cameraForward);
    if (glm::length2(cameraRight) > 0.0f) cameraRight = glm::normalize(cameraRight);

    glm::vec3 moveDir(0, 0, 0);
    bool isMoving = false;

    // 2. Determine Movement Input Direction
    if (m_MountedCharacter->keys['z'] || m_MountedCharacter->keys['Z']) { moveDir -= cameraForward; isMoving = true; } 
    if (m_MountedCharacter->keys['s'] || m_MountedCharacter->keys['S']) { moveDir += cameraForward; isMoving = true; }
    if (m_MountedCharacter->keys['q'] || m_MountedCharacter->keys['Q']) { moveDir -= cameraRight; isMoving = true; }
    if (m_MountedCharacter->keys['d'] || m_MountedCharacter->keys['D']) { moveDir += cameraRight; isMoving = true; }

    // --- RDR2 Rotation and Movement Blending ---
    if (isMoving && moveDir.length() > 0.0f) {
        glm::vec3 glmMoveDir = glm::normalize(glm::vec3(moveDir.x, 0.0f, moveDir.z));
        
        // A. Calculate Target Yaw Rotation
        float m_TargetYaw = std::atan2(glmMoveDir.x, -glmMoveDir.z); 

        // B. Apply Smooth Rotation Blending
        float deltaAngle = m_TargetYaw - m_CurrentYaw;
        while (deltaAngle > glm::pi<float>()) deltaAngle -= 2.0f * glm::pi<float>();
        while (deltaAngle < -glm::pi<float>()) deltaAngle += 2.0f * glm::pi<float>();

        m_CurrentYaw += deltaAngle * glm::min(deltaTime * ROTATION_SPEED, 1.0f);
        
        // C. Update public rotation members (used by render)
        m_RotationAngle = m_CurrentYaw; 
        m_ForwardDirection = glm::vec3(0.0f, 1.0f, 0.0f); 

        // D. Calculate Current Forward Vector for Movement
        glm::vec3 currentForward = glm::vec3(std::sin(m_CurrentYaw), 0.0f, -std::cos(m_CurrentYaw));
        
        // E. Update Position
        position.x += currentForward.x * horseSpeed * deltaTime; 
        position.z += currentForward.z * horseSpeed * deltaTime;
        
        // F. Update Animation
        m_CurrentAnimation = isShiftPressed ? "Run" : "Walk"; 
    } else {
        m_CurrentAnimation = "Idle";
    }

    // 3. Update Height and Terrain Alignment (Physics & Gravity)
    PhysicsSystem::applyGravity(position, velocity, deltaTime, terrain, 0.1f);

    if (terrain) {

        // Get the current forward direction based on the updated m_CurrentYaw
        glm::vec3 horizForward = getHorizontalForward(); 

        // 🎯 Terrain Alignment Logic (Pitch/Roll Matrix Calculation)
        glm::vec3 terrainNormal = terrain->getNormalAt(position.x, position.z);
        if (terrainNormal.y < 0.0f) { terrainNormal = -terrainNormal; } // Ensure Normal points Up
        
        // Calculate Right: cross(Forward, Up) (Fixes upside down issue)
        glm::vec3 right = glm::cross(horizForward, terrainNormal); 
        if (glm::length2(right) < 1e-6) { right = glm::vec3(1.0f, 0.0f, 0.0f); }
        right = glm::normalize(right);
        
        // Recalculate Forward: cross(Up, Right) (Ensures orthogonality)
        glm::vec3 newForward = glm::cross(terrainNormal, right);
        newForward = glm::normalize(newForward);
        
        // Construct the Pitch/Roll Matrix
        m_PitchRollMatrix = glm::mat4(1.0f);
        m_PitchRollMatrix[0] = glm::vec4(right, 0.0f);
        m_PitchRollMatrix[1] = glm::vec4(terrainNormal, 0.0f);
        m_PitchRollMatrix[2] = glm::vec4(newForward, 0.0f);
    }
    
    // 4. Update Animation
    if (model) {
        ((AnimatedModel*)model)->updateAnimation(m_CurrentAnimation, deltaTime);
    }
    
    // 5. Update Character's Position
    if (m_MountedCharacter) {
        m_MountedCharacter->setPosition(position);
        // Ensure character rotation matches the horse's smooth rotation
        m_MountedCharacter->setRotation(m_RotationAngle, m_ForwardDirection); 
    }
}

// --- Montée à Cheval (Appelée par Game/Interaction) ---
void Horse::mountCharacter(Character* character) {
    if (!character || m_MountedCharacter) return;

   Vec3 saddleOffset(0.0f, 0.3f, 0.0f); 
    Vec3 initialPos = this->position + saddleOffset;
    
    character->setPosition(initialPos);
    character->setMountedHorse(this);
    m_MountedCharacter = character;     
    
    // Le personnage doit hériter de la rotation du cheval

}

// --- Descente de Cheval (Appelée par Character::update ou Game) ---
void Horse::dismountCharacter(Character* character) {
    if (!character || character != m_MountedCharacter) return;
    
    // Placer le personnage à côté du cheval
    // Nécessite Character::getRight()
    Vec3 dismountPos = this->position + character->getRight() * 1.5f; 
    character->setPosition(dismountPos);
    
    m_MountedCharacter = nullptr;
    character->setMountedHorse(nullptr);
    
    // Le personnage doit récupérer une rotation orientée (à définir)
}

// --- Rendu (Moderne) ---
// void Horse::render(const glm::mat4& projection, const glm::mat4& view) {
//     if (!model || !horseShader) return;

//     // 1. Calculer la matrice Model
//     glm::mat4 modelMatrix = glm::mat4(1.0f);
//     // 1A. Apply the PITCH and ROLL rotation (Terrain Alignment)
//     modelMatrix = modelMatrix * m_PitchRollMatrix;
//     glm::vec3 horsePos(position.x, position.y, position.z);
    
//     // Appliquer la translation
//     modelMatrix = glm::translate(modelMatrix, horsePos);
    
//     // Appliquer la rotation
//     if (m_RotationAngle != 0.0f && glm::length(m_ForwardDirection) > 0.0f) {
//         modelMatrix = glm::rotate(modelMatrix, m_RotationAngle, m_ForwardDirection);
//     }
    
//     // Appliquer l'échelle (Ajustez ce facteur au besoin)
//     float scaleFactor = 0.0025f; 
//     modelMatrix = glm::scale(modelMatrix, glm::vec3(scaleFactor)); 
    
//     // 2. Activer le shader
//     horseShader->use();

//     // 3. Envoyer les matrices PVM
//     horseShader->setMat4("projection", projection);
//     horseShader->setMat4("view", view);
//     horseShader->setMat4("model", modelMatrix); 
    
//     // 4. Passer les matrices d'os calculées
//     ((AnimatedModel*)model)->setBoneUniforms(*horseShader);

//     // 5. Dessiner le maillage animé
//     ((AnimatedModel*)model)->draw();
// }


// In Horse.cpp

void Horse::render(const glm::mat4& projection, const glm::mat4& view) {
    if (!model || !horseShader) return;

    // 1. Initialize Model Matrix
    glm::mat4 modelMatrix = glm::mat4(1.0f);
    
    // --- Transformations applied in reverse order (right-to-left in GLM) ---
    
    // 5. Apply Translation (Position in World Space - APPLIED LAST)
    glm::vec3 horsePos(position.x, position.y, position.z);
    // glm::translate(M, T) results in T * M
    modelMatrix = glm::translate(modelMatrix, horsePos);
    
    // 4. Apply YAW Rotation (Horizontal movement direction)
    if (m_RotationAngle != 0.0f && glm::length(m_ForwardDirection) > 0.0f) {
        // glm::rotate(M, R, A) results in R(A) * M
        modelMatrix = glm::rotate(modelMatrix, m_RotationAngle, m_ForwardDirection);
    }
    
    // 3. Apply PITCH and ROLL Rotation (Terrain Alignment)
    // This matrix (m_PitchRollMatrix) must be applied via M * P/R
    // modelMatrix = M * P/R results in P/R is applied relative to the world frame.
   // modelMatrix = modelMatrix * m_PitchRollMatrix;

    // 2. Model Correction Rotation (Fix intrinsic model orientation)
    // Apply a fixed rotation to align the model's forward axis with the engine's Z-axis.
    // Try 90, -90, or 180 degrees if -90 is incorrect for your specific model.
    modelMatrix = glm::rotate(modelMatrix, glm::radians(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    // 1. Apply Scaling (Must be applied first to model coordinates - APPLIED FIRST)
    float scaleFactor = 0.0030f; 
    modelMatrix = glm::scale(modelMatrix, glm::vec3(scaleFactor)); 
    
    // --- End Transformations ---
    
    // 2. Activate the shader and set uniforms
    horseShader->use();

    // 3. Set the standard matrices (Projection, View, Model) as uniforms
    horseShader->setMat4("projection", projection);
    horseShader->setMat4("view", view);
    horseShader->setMat4("model", modelMatrix); 
    
    // 4. Pass the calculated bone matrices (gBones uniform)
    ((AnimatedModel*)model)->setBoneUniforms(*horseShader);
    
    // 5. Draw the animated mesh
    ((AnimatedModel*)model)->draw();
}