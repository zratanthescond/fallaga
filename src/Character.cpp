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
#include "Weapon.h"
#include "Car.h"
#include "GameplayUtils.h"

// Note: These constants are defined in Character.h; we reference them here for consistency
// Moving to header eliminates duplication between .h and .cpp
Character::Character() {
    position = Vec3(4275.0f, -46.5f, -835.0f); // Spawn at Place Pichon / Square de la Marine overlooking port
    velocity = Vec3(0, 0, 0);  // CRITICAL: Initialize velocity to zero
    m_noclipToggled = false;

    characterShader = new Shader("src/shaders/character.vert", "src/shaders/character.frag");
    
    model = new AnimatedModel("assets/character/mainCharacter.fbx"); 
    
    // Load all animations
    ((AnimatedModel*)model)->loadAnimation("assets/character/fastrun.fbx", "FastRun");
    ((AnimatedModel*)model)->loadAnimation("assets/character/rideHorse.fbx", "RideHorse"); 
    ((AnimatedModel*)model)->loadAnimation("assets/character/Idle.fbx", "Idle");
    ((AnimatedModel*)model)->loadAnimation("assets/character/Walking.fbx", "Walking");
    ((AnimatedModel*)model)->loadAnimation("assets/character/rideIdle.fbx", "rideIdle");
    ((AnimatedModel*)model)->loadAnimation("assets/character/DrivingCar.fbx", "DrivingCar");
    ((AnimatedModel*)model)->loadAnimation("assets/character/EntringCar.fbx", "EntringCar");
    ((AnimatedModel*)model)->loadAnimation("assets/character/ExitingCar.fbx", "ExitingCar");
    ((AnimatedModel*)model)->loadAnimation("assets/character/Jump.fbx", "Jump");
    ((AnimatedModel*)model)->loadAnimation("assets/character/Mp40Reloading.fbx", "Mp40Reloading");
    ((AnimatedModel*)model)->loadAnimation("assets/character/RifleAimingIdle.fbx", "RifleAimingIdle");
    ((AnimatedModel*)model)->loadAnimation("assets/character/movingJump.fbx", "movingJump");

    // Configure single-shot animations
    ((AnimatedModel*)model)->setAnimationCyclic("RideHorse", false);
    ((AnimatedModel*)model)->setAnimationCyclic("EntringCar", false);
    ((AnimatedModel*)model)->setAnimationCyclic("ExitingCar", false);
    ((AnimatedModel*)model)->setAnimationCyclic("Jump", false);
    ((AnimatedModel*)model)->setAnimationCyclic("Mp40Reloading", false);
    ((AnimatedModel*)model)->setAnimationCyclic("movingJump", false);

    // Custom Y offsets to ensure perfect terrain contact for each animation
    m_AnimYOffsets["Idle"] = 0.95f;
    m_AnimYOffsets["Walking"] = 0.95f;
    m_AnimYOffsets["FastRun"] = 0.98f;        // FastRun animation shifts center slightly
    m_AnimYOffsets["RifleAimingIdle"] = 0.92f; // Aiming crouch/stance adjustment
    m_AnimYOffsets["Mp40Reloading"] = 0.95f;
    m_AnimYOffsets["Jump"] = 0.0f;
    m_AnimYOffsets["movingJump"] = 0.0f;
    m_AnimYOffsets["EntringCar"] = 0.95f;
    m_AnimYOffsets["ExitingCar"] = 0.95f;

    // Print animation durations for verification
    ((AnimatedModel*)model)->updateAnimation("Jump", 0.0f);
    std::cout << "[DEBUG] Jump Duration: " << ((AnimatedModel*)model)->getAnimationDuration() << " seconds" << std::endl;
    ((AnimatedModel*)model)->updateAnimation("movingJump", 0.0f);
    std::cout << "[DEBUG] movingJump Duration: " << ((AnimatedModel*)model)->getAnimationDuration() << " seconds" << std::endl;
    ((AnimatedModel*)model)->updateAnimation("Idle", 0.0f);
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
void Character::update(Camera* camera, float deltaTime, Terrain* terrain, float time, ParticleSystem* particleSystem) {
    if (terrain) {
        glm::vec3 normal = terrain->getNormalAt(position.x, position.z);
        if (glm::length(normal) > 0.001f) {
            m_TerrainNormal = glm::normalize(normal);
        } else {
            m_TerrainNormal = glm::vec3(0.0f, 1.0f, 0.0f);
        }
    } else {
        m_TerrainNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    }

    // Check if noclip state was recently toggled to handle camera snapping and ground landing
    if (m_noclipToggled) {
        m_noclipToggled = false;
        if (!m_isNoclip) {
            // Snapping player Y position to ground terrain to prevent falling out/disappearing
            GameplayUtils::snapToTerrain(position, terrain, 0.01f);
            velocity = Vec3(0, 0, 0);
        }
        if (camera) {
            camera->reset();
        }
    }

    // --- LOGIQUE DE MONTURE (EXISTING) ---
    if (m_isRiding) {
        bool dismountAttempted = (keys['E'] || keys['e']);

        if (dismountAttempted && m_MountedHorse) {
            m_MountedHorse->dismountCharacter(this);
            m_isMounting = false;  // CRITICAL: Reset mount transition flag even if dismounting mid-animation
            keys['E'] = false;
            keys['e'] = false;
        }
        
        // Le cheval prend le contrôle du mouvement
        if (m_MountedHorse) {
            m_MountedHorse->updateMovement(camera, deltaTime, terrain); 
        }
        
        // ⭐ CRITICAL FIX: Update the character's animation while mounted.
        if (model) {
            std::string animToPlay = m_isMounting ? "RideHorse" : "rideIdle";
            
            if (m_isMounting && ((AnimatedModel*)model)->isAnimationFinished()) {
                m_isMounting = false;
                animToPlay = "rideIdle"; 
                
                Vec3 saddleOffset(0.0f, 0.7f, 0.0f); 
                Vec3 initialPos = this->position + saddleOffset;
                this->setPosition(initialPos);
            }

            m_CurrentAnim = animToPlay;
            ((AnimatedModel*)model)->updateAnimation(animToPlay, deltaTime);
        }
        
        return; // The function returns AFTER the animation has been updated.
    }

    // --- LOGIQUE DE MONTURE DE VOITURE (CAR STATE) ---
    if (m_isDriving || m_isEnteringCar || m_isExitingCar) {
        if (m_isDriving) {
            bool dismountAttempted = (keys['E'] || keys['e']);
            if (dismountAttempted && m_MountedCar) {
                keys['E'] = false; 
                keys['e'] = false; 
                startExitingCar();
            }
        }
        
        if (m_MountedCar) {
            m_RotationAngle = m_MountedCar->getYaw();
            m_ForwardDirection = glm::vec3(0.0f, 1.0f, 0.0f);

            if (m_isEnteringCar) {
                // Keep entry anchor locked to outside the driver door
                if (!m_CarEntryAnchorGrounded) {
                    m_CarEntryAnchor = m_MountedCar->getDriverOutsideWorldPosition();
                    if (terrain) {
                        m_CarEntryAnchor.y = terrain->getGroundSurfaceHeight(
                            m_CarEntryAnchor.x, m_CarEntryAnchor.z) + 0.01f;
                    }
                    m_CarEntryAnchorGrounded = true;
                }
                this->position = Vec3(m_CarEntryAnchor.x, m_CarEntryAnchor.y,
                                      m_CarEntryAnchor.z);
            } else {
                glm::vec3 seatWorld = m_MountedCar->getDriverSeatWorldPosition();
                this->position = Vec3(seatWorld.x, seatWorld.y, seatWorld.z);
            }
        }

        if (model) {
            std::string animName = "DrivingCar";
            if (m_isEnteringCar) {
                animName = "EntringCar";
                if (((AnimatedModel*)model)->isAnimationFinished()) {
                    m_isEnteringCar = false;
                    m_isDriving = true;
                    animName = "DrivingCar";
                    if (m_MountedCar) {
                        glm::vec3 seatWorld = m_MountedCar->getDriverSeatWorldPosition();
                        this->position = Vec3(seatWorld.x, seatWorld.y, seatWorld.z);
                    }
                    ((AnimatedModel*)model)->updateAnimation("DrivingCar", 0.0f);
                }
            } else if (m_isExitingCar) {
                animName = "ExitingCar";
                if (((AnimatedModel*)model)->isAnimationFinished()) {
                    m_isExitingCar = false;
                    m_isDriving = false;
                    
                    if (m_MountedCar) {
                        // Character stepped out 2.04m to the left door onto the ground
                        glm::vec3 exitPos = m_MountedCar->getDriverExitWorldPosition();
                        if (terrain) {
                            exitPos.y = terrain->getGroundSurfaceHeight(exitPos.x, exitPos.z) + 0.01f;
                        }
                        this->position = Vec3(exitPos.x, exitPos.y, exitPos.z);
                        m_MountedCar->dismountCharacter(this);
                        m_MountedCar = nullptr;
                    }
                    animName = "Idle";
                    ((AnimatedModel*)model)->updateAnimation("Idle", 0.0f);
                }
            }
            m_CurrentAnim = animName;
            float animSpeed = (m_isEnteringCar || m_isExitingCar) ? 1.8f : 1.0f;
            ((AnimatedModel*)model)->updateAnimation(animName, deltaTime * animSpeed);
        }
        return;
    }

    // 1. Calculate Target Direction (Camera-Relative)
    Vec3 inputDir(0, 0, 0);
    
    // Get camera vectors
    Vec3 camFwd = camera->getForward();
    Vec3 camRight = camera->getRight();
    
    if (!m_isNoclip) {
        // Flatten camera vectors to horizontal plane (y=0) to avoid flying/digging
        camFwd.y = 0; 
        camRight.y = 0;
    }
    if (camFwd.length() > 0.001f) camFwd.normalize();
    if (camRight.length() > 0.001f) camRight.normalize();

    // Note: Using subtract for forward to match original tank-style controls
    if (keys['z']|| keys['Z']) { inputDir += camFwd; }
    if (keys['s']|| keys['S']) { inputDir -= camFwd; }
    if (keys['q']|| keys['Q']) { inputDir -= camRight; }
    if (keys['d']|| keys['D']) { inputDir += camRight; }

    if (m_isNoclip) {
        if (keys[' ']) { inputDir.y += 1.0f; }
        if (keys['c'] || keys['C'] || keys['x'] || keys['X'] || keys[341]) { // 341 is Left Ctrl
            inputDir.y -= 1.0f;
        }
    }

    // 2. Determine Target Velocity
    Vec3 targetVelocity(0, 0, 0);
    bool isInputActive = (inputDir.length() > 0.1f);
    
    // Check for Run (Shift key)
    bool isRunning = (keys[340] || keys[344] || (GetKeyState(VK_SHIFT) & 0x8000)); 
    
    if (isInputActive) {
        inputDir.normalize();
        if (m_isNoclip) {
            float noclipSpeed = isRunning ? 500.0f : 120.0f;
            targetVelocity = inputDir * noclipSpeed;
        } else {
            float currentSpeedReq = isRunning ? MAX_SPEED : (MAX_SPEED * 0.35f); // 35% speed for walking
            targetVelocity = inputDir * currentSpeedReq;
        }
    }

    // 3. Apply Acceleration / Friction (Inertia)
    Vec3 diff = targetVelocity - velocity;
    float physicsFactor = isInputActive ? ACCELERATION : DAMPING * 10.0f; 
    if (m_isNoclip) {
        physicsFactor = 1000.0f; // Snappy direct response in noclip
    }
    
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
    if (velocity.length() > 1.0f) { 
        glm::vec3 glmVel = glm::normalize(glm::vec3(velocity.x, 0.0f, velocity.z));
        if (glm::length(glmVel) > 0.001f) {
            float targetYaw = std::atan2(glmVel.x, glmVel.z); // -PI to PI
            float angleDiff = targetYaw - m_RotationAngle;
            // Normalize angle -PI to PI
            while (angleDiff > glm::pi<float>()) angleDiff -= 2.0f * glm::pi<float>();
            while (angleDiff < -glm::pi<float>()) angleDiff += 2.0f * glm::pi<float>();
            
            m_RotationAngle += angleDiff * glm::min(ROTATION_SPEED * deltaTime, 1.0f);
            m_ForwardDirection = glm::vec3(0.0f, 1.0f, 0.0f); // Always rotate around Y
        }
    }

    // 6. Physics & Gravity
    if (!m_isNoclip) {
        if (m_isJumping) {
            // Keep the character's logical Y position on the terrain.
            // The jump height is driven entirely by the animation's internal transforms.
            GameplayUtils::snapToTerrain(position, terrain, 0.01f);
            velocity.y = 0.0f;
        } else {
            PhysicsSystem::applyGravity(position, velocity, deltaTime, terrain, 0.01f);
        }
    }

    // 7. Ground Check & Jump Logic
    float terrainHeight = terrain ? terrain->getGroundSurfaceHeight(position.x, position.z) : 0.0f;
    bool isGrounded = !m_isNoclip && (position.y <= terrainHeight + 0.1f);
    
    // Landing detection
    if (m_isJumping) {
        if (model && ((AnimatedModel*)model)->isAnimationFinished()) {
            m_isJumping = false;
        }
    }

    // Jump Input: Spacebar (only when grounded, not aiming/riding/driving)
    if (isGrounded && !m_isJumping && keys[' '] && !isAiming() && canPerformGeneralAction()) {
        m_isJumping = true;
        velocity.y = 0.0f; // Let animation handle the Y trajectory
        
        float horizSpeed = sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
        std::string jumpAnim = (horizSpeed > 1.0f) ? "movingJump" : "Jump";
        if (model) {
            ((AnimatedModel*)model)->updateAnimation(jumpAnim, 0.0f);
            m_CurrentAnim = jumpAnim;
        }
    }

    // 8. Weapon Logic
    if (weaponManager && m_IsWeaponDrawn) {
        if (m_MuzzleFlashTimer > 0.0f) {
            m_MuzzleFlashTimer -= deltaTime;
        }
        if (m_WeaponKickback > 0.0f) {
            m_WeaponKickback -= deltaTime * 10.0f; // Quick recovery
            if (m_WeaponKickback < 0.0f) m_WeaponKickback = 0.0f;
        }
        
        // Shoot key (Spacebar) - only when aiming
        if (keys[' '] && isAiming()) { 
            if (m_FireTimer <= 0.0f) {
                m_FireTimer = 0.1f; // 10 shots per second
                m_MuzzleFlashTimer = 0.05f;
                m_WeaponKickback = 0.5f;
                if (camera) {
                    camera->applyRecoil(1.0f, 0.5f);
                }
            }
        }
        if (m_FireTimer > 0.0f) {
            m_FireTimer -= deltaTime;
        }
        
        // Trigger Reload (starts reload animation)
        if (!m_IsReloading && (keys['r'] || keys['R']) && canPerformGeneralAction()) {
            m_IsReloading = true;
            if (model) {
                ((AnimatedModel*)model)->updateAnimation("Mp40Reloading", 0.0f);
                m_CurrentAnim = "Mp40Reloading";
            }
        }
    }

    // 9. Animation State Machine
    if (model) {
        std::string animName = "Idle";

        if (m_isJumping) {
            float horizSpeed = sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
            animName = (horizSpeed > 1.0f) ? "movingJump" : "Jump";
        }
        else if (m_IsReloading) {
            animName = "Mp40Reloading";
            if (((AnimatedModel*)model)->isAnimationFinished()) {
                m_IsReloading = false;
                animName = "Idle";
            }
        }
        else if (isAiming()) {
            animName = "RifleAimingIdle";
        }
        else {
            // Standard locomotion
            float speed = velocity.length();
            if (speed > 1.0f) {
                if (isRunning && speed > (MAX_SPEED * 0.5f)) {
                    animName = "FastRun";
                } else {
                    animName = "Walking";
                }
            } else {
                animName = "Idle";
            }
        }

        if (m_CurrentAnim != animName) {
            ((AnimatedModel*)model)->updateAnimation(animName, 0.0f);
            m_CurrentAnim = animName;
        } else {
            ((AnimatedModel*)model)->updateAnimation(animName, deltaTime);
        }
    }
}

void Character::render(const glm::mat4 &projection, const glm::mat4 &view, 
                       const glm::vec3& lightDir, const glm::vec3& lightColor, Camera* camera) {
    if (!model || !characterShader) return;

    glm::mat4 rotationMatrix(1.0f);
    if (m_isRiding && m_MountedHorse) {
        rotationMatrix = m_MountedHorse->m_PitchRollMatrix;
    } else if ((m_isDriving || m_isEnteringCar || m_isExitingCar) && m_MountedCar) {
        float yawOffset = m_isExitingCar ? glm::pi<float>() : 0.0f;
        rotationMatrix = glm::rotate(rotationMatrix, -m_MountedCar->getYaw() + m_MountedCar->getDriftAngle() * 0.1f + yawOffset, glm::vec3(0.0f, 1.0f, 0.0f));
        rotationMatrix = glm::rotate(rotationMatrix, m_MountedCar->getPitchAngle(), glm::vec3(1.0f, 0.0f, 0.0f));
        rotationMatrix = glm::rotate(rotationMatrix, m_MountedCar->getRollAngle(), glm::vec3(0.0f, 0.0f, 1.0f));
    } else {
        glm::vec3 U = m_TerrainNormal;
        if (glm::length(U) < 0.001f) U = glm::vec3(0.0f, 1.0f, 0.0f);
        
        float yaw = m_RotationAngle;
        glm::mat4 R_yaw = glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 R_slope(1.0f);
        glm::vec3 A(0.0f, 1.0f, 0.0f);
        glm::vec3 B = U;
        glm::vec3 axis = glm::cross(A, B);
        if (glm::length(axis) > 0.001f) {
            axis = glm::normalize(axis);
            float angle = std::acos(glm::clamp(glm::dot(A, B), -1.0f, 1.0f));
            R_slope = glm::rotate(glm::mat4(1.0f), angle, axis);
        }
        rotationMatrix = R_slope * R_yaw;
    }

    glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(position.x, position.y, position.z));
    modelMatrix = modelMatrix * rotationMatrix;
    modelMatrix = glm::rotate(modelMatrix, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    modelMatrix = glm::scale(modelMatrix, glm::vec3(0.01f)); 

    float yOffset = 0.0f;
    if (canPerformGeneralAction()) {
        if (m_CurrentAnim == "Jump" || m_CurrentAnim == "movingJump") {
            yOffset = 0.0f;
        } else {
            // Locomotion/standing/aiming/reloading: align dynamically with the terrain height under each foot
            Terrain* terrain = camera ? camera->getTerrain() : nullptr;
            if (terrain) {
                // Get foot bone transforms relative to model space
                glm::mat4 leftFootTransform = ((AnimatedModel*)model)->getBoneModelSpaceTransform("mixamorig:LeftFoot");
                if (leftFootTransform == glm::mat4(1.0f)) {
                    leftFootTransform = ((AnimatedModel*)model)->getBoneModelSpaceTransform("LeftFoot");
                }
                glm::mat4 rightFootTransform = ((AnimatedModel*)model)->getBoneModelSpaceTransform("mixamorig:RightFoot");
                if (rightFootTransform == glm::mat4(1.0f)) {
                    rightFootTransform = ((AnimatedModel*)model)->getBoneModelSpaceTransform("RightFoot");
                }

                // Compute world coordinates of feet without yOffset
                glm::vec4 leftFootWorld = modelMatrix * leftFootTransform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                glm::vec4 rightFootWorld = modelMatrix * rightFootTransform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

                // Sample terrain height directly under each foot
                float leftTerrainHeight = terrain->getGroundSurfaceHeight(leftFootWorld.x, leftFootWorld.z);
                float rightTerrainHeight = terrain->getGroundSurfaceHeight(rightFootWorld.x, rightFootWorld.z);

                // Anatomical constant: distance from ankle joint to the sole of the foot (approx. 0.10m)
                const float ANKLE_TO_SOLE = 0.10f;

                // Offset needed to put the sole of the foot on the terrain surface
                float leftOffsetNeeded = leftTerrainHeight - leftFootWorld.y + ANKLE_TO_SOLE;
                float rightOffsetNeeded = rightTerrainHeight - rightFootWorld.y + ANKLE_TO_SOLE;

                // Take the maximum offset to lift the character enough so that neither foot sinks into the ground
                yOffset = std::max(leftOffsetNeeded, rightOffsetNeeded);

                // Safety clamp to prevent anomalies/spikes during animation transitions
                yOffset = std::clamp(yOffset, 0.75f, 1.45f);
            } else {
                yOffset = 0.95f;
            }
        }
    }
    m_LastComputedYOffset = yOffset;

    characterShader->use();
    characterShader->setMat4("projection", projection);
    characterShader->setMat4("view", view);
    characterShader->setMat4("model", modelMatrix); 
    characterShader->setVec3("lightDir", lightDir);
    characterShader->setVec3("lightColor", lightColor);
    characterShader->setFloat("yOffset", yOffset);
    
    if (camera) {
        Vec3 camPos = camera->getPosition();
        characterShader->setVec3("viewPos", glm::vec3(camPos.x, camPos.y, camPos.z));
    }
    
    ((AnimatedModel*)model)->setBoneUniforms(*characterShader);
    ((AnimatedModel*)model)->draw();

    // Render Weapon
    if (m_IsWeaponDrawn && weaponManager && weaponManager->getCurrentWeapon()) {
        Weapon *weapon = weaponManager->getCurrentWeapon();
        if (weapon->model) {
            glm::mat4 weaponModelMatrix = getWeaponModelMatrix(true);
            // Translate the weapon by the same yOffset to align it with the character's hands
            weaponModelMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, yOffset, 0.0f)) * weaponModelMatrix;

            weaponShader->use();
            weaponShader->setMat4("projection", projection);
            weaponShader->setMat4("view", view);
            weaponShader->setMat4("model", weaponModelMatrix);
            weaponShader->setBool("isInstanced", false);
            weaponShader->setBool("deformToTerrain", false);
            weaponShader->setVec3("lightDir", lightDir);
            if (camera) {
                Vec3 camPos = camera->getPosition();
                weaponShader->setVec3("viewPos", glm::vec3(camPos.x, camPos.y, camPos.z));
            }
            
            glm::vec3 finalLightColor = lightColor;
            if (m_MuzzleFlashTimer > 0.0f) {
                finalLightColor = glm::vec3(5.0f, 4.0f, 2.0f); // Bright Flash
            }
            weaponShader->setVec3("lightColor", finalLightColor);

            for (int i = 0; i < weapon->model->getMeshCount(); i++) {
                std::string meshName = weapon->model->getMeshName(i);
                std::string lowerName = meshName;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                
                bool isLoader = (lowerName.find("loader") != std::string::npos || lowerName.find("magazine") != std::string::npos || lowerName.find("mag") != std::string::npos);

                if (isLoader && m_IsReloading) {
                    glm::mat4 leftHandTransform = ((AnimatedModel*)model)->getBoneWorldTransform("mixamorig:LeftHand");
                    if (leftHandTransform == glm::mat4(1.0f)) {
                        leftHandTransform = ((AnimatedModel*)model)->getBoneWorldTransform("LeftHand"); 
                    }
                    
                    glm::mat4 weaponScale = glm::scale(glm::mat4(1.0f), weapon->scaleOffset);
                    glm::mat4 loaderOffset = glm::translate(glm::mat4(1.0f), glm::vec3(-10.0f, 0.0f, 0.0f)); 
                    glm::mat4 loaderMatrix = modelMatrix * leftHandTransform * loaderOffset * weaponScale; 
                    
                    weaponShader->setMat4("model", loaderMatrix);
                    weapon->model->renderMesh(i, *weaponShader);
                    weaponShader->setMat4("model", weaponModelMatrix);
                } else {
                    weapon->model->renderMesh(i, *weaponShader);
                }
            }
        }
    }
}

