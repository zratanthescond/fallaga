#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <GL/glut.h> 
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp> // Pour glm::pi

#include "Character.h"
#include "Camera.h"
#include "AnimatedModel.h" 
#include "Terrain.h"
#include "Shader.h" 
#include "Horse.h" // Nécessaire pour les appels de monture
#include "PhysicsSystem.h" 

// Supprimer le commentaire sur les variables globales, car elles sont bien définies dans Character.h

Character::Character() {
    position = Vec3(-435.0f, 0.0f, 1315.0f); // Spawn at the corrected train station location
    velocity = Vec3(0, 0, 0);  // CRITICAL: Initialize velocity to zero

    characterShader = new Shader("src/shaders/character.vert", "src/shaders/character.frag");
    
    model = new AnimatedModel("assets/character/mainCharacter.fbx"); 
    ((AnimatedModel*)model)->loadAnimation("assets/character/FastRun.fbx", "FastRun");
    ((AnimatedModel*)model)->loadAnimation("assets/character/rideHorse.fbx", "RideHorse"); 
    ((AnimatedModel*)model)->loadAnimation("assets/character/Idle.fbx", "Idle");
    ((AnimatedModel*)model)->loadAnimation("assets/character/Walking.fbx", "Walking");
    ((AnimatedModel*)model)->loadAnimation("assets/character/rideIdle.fbx", "rideIdle");

    ((AnimatedModel*)model)->setAnimationCyclic("RideHorse", false);
}

Character::~Character() {
    delete model;
    delete characterShader;
}

// --- NOUVELLES FONCTIONS DE MONTURE/ÉTAT ---

/**
 * @brief Définit l'état de monture et la référence au cheval.
 */
void Character::setMountedHorse(Horse* horse) {
    m_MountedHorse = horse;
    m_isRiding = (horse != nullptr);
    
  if (m_isRiding && model) {
        // 1. Set the character to the Mounting transition state
        m_isMounting = true; 
        
        // 2. Start the animation from frame 0
        // NOTE: If you have a separate "Mount.fbx" file, use that name. 
        // Since you used "RideHorse" for the single-shot animation, we'll keep that name here.
        ((AnimatedModel*)model)->updateAnimation("RideHorse", 0.0f); 
    } else {
        // When dismounting, clear the transition flag
        m_isMounting = false;
    }
}

/**
 * @brief Définit la rotation du personnage (utilisée par le cheval).
 */
void Character::setRotation(float angle, const glm::vec3& axis) {
    m_RotationAngle = angle;
    m_ForwardDirection = axis;
}

/**
 * @brief Calcule le vecteur "droite" basé sur la rotation actuelle (utilisé pour le démontage).
 */
Vec3 Character::getRight() const {
    // Calculer la matrice de rotation actuelle
    glm::mat4 rotationMatrix(1.0f);
    if (m_RotationAngle != 0.0f && glm::length(m_ForwardDirection) > 0.0f) {
        rotationMatrix = glm::rotate(rotationMatrix, m_RotationAngle, m_ForwardDirection);
    }
    
    // Appliquer la rotation au vecteur 'droite' initial (1, 0, 0)
    glm::vec4 rightDir = rotationMatrix * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    return Vec3(rightDir.x, rightDir.y, rightDir.z); 
}


/**
 * @brief Updates character position, rotation, and animation state.
 */
