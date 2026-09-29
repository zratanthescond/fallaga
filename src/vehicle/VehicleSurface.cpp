#include "VehicleSurface.h"

float vehicleSurfaceGrip(VehicleSurfaceType surface,
                         const VehicleConfig &config) {
  switch (surface) {
  case VehicleSurfaceType::Asphalt: return config.asphaltGrip;
  case VehicleSurfaceType::Cobblestone: return config.cobblestoneGrip;
  case VehicleSurfaceType::Concrete: return config.concreteGrip;
  case VehicleSurfaceType::Dirt: return config.dirtGrip;
  case VehicleSurfaceType::Dust: return config.dustGrip;
  case VehicleSurfaceType::Sand: return config.sandGrip;
  case VehicleSurfaceType::Water: return config.waterGrip;
  }
  return 1.0f;
}
