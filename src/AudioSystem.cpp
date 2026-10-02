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

    // Load one sound. On failure the ma_sound must be destroyed immediately and
    // reported as null: every later call site (cleanup, updateEngine, the one-shot
    // events) would otherwise operate on a ma_sound that was never initialized,
    // which reads indeterminate fields and is undefined behaviour.
    auto loadSound = [this](const char* path, void*& out) {
        ma_sound* sound = new ma_sound;
        if (ma_sound_init_from_file((ma_engine*)m_engine, path, 0, NULL, NULL, sound) != MA_SUCCESS) {
            std::cerr << "[AudioSystem] Failed to load " << path
                      << " (sound disabled)" << std::endl;
            delete sound;
            out = nullptr;
            return false;
        }
        out = sound;
        return true;
    };

    // Load sounds (relative to working directory)
    loadSound("assets/cars/sound/engine_idle.mp3",   m_soundIdle);
    loadSound("assets/cars/sound/engine_driving.mp3", m_soundDriving);
    loadSound("assets/cars/sound/engin_ignition.mp3", m_soundIgnition);
    loadSound("assets/cars/sound/engin_stop.mp3",     m_soundStop);
    loadSound("assets/cars/sound/old-car-horn.mp3",   m_soundHorn);

    if (!m_soundIdle && !m_soundDriving) {
        std::cerr << "[AudioSystem] No engine audio could be loaded; audio disabled." << std::endl;
        return false;
    }

    // Set loops (only on sounds that actually loaded)
    if (m_soundIdle)    ma_sound_set_looping((ma_sound*)m_soundIdle, MA_TRUE);
    if (m_soundDriving) ma_sound_set_looping((ma_sound*)m_soundDriving, MA_TRUE);
    if (m_soundHorn)    ma_sound_set_looping((ma_sound*)m_soundHorn, MA_TRUE);

    // Initial volumes
    if (m_soundIdle)    ma_sound_set_volume((ma_sound*)m_soundIdle, 0.0f);
    if (m_soundDriving) ma_sound_set_volume((ma_sound*)m_soundDriving, 0.0f);

    // Configure 3D attenuation min distances
    if (m_soundIdle)     ma_sound_set_min_distance((ma_sound*)m_soundIdle, 5.0f);
    if (m_soundDriving)  ma_sound_set_min_distance((ma_sound*)m_soundDriving, 5.0f);
    if (m_soundIgnition) ma_sound_set_min_distance((ma_sound*)m_soundIgnition, 5.0f);
    if (m_soundStop)     ma_sound_set_min_distance((ma_sound*)m_soundStop, 5.0f);
    if (m_soundHorn)     ma_sound_set_min_distance((ma_sound*)m_soundHorn, 10.0f);

    m_initialized = true;
    m_engineRunning = true; // Auto-start for now unless explicitly stopped

    // Start looping sounds
    if (m_soundIdle)    ma_sound_start((ma_sound*)m_soundIdle);
    if (m_soundDriving) ma_sound_start((ma_sound*)m_soundDriving);

    std::cout << "[AudioSystem] Sample-Based Engine Audio initialized." << std::endl;
    return true;
}

void AudioSystem::cleanup() {
    if (!m_initialized) return;

    // uninit only what was really initialized; loadSound nulls every failure.
    if (m_soundIdle)     { ma_sound_uninit((ma_sound*)m_soundIdle);     delete (ma_sound*)m_soundIdle; }
    if (m_soundDriving)  { ma_sound_uninit((ma_sound*)m_soundDriving);  delete (ma_sound*)m_soundDriving; }
    if (m_soundIgnition) { ma_sound_uninit((ma_sound*)m_soundIgnition); delete (ma_sound*)m_soundIgnition; }
    if (m_soundStop)     { ma_sound_uninit((ma_sound*)m_soundStop);     delete (ma_sound*)m_soundStop; }
    if (m_soundHorn)     { ma_sound_uninit((ma_sound*)m_soundHorn);     delete (ma_sound*)m_soundHorn; }

    if (m_engine) { ma_engine_uninit((ma_engine*)m_engine); delete (ma_engine*)m_engine; }

    m_engine = nullptr; m_soundIdle = nullptr; m_soundDriving = nullptr;
    m_soundIgnition = nullptr; m_soundStop = nullptr; m_soundHorn = nullptr;
    m_initialized = false;
    m_engineRunning = false;
    std::cout << "[AudioSystem] Audio cleaned up." << std::endl;
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
    if (!sound) return;
    ma_sound_set_position(sound, position.x, position.y, position.z);
    ma_sound_seek_to_pcm_frame(sound, 0);
    ma_sound_start(sound);
}

void AudioSystem::playStop(const glm::vec3& position) {
    if (!m_initialized) return;
    m_engineRunning = false;
    if (m_soundIdle)    ma_sound_set_volume((ma_sound*)m_soundIdle, 0.0f);
    if (m_soundDriving) ma_sound_set_volume((ma_sound*)m_soundDriving, 0.0f);
    ma_sound* sound = (ma_sound*)m_soundStop;
    if (!sound) return;
    ma_sound_set_position(sound, position.x, position.y, position.z);
    ma_sound_seek_to_pcm_frame(sound, 0);
    ma_sound_start(sound);
}

void AudioSystem::playHorn(const glm::vec3& position) {
    if (!m_initialized) return;
    ma_sound* sound = (ma_sound*)m_soundHorn;
    if (!sound) return;
    ma_sound_set_position(sound, position.x, position.y, position.z);
    ma_sound_start(sound);
}

void AudioSystem::stopHorn() {
    if (!m_initialized) return;
    if (m_soundHorn) ma_sound_stop((ma_sound*)m_soundHorn);
}

void AudioSystem::updateEngine(float rpm, float throttle, float maxRpm, const glm::vec3& position) {
    if (!m_initialized || !m_engineRunning) return;

    ma_sound* sIdle = (ma_sound*)m_soundIdle;
    ma_sound* sDriving = (ma_sound*)m_soundDriving;
    // Either engine loop may have failed to load; the crossfade is still valid
    // for whichever one survived.
    if (!sIdle && !sDriving) return;

    // Update 3D positions
    if (sIdle)    ma_sound_set_position(sIdle, position.x, position.y, position.z);
    if (sDriving) ma_sound_set_position(sDriving, position.x, position.y, position.z);

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

    if (sIdle) {
        ma_sound_set_volume(sIdle, idleFade * 0.8f);
        ma_sound_set_pitch(sIdle, idlePitch);
    }
    if (sDriving) {
        ma_sound_set_volume(sDriving, drivingFade * (0.6f + throttle * 0.4f));
        ma_sound_set_pitch(sDriving, drivingPitch);
    }
}
