#include "RiverSystem.h"
#include "TerrainManager.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace {

class RiverJsonReader {
public:
    explicit RiverJsonReader(const std::string& text) : m_text(text) {}

    void skipSpace() {
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
            ++m_pos;
        }
    }

    bool atEnd() {
        skipSpace();
        return m_pos == m_text.size();
    }

    bool consume(char c) {
        skipSpace();
        if (m_pos >= m_text.size() || m_text[m_pos] != c) return false;
        ++m_pos;
        return true;
    }

    bool expect(char c) {
        if (consume(c)) return true;
        return fail(std::string("expected '") + c + "'");
    }

    bool memberKey(std::string& key, bool& first) {
        if (consume('}')) return false;
        if (!first && !consume(',')) {
            fail("expected ',' or '}'");
            return false;
        }
        first = false;
        return readString(key) && expect(':');
    }

    bool arrayItem(bool& first) {
        if (consume(']')) return false;
        if (!first && !consume(',')) {
            fail("expected ',' or ']'");
            return false;
        }
        first = false;
        return true;
    }

    bool readString(std::string& out) {
        if (!expect('"')) return false;
        out.clear();
        while (m_pos < m_text.size()) {
            const unsigned char c = static_cast<unsigned char>(m_text[m_pos++]);
            if (c == '"') return true;
            if (c < 0x20) return fail("control character in string");
            if (c != '\\') {
                out.push_back(static_cast<char>(c));
                continue;
            }
            if (m_pos >= m_text.size()) return fail("unterminated escape sequence");
            const char escaped = m_text[m_pos++];
            switch (escaped) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            default: return fail("unsupported escape sequence in string");
            }
        }
        return fail("unterminated string");
    }

    bool readNumber(double& value) {
        skipSpace();
        const size_t start = m_pos;
        if (m_pos < m_text.size() && m_text[m_pos] == '-') ++m_pos;
        if (m_pos >= m_text.size() || m_text[m_pos] < '0' || m_text[m_pos] > '9') {
            m_pos = start;
            return fail("expected number");
        }
        if (m_text[m_pos] == '0') {
            ++m_pos;
        } else {
            while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') ++m_pos;
        }
        if (m_pos < m_text.size() && m_text[m_pos] == '.') {
            ++m_pos;
            const size_t fraction = m_pos;
            while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') ++m_pos;
            if (fraction == m_pos) return fail("expected digits after decimal point");
        }
        if (m_pos < m_text.size() && (m_text[m_pos] == 'e' || m_text[m_pos] == 'E')) {
            ++m_pos;
            if (m_pos < m_text.size() && (m_text[m_pos] == '+' || m_text[m_pos] == '-')) ++m_pos;
            const size_t exponent = m_pos;
            while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') ++m_pos;
            if (exponent == m_pos) return fail("expected exponent digits");
        }
        const std::string token = m_text.substr(start, m_pos - start);
        char* end = nullptr;
        errno = 0;
        value = std::strtod(token.c_str(), &end);
        if (errno == ERANGE || end != token.c_str() + token.size() || !std::isfinite(value))
            return fail("number is out of range");
        return true;
    }

    bool readFloat(float& value) {
        double parsed = 0.0;
        if (!readNumber(parsed)) return false;
        if (std::fabs(parsed) > std::numeric_limits<float>::max())
            return fail("number is out of range for float");
        value = static_cast<float>(parsed);
        return true;
    }

    bool readBool(bool& value) {
        skipSpace();
        if (m_text.compare(m_pos, 4, "true") == 0) {
            m_pos += 4;
            value = true;
            return true;
        }
        if (m_text.compare(m_pos, 5, "false") == 0) {
            m_pos += 5;
            value = false;
            return true;
        }
        return fail("expected boolean");
    }

    bool reject(const std::string& message) { return fail(message); }

    bool skipValue(int depth = 0) {
        if (depth > 64) return fail("JSON nesting is too deep");
        skipSpace();
        if (m_pos >= m_text.size()) return fail("expected value");
        const char c = m_text[m_pos];
        if (c == '"') {
            std::string ignored;
            return readString(ignored);
        }
        if (c == '{') {
            ++m_pos;
            bool first = true;
            std::string key;
            while (memberKey(key, first)) {
                if (!skipValue(depth + 1)) return false;
            }
            return !failed();
        }
        if (c == '[') {
            ++m_pos;
            bool first = true;
            while (arrayItem(first)) {
                if (!skipValue(depth + 1)) return false;
            }
            return !failed();
        }
        if (m_text.compare(m_pos, 4, "true") == 0) { m_pos += 4; return true; }
        if (m_text.compare(m_pos, 5, "false") == 0) { m_pos += 5; return true; }
        if (m_text.compare(m_pos, 4, "null") == 0) { m_pos += 4; return true; }
        double ignored = 0.0;
        return readNumber(ignored);
    }

    bool failed() const { return !m_error.empty(); }
    const std::string& error() const { return m_error; }

