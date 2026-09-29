#pragma once

#include <array>

// Redistributes axle torque away from the faster-spinning wheel. A lock value
// of zero preserves the open-differential split; one is the strongest bias.
std::array<float, 4> applyLimitedSlipDifferentials(
    const std::array<float, 4> &wheelTorques,
    const std::array<float, 4> &wheelAngularVelocities,
    float frontLock, float rearLock);
