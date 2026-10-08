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

glm::vec2 riverTangent(const RiverChannel& channel, size_t index) {
    const auto& points = channel.points;
    for (size_t radius = 1; radius < points.size(); ++radius) {
        const size_t before = index >= radius ? index - radius : index;
        const size_t after = std::min(index + radius, points.size() - 1);
        glm::vec2 tangent = points[after].position - points[before].position;
        if (glm::dot(tangent, tangent) > 1e-8f) return glm::normalize(tangent);
    }
    return glm::vec2(1.0f, 0.0f);
}

float localFlowSpeed(const RiverChannel& channel, size_t index) {
    const auto& points = channel.points;
    const size_t a = index == 0 ? 0 : index - 1;
    const size_t b = index + 1 < points.size() ? index + 1 : index;
    const float distance = glm::length(points[b].position - points[a].position);
    float slope = channel.slope;
    if (distance > 1e-3f) {
        slope = std::max(0.0f, (points[a].bedY - points[b].bedY) / distance);
    }
    // A small minimum keeps flat source samples visibly moving downstream.
    return glm::clamp(std::sqrt(9.81f * std::max(slope, 0.001f)), 0.1f, 3.0f);
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

RiverMeshBuilder::~RiverMeshBuilder() {
    destroy();
}

void RiverMeshBuilder::build(const RiverNetwork& network, float perennialCatchmentKm2) {
    destroy();
    m_vertices.clear();
    m_indices.clear();
    m_channelCount = 0;

    for (const RiverChannel& river : network.channels()) {
        if (river.coastalFlat || river.drainageKm2 < perennialCatchmentKm2 || river.points.size() < 2)
            continue;
        if (m_vertices.size() > std::numeric_limits<uint32_t>::max() - river.points.size() * 2)
            break;

        const uint32_t base = static_cast<uint32_t>(m_vertices.size());
        const float halfWidth = glm::clamp(river.widthM * 0.45f, 0.5f, 15.0f);
        for (size_t i = 0; i < river.points.size(); ++i) {
            const RiverPoint& point = river.points[i];
            const glm::vec2 tangent = riverTangent(river, i);
            const glm::vec2 side(-tangent.y, tangent.x);
            const float speed = localFlowSpeed(river, i);
            const float depth = std::max(0.0f, point.waterY - point.bedY);
            const glm::vec2 left = point.position + side * halfWidth;
            const glm::vec2 right = point.position - side * halfWidth;
            m_vertices.push_back({glm::vec3(left.x, point.waterY + 0.03f, left.y), tangent,
                                  left, speed, depth});
            m_vertices.push_back({glm::vec3(right.x, point.waterY + 0.03f, right.y), tangent,
                                  right, speed, depth});
        }
        for (size_t i = 0; i + 1 < river.points.size(); ++i) {
            const uint32_t l0 = base + static_cast<uint32_t>(i * 2);
            const uint32_t r0 = l0 + 1;
            const uint32_t l1 = l0 + 2;
            const uint32_t r1 = l0 + 3;
            // Counter-clockwise from above so the ribbon has an upward normal.
            m_indices.insert(m_indices.end(), {l0, l1, r0, r0, l1, r1});
        }
        ++m_channelCount;
    }
}

void RiverMeshBuilder::upload() {
    destroy();
    if (m_vertices.empty() || m_indices.empty()) return;

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, m_vertices.size() * sizeof(RiverVertex), m_vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_indices.size() * sizeof(uint32_t), m_indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RiverVertex), reinterpret_cast<void*>(offsetof(RiverVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(RiverVertex), reinterpret_cast<void*>(offsetof(RiverVertex, flowDirection)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(RiverVertex), reinterpret_cast<void*>(offsetof(RiverVertex, uv)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(RiverVertex), reinterpret_cast<void*>(offsetof(RiverVertex, flowSpeed)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(RiverVertex), reinterpret_cast<void*>(offsetof(RiverVertex, depth)));
    glBindVertexArray(0);
}

void RiverMeshBuilder::destroy() {
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_ebo) glDeleteBuffers(1, &m_ebo);
    m_vao = m_vbo = m_ebo = 0;
}

void RiverMeshBuilder::render() const {
    if (!m_vao || m_indices.empty()) return;
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indices.size()), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}
