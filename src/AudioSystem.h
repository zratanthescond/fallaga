#ifndef AUDIOSYSTEM_H
#define AUDIOSYSTEM_H

#include <glm/glm.hpp>

class AudioSystem {
public:
    static AudioSystem& getInstance();

    bool init();
    void cleanup();

    // Set listener position and orientation for 3D positional audio
    void updateListener(const glm::vec3& pos, const glm::vec3& forward, const glm::vec3& up);

    // Set the parameters for the engine sound crossfading/pitching and 3D position
    void updateEngine(float rpm, float throttle, float maxRpm, const glm::vec3& position = glm::vec3(0.0f));
    
    // One-shot events with 3D positions
    void playIgnition(const glm::vec3& position = glm::vec3(0.0f));
    void playStop(const glm::vec3& position = glm::vec3(0.0f));
    void playHorn(const glm::vec3& position = glm::vec3(0.0f));
    void stopHorn();

private:
    AudioSystem();
    ~AudioSystem();

    // Prevent copying
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    bool m_initialized;
    
    // miniaudio structs (void* to avoid including miniaudio.h everywhere)
    void* m_engine;
    void* m_soundIdle;
    void* m_soundDriving;
    void* m_soundIgnition;
    void* m_soundStop;
    void* m_soundHorn;
    
    bool m_engineRunning;
};

#endif // AUDIOSYSTEM_H
