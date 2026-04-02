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

class Character {
    Shader* characterShader; // Si le shader est géré par Character
private:
    // Constants for RDR2 Feel (Adjust these to fit your 60,000 unit scale)
    const float MAX_SPEED = 100.0f;    // Max velocity (units/sec)
    const float ACCELERATION = 5000.0f; // How quickly speed is gained
    const float DAMPING = 8.0f;         // How quickly speed is lost when keys are released
    const float ROTATION_SPEED = 30.0f; // Degrees per second rotation limit

    // State Variables
    Vec3 position;
    Vec3 velocity;      // Current movement velocity
    float currentYAngle; // Current direction the character model is facing (for rendering)
    bool m_isRiding = false;
    
    // NOUVEAU MEMBRE CRITIQUE: Le cheval sur lequel le joueur est monté
    Horse* m_MountedHorse = nullptr; 

    // Input state
   
    
    AnimatedModel* model;
    float m_RotationAngle = 0.0f;
    glm::vec3 m_ForwardDirection = glm::vec3(0.0f, 0.0f, -1.0f);
    std::string m_CurrentAnim = "Idle";
    
public:
    Character();
    ~Character();
    
    Shader& getShader();
    std::map<int, bool> keys;
    void update(Camera* camera, float deltaTime, class Terrain* terrain);
    void render( const glm::mat4& projection, const glm::mat4& view);

    // Input handlers
    void keyDown(int key);
    void keyUp(int key);

    // Accessors
    Vec3 getPosition() const { return position; }
    void setPosition(const Vec3& newPos) { position = newPos; }
    
    // Méthodes de Monture
    void setMountedHorse(Horse* horse);
    bool isRiding() const { return m_isRiding; }
    
    // CORRECTION: Retourne une référence constante à la carte
    const std::map<int, bool>& getKeys() const { return keys; }
    
    void setRotation(float angle, const glm::vec3& axis); 
    Vec3 getRight() const; 
    bool m_isMounting = false;
};
// NOTE: La dernière accolade '}' a été supprimée de la fin du fichier pour éviter un double '}' si l'utilisateur l'avait mal placée.