private:
    bool fail(const std::string& message) {
        if (m_error.empty()) {
            size_t line = 1, column = 1;
            for (size_t i = 0; i < m_pos && i < m_text.size(); ++i) {
                if (m_text[i] == '\n') { ++line; column = 1; }
                else ++column;
            }
            m_error = message + " (line " + std::to_string(line) +", column " +
                      std::to_string(column) + ")";
        }
        return false;
    }

    const std::string& m_text;
    size_t m_pos = 0;
    std::string m_error;
};

bool parseRiverPoint(RiverJsonReader& json, RiverPoint& point) {
    if (!json.expect('{')) return false;
    bool haveX = false, haveZ = false, haveBed = false, haveWater = false;
    bool first = true;
    std::string key;
    while (json.memberKey(key, first)) {
        float value = 0.0f;
        if (key == "x") { if (!json.readFloat(value)) return false; point.position.x = value; haveX = true; }
        else if (key == "z") { if (!json.readFloat(value)) return false; point.position.y = value; haveZ = true; }
        else if (key == "bed") { if (!json.readFloat(value)) return false; point.bedY = value; haveBed = true; }
        else if (key == "water") { if (!json.readFloat(value)) return false; point.waterY = value; haveWater = true; }
        else if (!json.skipValue()) return false;
    }
    if (json.failed()) return false;
    if (!haveX || !haveZ || !haveBed || !haveWater)
        return json.reject("river point requires x, z, bed and water");
    if (std::fabs(point.position.x) > 1000000.0f || std::fabs(point.position.y) > 1000000.0f)
        return json.reject("river point coordinate is outside the supported range");
    return point.waterY + 1e-3f >= point.bedY || json.reject("river water height is below its bed");
}

bool parseRiverPoints(RiverJsonReader& json, std::vector<RiverPoint>& points) {
    if (!json.expect('[')) return false;
    bool first = true;
    while (json.arrayItem(first)) {
        RiverPoint point;
        if (!parseRiverPoint(json, point)) return false;
        points.push_back(point);
        if (points.size() > 1000000) return false;
    }
    if (json.failed()) return false;
    return points.size() >= 2 || json.reject("river polyline needs at least two points");
}

