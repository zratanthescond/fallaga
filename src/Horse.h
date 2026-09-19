// --- Horse.h (Assurez-vous que ces éléments existent) ---

#pragma once

#include "AnimatedModel.h"
#include "Shader.h"
#include "Vec3.h" // Ou votre propre classe de vecteur
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
class Character; // Déclaration anticipée
class Camera; // Déclaration anticipée
class ObjModel; 
class Terrain;
class Horse {
private:
    AnimatedModel* model = nullptr;
    Shader* horseShader = nullptr;
        Character* m_MountedCharacter = nullptr;
    const float m_WalkSpeed = 15.0f;
    const float m_RunSpeed = 40.0f;
    // État du cheval
    Vec3 position; 
    Vec3 velocity; // NEW: Horse vertical and horizontal velocity
    float m_RotationAngle = 0.0f;
    glm::vec3 m_ForwardDirection = glm::vec3(0.0f, 0.0f, -1.0f);
    std::string m_CurrentAnimation = "Idle"; // Animation par défaut
float m_CurrentYaw = 0.0f;
const float ROTATION_SPEED = 8.0f; // Controls turning speed
public:
    Horse();
    ~Horse(); // Nécessaire pour nettoyer les pointeurs
 glm::vec3 Horse::getHorizontalForward() const;
     glm::mat4 m_PitchRollMatrix = glm::mat4(1.0f);
    void render(const glm::mat4& projection, const glm::mat4& view);
      void update(float deltaTime, class Terrain* terrain); 
    void updateMovement(Camera* camera, float deltaTime, class Terrain* terrain); 
    void mountCharacter(Character* character);
    void dismountCharacter(Character* character);
    
    Shader& getShader() { return *horseShader; }

    // Ajoutez un getter pour l'interaction
    Vec3 getPosition() const { return position; } // Prend les matrices de la caméra
};
