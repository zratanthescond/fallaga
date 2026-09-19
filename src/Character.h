#pragma once

#include "Vec3.h"
#include <map>
#include <cmath>
#include <algorithm> 
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Shader.h"

// Forward declarations to avoid circular includes
class Camera;
class AnimatedModel;
class Horse; 
class Terrain;
class Car;
class ObjModel;
class WeaponManager;
class ParticleSystem;

class Character {
    Shader* characterShader; // Si le shader est géré par Character
private:
    // Constants for RDR2 Feel (Adjust these to fit realistic human scale)
    const float MAX_SPEED = 6.0f;       // Max velocity (units/sec)
    const float ACCELERATION = 35.0f;   // How quickly speed is gained
    const float DAMPING = 8.0f;         // How quickly speed is lost when keys are released
    const float ROTATION_SPEED = 10.0f; // Degrees per second rotation limit

    // State Variables
    Vec3 position;
    Vec3 velocity;      // Current movement velocity
    float currentYAngle; // Current direction the character model is facing (for rendering)
    bool m_isRiding = false;
    bool m_isNoclip = false;
    bool m_noclipToggled = false;
    
    // NOUVEAU MEMBRE CRITIQUE: Le cheval sur lequel le joueur est monté
    Horse* m_MountedHorse = nullptr; 

    // Input state
   
    
    AnimatedModel* model;
    float m_RotationAngle = 0.0f;
    glm::vec3 m_ForwardDirection = glm::vec3(0.0f, 0.0f, -1.0f);
    std::string m_CurrentAnim = "Idle";

    // Weapon state
    WeaponManager* weaponManager = nullptr;
    Shader* weaponShader = nullptr;
    bool m_IsWeaponDrawn = false;
    bool m_IsReloading = false;
    bool m_isJumping = false; // Added to track jump state
    float m_MuzzleFlashTimer = 0.0f;
    float m_WeaponKickback = 0.0f;
    float m_FireTimer = 0.0f;

    // Car/Driving state
    Car* m_MountedCar = nullptr;
    bool m_isDriving = false;
    bool m_isEnteringCar = false;
    bool m_isExitingCar = false;
    bool m_isDrivingNpcCar = false;
    glm::vec3 m_CarEntryAnchor = glm::vec3(0.0f);


    // Terrain normal for character alignment
    glm::vec3 m_TerrainNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    
    // Custom Y offsets per animation to ensure perfect terrain contact
    std::map<std::string, float> m_AnimYOffsets;
    float m_LastComputedYOffset = 0.95f;
    
public:
    Character();
    ~Character();
    
    Shader& getShader();
    std::map<int, bool> keys;
    void update(Camera* camera, float deltaTime, class Terrain* terrain, float time = 0.0f, ParticleSystem* particleSystem = nullptr);
    void render(const glm::mat4& projection, const glm::mat4& view,
                const glm::vec3& lightDir = glm::vec3(0.0f, 1.0f, 0.0f),
                const glm::vec3& lightColor = glm::vec3(1.0f),
                Camera* camera = nullptr);

    // Input handlers
    void keyDown(int key);
    void keyUp(int key);

    // Accessors
    Vec3 getPosition() const { return position; }
    void setPosition(const Vec3& newPos) { position = newPos; }
    Vec3 getVelocity() const { return velocity; }
    
    // Méthodes de Monture
    void setMountedHorse(Horse* horse);
    bool isRiding() const { return m_isRiding; }
    bool isNoclip() const { return m_isNoclip; }
    void toggleNoclip();
    
    // CORRECTION: Retourne une référence constante à la carte
    const std::map<int, bool>& getKeys() const { return keys; }

    // State machine helper methods to reduce conditional duplication
    bool canPerformGeneralAction() const {
        return !m_isRiding && !m_isDriving && !m_isEnteringCar && !m_isExitingCar;
    }

    bool isInVehicularState() const {
        return m_isRiding || m_isDriving || m_isEnteringCar || m_isExitingCar;
    }

    bool isTransitioningVehicle() const {
        return m_isEnteringCar || m_isExitingCar;
    }

    void setRotation(float angle, const glm::vec3& axis);
    Vec3 getRight() const; 
    float getRotationAngle() const { return m_RotationAngle; }
    glm::vec3 getForwardDirection() const { return m_ForwardDirection; }
    bool m_isMounting = false;

    // Car methods
    void setMountedCar(Car* car);
    void startEnteringCar(Car* car);
    void startExitingCar();
    void setDrivingNpcCar(bool driving);
    bool isDriving() const { return m_isDriving; }
    bool isEnteringCar() const { return m_isEnteringCar; }
    bool isExitingCar() const { return m_isExitingCar; }
    bool isDrivingOrTransitioning() const { return m_isDriving || m_isEnteringCar || m_isExitingCar; }


    // Weapon methods
    void initWeapons();
    void drawWeapon(bool draw);
    bool isAiming() const;
    Vec3 getRightHandLocalOffset() const;
    glm::mat4 getWeaponModelMatrix(bool applyKickback) const;
    bool getWeaponBarrelLine(glm::vec3& outMuzzleWorld, glm::vec3& outBarrelDir);
};