bool parseRiver(RiverJsonReader& json, RiverChannel& river) {
    if (!json.expect('{')) return false;
    bool haveId = false, haveClass = false, haveDrainage = false, haveWidth = false;
    bool haveDepth = false, haveLength = false, haveSlope = false, haveCoastal = false;
    bool havePoints = false;
    bool first = true;
    std::string key;
    while (json.memberKey(key, first)) {
        if (key == "id") {
            double id = 0.0;
            if (!json.readNumber(id) || id < 0.0 || id > UINT32_MAX || std::floor(id) != id) return false;
            river.id = static_cast<uint32_t>(id); haveId = true;
        } else if (key == "cls") {
            if (!json.readString(river.channelClass)) return false;
            haveClass = true;
        } else if (key == "drainage_km2") {
            if (!json.readFloat(river.drainageKm2)) return false;
            haveDrainage = true;
        } else if (key == "width_m") {
            if (!json.readFloat(river.widthM)) return false;
            haveWidth = true;
        } else if (key == "depth_m") {
            if (!json.readFloat(river.depthM)) return false;
            haveDepth = true;
        } else if (key == "length_m") {
            if (!json.readFloat(river.lengthM)) return false;
            haveLength = true;
        } else if (key == "slope") {
            if (!json.readFloat(river.slope)) return false;
            haveSlope = true;
        } else if (key == "coastal_flat") {
            if (!json.readBool(river.coastalFlat)) return false;
            haveCoastal = true;
        } else if (key == "points") {
            if (!parseRiverPoints(json, river.points)) return false;
            havePoints = true;
        } else if (!json.skipValue()) {
            return false;
        }
    }
    if (json.failed()) return false;
    if (!(haveId && haveClass && haveDrainage && haveWidth && haveDepth &&
          haveLength && haveSlope && haveCoastal && havePoints))
        return json.reject("river channel is missing a required field");
    if (river.channelClass != "stream" && river.channelClass != "river" && river.channelClass != "main")
        return json.reject("unknown river class");
    if (!(river.drainageKm2 >= 0.0f && river.drainageKm2 <= 100000.0f) ||
        !(river.widthM > 0.0f && river.widthM <= 1000.0f) ||
        !(river.depthM > 0.0f && river.depthM <= 100.0f) ||
        !(river.lengthM > 0.0f && river.lengthM <= 10000000.0f) ||
        !(river.slope >= 0.0f && river.slope <= 10.0f))
        return json.reject("river channel measurements are outside supported ranges");
    for (size_t i = 1; i < river.points.size(); ++i) {
        if (river.points[i].bedY > river.points[i - 1].bedY + 0.05f ||
            river.points[i].waterY > river.points[i - 1].waterY + 0.05f)
            return json.reject("river polyline rises while flowing downstream");
    }
    return true;
}

bool parseWetPoint(RiverJsonReader& json, glm::vec2& point) {
    if (!json.expect('{')) return false;
    bool haveX = false, haveZ = false, first = true;
    std::string key;
    while (json.memberKey(key, first)) {
        float value = 0.0f;
        if (key == "x") {
            if (!json.readFloat(value)) return false;
            point.x = value;
            haveX = true;
        } else if (key == "z") {
            if (!json.readFloat(value)) return false;
            point.y = value;
            haveZ = true;
        } else if (!json.skipValue()) {
            return false;
        }
    }
    if (json.failed()) return false;
    if (!haveX || !haveZ) return json.reject("wet point requires x and z");
    if (std::fabs(point.x) > 1000000.0f || std::fabs(point.y) > 1000000.0f)
        return json.reject("wet point coordinate is outside the supported range");
    return true;
}

bool parseWetPointArray(RiverJsonReader& json, std::vector<glm::vec2>& points) {
    if (!json.expect('[')) return false;
    bool first = true;
    while (json.arrayItem(first)) {
        glm::vec2 point(0.0f);
        if (!parseWetPoint(json, point)) return false;
        if (points.empty() || glm::length(point - points.back()) > 0.01f)
            points.push_back(point);
        if (points.size() > 1000000)
            return json.reject("trickle has too many points");
    }
    if (json.failed()) return false;
    return points.size() >= 2 || json.reject("trickle needs at least two distinct points");
}

