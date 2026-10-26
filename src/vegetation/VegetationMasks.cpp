#include "vegetation/VegetationMasks.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vegetation {

namespace {

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

std::uint8_t toByte(float value) {
    return static_cast<std::uint8_t>(clamp01(value) * 255.0f + 0.5f);
}

void writePixel(std::vector<unsigned char>& pixels, std::size_t index,
                unsigned char r, unsigned char g, unsigned char b) {
    pixels[index * 4 + 0] = r;
    pixels[index * 4 + 1] = g;
    pixels[index * 4 + 2] = b;
    pixels[index * 4 + 3] = 255;
}

void habitatColor(Habitat habitat, unsigned char& r, unsigned char& g,
                  unsigned char& b) {
    switch (habitat) {
        case Habitat::Water:         r = 24;  g = 82;  b = 156; break;
        case Habitat::Port:          r = 132; g = 42;  b = 132; break;
        case Habitat::Urban:         r = 94;  g = 94;  b = 100; break;
        case Habitat::Wadi:          r = 32;  g = 104; b = 96;  break;
        case Habitat::Beach:         r = 206; g = 184; b = 122; break;
        case Habitat::Steppe:        r = 142; g = 142; b = 58;  break;
        case Habitat::DryGrass:      r = 116; g = 158; b = 68;  break;
        case Habitat::CoastalScrub:  r = 78;  g = 132; b = 92;  break;
        case Habitat::Cultivated:    r = 164; g = 172; b = 58;  break;
        case Habitat::Barren:        r = 130; g = 108; b = 82;  break;
        case Habitat::Riparian:      r = 46;  g = 142; b = 72;  break;
        default:                     r = 0;   g = 0;   b = 0;   break;
    }
}

} // namespace

bool BuildingFootprint::contains(float x, float z) const {
    const float dx = x - center.x;
    const float dz = z - center.y;
    const float localX = cosAngle * dx - sinAngle * dz;
    const float localZ = sinAngle * dx + cosAngle * dz;
    return std::fabs(localX) <= halfExtents.x &&
           std::fabs(localZ) <= halfExtents.y;
}

