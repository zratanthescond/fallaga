#pragma once

#include "VehicleConfig.h"

enum class VehicleSurfaceType {
  Asphalt,
  Cobblestone,
  Concrete,
  Dirt,
  Dust,
  Sand,
  Water
};

float vehicleSurfaceGrip(VehicleSurfaceType surface,
                         const VehicleConfig &config);