bool parseWetPool(RiverJsonReader& json, WetPool& pool) {
    if (!json.expect('{')) return false;
    bool haveX = false, haveZ = false, haveLevel = false, haveBed = false;
    bool haveDepth = false, haveArea = false, haveVolume = false, haveRadius = false;
    bool first = true;
    std::string key;
    while (json.memberKey(key, first)) {
        float value = 0.0f;
        if (key == "x") {
            if (!json.readFloat(value)) return false;
            pool.center.x = value; haveX = true;
        } else if (key == "z") {
            if (!json.readFloat(value)) return false;
            pool.center.y = value; haveZ = true;
        } else if (key == "level_m") {
            if (!json.readFloat(pool.levelM)) return false;
            haveLevel = true;
        } else if (key == "bed_min_m") {
            if (!json.readFloat(pool.bedMinM)) return false;
            haveBed = true;
        } else if (key == "max_depth_m") {
            if (!json.readFloat(pool.maxDepthM)) return false;
            haveDepth = true;
        } else if (key == "area_m2") {
            if (!json.readFloat(pool.areaM2)) return false;
            haveArea = true;
        } else if (key == "volume_m3") {
            if (!json.readFloat(pool.volumeM3)) return false;
            haveVolume = true;
        } else if (key == "radius_m") {
            if (!json.readFloat(pool.radiusM)) return false;
            haveRadius = true;
        } else if (!json.skipValue()) {
            return false;
        }
    }
    if (json.failed()) return false;
    if (!(haveX && haveZ && haveLevel && haveBed && haveDepth && haveArea && haveVolume && haveRadius))
        return json.reject("pool is missing a required field");
    if (!(pool.maxDepthM > 0.0f && pool.maxDepthM <= 10.0f) ||
        !(pool.areaM2 > 0.0f && pool.areaM2 <= 10000000.0f) ||
        !(pool.volumeM3 > 0.0f && pool.volumeM3 <= 1000000000.0f) ||
        !(pool.radiusM > 0.0f && pool.radiusM <= 1000.0f))
        return json.reject("pool measurements are outside supported ranges");
    return true;
}

bool parseWetTrickle(RiverJsonReader& json, WetTrickle& trickle) {
    if (!json.expect('{')) return false;
    bool haveId = false, haveSpring = false, haveFlow = false, haveLength = false;
    bool haveWidth = false, haveDepth = false, haveIncision = false, havePoints = false;
    bool first = true;
    std::string key;
    while (json.memberKey(key, first)) {
        if (key == "river_id") {
            double id = 0.0;
            if (!json.readNumber(id) || id < 0.0 || id > UINT32_MAX || std::floor(id) != id)
                return json.reject("river_id must be a non-negative integer");
            trickle.riverId = static_cast<uint32_t>(id);
            haveId = true;
        } else if (key == "spring") {
            if (!parseWetPoint(json, trickle.spring)) return false;
            haveSpring = true;
        } else if (key == "flow_m3s") {
            if (!json.readFloat(trickle.flowM3s)) return false;
            haveFlow = true;
        } else if (key == "wet_length_m") {
            if (!json.readFloat(trickle.wetLengthM)) return false;
            haveLength = true;
        } else if (key == "width_m") {
            if (!json.readFloat(trickle.widthM)) return false;
            haveWidth = true;
        } else if (key == "depth_m") {
            if (!json.readFloat(trickle.depthM)) return false;
            haveDepth = true;
        } else if (key == "mean_incision_m") {
            if (!json.readFloat(trickle.meanIncisionM)) return false;
            haveIncision = true;
        } else if (key == "points") {
            if (!parseWetPointArray(json, trickle.points)) return false;
            havePoints = true;
        } else if (!json.skipValue()) {
            return false;
        }
    }
    if (json.failed()) return false;
    if (!(haveId && haveSpring && haveFlow && haveLength && haveWidth && haveDepth && haveIncision && havePoints))
        return json.reject("trickle is missing a required field");
    if (!(trickle.flowM3s > 0.0f && trickle.flowM3s <= 1000.0f) ||
        !(trickle.wetLengthM > 0.0f && trickle.wetLengthM <= 100000.0f) ||
        !(trickle.widthM > 0.0f && trickle.widthM <= 100.0f) ||
        !(trickle.depthM > 0.0f && trickle.depthM <= 5.0f) ||
        !(trickle.meanIncisionM >= 0.0f && trickle.meanIncisionM <= 100.0f))
        return json.reject("trickle measurements are outside supported ranges");
    return true;
}