Shader& Character::getShader() {
    return *characterShader; 
}

void Character::toggleNoclip() {
    m_isNoclip = !m_isNoclip;
    m_noclipToggled = true;
    if (m_isNoclip) {
        if (m_isRiding && m_MountedHorse) {
            m_MountedHorse->dismountCharacter(this);
        }
        if (m_isDriving) {
            m_isDriving = false;
            m_MountedCar = nullptr;
        }
        m_isEnteringCar = false;
        m_isExitingCar = false;
        velocity.y = 0.0f;
    } else {
        velocity.y = 0.0f;
    }
    std::cout << "[NOCLIP] " << (m_isNoclip ? "ENABLED" : "DISABLED") << " - Speed: 40.0 / 150.0 (Shift)" << std::endl;
}

void Character::keyDown(int key) {
    keys[key] = true;
    if (key == 'n' || key == 'N') {
        toggleNoclip();
    }
}
void Character::keyUp(int key) {
    keys[key] = false;
}


// NOTE: La dernière accolade '}' a été supprimée de la fin du fichier pour éviter un double '}' si l'utilisateur l'avait mal placée.
#include "Weapon.h"

void Character::setMountedCar(Car *car) {
    m_MountedCar = car;
}
void Character::startEnteringCar(Car *car) {
    if (!car) return;
    m_MountedCar = car;
    m_isEnteringCar = true;
    m_isDriving = false;
    m_isExitingCar = false;
    
    // Anchor the character entity at the entry position outside the driver door.
    // As EntringCar plays, root motion moves the character +1.97m on X into getDriverSeatWorldPosition().
    glm::vec3 outsidePos = car->getDriverOutsideWorldPosition();
    m_CarEntryAnchor = outsidePos;
    m_CarEntryAnchorGrounded = false;
    this->position = Vec3(outsidePos.x, outsidePos.y, outsidePos.z);
    m_RotationAngle = car->getYaw();
    m_ForwardDirection = glm::vec3(0.0f, 1.0f, 0.0f);

    if (model) {
        ((AnimatedModel*)model)->updateAnimation("EntringCar", 0.0f);
    }
}
void Character::startExitingCar() {
    if (!m_MountedCar) return;
    m_isDriving = false;
    m_isEnteringCar = false;
    m_isExitingCar = true;

    glm::vec3 seatWorld = m_MountedCar->getDriverSeatWorldPosition();
    this->position = Vec3(seatWorld.x, seatWorld.y, seatWorld.z);

    if (model) {
        ((AnimatedModel*)model)->updateAnimation("ExitingCar", 0.0f);
    }
}

