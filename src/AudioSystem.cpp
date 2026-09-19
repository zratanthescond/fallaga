#include "AudioSystem.h"
#include <iostream>
#include <cmath>
#include <algorithm>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

AudioSystem& AudioSystem::getInstance() {
    static AudioSystem instance;
    return instance;
}

AudioSystem::AudioSystem() : m_initialized(false), m_engine(nullptr), m_soundIdle(nullptr), m_soundDriving(nullptr), m_soundIgnition(nullptr), m_soundStop(nullptr), m_soundHorn(nullptr), m_engineRunning(false) {
}

AudioSystem::~AudioSystem() {
    cleanup();
}

bool AudioSystem::init() {
    if (m_initialized) return true;

    ma_engine* engine = new ma_engine;
    if (ma_engine_init(NULL, engine) != MA_SUCCESS) {
        std::cerr << "[AudioSystem] Failed to initialize miniaudio engine." << std::endl;
        delete engine;
        return false;
    }
    m_engine = engine;

    // Allocate sounds
    ma_sound* sIdle = new ma_sound;
    ma_sound* sDriving = new ma_sound;
    ma_sound* sIgnition = new ma_sound;
    ma_sound* sStop = new ma_sound;
    ma_sound* sHorn = new ma_sound;

    // Load sounds (relative to working directory)
    ma_result result;
    result = ma_sound_init_from_file(engine, "assets/cars/sound/engine_idle.mp3", 0, NULL, NULL, sIdle);
    if (result != MA_SUCCESS) std::cerr << "[AudioSystem] Failed to load engine_idle.mp3" << std::endl;
    
    result = ma_sound_init_from_file(engine, "assets/cars/sound/engine_driving.mp3", 0, NULL, NULL, sDriving);
    if (result != MA_SUCCESS) std::cerr << "[AudioSystem] Failed to load engine_driving.mp3" << std::endl;

    result = ma_sound_init_from_file(engine, "assets/cars/sound/engin_ignition.mp3", 0, NULL, NULL, sIgnition);
    if (result != MA_SUCCESS) std::cerr << "[AudioSystem] Failed to load engin_ignition.mp3" << std::endl;

    result = ma_sound_init_from_file(engine, "assets/cars/sound/engin_stop.mp3", 0, NULL, NULL, sStop);
    if (result != MA_SUCCESS) std::cerr << "[AudioSystem] Failed to load engin_stop.mp3" << std::endl;

    result = ma_sound_init_from_file(engine, "assets/cars/sound/old-car-horn.mp3", 0, NULL, NULL, sHorn);
    if (result != MA_SUCCESS) std::cerr << "[AudioSystem] Failed to load old-car-horn.mp3" << std::endl;

    // Set loops
    ma_sound_set_looping(sIdle, MA_TRUE);
    ma_sound_set_looping(sDriving, MA_TRUE);
    ma_sound_set_looping(sHorn, MA_TRUE);

    // Initial volumes
    ma_sound_set_volume(sIdle, 0.0f);
    ma_sound_set_volume(sDriving, 0.0f);

    // Configure 3D attenuation min distances
    ma_sound_set_min_distance(sIdle, 5.0f);
    ma_sound_set_min_distance(sDriving, 5.0f);
    ma_sound_set_min_distance(sIgnition, 5.0f);
    ma_sound_set_min_distance(sStop, 5.0f);
    ma_sound_set_min_distance(sHorn, 10.0f);

    m_soundIdle = sIdle;
    m_soundDriving = sDriving;
    m_soundIgnition = sIgnition;
    m_soundStop = sStop;
    m_soundHorn = sHorn;

    m_initialized = true;
    m_engineRunning = true; // Auto-start for now unless explicitly stopped
    
    // Start looping sounds
    ma_sound_start(sIdle);
    ma_sound_start(sDriving);

    std::cout << "[AudioSystem] Sample-Based Engine Audio initialized." << std::endl;
    return true;
}