float samplePoolMinimum(const WetPool& pool, const TerrainManager& terrain) {
    const float cellX = (terrain.getWorldMaxX() - terrain.getWorldMinX()) /
                        std::max(1, terrain.getVisualGridW() - 1);
    const float cellZ = (terrain.getWorldMaxZ() - terrain.getWorldMinZ()) /
                        std::max(1, terrain.getVisualGridH() - 1);
    const float step = glm::clamp(std::min(cellX, cellZ) * 0.45f, 2.0f, 12.0f);
    float minimum = std::numeric_limits<float>::infinity();
    for (float z = pool.center.y - pool.radiusM; z <= pool.center.y + pool.radiusM; z += step) {
        for (float x = pool.center.x - pool.radiusM; x <= pool.center.x + pool.radiusM; x += step) {
            const glm::vec2 delta = glm::vec2(x, z) - pool.center;
            if (glm::dot(delta, delta) > pool.radiusM * pool.radiusM) continue;
            if (x < terrain.getWorldMinX() || x > terrain.getWorldMaxX() ||
                z < terrain.getWorldMinZ() || z > terrain.getWorldMaxZ()) continue;
            minimum = std::min(minimum, terrain.getHeightAt(x, z));
        }
    }
    if (!std::isfinite(minimum))
        return terrain.getHeightAt(pool.center.x, pool.center.y);
    return minimum;
}

void emitPoolFan(const glm::vec2& center, float surfaceY, float radius, float maxDepth,
                 float kind, float flowSpeed, float fade,
                 std::vector<WetWaterVertex>& vertices,
                 std::vector<uint32_t>& indices) {
    constexpr uint32_t kSegments = 48;
    if (vertices.size() > UINT32_MAX - (kSegments + 2)) return;
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    auto makeVertex = [&](const glm::vec2& pos) {
        return WetWaterVertex{glm::vec3(pos.x, surfaceY + 0.025f, pos.y),
                              glm::vec2(0.0f), pos, center, radius, maxDepth,
                              flowSpeed, kind, fade};
    };
    vertices.push_back(makeVertex(center));
    for (uint32_t i = 0; i <= kSegments; ++i) {
        const float angle = 6.28318530718f * static_cast<float>(i) / kSegments;
        const glm::vec2 pos = center + glm::vec2(std::cos(angle), std::sin(angle)) * radius;
        vertices.push_back(makeVertex(pos));
    }
    for (uint32_t i = 0; i < kSegments; ++i) {
        // Reverse the ring order to give the fan an upward-facing normal.
        indices.insert(indices.end(), {base, base + i + 2, base + i + 1});
    }
}

} // namespace

bool RiverNetwork::loadFromJSON(const std::string& path, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error) *error = "could not open file";
        return false;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    const std::string text = contents.str();
    if (text.size() > 128u * 1024u * 1024u) {
        if (error) *error = "file exceeds 128 MiB limit";
        return false;
    }

    RiverJsonReader json(text);
    if (!json.expect('{')) {
        if (error) *error = json.error();
        return false;
    }
    float seaLevel = 0.0f;
    bool haveSeaLevel = false, haveRivers = false;
    std::vector<RiverChannel> parsed;
    bool first = true;
    std::string key;
    while (json.memberKey(key, first)) {
        if (key == "sea_level_m") {
            if (!json.readFloat(seaLevel)) break;
            haveSeaLevel = true;
        } else if (key == "rivers") {
            if (!json.expect('[')) break;
            bool firstRiver = true;
            std::unordered_set<uint32_t> ids;
            while (json.arrayItem(firstRiver)) {
                RiverChannel river;
                if (!parseRiver(json, river)) {
                    break;
                }
                if (!ids.insert(river.id).second) {
                    json.reject("duplicate river id");
                    break;
                }
                parsed.push_back(std::move(river));
            }
            if (json.failed() || parsed.empty()) break;
            haveRivers = true;
        } else if (!json.skipValue()) {
            break;
        }
    }
    if (json.failed() || !haveSeaLevel || !haveRivers || !json.atEnd()) {
        if (error) *error = json.failed() ? json.error() : "missing required root fields or trailing data";
        return false;
    }

    m_channels.swap(parsed);
    m_sourceSeaLevel = seaLevel;
    if (error) error->clear();
    return true;
}