void Character::setDrivingNpcCar(bool driving) {
    m_isDrivingNpcCar = driving;
}

void Character::initWeapons() {
    weaponManager = new WeaponManager();
    weaponShader = new Shader("src/shaders/model.vert", "src/shaders/model.frag");
    
    // MP40
    Weapon mp40;
    mp40.model = new ObjModel("assets/guns/mp40/MP40.fbx");
    mp40.positionOffset = glm::vec3(9.15002f, 4.55f, 0.7f);
    mp40.rotationOffset = glm::vec3(290.0f, 340.0f, 270.0f);
    mp40.scaleOffset = glm::vec3(0.5f, 0.5f, 0.5f);
    mp40.name = "MP40 Submachine Gun";
    mp40.fireRate = 0.1f;
    mp40.recoilPitch = 0.5f;
    mp40.recoilYawMin = -0.2f;
    mp40.recoilYawMax = 0.2f;
    mp40.recoilRecovery = 2.0f;
    mp40.visualRecoilMult = 1.0f;
    
    mp40.poseOffsets["Idle"] = { mp40.positionOffset, mp40.rotationOffset };
    mp40.poseOffsets["Walk"] = { mp40.positionOffset, mp40.rotationOffset };
    mp40.poseOffsets["Run"] = { mp40.positionOffset, mp40.rotationOffset };
    mp40.poseOffsets["Aim"] = { mp40.positionOffset, mp40.rotationOffset };
    mp40.poseOffsets["EnterCar"] = { mp40.positionOffset, mp40.rotationOffset };
    mp40.poseOffsets["ExitCar"] = { mp40.positionOffset, mp40.rotationOffset };
    mp40.poseOffsets["Drive"] = { mp40.positionOffset, mp40.rotationOffset };
    weaponManager->addWeapon(mp40);
    
    m_IsWeaponDrawn = true;
}