bool VegetationMasks::build(const BuildParams& params) {
    m_ready = false;
    m_width = 0;
    m_height = 0;
    m_habitats.clear();
    m_flags.clear();
    m_densities.clear();
    m_exclusions.clear();
    m_debugPixels.clear();
    m_debugMode = -1;

    const int resolution = std::clamp(params.resolution, 64, 4096);
    const float spanX = params.maxX - params.minX;
    const float spanZ = params.maxZ - params.minZ;
    if (!(spanX > 0.0f) || !(spanZ > 0.0f)) {
        std::fprintf(stderr, "[VegetationMasks] invalid bounds (%.2f, %.2f)\n",
                     spanX, spanZ);
        return false;
    }

    m_width = resolution;
    m_height = resolution;
    m_minX = params.minX;
    m_maxX = params.maxX;
    m_minZ = params.minZ;
    m_maxZ = params.maxZ;

    const std::size_t texelCount =
        static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height);
    m_habitats.assign(texelCount, static_cast<std::uint8_t>(Habitat::Barren));
    m_flags.assign(texelCount, 0);
    m_densities.assign(texelCount, 0);
    m_exclusions.assign(texelCount, 0);

    const float cellX = spanX / static_cast<float>(m_width);
    const float cellZ = spanZ / static_cast<float>(m_height);
    const bool hasDensityMasks = params.steppeAt || params.oliveAt ||
                                 params.duneAt || params.exclusionAt;

    for (int y = 0; y < m_height; ++y) {
        const float worldZ = params.minZ + (static_cast<float>(y) + 0.5f) * cellZ;
        for (int x = 0; x < m_width; ++x) {
            const float worldX =
                params.minX + (static_cast<float>(x) + 0.5f) * cellX;
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width) +
                static_cast<std::size_t>(x);

            float height = 0.0f;
            if (params.heightAt) height = params.heightAt(worldX, worldZ);

            int zone = 5; // PLAIN
            if (params.zoneAt) zone = params.zoneAt(worldX, worldZ);

            const float steppe = params.steppeAt
                ? clamp01(params.steppeAt(worldX, worldZ)) : 0.0f;
            const float olive = params.oliveAt
                ? clamp01(params.oliveAt(worldX, worldZ)) : 0.0f;
            const float dune = params.duneAt
                ? clamp01(params.duneAt(worldX, worldZ)) : 0.0f;
            const float exclusion = params.exclusionAt
                ? clamp01(params.exclusionAt(worldX, worldZ)) : 0.0f;

            std::uint8_t flags = 0;
            Habitat habitat = Habitat::Barren;

            const float shore = params.shoreAt
                ? clamp01(params.shoreAt(worldX, worldZ)) : 0.0f;
            const float riparian = params.riparianAt
                ? clamp01(params.riparianAt(worldX, worldZ)) : 0.0f;
            const bool inWater = params.waterAt
                ? params.waterAt(worldX, worldZ)
                : (height <= params.waterLevel - 0.05f || zone == 0);

            if (inWater) {
                flags |= MaskWater;
                habitat = Habitat::Water;
            } else if (zone == 1) {
                flags |= MaskPort;
                habitat = Habitat::Port;
            } else if (zone == 2 || zone == 4 || zone == 7) {
                flags |= MaskUrbanZone;
                habitat = Habitat::Urban;
            } else if (shore >= 0.20f || zone == 6) {
                flags |= MaskBeach;
                habitat = Habitat::Beach;
            } else if (riparian >= 0.20f) {
                habitat = Habitat::Riparian;
            } else if (zone == 3) {
                flags |= MaskWadi;
                habitat = Habitat::Wadi;
            } else {
                if (olive >= 0.32f && olive * 1.15f >= steppe &&
                    olive >= dune) {
                    habitat = Habitat::Cultivated;
                } else if (dune >= 0.30f && dune >= steppe) {
                    habitat = Habitat::CoastalScrub;
                } else if (steppe >= 0.45f) {
                    habitat = Habitat::Steppe;
                } else if (steppe >= 0.08f || olive >= 0.08f ||
                           dune >= 0.08f) {
                    habitat = Habitat::DryGrass;
                } else {
                    habitat = Habitat::Barren;
                }
                if (exclusion >= 0.75f) habitat = Habitat::Barren;
            }

            if (!(flags & MaskWater)) {
                if (shore > 0.02f ||
                    (height <= params.waterLevel + 1.20f && riparian < 0.20f))
                    flags |= MaskShoreline;
                if (params.roadAt && params.roadAt(worldX, worldZ))
                    flags |= MaskRoad;
            }

            float density = 0.0f;
            if (hasDensityMasks) {
                // Same normalisation the legacy scatter used: the painted maps
                // are deliberately mid-grey, so a healthy steppe must read as
                // near-full coverage instead of a third-full one.
                const float raw = clamp01(
                    steppe * 2.2f + olive * 1.3f + dune * 0.45f);
                density = raw * (1.0f - clamp01(exclusion * 1.3f));
            } else {
                switch (habitat) {
                    case Habitat::Steppe:       density = 0.58f; break;
                    case Habitat::DryGrass:     density = 0.42f; break;
                    case Habitat::CoastalScrub:density = 0.28f; break;
                    case Habitat::Cultivated:   density = 0.64f; break;
                    case Habitat::Riparian:    density = 0.62f; break;
                    case Habitat::Barren:       density = 0.07f; break;
                    default:                    density = 0.0f;  break;
                }
            }
            // Ensure a green bank exists even where the painted dry-biome map is
            // weak. The shared water-distance band supplies its smooth falloff.
            density = std::max(density, riparian * 0.62f);

            m_habitats[index] = static_cast<std::uint8_t>(habitat);
            m_flags[index] = flags;
            m_densities[index] = toByte(density);
            m_exclusions[index] = toByte(exclusion);
        }
    }

    // Rasterize placed-model footprints after the terrain pass. Iterating the
    // footprint bounds instead of every texel keeps thousands of curbs, palms
    // and buildings cheap to stamp.
    for (const BuildingFootprint& footprint : params.buildings) {
        const float extentX =
            std::fabs(footprint.cosAngle) * footprint.halfExtents.x +
            std::fabs(footprint.sinAngle) * footprint.halfExtents.y;
        const float extentZ =
            std::fabs(footprint.sinAngle) * footprint.halfExtents.x +
            std::fabs(footprint.cosAngle) * footprint.halfExtents.y;
        if (extentX <= 0.0f || extentZ <= 0.0f) continue;

        const int x0 = std::clamp(
            static_cast<int>(std::floor(
                (footprint.center.x - extentX - params.minX) / cellX)),
            0, m_width - 1);
        const int x1 = std::clamp(
            static_cast<int>(std::ceil(
                (footprint.center.x + extentX - params.minX) / cellX)),
            0, m_width - 1);
        const int z0 = std::clamp(
            static_cast<int>(std::floor(
                (footprint.center.y - extentZ - params.minZ) / cellZ)),
            0, m_height - 1);
        const int z1 = std::clamp(
            static_cast<int>(std::ceil(
                (footprint.center.y + extentZ - params.minZ) / cellZ)),
            0, m_height - 1);

        for (int z = z0; z <= z1; ++z) {
            const float worldZ =
                params.minZ + (static_cast<float>(z) + 0.5f) * cellZ;
            for (int x = x0; x <= x1; ++x) {
                const float worldX =
                    params.minX + (static_cast<float>(x) + 0.5f) * cellX;
                if (!footprint.contains(worldX, worldZ)) continue;
                const std::size_t index =
                    static_cast<std::size_t>(z) *
                        static_cast<std::size_t>(m_width) +
                    static_cast<std::size_t>(x);
                m_flags[index] |= MaskBuilding;
            }
        }
    }

    m_ready = true;
    return true;
}

