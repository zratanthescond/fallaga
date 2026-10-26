#pragma once

#include <cstdint>
#include <functional>
#include <vector>
#include <glm/glm.hpp>

namespace vegetation {

// Coarse habitat classes used by the scatter rules. The first entries are hard
// exclusions; the last entries describe the plant communities on open ground.
enum class Habitat : std::uint8_t {
    Water = 0,
    Port,
    Urban,
    Wadi,
    Beach,
    Steppe,
    DryGrass,
    CoastalScrub,
    Cultivated,
    Barren,
    Riparian,
    Count
};

enum MaskFlag : std::uint8_t {
    MaskWater     = 1u << 0,
    MaskRoad      = 1u << 1,
    MaskBuilding  = 1u << 2,
    MaskUrbanZone = 1u << 3,
    MaskPort      = 1u << 4,
    MaskBeach     = 1u << 5,
    MaskWadi      = 1u << 6,
    MaskShoreline = 1u << 7
};

// Oriented footprint for a placed model. Full 3D bounds are not needed for a
// top-down mask, so the horizontal extent is stored after scale and terrain
// alignment have been applied by the caller.
struct BuildingFootprint {
    glm::vec2 center{0.0f};
    glm::vec2 halfExtents{0.0f};
    float cosAngle = 1.0f;
    float sinAngle = 0.0f;

    bool contains(float x, float z) const;
};

struct BuildParams {
    float minX = 0.0f;
    float maxX = 0.0f;
    float minZ = 0.0f;
    float maxZ = 0.0f;
    int resolution = 1024;
    float waterLevel = 0.0f;

    // All callbacks are optional; missing inputs fall back to neutral values.
    std::function<float(float, float)> heightAt;
    std::function<int(float, float)> zoneAt; // Terrain::Zone numeric value
    std::function<bool(float, float)> waterAt; // shared rendered-water footprint
    std::function<float(float, float)> shoreAt; // shared 0..1 shore/sand weight
    std::function<float(float, float)> riparianAt; // shared 0..1 lush bank weight
    std::function<bool(float, float)> roadAt;
    std::function<float(float, float)> steppeAt;
    std::function<float(float, float)> oliveAt;
    std::function<float(float, float)> duneAt;
    std::function<float(float, float)> exclusionAt;

    std::vector<BuildingFootprint> buildings;
};

class VegetationMasks {
public:
    bool build(const BuildParams& params);

    bool ready() const { return m_ready; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    float minX() const { return m_minX; }
    float maxX() const { return m_maxX; }
    float minZ() const { return m_minZ; }
    float maxZ() const { return m_maxZ; }

    Habitat habitatAt(float x, float z) const;
    std::uint8_t flagsAt(float x, float z) const;
    float densityAt(float x, float z) const;
    float exclusionAt(float x, float z) const;
    bool blockedAt(float x, float z) const;

    int indexAt(float x, float z) const;

    const std::vector<std::uint8_t>& habitats() const { return m_habitats; }
    const std::vector<std::uint8_t>& flags() const { return m_flags; }
    const std::vector<std::uint8_t>& densities() const { return m_densities; }
    const std::vector<std::uint8_t>& exclusions() const { return m_exclusions; }

    // RGBA, row 0 == minZ, column 0 == minX. Built on demand.
    const std::vector<unsigned char>& debugPixels(int mode) const;

    static const char* habitatName(Habitat habitat);

private:
    float effectiveDensity(std::size_t index) const;
    void ensureDebugPixels(int mode) const;

    bool m_ready = false;
    int m_width = 0;
    int m_height = 0;
    float m_minX = 0.0f;
    float m_maxX = 0.0f;
    float m_minZ = 0.0f;
    float m_maxZ = 0.0f;

    std::vector<std::uint8_t> m_habitats;
    std::vector<std::uint8_t> m_flags;
    std::vector<std::uint8_t> m_densities;
    std::vector<std::uint8_t> m_exclusions;

    mutable std::vector<unsigned char> m_debugPixels;
    mutable int m_debugMode = -1;
};

} // namespace vegetation
