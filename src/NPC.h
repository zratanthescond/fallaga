#ifndef NPC_H
#define NPC_H

#include "Vec3.h"
#include "AnimatedModel.h"
#include "Shader.h"
#include "PhysicsSystem.h"
#include "Terrain.h"

class NPC {
public:
    Vec3 position;
    Vec3 velocity;
    float rotation = 0.0f;
    std::string currentAnimation = "Idle";
    AnimatedModel* model = nullptr;

    NPC(const std::string& modelPath, Vec3 spawnPos, Terrain* terrain) {
        position = spawnPos;
        velocity = Vec3(0, 0, 0);
        model = new AnimatedModel(modelPath);
        
        // Spawn correctly on terrain
        PhysicsSystem::snapToGround(position, terrain, 0.01f);
    }

    ~NPC() {
        if (model) delete model;
    }

    void update(float deltaTime, Terrain* terrain) {
        // Apply gravity system
        PhysicsSystem::applyGravity(position, velocity, deltaTime, terrain, 0.01f);

        // Simple animation update
        if (model) {
            model->updateAnimation(currentAnimation, deltaTime);
        }
    }

    void render(Shader& shader, const glm::mat4& projection, const glm::mat4& view) {
        if (!model) return;

        glm::mat4 modelMat = glm::mat4(1.0f);
        modelMat = glm::translate(modelMat, glm::vec3(position.x, position.y, position.z));
        modelMat = glm::rotate(modelMat, rotation, glm::vec3(0, 1, 0));
        modelMat = glm::scale(modelMat, glm::vec3(0.01f)); // Match character scale

        shader.use();
        shader.setMat4("projection", projection);
        shader.setMat4("view", view);
        shader.setMat4("model", modelMat);
        
        model->setBoneUniforms(shader);
        model->draw();
    }
};

#endif