void AudioSystem::cleanup() {
    if (m_initialized) {
        if (m_soundIdle) { ma_sound_uninit((ma_sound*)m_soundIdle); delete (ma_sound*)m_soundIdle; }
        if (m_soundDriving) { ma_sound_uninit((ma_sound*)m_soundDriving); delete (ma_sound*)m_soundDriving; }
        if (m_soundIgnition) { ma_sound_uninit((ma_sound*)m_soundIgnition); delete (ma_sound*)m_soundIgnition; }
        if (m_soundStop) { ma_sound_uninit((ma_sound*)m_soundStop); delete (ma_sound*)m_soundStop; }
        if (m_soundHorn) { ma_sound_uninit((ma_sound*)m_soundHorn); delete (ma_sound*)m_soundHorn; }
        
        if (m_engine) { ma_engine_uninit((ma_engine*)m_engine); delete (ma_engine*)m_engine; }
        
        m_engine = nullptr; m_soundIdle = nullptr; m_soundDriving = nullptr;
        m_soundIgnition = nullptr; m_soundStop = nullptr; m_soundHorn = nullptr;
        m_initialized = false;
        std::cout << "[AudioSystem] Audio cleaned up." << std::endl;
    }
}

void AudioSystem::updateListener(const glm::vec3& pos, const glm::vec3& forward, const glm::vec3& up) {
    if (!m_initialized) return;
    ma_engine* engine = (ma_engine*)m_engine;
    ma_engine_listener_set_position(engine, 0, pos.x, pos.y, pos.z);
    ma_engine_listener_set_direction(engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(engine, 0, up.x, up.y, up.z);
}

void AudioSystem::playIgnition(const glm::vec3& position) {
    if (!m_initialized) return;
    m_engineRunning = true;
    ma_sound* sound = (ma_sound*)m_soundIgnition;
    ma_sound_set_position(sound, position.x, position.y, position.z);
    ma_sound_seek_to_pcm_frame(sound, 0);
    ma_sound_start(sound);
}

void AudioSystem::playStop(const glm::vec3& position) {
    if (!m_initialized) return;
    m_engineRunning = false;
    ma_sound_set_volume((ma_sound*)m_soundIdle, 0.0f);
    ma_sound_set_volume((ma_sound*)m_soundDriving, 0.0f);
    ma_sound* sound = (ma_sound*)m_soundStop;
    ma_sound_set_position(sound, position.x, position.y, position.z);
    ma_sound_seek_to_pcm_frame(sound, 0);
    ma_sound_start(sound);
}

void AudioSystem::playHorn(const glm::vec3& position) {
    if (!m_initialized) return;
    ma_sound* sound = (ma_sound*)m_soundHorn;
    ma_sound_set_position(sound, position.x, position.y, position.z);
    ma_sound_start(sound);
}

void AudioSystem::stopHorn() {
    if (!m_initialized) return;
    ma_sound_stop((ma_sound*)m_soundHorn);
}

void AudioSystem::updateEngine(float rpm, float throttle, float maxRpm, const glm::vec3& position) {
    if (!m_initialized || !m_engineRunning) return;

    ma_sound* sIdle = (ma_sound*)m_soundIdle;
    ma_sound* sDriving = (ma_sound*)m_soundDriving;

    // Update 3D positions
    ma_sound_set_position(sIdle, position.x, position.y, position.z);
    ma_sound_set_position(sDriving, position.x, position.y, position.z);

    // Crossfade Logic
    // Idle sounds best at 800-1200 RPM, driving sounds best above 1500 RPM
    float idleFade = 1.0f - std::clamp((rpm - 1000.0f) / 1000.0f, 0.0f, 1.0f);
    float drivingFade = std::clamp((rpm - 1000.0f) / 2000.0f, 0.0f, 1.0f);

    // Pitch Logic
    // Adjust these denominators based on the original pitch of the sound files
    float idlePitch = std::clamp(rpm / 800.0f, 0.8f, 1.5f);
    float drivingPitch = std::clamp(rpm / 2500.0f, 0.5f, 2.5f);

    // Rev limiter stutter
    if (rpm > maxRpm - 100.0f) {
        float stutter = 1.0f - (rpm - (maxRpm - 100.0f)) / 200.0f;
        if (stutter < 0.2f) stutter = 0.2f;
        idleFade *= stutter;
        drivingFade *= stutter;
    }

    ma_sound_set_volume(sIdle, idleFade * 0.8f);
    ma_sound_set_pitch(sIdle, idlePitch);

    ma_sound_set_volume(sDriving, drivingFade * (0.6f + throttle * 0.4f));
    ma_sound_set_pitch(sDriving, drivingPitch);
}