bool WetWaterNetwork::loadFromJSON(const std::string& path, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error) *error = "could not open file";
        return false;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    const std::string text = contents.str();
    if (text.size() > 64u * 1024u * 1024u) {
        if (error) *error = "file exceeds 64 MiB limit";
        return false;
    }

    RiverJsonReader json(text);
    if (!json.expect('{')) {
        if (error) *error = json.error();
        return false;
    }
    bool havePools = false, haveTrickles = false, first = true;
    std::vector<WetPool> pools;
    std::vector<WetTrickle> trickles;
    std::string key;
    while (json.memberKey(key, first)) {
        if (key == "pools") {
            if (!json.expect('[')) break;
            bool firstPool = true;
            while (json.arrayItem(firstPool)) {
                WetPool pool;
                if (!parseWetPool(json, pool)) break;
                pools.push_back(pool);
                if (pools.size() > 1000000) {
                    json.reject("too many wet pools");
                    break;
                }
            }
            if (json.failed()) break;
            havePools = true;
        } else if (key == "trickles") {
            if (!json.expect('[')) break;
            bool firstTrickle = true;
            while (json.arrayItem(firstTrickle)) {
                WetTrickle trickle;
                if (!parseWetTrickle(json, trickle)) break;
                trickles.push_back(std::move(trickle));
                if (trickles.size() > 1000000) {
                    json.reject("too many wet trickles");
                    break;
                }
            }
            if (json.failed()) break;
            haveTrickles = true;
        } else if (!json.skipValue()) {
            break;
        }
    }
    if (json.failed() || !havePools || !haveTrickles || !json.atEnd()) {
        if (error) *error = json.failed() ? json.error() : "missing pools/trickles arrays or trailing data";
        return false;
    }

    m_pools.swap(pools);
    m_trickles.swap(trickles);
    if (error) error->clear();
    return true;
}

WetWaterMeshBuilder::~WetWaterMeshBuilder() {
    destroy();
}