void Character::drawWeapon(bool draw) { m_IsWeaponDrawn = draw; }

bool Character::isAiming() const {
    auto it = keys.find((int)'2');
    bool isRightClick = (it != keys.end() && it->second);
    return m_IsWeaponDrawn && isRightClick; 
}


Vec3 Character::getRightHandLocalOffset() const {
    return Vec3(0,0,0);
}

glm::mat4 Character::getWeaponModelMatrix(bool applyKickback) const {
    glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(position.x, position.y, position.z));
    if (m_RotationAngle != 0.0f && glm::length(m_ForwardDirection) > 0.0f) {
        modelMatrix = glm::rotate(modelMatrix, m_RotationAngle, m_ForwardDirection);
    }
    modelMatrix = glm::rotate(modelMatrix, glm::radians(-90.0f), glm::vec3(1, 0, 0));
    modelMatrix = glm::scale(modelMatrix, glm::vec3(0.01f));

    glm::mat4 handTransform = ((AnimatedModel*)model)->getBoneWorldTransform("mixamorig:RightHand");
    if (handTransform == glm::mat4(1.0f)) {
        handTransform = ((AnimatedModel*)model)->getBoneWorldTransform("RightHand"); // Fallback
    }
    
    Weapon *weapon = weaponManager->getCurrentWeapon();
    glm::mat4 weaponModelMatrix = glm::mat4(1.0f);
    
    glm::mat4 weaponScale = glm::scale(glm::mat4(1.0f), weapon->scaleOffset);
    
    const aiNode* handleNode = weapon->model->findNode("weaponHandle");
    if (handleNode) {
        glm::mat4 handleTransform = weapon->model->getNodeAbsoluteTransform(handleNode);
        glm::mat4 invHandle = glm::inverse(handleTransform);
        
        auto pose = weapon->poseOffsets.find(m_CurrentAnim);
        glm::vec3 posOffsetVec = weapon->positionOffset;
        glm::vec3 rotOffsetVec = weapon->rotationOffset;
        if (pose != weapon->poseOffsets.end()) {
            posOffsetVec = pose->second.positionOffset;
            rotOffsetVec = pose->second.rotationOffset;
        }
        
        if (applyKickback) {
            rotOffsetVec.x += m_WeaponKickback * 20.0f;
            posOffsetVec.z -= m_WeaponKickback * 1.5f;
            posOffsetVec.y += m_WeaponKickback * 1.0f;
        }
        
        glm::mat4 transOffset = glm::translate(glm::mat4(1.0f), posOffsetVec);
        glm::mat4 rotOffset = glm::rotate(glm::mat4(1.0f), glm::radians(rotOffsetVec.x), glm::vec3(1, 0, 0));
        rotOffset = glm::rotate(rotOffset, glm::radians(rotOffsetVec.y), glm::vec3(0, 1, 0));
        rotOffset = glm::rotate(rotOffset, glm::radians(rotOffsetVec.z), glm::vec3(0, 0, 1));
        
        // Critical FIX from earlier: multiply invHandle *BEFORE* weaponScale
        weaponModelMatrix = modelMatrix * handTransform * transOffset * rotOffset * weaponScale * invHandle;
    } else {
        // manual fallback
        glm::mat4 transOffset = glm::translate(glm::mat4(1.0f), weapon->positionOffset);
        glm::mat4 rotOffset = glm::rotate(glm::mat4(1.0f), glm::radians(weapon->rotationOffset.x), glm::vec3(1, 0, 0));
        rotOffset = glm::rotate(rotOffset, glm::radians(weapon->rotationOffset.y), glm::vec3(0, 1, 0));
        rotOffset = glm::rotate(rotOffset, glm::radians(weapon->rotationOffset.z), glm::vec3(0, 0, 1));
        
        weaponModelMatrix = modelMatrix * handTransform * transOffset * rotOffset * weaponScale;
    }
    
    return weaponModelMatrix;
}

