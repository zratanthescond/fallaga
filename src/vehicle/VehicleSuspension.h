#pragma once

#include <array>

struct SuspensionWheelInput {
  float compressionM = 0.0f;
  float compressionVelocityMps = 0.0f;
  bool hasGroundContact = false;
};

struct SuspensionSettings {
  float springRateNPerM = 38000.0f;
  float compressionDampingNsPerM = 4200.0f;
  float reboundDampingNsPerM = 6300.0f;
  float frontAntiRollStiffnessNPerM = 5000.0f;
  float rearAntiRollStiffnessNPerM = 3500.0f;
  float bumpStopStartCompressionM = 0.16f;
  float bumpStopQuadraticStiffnessNPerM2 = 180000.0f;
  float maxLoadN = 28000.0f;
};

// Wheel order is front-left, front-right, rear-left, rear-right. Anti-roll
// forces move load from the more-compressed wheel to its axle mate.
std::array<float, 4> calculateSuspensionLoads(
    const std::array<SuspensionWheelInput, 4> &wheels,
    const SuspensionSettings &settings);
