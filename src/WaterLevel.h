#pragma once

// The edited heightmap's sea-level sample (uint16 value 3000) maps to this
// world elevation using the bounds passed to TerrainManager.
namespace TerrainConfig {
inline constexpr float kBaseWaterLevel = -62.78f;
}