int VegetationMasks::indexAt(float x, float z) const {
    if (!m_ready || m_width <= 0 || m_height <= 0) return -1;
    const float u = (x - m_minX) / (m_maxX - m_minX);
    const float v = (z - m_minZ) / (m_maxZ - m_minZ);
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) return -1;
    // Build stores cell i at min + (i + 0.5) * cell, so the containing cell is
    // floor(u * width), not a lerp across width - 1 (which shifts features by
    // half a texel and matters at 13 m per texel).
    const int px = std::clamp(static_cast<int>(u * static_cast<float>(m_width)),
                              0, m_width - 1);
    const int pz = std::clamp(static_cast<int>(v * static_cast<float>(m_height)),
                              0, m_height - 1);
    return pz * m_width + px;
}

Habitat VegetationMasks::habitatAt(float x, float z) const {
    const int index = indexAt(x, z);
    if (index < 0) return Habitat::Barren;
    return static_cast<Habitat>(m_habitats[static_cast<std::size_t>(index)]);
}

std::uint8_t VegetationMasks::flagsAt(float x, float z) const {
    const int index = indexAt(x, z);
    if (index < 0) return 0;
    return m_flags[static_cast<std::size_t>(index)];
}

float VegetationMasks::densityAt(float x, float z) const {
    const int index = indexAt(x, z);
    if (index < 0) return 0.0f;

    // Hard exclusions stay crisp; the authored density blends across texels so
    // 13 m blocks do not read as square patches in the scattered grass.
    constexpr std::uint8_t kHardBlock =
        MaskWater | MaskRoad | MaskBuilding | MaskPort;
    if (m_flags[static_cast<std::size_t>(index)] & kHardBlock) return 0.0f;
    if (m_exclusions[static_cast<std::size_t>(index)] / 255.0f >= 0.55f)
        return 0.0f;

    const float u = (x - m_minX) / (m_maxX - m_minX);
    const float v = (z - m_minZ) / (m_maxZ - m_minZ);
    const float tx = u * static_cast<float>(m_width) - 0.5f;
    const float tz = v * static_cast<float>(m_height) - 0.5f;
    const int x0 = static_cast<int>(std::floor(tx));
    const int z0 = static_cast<int>(std::floor(tz));
    const int x1 = std::clamp(x0 + 1, 0, m_width - 1);
    const int z1 = std::clamp(z0 + 1, 0, m_height - 1);
    const int cx0 = std::clamp(x0, 0, m_width - 1);
    const int cz0 = std::clamp(z0, 0, m_height - 1);
    const float fx = clamp01(tx - static_cast<float>(x0));
    const float fz = clamp01(tz - static_cast<float>(z0));

    const auto sample = [&](int px, int pz) {
        return effectiveDensity(static_cast<std::size_t>(pz) * m_width + px);
    };
    const float d00 = sample(cx0, cz0);
    const float d10 = sample(x1, cz0);
    const float d01 = sample(cx0, z1);
    const float d11 = sample(x1, z1);
    return glm::mix(glm::mix(d00, d10, fx), glm::mix(d01, d11, fx), fz);
}