void WetWaterMeshBuilder::build(const WetWaterNetwork& network, const TerrainManager& terrain) {
    destroy();
    m_vertices.clear();
    m_indices.clear();
    m_terrainHeights.clear();
    m_terrainWidth = terrain.getVisualGridW();
    m_terrainHeight = terrain.getVisualGridH();
    m_poolCount = 0;
    m_trickleCount = 0;
    if (m_terrainWidth < 2 || m_terrainHeight < 2) return;

    const std::vector<float>& physicsHeights = terrain.getPhysicsVisualHeightsData();
    const std::vector<float>& visualHeights = terrain.getVisualHeightsData();
    const std::vector<float>& finalHeights = physicsHeights.size() >=
        static_cast<size_t>(m_terrainWidth) * m_terrainHeight ? physicsHeights : visualHeights;
    if (finalHeights.size() < static_cast<size_t>(m_terrainWidth) * m_terrainHeight) return;
    m_terrainHeights.assign(finalHeights.begin(),
                            finalHeights.begin() + static_cast<size_t>(m_terrainWidth) * m_terrainHeight);

    for (const WetPool& pool : network.pools()) {
        if (pool.center.x - pool.radiusM < terrain.getWorldMinX() ||
            pool.center.x + pool.radiusM > terrain.getWorldMaxX() ||
            pool.center.y - pool.radiusM < terrain.getWorldMinZ() ||
            pool.center.y + pool.radiusM > terrain.getWorldMaxZ()) continue;
        const float localBed = samplePoolMinimum(pool, terrain);
        // Keep the supplied pool stage, but cap its depth against the final
        // terrain's sampled basin floor. If the final terrain no longer contains
        // a depression below this stage, that source pool stays dry.
        const float surfaceY = std::min(pool.levelM, localBed + pool.maxDepthM);
        if (surfaceY <= localBed + 0.025f) continue;
        emitPoolFan(pool.center, surfaceY, pool.radiusM, pool.maxDepthM,
                    0.0f, 0.012f, 1.0f, m_vertices, m_indices);
        ++m_poolCount;
    }

    struct PathPoint {
        glm::vec2 position;
        float distance = 0.0f;
    };
    for (const WetTrickle& trickle : network.trickles()) {
        if (trickle.points.size() < 2 || trickle.wetLengthM <= 0.0f) continue;
        std::vector<glm::vec2> sourceLine;
        sourceLine.reserve(trickle.points.size() + 1);
        sourceLine.push_back(trickle.spring);
        for (const glm::vec2& point : trickle.points) {
            if (glm::length(point - sourceLine.back()) > 0.05f)
                sourceLine.push_back(point);
        }
        if (sourceLine.size() < 2) continue;

        std::vector<PathPoint> path;
        path.push_back({sourceLine.front(), 0.0f});
        float totalLength = 0.0f;
        for (size_t i = 1; i < sourceLine.size(); ++i) {
            const glm::vec2 a = sourceLine[i - 1];
            const glm::vec2 b = sourceLine[i];
            const float segmentLength = glm::length(b - a);
            if (segmentLength <= 0.01f) continue;
            if (totalLength + segmentLength >= trickle.wetLengthM) {
                const float remaining = trickle.wetLengthM - totalLength;
                if (remaining > 0.01f) {
                    const glm::vec2 end = a + (b - a) * (remaining / segmentLength);
                    path.push_back({end, trickle.wetLengthM});
                    totalLength = trickle.wetLengthM;
                }
                break;
            }
            totalLength += segmentLength;
            path.push_back({b, totalLength});
        }
        if (path.size() < 2 || totalLength < 1.0f) continue;
        const float fadeLength = std::min(trickle.wetLengthM, totalLength);
        const uint32_t base = static_cast<uint32_t>(m_vertices.size());
        std::vector<float> surfaceLevels(path.size(), 0.0f);
        std::vector<float> fades(path.size(), 0.0f);
        std::vector<glm::vec2> tangents(path.size(), glm::vec2(1.0f, 0.0f));
        float previousSurface = 0.0f;
        for (size_t i = 0; i < path.size(); ++i) {
            const glm::vec2 before = path[i == 0 ? 0 : i - 1].position;
            const glm::vec2 after = path[std::min(i + 1, path.size() - 1)].position;
            glm::vec2 tangent = after - before;
            if (glm::dot(tangent, tangent) > 1e-8f) tangent = glm::normalize(tangent);
            else if (i > 0) tangent = tangents[i - 1];
            tangents[i] = tangent;

            const float fade = glm::clamp(1.0f - path[i].distance / std::max(fadeLength, 1.0f), 0.0f, 1.0f);
            fades[i] = fade;
            const float localDepth = glm::clamp(trickle.depthM * (0.45f + 0.55f * fade), 0.05f, 0.5f);
            const float terrainY = terrain.getHeightAt(path[i].position.x, path[i].position.y);
            float surfaceY = terrainY + localDepth;
            if (i > 0) {
                const float segmentLength = path[i].distance - path[i - 1].distance;
                const float minimumFall = std::max(0.005f, segmentLength * 0.001f);
                surfaceY = std::min(surfaceY, previousSurface - minimumFall);
            }
            surfaceLevels[i] = surfaceY;
            previousSurface = surfaceY;
        }

        const float baseSpeed = glm::clamp(
            trickle.flowM3s / std::max(trickle.widthM * trickle.depthM, 0.05f), 0.04f, 2.0f);
        for (size_t i = 0; i < path.size(); ++i) {
            const glm::vec2 tangent = tangents[i];
            const glm::vec2 side(-tangent.y, tangent.x);
            const float widthFade = std::sqrt(fades[i]);
            const float halfWidth = 0.5f * trickle.widthM * widthFade;
            const glm::vec2 left = path[i].position + side * halfWidth;
            const glm::vec2 right = path[i].position - side * halfWidth;
            const float surfaceY = surfaceLevels[i] + 0.025f;
            auto makeTrickleVertex = [&](const glm::vec2& position) {
                return WetWaterVertex{glm::vec3(position.x, surfaceY, position.y), tangent,
                                      position, path[i].position, 0.0f,
                                      trickle.depthM + 0.12f, baseSpeed, 1.0f, fades[i]};
            };
            m_vertices.push_back(makeTrickleVertex(left));
            m_vertices.push_back(makeTrickleVertex(right));
        }
        for (size_t i = 0; i + 1 < path.size(); ++i) {
            const uint32_t l0 = base + static_cast<uint32_t>(i * 2);
            const uint32_t r0 = l0 + 1;
            const uint32_t l1 = l0 + 2;
            const uint32_t r1 = l0 + 3;
            m_indices.insert(m_indices.end(), {l0, l1, r0, r0, l1, r1});
        }

        // Small seep and terminal pools fade the wet trace at each end.
        const float seepRadius = std::max(1.2f, trickle.widthM * 0.85f);
        const float endRadius = std::max(1.5f, trickle.widthM * 1.35f);
        emitPoolFan(path.front().position, surfaceLevels.front(), seepRadius,
                    trickle.depthM + 0.12f, 0.0f, 0.014f, 1.0f,
                    m_vertices, m_indices);
        emitPoolFan(path.back().position, surfaceLevels.back(), endRadius,
                    trickle.depthM + 0.12f, 0.0f, 0.008f,
                    std::max(fades.back(), 0.55f),
                    m_vertices, m_indices);
        ++m_trickleCount;
    }
}

