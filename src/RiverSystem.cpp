#include "RiverSystem.h"

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