float VegetationMasks::effectiveDensity(std::size_t index) const {
    constexpr std::uint8_t kHardBlock =
        MaskWater | MaskRoad | MaskBuilding | MaskPort;
    const std::uint8_t flags = m_flags[index];
    if (flags & kHardBlock) return 0.0f;
    if (m_exclusions[index] / 255.0f >= 0.55f) return 0.0f;

    // Habitat suitability is separate from the painted density: the artist map
    // says how thick the cover should be, this says how much of it survives in
    // each biome. Beaches and wadis stay walkable and sparse rather than empty.
    float scale = 1.0f;
    switch (static_cast<Habitat>(m_habitats[index])) {
        case Habitat::Water:
        case Habitat::Port:       scale = 0.0f;  break;
        case Habitat::Urban:      scale = 0.08f; break;
        case Habitat::Barren:     scale = 0.22f; break;
        case Habitat::Beach:      scale = 0.08f; break;
        case Habitat::Wadi:       scale = 0.45f; break;
        case Habitat::DryGrass:   scale = 0.85f; break;
        case Habitat::CoastalScrub: scale = 0.90f; break;
        case Habitat::Cultivated: scale = 1.10f; break;
        case Habitat::Riparian:   scale = 1.25f; break;
        case Habitat::Steppe:
        default:                  scale = 1.0f;  break;
    }
    if (flags & MaskBeach) scale = std::min(scale, 0.08f);
    if (flags & MaskWadi)  scale = std::min(scale, 0.45f);
    if (flags & MaskShoreline) scale = std::min(scale, 0.35f);
    return (m_densities[index] / 255.0f) * scale;
}

float VegetationMasks::exclusionAt(float x, float z) const {
    const int index = indexAt(x, z);
    if (index < 0) return 1.0f;
    return m_exclusions[static_cast<std::size_t>(index)] / 255.0f;
}

bool VegetationMasks::blockedAt(float x, float z) const {
    const int index = indexAt(x, z);
    if (index < 0) return true;

    constexpr std::uint8_t kHardBlock =
        MaskWater | MaskRoad | MaskBuilding | MaskPort;
    if (m_flags[static_cast<std::size_t>(index)] & kHardBlock) return true;
    return exclusionAt(x, z) >= 0.55f;
}