void Character::update(Camera* camera, float deltaTime, Terrain* terrain) {

    // --- LOGIQUE DE MONTURE (EXISTING) ---
    // ... (Keep existing mount logic) ... 
    
    // --- PHYSICS-BASED MOVEMENT ---
    if (m_isRiding) {
        
         if (m_isRiding) {
        
        bool dismountAttempted = (keys['E'] || keys['e']);

        if (dismountAttempted && m_MountedHorse) {
            
            m_MountedHorse->dismountCharacter(this);
            
            keys['E'] = false; 
            keys['e'] = false; 
        }
        
        // Le cheval prend le contrôle du mouvement
        if (m_MountedHorse) {
            m_MountedHorse->updateMovement(camera, deltaTime, terrain); 
        }
        
        // ⭐ CRITICAL FIX: Update the character's animation while mounted.
        // This call is required every frame to apply the bone transformations.
        // Since "RideHorse" is non-cyclic and clamped, it holds the final pose.
        if (model) {
           std::string animToPlay = "RideHorse";
            
            // ⭐ CRITICAL FIX: Check if the single-shot animation has completed.
            // Assuming your "Idle" animation is a looping animation (cyclic: true by default).
            if (((AnimatedModel*)model)->isAnimationFinished()) {
                   Vec3 saddleOffset(0.0f, 0.7f, 0.0f); 
                   Vec3 initialPos = this->position + saddleOffset;
    
                this->setPosition(initialPos);
                // Once finished, switch the character's animation to a looping pose
                // that represents the final riding state (e.g., Idle).
                animToPlay = "rideIdle"; 
            }

            // Update the animation with the determined clip name
            m_CurrentAnim = animToPlay;
            ((AnimatedModel*)model)->updateAnimation(animToPlay, deltaTime);        }
        
        return; // 🛑 The function now returns AFTER the animation has been updated.
    }
    }

    // 1. Calculate Target Direction (Camera-Relative)
    Vec3 inputDir(0, 0, 0);
    
    // Get camera vectors (assuming they are normalized)
    Vec3 camFwd = camera->getForward();
    Vec3 camRight = camera->getRight();
    
    // Flatten camera vectors to horizontal plane (y=0) to avoid flying/digging
    camFwd.y = 0; 
    camRight.y = 0;
    if (camFwd.length() > 0.001f) camFwd.normalize();
    if (camRight.length() > 0.001f) camRight.normalize();

    // Note: Using subtract for forward to match original tank-style controls
    // Z moves character in direction camera is facing (forward from camera perspective)
    if (keys['z']|| keys['Z']) { inputDir -= camFwd; }
    if (keys['s']|| keys['S']) { inputDir += camFwd; }
    if (keys['q']|| keys['Q']) { inputDir -= camRight; }
    if (keys['d']|| keys['D']) { inputDir += camRight; }

    // 2. Determine Target Velocity
    Vec3 targetVelocity(0, 0, 0);
    bool isInputActive = (inputDir.length() > 0.1f);
    
    // Check for Run Toggle (Shift key)
    // You might need to add VK_SHIFT handling in your key callback or use GetAsyncKeyState
    // For now, assuming standard WASD. If you want Shift, check input map.
    bool isRunning = (GetKeyState(VK_SHIFT) & 0x8000); 
    
    if (isInputActive) {
        inputDir.normalize();
        float currentSpeedReq = isRunning ? MAX_SPEED : (MAX_SPEED * 0.35f); // 35% speed for walking
        targetVelocity = inputDir * currentSpeedReq;
    }

    // 3. Apply Acceleration / Friction (Inertia)
    Vec3 diff = targetVelocity - velocity;
    
    // If we want to change velocity, apply acceleration
    // If no input, we are relying on Damping (or just decelerating towards 0)
    // We use a simple P-controller approach for velocity:
    // v_new = v_current + (v_target - v_current) * factor
    
    float physicsFactor = isInputActive ? ACCELERATION : DAMPING * 10.0f; 
    // Use deltaTime integration
    // velocity += (targetVelocity - velocity) * (physicsFactor * deltaTime); (Approximate exponential decay)
    
    // Linear approach for more predictable "weight"
    if (diff.length() > 0.001f) {
        Vec3 dir = diff;
        dir.normalize();
        float speedChange = physicsFactor * deltaTime;
        if (speedChange > diff.length()) {
            velocity = targetVelocity;
        } else {
            velocity += dir * speedChange;
        }
    }

    // 4. Update Position
    position += velocity * deltaTime;

    // 5. Smooth Rotation
    // Rotate character to face velocity direction if moving
    if (velocity.length() > 1.0f) { // Only rotate if moving significantly
        glm::vec3 glmVel = glm::normalize(glm::vec3(velocity.x, 0.0f, velocity.z));
        glm::vec3 forward = glm::vec3(0.0f, 0.0f, -1.0f); // Default model forward
        
        // Calculate target angle
        float dot = glm::clamp(glm::dot(forward, glmVel), -1.0f, 1.0f);
        float targetAngle = std::acos(dot);
        glm::vec3 cross = glm::cross(forward, glmVel);
        if (cross.y < 0) targetAngle = -targetAngle;
        
        // Smoothly interpolate current angle towards target
        // We handle the wrap-around case (-PI to PI) implicitly by using vector interpolation for direction
        // But here we store Angle/Axis. Let's interpolate the Axis/Angle or just the forward vector.
        
        // Approach: Interpolate m_ForwardDirection vector directly
        // Current forward derived from simple rotation is tricky. 
        // Let's rely on constructing the rotation from the Velocity direction directly, 
        // but smoothed over time.
        
        // Simple Slerp-like for direction
        float rotSpeed = 10.0f * deltaTime;
        m_ForwardDirection = glm::mix(m_ForwardDirection, glm::vec3(0,1,0), 0.0f); // dummy
        
        // Better: Use ATAN2 for robust yaw calculation
        float currentYaw = m_RotationAngle; 
        // IF axis was (0,1,0), rotation is simple yaw.
        
        // Target Yaw
        // atan2(x, z) gives angle from Z axis? Checking standard math...
        // atan2(velocity.x, velocity.z)
        // Let's stick to the vector mixing which is robust against gimbal lock for Y-axis rotation
        
        // We basically want the model to face "glmVel".
        // Current facing is implicit. Let's make it explicit.
        // We updated m_ForwardDirection in previous code to be the axis (0,1,0 usually).
        // Let's change semantic: m_ForwardDirection = The vector the character FACES.
        
        // ERROR: The render function expects m_ForwardDirection to be the ROTATION AXIS.
        // Let's stick to that contract. Axis is usually (0,1,0). Angle is Yaw.
        
        float targetYaw = std::atan2(glmVel.x, glmVel.z); // -PI to PI
        
        // Smooth changes
        float angleDiff = targetYaw - m_RotationAngle;
        // Normalize angle -PI to PI
        while (angleDiff > glm::pi<float>()) angleDiff -= 2.0f * glm::pi<float>();
        while (angleDiff < -glm::pi<float>()) angleDiff += 2.0f * glm::pi<float>();
        
        m_RotationAngle += angleDiff * glm::min(ROTATION_SPEED * deltaTime, 1.0f);
        m_ForwardDirection = glm::vec3(0.0f, 1.0f, 0.0f); // Always rotate around Y
    }

    // 6. Physics & Gravity
    PhysicsSystem::applyGravity(position, velocity, deltaTime, terrain, 0.01f);

    // 7. Animation State Machine
    if (model) {
        std::string animName = "Idle";
        float speed = velocity.length();
        
        if (speed > 5.0f) {
            if (speed > (MAX_SPEED * 0.6f)) {
                animName = "FastRun";
            } else {
                animName = "Walking";
            }
        }
        
        m_CurrentAnim = animName;
        ((AnimatedModel*)model)->updateAnimation(animName, deltaTime);
    }
}