void WetWaterMeshBuilder::upload() {
    destroy();
    if (m_vertices.empty() || m_indices.empty()) return;
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);
    if (m_terrainHeights.size() == static_cast<size_t>(m_terrainWidth) * m_terrainHeight) {
        glGenTextures(1, &m_terrainHeightTex);
        glBindTexture(GL_TEXTURE_2D, m_terrainHeightTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, m_terrainWidth, m_terrainHeight, 0,
                     GL_RED, GL_FLOAT, m_terrainHeights.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, m_vertices.size() * sizeof(WetWaterVertex), m_vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_indices.size() * sizeof(uint32_t), m_indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(WetWaterVertex), reinterpret_cast<void*>(offsetof(WetWaterVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(WetWaterVertex), reinterpret_cast<void*>(offsetof(WetWaterVertex, flowDirection)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(WetWaterVertex), reinterpret_cast<void*>(offsetof(WetWaterVertex, worldXZ)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(WetWaterVertex), reinterpret_cast<void*>(offsetof(WetWaterVertex, patchCenter)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(WetWaterVertex), reinterpret_cast<void*>(offsetof(WetWaterVertex, patchRadius)));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(WetWaterVertex), reinterpret_cast<void*>(offsetof(WetWaterVertex, maxDepth)));
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(WetWaterVertex), reinterpret_cast<void*>(offsetof(WetWaterVertex, flowSpeed)));
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, sizeof(WetWaterVertex), reinterpret_cast<void*>(offsetof(WetWaterVertex, kind)));
    glEnableVertexAttribArray(8);
    glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, sizeof(WetWaterVertex), reinterpret_cast<void*>(offsetof(WetWaterVertex, flowFade)));
    glBindVertexArray(0);
    std::vector<float>().swap(m_terrainHeights);
}

void WetWaterMeshBuilder::destroy() {
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_ebo) glDeleteBuffers(1, &m_ebo);
    if (m_terrainHeightTex) glDeleteTextures(1, &m_terrainHeightTex);
    m_vao = m_vbo = m_ebo = 0;
    m_terrainHeightTex = 0;
}

void WetWaterMeshBuilder::render() const {
    if (!m_vao || m_indices.empty()) return;
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indices.size()), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}