const std::vector<unsigned char>& VegetationMasks::debugPixels(int mode) const {
    if (!m_ready) {
        m_debugPixels.clear();
        m_debugMode = -1;
        return m_debugPixels;
    }
    if (mode == m_debugMode && m_debugPixels.size() ==
            static_cast<std::size_t>(m_width) * m_height * 4) {
        return m_debugPixels;
    }

    m_debugMode = mode;
    m_debugPixels.resize(
        static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height) *
        4);
    ensureDebugPixels(mode);
    return m_debugPixels;
}

void VegetationMasks::ensureDebugPixels(int mode) const {
    const std::size_t texelCount =
        static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height);

    for (std::size_t index = 0; index < texelCount; ++index) {
        const Habitat habitat =
            static_cast<Habitat>(m_habitats[index]);
        const std::uint8_t flags = m_flags[index];
        const float density = effectiveDensity(index);
        const float exclusion = m_exclusions[index] / 255.0f;

        unsigned char r = 0;
        unsigned char g = 0;
        unsigned char b = 0;

        if (mode == 1) {
            const unsigned char value =
                static_cast<unsigned char>(density * 255.0f + 0.5f);
            r = static_cast<unsigned char>(value * 0.35f);
            g = value;
            b = static_cast<unsigned char>(value * 0.30f);
        } else if (mode == 2) {
            habitatColor(Habitat::Barren, r, g, b);
            r = static_cast<unsigned char>(r * 0.35f);
            g = static_cast<unsigned char>(g * 0.35f);
            b = static_cast<unsigned char>(b * 0.35f);
            if (flags & MaskUrbanZone) { r = 108; g = 108; b = 116; }
            if (flags & MaskPort)      { r = 176; g = 52;  b = 176; }
            if (flags & MaskWadi)      { r = 32;  g = 158; b = 144; }
            if (flags & MaskBeach)     { r = 224; g = 202; b = 126; }
            if (flags & MaskWater)     { r = 28;  g = 92;  b = 176; }
            if (flags & MaskRoad)      { r = 220; g = 40;  b = 34;  }
            if (flags & MaskBuilding)  { r = 244; g = 142; b = 34;  }
            if (exclusion >= 0.55f && !(flags & (MaskRoad | MaskBuilding))) {
                r = static_cast<unsigned char>(r * 0.45f);
                g = static_cast<unsigned char>(g * 0.45f);
                b = static_cast<unsigned char>(b * 0.45f);
            }
        } else if (mode == 3) {
            habitatColor(habitat, r, g, b);
            const float shade = 0.35f + 0.65f * density;
            r = static_cast<unsigned char>(r * shade);
            g = static_cast<unsigned char>(g * shade);
            b = static_cast<unsigned char>(b * shade);
            if (flags & MaskRoad)     { r = 235; g = 42;  b = 38;  }
            if (flags & MaskBuilding) { r = 255; g = 146; b = 36;  }
            if (flags & MaskWater)    { r = 28;  g = 92;  b = 176; }
            if (flags & MaskBeach)    { r = 224; g = 202; b = 126; }
            if (exclusion >= 0.55f) {
                r = static_cast<unsigned char>(r * 0.55f);
                g = static_cast<unsigned char>(g * 0.55f);
                b = static_cast<unsigned char>(b * 0.55f);
            }
        } else {
            habitatColor(habitat, r, g, b);
        }

        writePixel(m_debugPixels, index, r, g, b);
    }
}

const char* VegetationMasks::habitatName(Habitat habitat) {
    switch (habitat) {
        case Habitat::Water:        return "Water";
        case Habitat::Port:         return "Port";
        case Habitat::Urban:        return "Urban";
        case Habitat::Wadi:         return "Wadi";
        case Habitat::Beach:        return "Beach";
        case Habitat::Steppe:       return "Steppe";
        case Habitat::DryGrass:     return "Dry grass";
        case Habitat::CoastalScrub: return "Coastal scrub";
        case Habitat::Cultivated:   return "Cultivated";
        case Habitat::Barren:       return "Barren";
        case Habitat::Riparian:     return "Riparian";
        default:                    return "Unknown";
    }
}

} // namespace vegetation