void Character::render(const glm::mat4& projection, const glm::mat4& view) {
    // ... (Rendu inchangé)
    if (!model || !characterShader) return;

    // 1. Calculate Model Matrix
    glm::mat4 modelMatrix = glm::mat4(1.0f);
    glm::vec3 characterPos(position.x, position.y, position.z);
    
    // Apply translation
    modelMatrix = glm::translate(modelMatrix, characterPos);
    
    // Apply Rotation (from Step 3 in update)
    if (m_RotationAngle != 0.0f && glm::length(m_ForwardDirection) > 0.0f) {
        // Rotate around the rotation axis by the calculated angle
        modelMatrix = glm::rotate(modelMatrix, m_RotationAngle, m_ForwardDirection);
    }
    
    // Apply uniform scaling (Adjust scaleFactor as needed for your model)
    float scaleFactor = 0.01f; 
    modelMatrix = glm::scale(modelMatrix, glm::vec3(scaleFactor)); 
    
    // 2. Activate the character's shader
    characterShader->use();

    // 3. Set the standard matrices (Projection, View, Model) as uniforms
    characterShader->setMat4("projection", projection);
    characterShader->setMat4("view", view);
    characterShader->setMat4("model", modelMatrix); 
    
    // 4. Pass the calculated bone matrices
    ((AnimatedModel*)model)->setBoneUniforms(*characterShader);

    // 5. Draw the animated mesh
    ((AnimatedModel*)model)->draw();
}

Shader& Character::getShader() {
    return *characterShader; 
}

void Character::keyDown(int key) {
    keys[key] = true;
}
void Character::keyUp(int key) {
    keys[key] = false;
}


// NOTE: La dernière accolade '}' a été supprimée de la fin du fichier pour éviter un double '}' si l'utilisateur l'avait mal placée.