bool Character::getWeaponBarrelLine(glm::vec3& outMuzzleWorld, glm::vec3& outBarrelDir) {
    if (!weaponManager || !weaponManager->getCurrentWeapon() || !weaponManager->getCurrentWeapon()->model) return false;
    Weapon *weapon = weaponManager->getCurrentWeapon();
    
    glm::mat4 weaponModelMatrix = getWeaponModelMatrix(false);
    float yOffset = m_LastComputedYOffset;
    weaponModelMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, yOffset, 0.0f)) * weaponModelMatrix;
    
    bool foundMuzzle = false;
    glm::vec3 muzzleLocalCenter(0.0f);
    for (int i = 0; i < weapon->model->getMeshCount(); i++) {
        std::string meshName = weapon->model->getMeshName(i);
        std::string lowerName = meshName;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
        if (lowerName.find("muzzle") != std::string::npos) {
            muzzleLocalCenter = weapon->model->getMeshCenter(i);
            foundMuzzle = true;
            break;
        }
    }
    
    glm::vec4 muzzleLocal(1.0f);
    glm::vec4 rearLocal(1.0f);
    
    if (foundMuzzle) {
        muzzleLocal = glm::vec4(muzzleLocalCenter, 1.0f);
        rearLocal = glm::vec4(muzzleLocalCenter.x, muzzleLocalCenter.y, muzzleLocalCenter.z - 20.0f, 1.0f);
    } else {
        float barrelX = -0.7f;
        float barrelY = 45.3f;
        muzzleLocal = glm::vec4(barrelX, barrelY, 57.76f, 1.0f);
        rearLocal = glm::vec4(barrelX, barrelY, -20.0f, 1.0f);
    }
    
    glm::vec4 muzzleWorld = weaponModelMatrix * muzzleLocal;
    glm::vec4 rearWorld = weaponModelMatrix * rearLocal;
    
    outMuzzleWorld = glm::vec3(muzzleWorld);
    outBarrelDir = glm::normalize(glm::vec3(muzzleWorld - rearWorld));
    return true;
}
