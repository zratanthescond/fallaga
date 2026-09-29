#include "RoadSystem.h"
#include <cstdlib>
#include <climits>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <array>
#include <glm/gtc/type_ptr.hpp>

namespace {
// Keep the asphalt crossfall and the physical terrain bed in lockstep. A small
// residual slope follows broad terrain shape without copying bumps into roads.
constexpr float kRoadEdgeConform = 0.02f;

// Horizontal road frame helper:
// Computes purely horizontal right vectors perpendicular to the road tangent in the XZ plane.
// Roads on terrain must maintain constant width in world space and must not roll or twist
// on slopes, over crests, or around closed loops (roundabouts).
//
// Averaging neighbouring perpendiculars yields the corner BISECTOR, whose projection
// onto either edge normal is only cos(half the turn angle). Using that vector raw as a
// width multiplier quietly narrows the ribbon to ~71% of its width at every 90 degree
// corner (and to ~50% at 120 degrees), which reads as the road pinching shut on corners.
// So the averaged direction is Miter-compensated: rescaled by 1/cos(half turn) so the
// offset ring keeps the full half-width, clamped at 2x for turns past ~120 degrees where
// the true miter would spike. |rightVecs[i]| is therefore >= 1 on bends; normalise before
// using one as a pure direction (caps and junction pads do exactly that).
void computeRoadFrames(std::vector<glm::vec3>& rightVecs,
                       const std::vector<glm::vec3>& tangents,
                       bool closed) {
    const int m = (int)tangents.size();
    if (m <= 0) return;
    rightVecs.resize(m);
    std::vector<glm::vec3> perps(m);
    for (int i = 0; i < m; ++i) {
        const glm::vec3& t = tangents[i];
        glm::vec2 t_xz(t.x, t.z);
        float len = glm::length(t_xz);
        if (len > 1e-5f) {
            t_xz /= len;
            perps[i] = glm::vec3(t_xz.y, 0.0f, -t_xz.x);
        } else {
            perps[i] = glm::vec3(1.0f, 0.0f, 0.0f);
        }
    }
    // Cyclic/moving-average smoothing pass, mitre-compensated
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<glm::vec3> smoothed(m);
        for (int i = 0; i < m; ++i) {
            const int pr = closed ? (i - 1 + m) % m : (i == 0 ? 0 : i - 1);
            const int nx = closed ? (i + 1) % m : (i == m - 1 ? m - 1 : i + 1);
            glm::vec3 r = perps[pr] * 0.25f + perps[i] * 0.50f + perps[nx] * 0.25f;
            r.y = 0.0f;
            const float rLen = glm::length(r);
            if (rLen < 1e-5f) { smoothed[i] = perps[i]; continue; }
            r /= rLen;
            // Projection onto the two edge normals: both are 1.0 on a straight, so
            // dividing by their minimum restores the width lost to the bisector.
            float dIn  = glm::dot(r, glm::vec3(perps[pr].x,  0.0f, perps[pr].z));
            float dOut = glm::dot(r, glm::vec3(perps[nx].x,  0.0f, perps[nx].z));
            const float scale = 1.0f / glm::clamp(std::min(dIn, dOut), 0.5f, 1.0f);
            smoothed[i] = r * scale;
        }
        perps = smoothed;
    }
    rightVecs = perps;
}

// Backward-compatible forwarder
void computeParallelFrames(std::vector<glm::vec3>& rightVecs,
                           const std::vector<glm::vec3>& tangents,
                           int from) {
    (void)from;
    computeRoadFrames(rightVecs, tangents, false);
}
}  // namespace

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  HeightSampler
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

void HeightSampler::init(const TerrainCoordinateMapper& mapper,
                         const float* heights, int w, int h) {
    m_mapper = mapper;
    m_heights = heights;
    m_w = w;
    m_h = h;
}

float HeightSampler::sample(float x, float z) const {
    if (!m_heights || m_w <= 0 || m_h <= 0) return 0.0f;

    glm::vec2 uv = m_mapper.worldToUV(x, z);
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) {
        return 0.0f;
    }

    float fx = uv.x * float(m_w - 1);
    float fy = uv.y * float(m_h - 1);

    int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, m_w - 1);
    int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, m_h - 1);
    int x1 = std::min(x0 + 1, m_w - 1);
    int y1 = std::min(y0 + 1, m_h - 1);

    float tx = fx - float(x0);
    float ty = fy - float(y0);

    float h00 = m_heights[y0 * m_w + x0];
    float h10 = m_heights[y0 * m_w + x1];
    float h01 = m_heights[y1 * m_w + x0];
    float h11 = m_heights[y1 * m_w + x1];

    return h00 * (1.0f - tx) * (1.0f - ty)
         + h10 *    tx    * (1.0f - ty)
         + h01 * (1.0f - tx) *    ty
         + h11 *    tx    *    ty;
}

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  RoadGraph â€” JSON loading
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

namespace {

// Minimal cursor over a JSON document. It never guesses: every read either
// consumes input and returns true, or records a diagnostic and returns false,
// so no caller can loop without making progress.
class JsonReader {
public:
    explicit JsonReader(const std::string& text) : m_text(text) {}

    // â”€â”€ scanning â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    void skipSpace() {
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { m_pos++; continue; }
            break;
        }
    }

    char peek() const { return m_pos < m_text.size() ? m_text[m_pos] : '\0'; }
    bool atEnd() const { return m_pos >= m_text.size(); }

    bool consume(char c) {
        skipSpace();
        if (peek() != c) return false;
        m_pos++;
        return true;
    }

    bool expect(char c) {
        if (consume(c)) return true;
        return fail(std::string("expected '") + c + "'");
    }

    // â”€â”€ diagnostics â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // Only the first error is kept: it is the one that explains the rest.
    bool fail(const std::string& what) {
        if (m_error.empty()) {
            size_t line = 1, column = 1;
            for (size_t i = 0; i < m_pos && i < m_text.size(); ++i) {
                if (m_text[i] == '\n') { line++; column = 1; } else { column++; }
            }
            m_error = what + " (line " + std::to_string(line) + ", column " +
                      std::to_string(column) + ")";
        }
        return false;
    }

    bool failed() const { return !m_error.empty(); }
    const std::string& error() const { return m_error; }

    // â”€â”€ structure â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // Reads the next "key": of an object whose '{' was already consumed. Returns
    // false at the closing '}' or on error, so callers must test failed() to tell
    // "object finished" from "object broken". A trailing comma is a syntax
    // error, as in JSON.
    bool memberKey(std::string& key, bool& first) {
        if (consume('}')) return false;
        if (!first && !consume(',')) { fail("expected ',' or '}'"); return false; }
        first = false;
        if (!readString(key)) return false;
        if (!expect(':')) return false;
        return true;
    }

    // â”€â”€ values â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    bool readString(std::string& out) {
        if (!expect('"')) return false;
        out.clear();
        while (true) {
            if (atEnd()) return fail("unterminated string");
            const char c = m_text[m_pos++];
            if (c == '"') return true;
            if (c != '\\') {
                // Compared as unsigned: a plain char is signed on MSVC, so any
                // UTF-8 byte >= 0x80 would look like a control character.
                if ((unsigned char)c < 0x20) return fail("unescaped control character in string");
                out += c;
                continue;
            }
            if (atEnd()) return fail("unterminated escape sequence");
            switch (m_text[m_pos++]) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u': {
                    uint32_t code = 0;
                    if (!readHex4(code)) return false;
                    // Surrogate pair, when a low surrogate follows.
                    if (code >= 0xD800 && code <= 0xDBFF && m_pos + 1 < m_text.size() &&
                        m_text[m_pos] == '\\' && m_text[m_pos + 1] == 'u') {
                        const size_t save = m_pos;
                        uint32_t low = 0;
                        m_pos += 2;
                        if (readHex4(low) && low >= 0xDC00 && low <= 0xDFFF)
                            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                        else
                            m_pos = save;
                    }
                    appendUtf8(code, out);
                    break;
                }
                default: return fail("unknown escape sequence");
            }
        }
    }

    bool readFloat(float& out) {
        double value = 0.0;
        if (!readNumber(value)) return false;
        if (std::abs(value) > (double)std::numeric_limits<float>::max())
            return fail("number out of range for a float");
        out = (float)value;
        return true;
    }

    // Reads a JSON number as a double. Public because "id" must be validated as
    // an exact integer rather than a float. Strict grammar, and strtod turns an
    // overflow into infinity which the isfinite test rejects — std::stof used to
    // throw an uncaught std::out_of_range on such a literal.
    bool readNumber(double& out) {
        skipSpace();
        const size_t start = m_pos;
        if (peek() == '-') m_pos++;
        if (peek() == '0') {
            m_pos++;
        } else if (isDigit(peek())) {
            while (isDigit(peek())) m_pos++;
        } else {
            m_pos = start;
            return fail("expected a number");
        }
        if (peek() == '.') {
            m_pos++;
            if (!isDigit(peek())) { m_pos = start; return fail("expected digits after '.'"); }
            while (isDigit(peek())) m_pos++;
        }
        if (peek() == 'e' || peek() == 'E') {
            m_pos++;
            if (peek() == '+' || peek() == '-') m_pos++;
            if (!isDigit(peek())) { m_pos = start; return fail("expected digits in exponent"); }
            while (isDigit(peek())) m_pos++;
        }
        const std::string token = m_text.substr(start, m_pos - start);
        char* end = nullptr;
        const double value = std::strtod(token.c_str(), &end);
        if (end != token.c_str() + token.size() || !std::isfinite(value))
            return fail("number '" + token + "' is out of range");
        out = value;
        return true;
    }

    bool readBool(bool& out) {
        if (match("true"))  { out = true;  return true; }
        if (match("false")) { out = false; return true; }
        return fail("expected true or false");
    }

    // Consumes a whole value whatever its type. This is what keeps an unknown
    // key from stalling the reader on its value.
    bool skipValue(int depth = 0) {
        if (depth > 64) return fail("JSON nested too deeply");
        skipSpace();
        switch (peek()) {
            case '{': return skipContainer('}', depth, true);
            case '[': return skipContainer(']', depth, false);
            case '"': { std::string ignored; return readString(ignored); }
            case 't': if (match("true"))  return true; return fail("invalid value");
            case 'f': if (match("false")) return true; return fail("invalid value");
            case 'n': if (match("null"))  return true; return fail("invalid value");
            default:  { double ignored; return readNumber(ignored); }
        }
    }

private:
    bool isDigit(char c) const { return c >= '0' && c <= '9'; }

    bool match(const char* literal) {
        skipSpace();
        const size_t n = std::strlen(literal);
        if (m_text.compare(m_pos, n, literal) != 0) return false;
        m_pos += n;
        return true;
    }

    bool readHex4(uint32_t& out) {
        if (m_pos + 4 > m_text.size()) return fail("truncated \\u escape");
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = m_text[m_pos++];
            out <<= 4;
            if (c >= '0' && c <= '9')      out |= (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') out |= (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= (uint32_t)(c - 'A' + 10);
            else return fail("invalid \\u escape");
        }
        return true;
    }

    static void appendUtf8(uint32_t code, std::string& out) {
        if (code < 0x80) {
            out += (char)code;
        } else if (code < 0x800) {
            out += (char)(0xC0 | (code >> 6));
            out += (char)(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += (char)(0xE0 | (code >> 12));
            out += (char)(0x80 | ((code >> 6) & 0x3F));
            out += (char)(0x80 | (code & 0x3F));
        } else {
            out += (char)(0xF0 | (code >> 18));
            out += (char)(0x80 | ((code >> 12) & 0x3F));
            out += (char)(0x80 | ((code >> 6) & 0x3F));
            out += (char)(0x80 | (code & 0x3F));
        }
    }

    bool skipContainer(char close, int depth, bool isObject) {
        m_pos++;   // opening bracket
        if (consume(close)) return true;
        while (true) {
            if (isObject) {
                bool first = true;
                std::string key;
                if (!memberKey(key, first)) {
                    if (failed()) return false;   // broken object
                    return true;                  // closing '}' reached
                }
                if (!skipValue(depth + 1)) return false;
            } else if (!skipValue(depth + 1)) {
                return false;
            }
            if (consume(close)) return true;
            if (!consume(',')) return fail(std::string("expected ',' or '") + close + "'");
        }
    }

    const std::string& m_text;
    size_t m_pos = 0;
    std::string m_error;
};

}  // namespace

//  Strict reader for the known roads.json format:
//  { "roads": [ { "id":N, "type":"...", "width":F, "shoulder":F,
//                  "corner":F, "closed":B, "points":[ ... ] }, ... ] }
//
//  Not a general JSON library, but a strict one: every token is validated, an
//  unknown key has its value properly consumed, and a syntax error is reported
//  with its line and column instead of being guessed at. The previous scanner
//  returned 0 for unparsable numbers and could spin forever on a single
//  unexpected token, which froze the game at startup on a file it had just
//  written itself.

static bool parseRoadType(const std::string& s, RoadType& out) {
    if (s == "colonial_avenue")  { out = RoadType::ColonialAvenue; return true; }
    if (s == "urban_street")     { out = RoadType::UrbanStreet;    return true; }
    if (s == "country_highway")  { out = RoadType::CountryHighway; return true; }
    if (s == "country_dirt")     { out = RoadType::CountryDirt;    return true; }
    if (s == "port")             { out = RoadType::Port;           return true; }
    if (s == "medina")           { out = RoadType::Medina;         return true; }
    return false;
}

// Hierarchy-based road dimensions (world meters).  Used when a JSON segment
// omits width/shoulder so road sizes stay consistent with the road type.
static float roadTypeDefaultWidth(RoadType t) {
    switch (t) {
        case RoadType::ColonialAvenue:  return 11.5f;
        case RoadType::UrbanStreet:     return 8.0f;
        case RoadType::CountryHighway:  return 8.5f;
        case RoadType::CountryDirt:     return 6.0f;
        case RoadType::Port:            return 10.0f;
        case RoadType::Medina:          return 5.0f;
    }
    return 6.0f;
}

static float roadTypeDefaultShoulder(RoadType t) {
    return (t == RoadType::Medina) ? 0.75f : 1.5f;
}

static const char* roadTypeToString(RoadType t) {
    switch (t) {
        case RoadType::ColonialAvenue:  return "colonial_avenue";
        case RoadType::UrbanStreet:     return "urban_street";
        case RoadType::CountryHighway:  return "country_highway";
        case RoadType::CountryDirt:     return "country_dirt";
        case RoadType::Port:            return "port";
        case RoadType::Medina:          return "medina";
    }
    return "country_dirt";
}

// Reads one "points" array into control points. Requires both x and z on every
// point, so a typo can no longer be silently read as a point on the origin.
static bool parsePointList(JsonReader& json, std::vector<RoadPoint>& out) {
    if (!json.expect('[')) return false;
    if (json.consume(']')) return true;
    while (true) {
        if (!json.expect('{')) return false;
        bool hasX = false, hasZ = false;
        float x = 0.0f, z = 0.0f;
        bool first = true;
        std::string pkey;
        while (json.memberKey(pkey, first)) {
            if (pkey == "x") {
                if (!json.readFloat(x)) return false;
                hasX = true;
            } else if (pkey == "z") {
                if (!json.readFloat(z)) return false;
                hasZ = true;
            } else if (!json.skipValue()) {
                return false;
            }
        }
        if (json.failed()) return false;
        if (!hasX || !hasZ) return json.fail("a point needs both \"x\" and \"z\"");

        RoadPoint pt;
        pt.pos.x = x;
        pt.pos.y = z;   // pos.y stores world Z
        out.push_back(pt);

        if (json.consume(',')) continue;
        return json.expect(']');
    }
}

static bool parseRoadObject(JsonReader& json, RoadSegment& seg) {
    if (!json.expect('{')) return false;
    seg.width = 6.0f;
    seg.shoulderWidth = 1.5f;
    bool hasWidth = false, hasShoulder = false;
    bool first = true;
    std::string key;
    while (json.memberKey(key, first)) {
        if (key == "id") {
            double id = 0.0;
            if (!json.readNumber(id)) return false;
            if (id < 0.0 || id > 4294967295.0 || id != std::floor(id))
                return json.fail("\"id\" must be an integer between 0 and 4294967295");
            seg.id = (uint32_t)id;
        } else if (key == "type") {
            std::string name;
            if (!json.readString(name)) return false;
            if (!parseRoadType(name, seg.type))
                return json.fail("unknown road type \"" + name + "\"");
        } else if (key == "width") {
            if (!json.readFloat(seg.width)) return false;
            hasWidth = true;
        } else if (key == "shoulder") {
            if (!json.readFloat(seg.shoulderWidth)) return false;
            hasShoulder = true;
        } else if (key == "corner") {
            if (!json.readFloat(seg.cornerRadius)) return false;
            if (!(seg.cornerRadius >= 0.0f) || seg.cornerRadius > 1000.0f)
                return json.fail("\"corner\" must be between 0 and 1000 metres");
        } else if (key == "closed") {
            if (!json.readBool(seg.closed)) return false;
        } else if (key == "points") {
            if (!parsePointList(json, seg.controlPoints)) return false;
        } else if (!json.skipValue()) {
            return false;
        }
    }
    if (json.failed()) return false;

    // Resolve missing width/shoulder from the road-type hierarchy so sizes
    // are always well-defined in world meters.
    if (!hasWidth)   seg.width = roadTypeDefaultWidth(seg.type);
    if (!hasShoulder) seg.shoulderWidth = roadTypeDefaultShoulder(seg.type);
    return true;
}

static bool parseRoadsArray(JsonReader& json, std::vector<RoadSegment>& out) {
    if (!json.expect('[')) return false;
    if (json.consume(']')) return true;
    while (true) {
        RoadSegment seg;
        if (!parseRoadObject(json, seg)) return false;
        if (seg.controlPoints.size() >= 2) {
            out.push_back(std::move(seg));
        } else {
            std::cerr << "[RoadGraph] ignoring road " << seg.id << " with only "
                      << seg.controlPoints.size() << " point(s)" << std::endl;
        }
        if (json.consume(',')) continue;
        return json.expect(']');
    }
}

bool RoadGraph::loadFromJSON(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[RoadGraph] Failed to open: " << path << std::endl;
        return false;
    }

    // Read entire file into string
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    // Editors sometimes leave a UTF-8 BOM behind; it is not part of the document.
    if (content.size() >= 3 && (unsigned char)content[0] == 0xEF &&
        (unsigned char)content[1] == 0xBB && (unsigned char)content[2] == 0xBF) {
        content.erase(0, 3);
    }

    JsonReader json(content);
    auto report = [&]() {
        std::cerr << "[RoadGraph] " << path << ": " << json.error() << std::endl;
        return false;
    };

    // Parse into a scratch list: a syntax error must not leave the caller with
    // a half-loaded graph, and the previous content stays available.
    std::vector<RoadSegment> loaded;
    bool foundRoads = false;
    if (json.expect('{')) {
        bool first = true;
        std::string key;
        while (json.memberKey(key, first)) {
            if (key == "roads") {
                if (!parseRoadsArray(json, loaded)) return report();
                foundRoads = true;
            } else if (!json.skipValue()) {
                return report();
            }
        }
    }
    if (json.failed()) return report();
    if (!foundRoads) {
        std::cerr << "[RoadGraph] No 'roads' key found in " << path << std::endl;
        return false;
    }
    json.skipSpace();
    if (!json.atEnd())
        std::cerr << "[RoadGraph] " << path
                  << ": ignoring trailing content after the JSON object" << std::endl;

    segments = std::move(loaded);
    nodes.clear();

    std::cout << "[RoadGraph] Loaded " << segments.size() << " road segments from " << path << std::endl;
    return !segments.empty();
}

bool RoadGraph::saveToJSON(const std::string& path) const {
    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << "{\n  \"roads\": [\n";
    for (size_t i = 0; i < segments.size(); ++i) {
        const auto& seg = segments[i];
        file << "    {\n";
        file << "      \"id\": " << seg.id << ",\n";
        file << "      \"type\": \"" << roadTypeToString(seg.type) << "\",\n";
        file << "      \"width\": " << seg.width << ",\n";
        file << "      \"shoulder\": " << seg.shoulderWidth << ",\n";
        if (seg.cornerRadius > 0.0f) file << "      \"corner\": " << seg.cornerRadius << ",\n";
        if (seg.closed) file << "      \"closed\": true,\n";
        file << "      \"points\": [\n";
        for (size_t j = 0; j < seg.controlPoints.size(); ++j) {
            file << "        {\"x\": " << seg.controlPoints[j].pos.x
                 << ", \"z\": " << seg.controlPoints[j].pos.y << "}";
            if (j + 1 < seg.controlPoints.size()) file << ",";
            file << "\n";
        }
        file << "      ]\n";
        file << "    }";
        if (i + 1 < segments.size()) file << ",";
        file << "\n";
    }
    file << "  ]\n}\n";
    return true;
}

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  RoadGraph â€” Topology
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

void RoadGraph::buildTopology(float snapTolerance) {
    nodes.clear();

    // Collect all endpoints
    struct EndpointRef {
        uint32_t segIdx;
        bool isEnd; // false = start, true = end
        glm::vec2 pos;
    };
    std::vector<EndpointRef> endpoints;

    for (uint32_t i = 0; i < (uint32_t)segments.size(); ++i) {
        auto& seg = segments[i];
        if (seg.controlPoints.empty()) continue;
        endpoints.push_back({i, false, seg.controlPoints.front().pos});
        endpoints.push_back({i, true,  seg.controlPoints.back().pos});
    }

    // Cluster endpoints within tolerance
    std::vector<int> clusterID(endpoints.size(), -1);
    int nextCluster = 0;

    for (size_t i = 0; i < endpoints.size(); ++i) {
        if (clusterID[i] >= 0) continue;
        clusterID[i] = nextCluster;

        for (size_t j = i + 1; j < endpoints.size(); ++j) {
            if (clusterID[j] >= 0) continue;
            float d = glm::length(endpoints[i].pos - endpoints[j].pos);
            if (d <= snapTolerance) {
                clusterID[j] = nextCluster;
            }
        }
        nextCluster++;
    }

    // Create nodes from clusters
    nodes.resize(nextCluster);
    std::vector<glm::vec2> clusterSum(nextCluster, glm::vec2(0.0f));
    std::vector<int> clusterCount(nextCluster, 0);

    for (size_t i = 0; i < endpoints.size(); ++i) {
        int c = clusterID[i];
        clusterSum[c] += endpoints[i].pos;
        clusterCount[c]++;
        nodes[c].connectedSegments.push_back(endpoints[i].segIdx);
    }

    for (int i = 0; i < nextCluster; ++i) {
        nodes[i].id = (uint32_t)i;
        glm::vec2 avg = clusterSum[i] / float(clusterCount[i]);
        nodes[i].position = glm::vec3(avg.x, 0.0f, avg.y);
    }

    // Count junctions (nodes with 3+ connections)
    int junctions = 0;
    for (auto& n : nodes) {
        if (n.connectedSegments.size() >= 3) junctions++;
    }

    std::cout << "[RoadGraph] Topology: " << nodes.size() << " nodes, "
              << junctions << " junctions" << std::endl;
}

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  RoadSpline â€” Catmull-Rom evaluation
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

static glm::vec2 catmullRom(const glm::vec2& p0, const glm::vec2& p1,
                             const glm::vec2& p2, const glm::vec2& p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) +
                   (-p0 + p2) * t +
                   (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

void RoadSpline::evaluate(RoadSegment& seg, const HeightSampler& sampler,
                          float samplesPerMeter) {
    const auto& cps = seg.controlPoints;
    int n = (int)cps.size();
    seg.surfaceBoundsValid = false;
    if (n < 2) return;

    seg.splinePoints.clear();
    seg.tangents.clear();
    seg.rightVecs.clear();
    seg.cumulativeDist.clear();

    // Collect 2D control points
    std::vector<glm::vec2> pts(n);
    for (int i = 0; i < n; ++i)
        pts[i] = cps[i].pos;

    // Detect a closed loop (round road / roundabout): first control point lands back
    // on the last one. Such roads are rendered as continuous rings.
    //
    // The authored `closed` flag from the JSON wins: a closed segment takes a
    // completely different mesh path (ring, no end caps) and is EXCLUDED from
    // endpoint clustering, so silently promoting an open road to a ring severs it
    // from every junction it was supposed to meet. Only auto-detect a genuine loop,
    // i.e. one where the author actually duplicated the first point at the end.
    const float loopTol = glm::max(1.0f, seg.width * 0.5f);
    if (!seg.closed) {
        seg.closed = (n >= 4 &&
            glm::length(pts.front() - pts.back()) < loopTol);
    }

    // â”€â”€ Adaptive sampling â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // Straight stretches resample at the requested base step (samplesPerMeter);
    // curved sections (the rounded corners) tessellate by turn angle instead, so
    // tight Medina turns get far more samples than long straight avenues without
    // wasting triangle density on the straights.
    const float baseStep = glm::max(0.5f, 1.0f / glm::max(samplesPerMeter, 0.1f));
    const float maxArcAngle = 0.045f;  // one sample per ~2.6 degrees of corner turn

    auto rsewTo = [&](std::vector<glm::vec2>& out, const glm::vec2& p, float step) {
        if (out.empty()) { out.push_back(p); return; }
        glm::vec2 a = out.back();
        float len = glm::length(p - a);
        if (len < 0.01f) return;
        int c = std::max(1, (int)std::ceil(len / glm::max(step, 0.25f)));
        for (int k = 1; k <= c; ++k) {
            glm::vec2 q = glm::mix(a, p, float(k) / float(c));
            if (glm::length(q - out.back()) > 0.01f) out.push_back(q);
        }
    };

    // â”€â”€ Build the smooth centerline path â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    //  * open roads : circular fillet arcs round every interior corner, so the
    //    ribbon edge-lines and curbs curve through the turn like real streets.
    //  * closed loops: sampled around the ring continuously; the seam corner
    //    (between the last and first anchors) is rounded exactly like the others
    //    instead of letting the closing sliver cut straight across the corner.
    std::vector<glm::vec2> path;
    auto emitCorner = [&](std::vector<glm::vec2>& out, const glm::vec2& a,
                          const glm::vec2& b, const glm::vec2& c, float step) {
        // Returns true when the corner at b is rounded by a fillet arc.
        if (glm::length(b - a) < 1e-4f || glm::length(c - b) < 1e-4f) return false;
        glm::vec2 u1 = glm::normalize(b - a);
        glm::vec2 u2 = glm::normalize(c - b);
        const float dotC = glm::clamp(glm::dot(u1, u2), -1.0f, 1.0f);
        // The fillet's tangent run. A per-road cornerRadius (the editor's CORNER RADIUS
        // slider) wins when set; otherwise fall back to the automatic half-width so
        // every road that never opts in keeps rounding exactly as it did before. Either
        // way it is capped at half of each adjacent segment, which is what stops two
        // neighbouring corners from eating each other on a short zig-zag.
        float r = 0.0f;
        if (dotC < 0.93f) {                              // ~21.5 deg: straighter than this is already smooth
            const float cap = glm::min(glm::length(b - a) * 0.5f, glm::length(c - b) * 0.5f);
            // Keep the inner road edge from folding into itself. A radius of only
            // half the width collapses the inside edge on a 90-degree bend.
            const float want = (seg.cornerRadius > 0.0f) ? seg.cornerRadius : seg.width * 1.5f;
            r = glm::min(want, cap);
        }
        if (r < 0.15f) r = 0.0f;
        if (r <= 0.0f) return false;
        const glm::vec2 T1 = b - u1 * r;            // tangent point on incoming edge
        const glm::vec2 T2 = b + u2 * r;            // tangent point on outgoing edge
        rsewTo(out, T1, step);
        const float theta = std::acos(dotC);
        // `r` is the tangent run, not the circle radius. They are equal only at
        // a 90-degree turn. Using r as the arc radius made shallow bends miss T2
        // and created the sharp kinks visible where roads curve and join.
        const float tanHalf = std::tan(theta * 0.5f);
        if (tanHalf < 1e-4f) return false;
        const float arcRadius = r / tanHalf;
        // Circle center from the chord T1T2, exact for every turn angle.
        const glm::vec2 mid = (T1 + T2) * 0.5f;
        const glm::vec2 chord = T2 - T1;
        const float halfChord = glm::length(chord) * 0.5f;
        glm::vec2 C;
        if (halfChord <= arcRadius) {
            const float h = std::sqrt(std::max(arcRadius * arcRadius - halfChord * halfChord, 0.0f));
            glm::vec2 perp = glm::dot(chord, chord) > 1e-8f
                ? glm::normalize(glm::vec2(-chord.y, chord.x)) : glm::vec2(0.0f);
            glm::vec2 c1 = mid + perp * h;
            glm::vec2 c2 = mid - perp * h;
            C = (glm::dot(c1 - b, c1 - b) > glm::dot(c2 - b, c2 - b)) ? c1 : c2; // bulge away from apex
        } else {
            C = mid;                                  // degenerate: straight across
        }
        const float a0 = std::atan2(T1.y - C.y, T1.x - C.x);
        const float ccw = (u1.x * u2.y - u1.y * u2.x >= 0.0f) ? 1.0f : -1.0f;
        const int na = std::max(2, (int)std::ceil(theta / maxArcAngle));
        for (int k = 1; k <= na; ++k) {
            const float an = a0 + ccw * (theta * float(k) / float(na));
            const glm::vec2 p = C + glm::vec2(std::cos(an), std::sin(an)) * arcRadius;
            if (glm::length(p - out.back()) > 0.01f) out.push_back(p);
        }
        return true;
    };

    if (seg.closed) {
        // Collect unique loop control points (discard duplicate closing point if present)
        std::vector<glm::vec2> loopPts;
        loopPts.reserve(n);
        for (int i = 0; i < n; ++i) {
            if (i == n - 1 && glm::length(pts[i] - pts[0]) < loopTol)
                continue;
            loopPts.push_back(pts[i]);
        }
        const int N = (int)loopPts.size();
        if (N >= 3) {
            // Use the same circular fillets for closed loops as for open roads.
            // The old periodic Catmull-Rom path rounded a corner differently from
            // the Corner Radius tool, and could cut sharply across small round roads.
            for (int k = 0; k < N; ++k) {
                const glm::vec2& a = loopPts[(k - 1 + N) % N];
                const glm::vec2& b = loopPts[k];
                const glm::vec2& c = loopPts[(k + 1) % N];
                if (!emitCorner(path, a, b, c, baseStep))
                    rsewTo(path, b, baseStep);
            }
            // Fill the final straight span up to the seam. Drop the duplicated first
            // sample afterward; the closed ribbon joins the last sample back to it.
            if (path.size() > 1) {
                rsewTo(path, path.front(), baseStep);
                if (path.size() > 1 && glm::length(path.back() - path.front()) < 0.01f)
                    path.pop_back();
            }
        } else {
            for (const auto& p : pts) path.push_back(p);
        }
    } else {
        path.push_back(pts[0]);
        for (int j = 1; j < n - 1; ++j) {
            const glm::vec2& a = pts[j - 1];
            const glm::vec2& b = pts[j];
            const glm::vec2& c = pts[j + 1];
            if (!emitCorner(path, a, b, c, baseStep))
                rsewTo(path, b, baseStep);
        }
        rsewTo(path, pts.back(), baseStep);
    }

    // Sampling at >0.5/m is already done above; store as sampled geometry.
    // Sample heightmap for elevation directly without alteration
    int m = (int)path.size();
    seg.splinePoints.resize(m);
    for (int i = 0; i < m; ++i) {
        float y = sampler.isValid() ? sampler.sample(path[i].x, path[i].y) : 0.0f;
        seg.splinePoints[i] = glm::vec3(path[i].x, y, path[i].y);
    }

    // Grade the road like a built surface: remove short DEM bumps along the
    // centerline, then enforce a realistic maximum longitudinal grade. The same
    // sampled profile drives the asphalt mesh and terrain bed below it.
    smoothElevation(seg.splinePoints, 0.08f, 5.0f, seg.closed);

    // Compute tangents (central differences, wrapping for closed rings)
    seg.tangents.resize(m);
    for (int i = 0; i < m; ++i) {
        glm::vec3 fwd;
        if (seg.closed) {
            int pr = (i - 1 + m) % m;
            int nx = (i + 1) % m;
            fwd = (seg.splinePoints[nx] - seg.splinePoints[pr]) * 0.5f;
        } else if (i == 0)
            fwd = seg.splinePoints[1] - seg.splinePoints[0];
        else if (i == m - 1)
            fwd = seg.splinePoints[m - 1] - seg.splinePoints[m - 2];
        else
            fwd = (seg.splinePoints[i + 1] - seg.splinePoints[i - 1]) * 0.5f;

        float len = glm::length(glm::vec2(fwd.x, fwd.z));
        seg.tangents[i] = (len > 1e-6f) ? glm::normalize(glm::vec3(fwd.x, 0.0f, fwd.z)) : glm::vec3(1, 0, 0);
    }

    // Smooth tangents (Gaussian-like: 3-tap moving average, 2 passes)
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<glm::vec3> smoothed = seg.tangents;
        if (seg.closed) {
            // cyclic smoothing so the ring seam is as smooth as the rest
            for (int i = 0; i < m; ++i) {
                int pv = (i - 1 + m) % m;
                int nx = (i + 1) % m;
                glm::vec3 t = seg.tangents[pv] * 0.25f +
                              seg.tangents[i] * 0.50f +
                              seg.tangents[nx] * 0.25f;
                float tLen = glm::length(glm::vec2(t.x, t.z));
                smoothed[i] = (tLen > 1e-6f) ? glm::normalize(glm::vec3(t.x, 0.0f, t.z)) : seg.tangents[i];
            }
        } else {
            for (int i = 1; i < m - 1; ++i) {
                glm::vec3 t = seg.tangents[i - 1] * 0.25f +
                              seg.tangents[i]     * 0.50f +
                              seg.tangents[i + 1] * 0.25f;
                float tLen = glm::length(glm::vec2(t.x, t.z));
                smoothed[i] = (tLen > 1e-6f) ? glm::normalize(glm::vec3(t.x, 0.0f, t.z)) : seg.tangents[i];
            }
        }
        seg.tangents = smoothed;
    }

    // Compute right vectors with horizontal perpendicular (no holonomy, no twist)
    computeRoadFrames(seg.rightVecs, seg.tangents, seg.closed);

    // Cumulative distance
    seg.cumulativeDist.resize(m);
    seg.cumulativeDist[0] = 0.0f;
    for (int i = 1; i < m; ++i) {
        seg.cumulativeDist[i] = seg.cumulativeDist[i - 1] +
            glm::length(seg.splinePoints[i] - seg.splinePoints[i - 1]);
    }

    if (!seg.splinePoints.empty()) {
        glm::vec2 minPoint(seg.splinePoints.front().x,
                           seg.splinePoints.front().z);
        glm::vec2 maxPoint = minPoint;
        for (const glm::vec3& point : seg.splinePoints) {
            minPoint.x = std::min(minPoint.x, point.x);
            minPoint.y = std::min(minPoint.y, point.z);
            maxPoint.x = std::max(maxPoint.x, point.x);
            maxPoint.y = std::max(maxPoint.y, point.z);
        }
        seg.surfaceBounds = glm::vec4(minPoint.x, minPoint.y,
                                      maxPoint.x, maxPoint.y);
        seg.surfaceBoundsValid = true;
    }

    // Curvature (discrete curvature = |T[i+1] - T[i-1]| / (2*ds))
    seg.curvature.resize(m, 0.0f);
    for (int i = 1; i < m - 1; ++i) {
        glm::vec3 dT = seg.tangents[i + 1] - seg.tangents[i - 1];
        float ds = seg.cumulativeDist[i + 1] - seg.cumulativeDist[i - 1];
        seg.curvature[i] = (ds > 0.01f) ? (glm::length(dT) / ds) : 0.0f;
    }
}

void RoadSpline::smoothElevation(std::vector<glm::vec3>& positions,
                                  float maxGrade, float smoothSigma, bool closed) {
    int n = (int)positions.size();
    if (n < 3) return;

    // Smooth by world-space distance rather than sample index. Curves are sampled
    // more densely than straights, so an index-based kernel smoothed them at a
    // different physical scale and left visible DEM bumps along straighter roads.
    const float sigma = std::max(smoothSigma, 0.01f);
    const float radius = sigma * 3.0f;
    std::vector<float> arc(n, 0.0f);
    for (int i = 1; i < n; ++i) {
        const float dx = positions[i].x - positions[i - 1].x;
        const float dz = positions[i].z - positions[i - 1].z;
        arc[i] = arc[i - 1] + std::sqrt(dx * dx + dz * dz);
    }
    const float totalLength = arc.back() + (closed ? glm::length(glm::vec2(
        positions.front().x - positions.back().x,
        positions.front().z - positions.back().z)) : 0.0f);
    std::vector<float> smoothedY(n);

    for (int i = 0; i < n; ++i) {
        float sumW = 0.0f, sumY = 0.0f;
        for (int j = 0; j < n; ++j) {
            float d = std::abs(arc[j] - arc[i]);
            if (closed && totalLength > 0.01f) d = std::min(d, totalLength - d);
            if (d > radius) continue;
            const float w = std::exp(-0.5f * (d * d) / (sigma * sigma));
            sumW += w;
            sumY += w * positions[j].y;
        }
        smoothedY[i] = sumY / sumW;
    }

    if (!closed) {
        smoothedY[0] = positions[0].y;
        smoothedY[n - 1] = positions[n - 1].y;
    }

    // Limit longitudinal grade in metres/metre. Closed curves also constrain the
    // seam so that the first and last cross-sections meet without a slope jump.
    const int passes = closed ? 4 : 1;
    for (int pass = 0; pass < passes; ++pass) {
        const int forwardEnd = closed ? n : n - 1;
        for (int i = 1; i < forwardEnd; ++i) {
            const float dist = arc[i] - arc[i - 1];
            if (dist <= 0.01f) continue;
            const float maxDy = maxGrade * dist;
            smoothedY[i] = std::clamp(smoothedY[i], smoothedY[i - 1] - maxDy,
                                      smoothedY[i - 1] + maxDy);
        }
        const int backwardStart = closed ? 0 : 1;
        for (int i = n - 2; i >= backwardStart; --i) {
            const float dist = arc[i + 1] - arc[i];
            if (dist <= 0.01f) continue;
            const float maxDy = maxGrade * dist;
            smoothedY[i] = std::clamp(smoothedY[i], smoothedY[i + 1] - maxDy,
                                      smoothedY[i + 1] + maxDy);
        }
        if (closed && totalLength > 0.01f) {
            const float seamGrade = maxGrade * (totalLength - arc.back());
            smoothedY[0] = std::clamp(smoothedY[0], smoothedY[n - 1] - seamGrade,
                                      smoothedY[n - 1] + seamGrade);
        }
    }

    for (int i = 0; i < n; ++i)
        positions[i].y = smoothedY[i];
}

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  RoadDeformer â€” Non-destructive terrain deformation
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

// Helper: find nearest point on a polyline and return distance + nearest elevation
struct RoadBedSample {
    float centerDistance = 1e9f;
    float edgeDistance = 1e9f;
    float halfWidth = 0.0f;
    float elevation = 0.0f;
};

static float roadMeshHalfWidth(const RoadSegment& road, int idx, bool wrap,
                               float side, float fullHalfWidth) {
    const int n = (int)road.splinePoints.size();
    if (!wrap && (idx <= 0 || idx + 1 >= n)) return fullHalfWidth;
    const int pr = wrap ? (idx - 1 + n) % n : idx - 1;
    const int nx = wrap ? (idx + 1) % n : idx + 1;
    const glm::vec2 a(road.splinePoints[pr].x, road.splinePoints[pr].z);
    const glm::vec2 b(road.splinePoints[idx].x, road.splinePoints[idx].z);
    const glm::vec2 c(road.splinePoints[nx].x, road.splinePoints[nx].z);
    const glm::vec2 ab = b - a, ac = c - a, bc = c - b;
    const float turn = ab.x * bc.y - ab.y * bc.x;
    if (std::fabs(turn) < 1e-5f || !(side * turn < 0.0f)) return fullHalfWidth;
    const float area2 = 0.5f * std::fabs(ab.x * ac.y - ab.y * ac.x);
    if (area2 < 1e-6f) return fullHalfWidth;
    const float la = glm::length(b - c), lb = glm::length(c - a), lc = glm::length(a - b);
    const float rho = (la * lb * lc) / (4.0f * area2);
    if (rho >= fullHalfWidth * 2.0f) return fullHalfWidth;
    return std::max(0.3f, rho - 0.3f);
}

static void accumulateLowestRoadBed(float baseHeight, float candidateBed,
                                    float influence, float& bedOffset,
                                    float& accumulatedInfluence) {
    // Blend every overlapping road candidate against the original terrain, then
    // keep the lowest result. Comparing against the already accumulated result
    // makes the outcome independent of segment iteration order.
    const float candidate = glm::mix(baseHeight, candidateBed, influence);
    const float current = baseHeight + bedOffset;
    if (accumulatedInfluence <= 0.001f || candidate < current) {
        bedOffset = candidate - baseHeight;
    }
    accumulatedInfluence = std::max(accumulatedInfluence, influence);
}

// Query the ribbon using the same mitered right vectors as RoadMeshBuilder.
// A centerline-radius test misses the extra width on outside bends and therefore
// leaves untouched terrain poking over the rendered asphalt at corners.
static RoadBedSample nearestRoadBed(const RoadSegment& road, float qx, float qz) {
    RoadBedSample best;
    const int n = (int)road.splinePoints.size();
    if (n < 2 || road.rightVecs.size() != road.splinePoints.size()) return best;
    const int edgeCount = road.closed ? n : n - 1;
    for (int i = 0; i < edgeCount; ++i) {
        const int j = (i + 1) % n;
        const glm::vec2 a(road.splinePoints[i].x, road.splinePoints[i].z);
        const glm::vec2 b(road.splinePoints[j].x, road.splinePoints[j].z);
        const glm::vec2 d = b - a;
        const float len2 = glm::dot(d, d);
        if (len2 < 1e-8f) continue;
        const float len = std::sqrt(len2);
        const glm::vec2 p(qx, qz);
        const float rawT = glm::dot(p - a, d) / len2;
        const float t = std::clamp(rawT, 0.0f, 1.0f);
        const glm::vec2 center = a + d * t;
        const glm::vec2 delta = p - center;
        const float centerDistance = glm::length(delta);
        const glm::vec3 r3 = glm::mix(road.rightVecs[i], road.rightVecs[j], t);
        const glm::vec2 r2(r3.x, r3.z);
        const float rLen = glm::length(r2);
        if (rLen < 1e-5f) continue;
        const float signedLateral = glm::dot(delta, r2 / rLen);
        const float side = signedLateral < 0.0f ? -1.0f : 1.0f;
        const float hw0 = roadMeshHalfWidth(road, i, road.closed, side, road.width * 0.5f);
        const float hw1 = roadMeshHalfWidth(road, j, road.closed, side, road.width * 0.5f);
        // The rendered ribbon interpolates two miter-corrected rings while this
        // raster query projects onto their centerline chord. A small overlap
        // absorbs that sub-metre projection error so no terrain sliver survives
        // at a sharp inner fold.
        constexpr float kRasterOverlap = 0.8f;
        const float halfWidth = glm::mix(hw0, hw1, t) * rLen + kRasterOverlap;
        const float lateral = std::fabs(signedLateral);
        // Interior spline-ring ends are shared with the adjacent ribbon quad;
        // their interpolated miter vector may lean slightly along the tangent.
        // Only apply longitudinal end-cap distance at the road's true endpoints.
        float capOutside = 0.0f;
        if (!road.closed && i == 0 && rawT < 0.0f) capOutside = -rawT * len;
        if (!road.closed && i == n - 2 && rawT > 1.0f) capOutside = (rawT - 1.0f) * len;
        const float outsideLateral = std::max(0.0f, lateral - halfWidth);
        const float edgeDistance = capOutside > 0.0f
            ? std::max(0.0f, std::sqrt(lateral * lateral + capOutside * capOutside) - halfWidth)
            : outsideLateral;
        best.centerDistance = std::min(best.centerDistance, centerDistance);
        if (edgeDistance < best.edgeDistance) {
            best.edgeDistance = edgeDistance;
            best.halfWidth = halfWidth;
            best.elevation = road.splinePoints[i].y * (1.0f - t) + road.splinePoints[j].y * t;
        }
    }
    return best;
}

void RoadDeformer::build(const std::vector<RoadSegment>& segments,
                         const HeightSampler& sampler,
                         const TerrainCoordinateMapper& mapper,
                         int deformW, int deformH) {
    m_mapper = mapper;
    m_mapW = deformW;
    m_mapH = deformH;
    m_deformMap.assign(deformW * deformH, 0.0f);
    m_influenceMap.assign(deformW * deformH, 0.0f);

    std::cout << "[RoadDeformer] Building deformation field (" << deformW << "x" << deformH << ")..." << std::endl;

    float worldW = mapper.bounds.maxX - mapper.bounds.minX;
    float worldH = mapper.bounds.maxZ - mapper.bounds.minZ;
    float pixelSizeX = worldW / float(deformW - 1);
    float pixelSizeZ = worldH / float(deformH - 1);

    // Segment-centric approach: for each road segment, only rasterize pixels
    // within its bounding box + influence radius.  This avoids the O(pixels Ã— segments)
    // brute-force that was causing 60+ second startup times.
    for (const auto& seg : segments) {
        if (seg.splinePoints.empty()) continue;

        float halfWidth = seg.width * 0.5f;
        // A buildable road needs a broad, gradual tie-in to the existing land.
        // Keep this grading width independent of the visible curb/shoulder width.
        const float terrainBlendWidth = std::max(seg.shoulderWidth, 5.0f);
        float maxReach  = halfWidth + terrainBlendWidth;

        // Compute world-space AABB of this segment's spline points + margin
        float segMinX = 1e9f, segMaxX = -1e9f;
        float segMinZ = 1e9f, segMaxZ = -1e9f;
        for (const auto& pt : seg.splinePoints) {
            segMinX = std::min(segMinX, pt.x);
            segMaxX = std::max(segMaxX, pt.x);
            segMinZ = std::min(segMinZ, pt.z);
            segMaxZ = std::max(segMaxZ, pt.z);
        }
        segMinX -= maxReach;  segMaxX += maxReach;
        segMinZ -= maxReach;  segMaxZ += maxReach;

        // Convert AABB to pixel range in the deformation grid
        // Column: u = (wx - minX) / worldW  â†’  col = u * (deformW - 1)
        int colMin = std::max(0, (int)floorf((segMinX - mapper.bounds.minX) / worldW * (deformW - 1)));
        int colMax = std::min(deformW - 1, (int)ceilf((segMaxX - mapper.bounds.minX) / worldW * (deformW - 1)));
        // Row: TerrainManager's row 0 is the south edge (minZ).
        int rowMin = std::max(0, (int)floorf((segMinZ - mapper.bounds.minZ) / worldH * (deformH - 1)));
        int rowMax = std::min(deformH - 1, (int)ceilf((segMaxZ - mapper.bounds.minZ) / worldH * (deformH - 1)));

        // Rasterize only the local pixel region around this road segment
        for (int row = rowMin; row <= rowMax; ++row) {
            for (int col = colMin; col <= colMax; ++col) {
                // Pixel â†’ World
                float u = float(col) / float(deformW - 1);
                float v = float(row) / float(deformH - 1);
                float wx = mapper.bounds.minX + u * worldW;
                float wz = mapper.bounds.minZ + v * worldH;

                const RoadBedSample bed = nearestRoadBed(seg, wx, wz);
                if (bed.centerDistance > halfWidth + terrainBlendWidth) continue;

                // Influence is 1 over the exact rendered ribbon footprint and
                // fades smoothly out through the shoulder.
                float influence = 0.0f;
                if (bed.edgeDistance <= 1e-4f) {
                    influence = 1.0f;
                } else if (terrainBlendWidth > 1e-4f && bed.edgeDistance <= terrainBlendWidth) {
                    float t = bed.edgeDistance / terrainBlendWidth;
                    t = std::clamp(t, 0.0f, 1.0f);
                    influence = 1.0f - (t * t * (3.0f - 2.0f * t)); // smoothstep
                }

                if (influence > 0.001f) {
                    float baseH = sampler.sample(wx, wz);
                    // Match the actual road ribbon cross-section. RoadMeshBuilder
                    // keeps 30% of the DEM's cross-slope at the asphalt edge; using
                    // the centreline height here instead flattened the terrain to a
                    // different profile than the road and left a visible ledge/seam.
                    // Keep the terrain bed slightly below asphalt. Exact coplanar
                    // surfaces still let the terrain win depth tests at grazing
                    // angles, which shows up as terrain crawling over the road.
                    // RoadMeshBuilder adds a 4 cm depth-safe surface bias. Keep
                    // the terrain bed at the matching unsurfaced road profile.
                    constexpr float kRoadBedClearance = 0.0f;
                    const float roadBedH = bed.elevation + (baseH - bed.elevation) * kRoadEdgeConform
                                         - kRoadBedClearance;
                    int idx = row * deformW + col;
                    // At crossings, constrain the terrain below the lowest
                    // overlapping road surface. Choosing only the first/equal
                    // influence made the bed alternate above and below roads
                    // depending on segment order.
                    accumulateLowestRoadBed(baseH, roadBedH, influence,
                                            m_deformMap[idx], m_influenceMap[idx]);
                }
            }
        }
    }

    // Count affected pixels
    int affected = 0;
    for (int i = 0; i < deformW * deformH; ++i) {
        if (m_influenceMap[i] > 0.001f) affected++;
    }

    std::cout << "[RoadDeformer] Done. " << affected << " pixels affected ("
              << (100.0f * affected / (deformW * deformH)) << "%)" << std::endl;
}

float RoadDeformer::getDeformation(float x, float z) const {
    if (m_deformMap.empty()) return 0.0f;

    float ux = (x - m_mapper.bounds.minX) / (m_mapper.bounds.maxX - m_mapper.bounds.minX);
    float uz = (z - m_mapper.bounds.minZ) / (m_mapper.bounds.maxZ - m_mapper.bounds.minZ);
    ux = std::clamp(ux, 0.0f, 1.0f);
    uz = std::clamp(uz, 0.0f, 1.0f);

    float fx = ux * (m_mapW - 1);
    float fz = uz * (m_mapH - 1);

    int x0 = (int)fx, x1 = std::min(x0 + 1, m_mapW - 1);
    int z0 = (int)fz, z1 = std::min(z0 + 1, m_mapH - 1);
    float tx = fx - x0, tz = fz - z0;

    float d00 = m_deformMap[z0 * m_mapW + x0];
    float d10 = m_deformMap[z0 * m_mapW + x1];
    float d01 = m_deformMap[z1 * m_mapW + x0];
    float d11 = m_deformMap[z1 * m_mapW + x1];

    return d00 * (1 - tx) * (1 - tz)
         + d10 *    tx    * (1 - tz)
         + d01 * (1 - tx) *    tz
         + d11 *    tx    *    tz;
}

float RoadDeformer::getRoadInfluence(float x, float z) const {
    if (m_influenceMap.empty()) return 0.0f;

    float ux = (x - m_mapper.bounds.minX) / (m_mapper.bounds.maxX - m_mapper.bounds.minX);
    float uz = (z - m_mapper.bounds.minZ) / (m_mapper.bounds.maxZ - m_mapper.bounds.minZ);
    ux = std::clamp(ux, 0.0f, 1.0f);
    uz = std::clamp(uz, 0.0f, 1.0f);

    float fx = ux * (m_mapW - 1);
    float fz = uz * (m_mapH - 1);

    int x0 = (int)fx, x1 = std::min(x0 + 1, m_mapW - 1);
    int z0 = (int)fz, z1 = std::min(z0 + 1, m_mapH - 1);
    float tx = fx - x0, tz = fz - z0;

    float i00 = m_influenceMap[z0 * m_mapW + x0];
    float i10 = m_influenceMap[z0 * m_mapW + x1];
    float i01 = m_influenceMap[z1 * m_mapW + x0];
    float i11 = m_influenceMap[z1 * m_mapW + x1];

    return i00 * (1 - tx) * (1 - tz)
         + i10 *    tx    * (1 - tz)
         + i01 * (1 - tx) *    tz
         + i11 *    tx    *    tz;
}

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  RoadTileField â€” 4-direction auto-tiling field derived from road splines
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

void RoadTileField::build(const std::vector<RoadSegment>& segments,
                          const TerrainCoordinateMapper& mapper,
                          float cellSize,
                          int maxResolution) {
    m_cellSize = std::max(0.5f, cellSize);
    m_map = { mapper.bounds.minX, mapper.bounds.maxX,
              mapper.bounds.minZ, mapper.bounds.maxZ,
              mapper.flipHeightmapZ };

    const float worldW = mapper.bounds.maxX - mapper.bounds.minX;
    const float worldH = mapper.bounds.maxZ - mapper.bounds.minZ;
    const int cap = std::max(2, maxResolution);
    const int w = std::clamp((int)std::floor(worldW / m_cellSize) + 1, 2, cap);
    const int h = std::clamp((int)std::floor(worldH / m_cellSize) + 1, 2, cap);
    // Reallocates only when the resolution changed, then drops the previous
    // stamp set. A rebuild triggered by a road edit costs time proportional to
    // the road network, not to the 3485 x 3485 grid.
    m_grid.beginBulkStamp(w, h);

    // One cell per road cross-section: the mask describes topology, not paved
    // surface, so the segment width plays no part (use Terrain::getRoadAt for
    // continuous road presence). Walking the spline sample by sample keeps the
    // ribbon 4-connected even where a road is thinner than a cell.
    //
    // Pass 1 stamps occupancy and records one rect per walked span; pass 2
    // recomputes only those rects, dilated by one cell because a mask depends on
    // its four neighbours. Stamping first and recomputing afterwards keeps the
    // result independent of segment order.
    struct SpanRect { int c0, r0, c1, r1; };
    std::vector<SpanRect> dirty;
    dirty.reserve(segments.size() * 4);
    for (const auto& seg : segments) {
        const int n = (int)seg.splinePoints.size();
        if (n < 2) continue;

        const uint8_t stamp = (uint8_t)((int)seg.type + 1);
        for (int i = 0; i + 1 < n; ++i) {
            const glm::vec3& a = seg.splinePoints[i];
            const glm::vec3& b = seg.splinePoints[i + 1];
            int c0 = INT_MAX, r0 = INT_MAX, c1 = INT_MIN, r1 = INT_MIN;
            RoadTile::walkRibbon(m_map, w, h, a.x, a.z, b.x, b.z,
                                 [&](int col, int row) {
                                     c0 = std::min(c0, col); c1 = std::max(c1, col);
                                     r0 = std::min(r0, row); r1 = std::max(r1, row);
                                     m_grid.stampOccupancy(col, row, stamp);
                                 });
            if (c1 >= c0) dirty.push_back({c0, r0, c1, r1});   // empty if out of bounds
        }
    }

    for (const SpanRect& r : dirty) m_grid.recomputeMasksInRect(r.c0 - 1, r.r0 - 1,
                                                               r.c1 + 1, r.r1 + 1);
    m_grid.rebuildHistogram();

    std::cout << "[RoadTileField] " << w << "x" << h << " cells (" << m_cellSize
              << " m), " << m_grid.roadCellCount() << " road cells: "
              << shapeSummary() << std::endl;

    if (std::getenv("FALLAGA_ROADTILES")) m_grid.debugPrint();
}

bool RoadTileField::isRoadAt(float x, float z) const {
    if (!isBuilt()) return false;
    return m_grid.isRoad(m_map.colFor(x, m_grid.width()),
                         m_map.rowFor(z, m_grid.height()));
}

uint8_t RoadTileField::maskAt(float x, float z) const {
    if (!isBuilt()) return 0;
    return m_grid.maskAt(m_map.colFor(x, m_grid.width()),
                         m_map.rowFor(z, m_grid.height()));
}

std::optional<RoadTile::Shape> RoadTileField::shapeAt(float x, float z) const {
    if (!isBuilt()) return std::nullopt;
    return m_grid.shapeAt(m_map.colFor(x, m_grid.width()),
                          m_map.rowFor(z, m_grid.height()));
}

int RoadTileField::armCountAt(float x, float z) const {
    return RoadTile::armCount(maskAt(x, z));
}

bool RoadTileField::isConnectedTowards(float x, float z, uint8_t direction) const {
    return (maskAt(x, z) & (direction & RoadTile::All)) != 0;
}

std::string RoadTileField::shapeSummary() const {
    auto n = [this](RoadTile::Shape s) { return m_grid.countShape(s); };
    std::string out;
    auto add = [&out](const char* label, int count) {
        if (count <= 0) return;
        if (!out.empty()) out += ", ";
        out += std::string(label) + " " + std::to_string(count);
    };
    add("straights", n(RoadTile::Shape::Straight_NS) + n(RoadTile::Shape::Straight_EW));
    add("corners",   n(RoadTile::Shape::Corner_NE) + n(RoadTile::Shape::Corner_SE) +
                           n(RoadTile::Shape::Corner_SW) + n(RoadTile::Shape::Corner_NW));
    add("T-junctions", n(RoadTile::Shape::TJunction_NoN) + n(RoadTile::Shape::TJunction_NoE) +
                             n(RoadTile::Shape::TJunction_NoS) + n(RoadTile::Shape::TJunction_NoW));
    add("crossroads", n(RoadTile::Shape::Crossroad));
    add("dead-ends",  n(RoadTile::Shape::DeadEnd_N) + n(RoadTile::Shape::DeadEnd_E) +
                            n(RoadTile::Shape::DeadEnd_S) + n(RoadTile::Shape::DeadEnd_W));
    add("isolated",   n(RoadTile::Shape::Single));
    if (out.empty()) out = "no road cells";
    return out;
}

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  RoadMeshBuilder â€” Runtime ribbon mesh generation
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

RoadMeshBuilder::~RoadMeshBuilder() {
    destroy();
}

// ── Rounded junction correction ──────────────────────────────────────────────
// A road meeting another road is not a question of yes or no, it is a question of how
// much the direction has to change. Given a branch that arrives at direction d1 and a
// host travelling at d2, the correction angle is the turn between them:
//
//     theta = 0    the two are already aligned    -> no correction, run straight in
//     theta = 90   a square T-junction           -> a tight fillet
//     theta -> 180 a hairpin / parallel overlap  -> a very large, sweeping fillet
//
// The fillet radius follows the standard road-corner relation: a turn of theta is
// rounded over a tangent run of t = R * tan(theta/2). Holding that tangent run to a
// fraction of the road width - rather than holding R - is what keeps the correction
// "small": a sharp corner gets a tight arc, a shallow one gets a long shallow sweep,
// and neither can grow without bound. R is then clamped so a near-straight join
// degenerates gracefully to no arc at all.
static float junctionFilletRadius(float theta, float tangentRun) {
    const float half = theta * 0.5f;
    const float t = std::tan(half);
    if (t < 1e-4f) return tangentRun * 4.0f;          // ~straight: no meaningful fillet
    return tangentRun / t;
}

// Sample a circular arc from a to b, bulging toward `bulge`, into `out`. Used to
// replace the sharp corner between a branch and its host with a rounded one.
static void appendCornerArc(std::vector<glm::vec2>& out,
                            const glm::vec2& a, const glm::vec2& b,
                            const glm::vec2& bulge, int steps) {
    // Circumcentre of the circle through a, b with the given bulge direction.
    const glm::vec2 mid = (a + b) * 0.5f;
    const glm::vec2 chord = b - a;
    const float chordLen = glm::length(chord);
    if (chordLen < 1e-4f) { out.push_back(b); return; }
    const glm::vec2 perp = glm::vec2(-chord.y, chord.x) / chordLen;

    // Sagitta sets the radius. Positive sagitta bulges toward `perp`; the caller's
    // bulge vector only chooses the side.
    const float side = (glm::dot(bulge, perp) >= 0.0f) ? 1.0f : -1.0f;
    const float sag = chordLen * 0.18f;
    const float R = (sag * sag + 0.25f * chordLen * chordLen) / (2.0f * sag);
    const glm::vec2 centre = mid - perp * (side * (R - sag));

    const glm::vec2 v0 = a - centre, v1 = b - centre;
    constexpr float kTwoPi = 6.28318530718f;
    float a0 = std::atan2(v0.y, v0.x), a1 = std::atan2(v1.y, v1.x);
    float da = a1 - a0;
    while (da >  3.14159265f) da -= kTwoPi;
    while (da < -3.14159265f) da += kTwoPi;

    for (int i = 1; i <= steps; ++i) {
        const float ang = a0 + da * (float(i) / float(steps));
        out.push_back(centre + glm::vec2(std::cos(ang), std::sin(ang)) * R);
    }
}

void RoadMeshBuilder::destroy() {
    if (m_VAO) { glDeleteVertexArrays(1, &m_VAO); m_VAO = 0; }
    if (m_VBO) { glDeleteBuffers(1, &m_VBO); m_VBO = 0; }
    if (m_EBO) { glDeleteBuffers(1, &m_EBO); m_EBO = 0; }
    m_indexCount = 0;
    m_vertices.clear();
    m_indices.clear();
}

void RoadMeshBuilder::build(std::vector<RoadSegment> segments,
                            const HeightSampler& sampler,
                            const RoadDeformer* deformer,
                            float snapMargin) {
    m_vertices.clear();
    m_indices.clear();
    m_joinShaderStrokes.clear();
    m_joinShaderContacts.clear();

    // Roads share a height at crossings, so their fragments can have identical
    // depth. With GL_LEQUAL the later ribbon owns those pixels. Draw lower-priority
    // surface styles first and the dominant road last to avoid texture z-fighting.
    // RoadType ordering is used only for render priority; it does not affect joins.
    std::stable_sort(segments.begin(), segments.end(), [](const RoadSegment& a, const RoadSegment& b) {
        if (a.type != b.type) return (int)a.type > (int)b.type;
        return a.id < b.id;
    });

    // Keep the road bed close to the sampled terrain. Road-over-road depth
    // ordering is handled in clip space by street.vert; lifting the whole ribbon
    // by 25 cm left an obvious raised slab along otherwise flat ground.
    const float Z_BIAS = 0.04f;
    const float CAMBER = 0.0f;      // Flat cross-section to eliminate edge twisting
    const float TEX_SCALE = 0.20f;  // UV repeat scale along road
    // How much of the terrain cross-slope the road bed adopts. 1.0 makes the ribbon
    // twist with the ground (both rims ride separate terrain heights -> the visible
    // "melted edge" on every side slope); 0.0 leaves the road dead flat and floating
    // above cuts. 0.3 keeps the bed close to the ground while cutting the twist ~3x.
    const float EDGE_CONFORM = kRoadEdgeConform;
    // Endpoint clustering tolerance, metres. Shared with RoadGraph::buildTopology so
    // the reported topology and the drawn junctions always agree.
    const float JN_TOL = kRoadJunctionTolerance;
    // A road only forms a junction with a host it arrives at transversely, the same
    // test the mid-span crossing detector uses. See the constant for the reasoning.
    const float kJunctionApproachCos = kRoadJunctionApproachCos;
    const float PI = 3.14159265f;

    // Dense cross-section so lane markings / edges get crisp geometry.
    // Vertex pillars sit exactly on marking boundaries (u across road).
    static const float kCross[] = {
        0.000f,  // left rim
        0.055f,  // left edge-line outer
        0.105f,  // left edge-line inner
        0.435f,  // center zone
        0.455f,  // double-yellow left outer
        0.495f,  // double-yellow left inner
        0.505f,  // double-yellow right inner
        0.545f,  // double-yellow right outer
        0.565f,  // center zone
        0.895f,  // right edge-line inner
        0.945f,  // right edge-line outer
        1.000f   // right rim
    };
    static const int kCrossCount = (int)(sizeof(kCross) / sizeof(kCross[0]));

    const int S = (int)segments.size();
    if (S == 0) { m_indexCount = 0; return; }

    auto segLen = [&](const RoadSegment& s) {
        if (s.cumulativeDist.size() < 2) return 0.0f;
        return s.cumulativeDist.back() - s.cumulativeDist[0];
    };
    auto isPaved = [&](int t) { return t == 0 || t == 1 || t == 4; };

    // Bit 0/1 marks the specific start/end that joins a host road, so only that
    // endpoint loses its dead-end cap.
    std::vector<unsigned char> branchAttachedEnds(S, 0);
    struct BranchContact {
        int seg, end, host, hostSample;
        glm::vec2 position;       // branch centreline at the host edge stripe
        glm::vec2 hostPosition;   // closest point on host centreline
        glm::vec2 hostTangent;
        float hostSide;
    };
    std::vector<BranchContact> branchContacts;
    auto branchFilletRadius = [&](const BranchContact& contact) {
        const float branchWidth = segments[contact.seg].width;
        const float hostWidth = segments[contact.host].width;
        const RoadSegment& branch = segments[contact.seg];
        const float branchLength = branch.cumulativeDist.size() >= 2
            ? branch.cumulativeDist.back() - branch.cumulativeDist.front() : 0.0f;
        // Match the round footprint and the border curves to one shared quarter-
        // circle. A radius no larger than the branch half-width keeps the two
        // corners from crossing through one another at a narrow T mouth.
        const float designRadius = std::min(branchWidth * 0.48f, hostWidth * 0.45f);
        const float availableRun = std::max(0.25f, branchLength * 0.45f);
        return std::min({ designRadius, availableRun, 8.0f });
    };

    // A paved junction pad: the flat polygon that fills the void where ribbons meet.
    // Emitted for mid-span crossings AND for every branch mouth retracted onto a host
    // in step 0, so a connection is never left as a bare gap.
    struct CrossSite {
        glm::vec2 c;
        float r = 0.0f;
        std::vector<std::pair<int, int>> members;   // (segment, nearest sample)
    };
    std::vector<CrossSite> crossSites;

    // â”€â”€â”€ 0. Branch alignment to host edge â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // An open endpoint that lands INSIDE another road's corridor is aligned
    // to that road's near edge corridor, so the branch fuses edge-to-edge
    // with the host without burying or twisting its ribbon.
    // Road type describes surface style, not connectivity priority. Each endpoint
    // may join any road it approaches transversely.
    for (int s = 0; s < S; ++s) {
        RoadSegment& seg = segments[s];
        const int n = (int)seg.splinePoints.size();
        if (seg.closed || n < 2) continue;

        for (int end = 0; end < 2; ++end) {
            const glm::vec2 pos = (end == 0)
                ? glm::vec2(seg.splinePoints.front().x, seg.splinePoints.front().z)
                : glm::vec2(seg.splinePoints.back().x,  seg.splinePoints.back().z);

            // Check if already shared with another endpoint
            bool sharedEnd = false;
            for (int h = 0; h < S && !sharedEnd; ++h) {
                const RoadSegment& ho = segments[h];
                if (h == s || ho.splinePoints.size() < 2) continue;
                glm::vec2 a(ho.splinePoints.front().x, ho.splinePoints.front().z);
                glm::vec2 b(ho.splinePoints.back().x,  ho.splinePoints.back().z);
                if (glm::length(a - pos) <= JN_TOL + 0.05f ||
                    glm::length(b - pos) <= JN_TOL + 0.05f) sharedEnd = true;
            }
            if (sharedEnd) continue;

            // Check if endpoint lands inside another road's corridor (T-junction)
            // Host = closest matching spline sample across ALL candidates,
            // with no road-type restriction; type controls appearance only.
            //
            // The reach is the host's own width, so a street that stops a lane short
            // of a boulevard still welds on instead of ending in mid-air with a
            // rounded cap. Reach alone is not enough: it also catches a road merely
            // running ALONGSIDE another. A junction only exists where the branch
            // arrives transversely, so the approach angle decides it.
            const glm::vec2 branchOut = (end == 0)
                ? -glm::vec2(seg.tangents.front().x, seg.tangents.front().z)
                :  glm::vec2(seg.tangents.back().x,  seg.tangents.back().z);
            const float branchLen = glm::length(branchOut);
            if (branchLen < 1e-4f) continue;
            const glm::vec2 branchDir = branchOut / branchLen;

            // A junction exists where the two roads' paved ends actually overlap.
            // Reach is the host's own half-width plus a hair - the distance at which
            // the branch's tarmac meets the host's tarmac. Testing against a road's
            // full width instead would join two roads merely running side by side,
            // which overlap along their whole length and meet at no point on it.
            // Require a transverse approach. End-to-end continuations are handled
            // by endpoint clustering and gap splicing instead.
            const float kJunctionEpsilon = 0.5f;
            int hSeg = -1; float hDist = 1e9f; int hSample = -1;
            glm::vec2 hostPosition(0.0f);
            glm::vec2 hostRight(1.0f, 0.0f);
            glm::vec2 hostTangent(0.0f, 1.0f);
            for (int h = 0; h < S; ++h) {
                if (h == s) continue;
                const RoadSegment& ho = segments[h];
                if (ho.splinePoints.size() < 2) continue;
                // Allow the branch endpoint to be one branch-width short of the
                // host curb. The old half-width-only radius left a visible capped
                // stub when the pavement was close but its centreline was outside
                // the corridor test. The transverse test below prevents side-by-side
                // roads from snapping together.
                const float reach = ho.width * 0.5f + seg.width + kJunctionEpsilon;
                const int hostPointCount = (int)ho.splinePoints.size();
                const int hostEdgeCount = ho.closed ? hostPointCount : hostPointCount - 1;
                for (int k = 0; k < hostEdgeCount; ++k) {
                    const int next = (k + 1) % hostPointCount;
                    const glm::vec2 a(ho.splinePoints[k].x, ho.splinePoints[k].z);
                    const glm::vec2 b(ho.splinePoints[next].x, ho.splinePoints[next].z);
                    const glm::vec2 ab = b - a;
                    const float ab2 = glm::dot(ab, ab);
                    if (ab2 < 1e-8f) continue;
                    const float t = glm::clamp(glm::dot(pos - a, ab) / ab2, 0.0f, 1.0f);
                    const glm::vec2 closest = a + ab * t;
                    const float d = glm::length(closest - pos);
                    if (d >= reach || d >= hDist) continue;
                    const glm::vec2 ht = glm::normalize(ab);
                    if (std::fabs(glm::dot(branchDir, ht)) > kJunctionApproachCos) continue;
                    hDist = d; hSeg = h;
                    hSample = (t < 0.5f) ? k : next;
                    hostPosition = closest;
                    // Match computeRoadFrames' XZ right-vector convention.
                    hostRight = glm::vec2(ht.y, -ht.x);
                    hostTangent = ht;
                }
            }
            if (hSeg < 0) continue;

            // branchDir is the outward direction beyond the road endpoint, so the
            // branch's interior lies in the opposite direction. Use that interior
            // direction to select the host's near edge; using branchDir itself
            // selects the far edge and drags the branch across the whole host road.
            const glm::vec2 branchInteriorDir = -branchDir;
            const float side = glm::dot(branchInteriorDir, hostRight) >= 0.0f
                ? 1.0f : -1.0f;
            const glm::vec2 hostEdge = hostPosition + hostRight *
                (side * roadWhiteEdgeLineOffset(segments[hSeg].width));
            if (end == 0) seg.controlPoints.front().pos = hostEdge;
            else          seg.controlPoints.back().pos = hostEdge;
            RoadSpline::evaluate(seg, sampler, 0.5f);
            const glm::vec2 joinedPos = end == 0
                ? glm::vec2(seg.splinePoints.front().x, seg.splinePoints.front().z)
                : glm::vec2(seg.splinePoints.back().x, seg.splinePoints.back().z);
            branchAttachedEnds[s] |= (end == 0) ? 0x1 : 0x2;
            branchContacts.push_back({ s, end, hSeg, hSample, joinedPos,
                                       hostPosition, hostTangent, side });
        }
    }

    // â”€â”€â”€ 1. Endpoint clustering â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // ── 0c. Automatic gap splicing ───────────────────────────────────────────
    // Ends that face each other across a gap are extended along their own axes until
    // the axes meet, so a road drawn in two strokes becomes one road instead of two
    // end caps facing a hole. See the header for why each guard is needed. The spliced
    // ends land within kRoadJunctionTolerance of each other, so the clustering below
    // turns them into a proper shared-vertex junction.
    {
        struct GapEnd { int seg; bool back; glm::vec2 pos; glm::vec2 out; };
        std::vector<GapEnd> ge;
        for (int s = 0; s < S; ++s) {
            RoadSegment& seg = segments[s];
            if (seg.closed || seg.splinePoints.size() < 2) continue;
            glm::vec2 oF(-seg.tangents.front().x, -seg.tangents.front().z);
            glm::vec2 oB( seg.tangents.back().x,   seg.tangents.back().z);
            if (glm::dot(oF, oF) > 1e-8f) oF = glm::normalize(oF);
            if (glm::dot(oB, oB) > 1e-8f) oB = glm::normalize(oB);
            ge.push_back({ s, false, glm::vec2(seg.splinePoints.front().x, seg.splinePoints.front().z), oF });
            ge.push_back({ s, true,  glm::vec2(seg.splinePoints.back().x,  seg.splinePoints.back().z),  oB });
        }

        // True when some third road's corridor covers the straight gap, which would
        // mean the splice reaches across the network rather than along it.
        auto gapIsBlocked = [&](const glm::vec2& a, const glm::vec2& b, int s1, int s2) {
            for (int h = 0; h < S; ++h) {
                if (h == s1 || h == s2) continue;
                const auto& hp = segments[h].splinePoints;
                const float hw = segments[h].width * 0.5f;
                const size_t stride = (hp.size() > 64) ? (hp.size() / 64) : 1;
                for (int t = 0; t <= 24; ++t) {
                    const glm::vec2 m = glm::mix(a, b, t / 24.0f);
                    for (size_t k = 0; k + 1 < hp.size(); k += stride) {
                        const glm::vec2 p(hp[k].x, hp[k].z);
                        const glm::vec2 ab(hp[k + 1].x - p.x, hp[k + 1].z - p.y);
                        const float l2 = glm::dot(ab, ab);
                        if (l2 < 1e-9f) continue;
                        const float u = glm::clamp(glm::dot(m - p, ab) / l2, 0.0f, 1.0f);
                        if (glm::length(m - (p + ab * u)) <= hw) return true;
                    }
                }
            }
            return false;
        };

        std::vector<bool> done(ge.size(), false);
        int spliceCount = 0;
        for (size_t i = 0; i < ge.size(); ++i) {
            if (done[i]) continue;
            for (size_t j = i + 1; j < ge.size(); ++j) {
                if (done[j] || ge[i].seg == ge[j].seg) continue;
                const glm::vec2 delta = ge[j].pos - ge[i].pos;
                const float d = glm::length(delta);
                if (d <= kRoadJunctionTolerance || d > kMaxRoadGapSplice) continue;

                // Each end must aim at the other. This is what separates one road drawn
                // in two strokes from two roads that merely pass near each other.
                const glm::vec2 u = delta / d;
                if (glm::dot(ge[i].out, u) < kRoadGapSpliceFacing) continue;
                if (glm::dot(ge[j].out, -u) < kRoadGapSpliceFacing) continue;

                // Where the two axes meet, and how cleanly they meet there.
                const glm::vec2 mI = ge[i].pos + ge[i].out * glm::dot(ge[j].pos - ge[i].pos, ge[i].out);
                const glm::vec2 mJ = ge[j].pos + ge[j].out * glm::dot(ge[i].pos - ge[j].pos, ge[j].out);
                if (glm::length(mI - mJ) > kRoadGapSpliceLock) continue;
                if (gapIsBlocked(ge[i].pos, ge[j].pos, ge[i].seg, ge[j].seg)) continue;

                // Extend each end along its own axis to the shared meeting point, so the
                // road keeps the direction it was drawn with instead of kinking sideways.
                const glm::vec2 joint = (mI + mJ) * 0.5f;
                if (glm::dot(joint - ge[i].pos, ge[i].out) <= 0.0f) continue;
                if (glm::dot(joint - ge[j].pos, ge[j].out) <= 0.0f) continue;

                const float jy = sampler.isValid() ? sampler.sample(joint.x, joint.y) : 0.0f;
                RoadSegment& si = segments[ge[i].seg];
                RoadSegment& sj = segments[ge[j].seg];
                if (ge[i].back) si.splinePoints.back()  = glm::vec3(joint.x, jy, joint.y);
                else            si.splinePoints.front() = glm::vec3(joint.x, jy, joint.y);
                if (ge[j].back) sj.splinePoints.back()  = glm::vec3(joint.x, jy, joint.y);
                else            sj.splinePoints.front() = glm::vec3(joint.x, jy, joint.y);

                done[i] = done[j] = true;
                ++spliceCount;
                break;
            }
        }
        if (spliceCount > 0)
            std::cout << "[RoadMeshBuilder] Auto-spliced " << spliceCount
                      << " road gap(s) up to " << (int)kMaxRoadGapSplice << " m" << std::endl;
    }

    // Two ends join only when they are within JN_TOL of each other. Note that a wider
    // "near-collinear continuation" rule is tempting here - a road drawn in two strokes
    // that stopped short leaves two parallel-pointing ends a road-width apart - but it
    // is not distinguishable from two genuinely parallel roads at that distance, and
    // clustering those welds a pavement wedge between roads that must stay separate.
    // Bridging a deliberate authoring gap is the road editor's CONNECT ROADS job.
    struct EndRef { int seg; bool isBack; glm::vec2 pos; float hw; };
    std::vector<EndRef> ends;
    std::vector<std::pair<int, int>> segEndRef(S, { -1, -1 });
    for (int s = 0; s < S; ++s) {
        const RoadSegment& seg = segments[s];
        if (seg.splinePoints.size() < 2) continue;
        const int fi = (int)ends.size();
        ends.push_back({ s, false, glm::vec2(seg.splinePoints.front().x, seg.splinePoints.front().z), seg.width * 0.5f });
        ends.push_back({ s, true,  glm::vec2(seg.splinePoints.back().x,  seg.splinePoints.back().z),  seg.width * 0.5f });
        segEndRef[s] = { fi, fi + 1 };
    }
    const int E = (int)ends.size();
    std::vector<int> cid(E, -1);
    int nc = 0;
    for (int i = 0; i < E; ++i) {
        if (cid[i] >= 0) continue;
        cid[i] = nc;
        for (int j = i + 1; j < E; ++j)
            if (cid[j] < 0 && glm::length(ends[i].pos - ends[j].pos) <= JN_TOL) cid[j] = nc;
        ++nc;
    }
    std::vector<std::vector<int>> cluster(nc);
    for (int i = 0; i < E; ++i) cluster[cid[i]].push_back(i);

    auto countRealSegsInCluster = [&](const std::vector<int>& cl) {
        int seen = 0;
        for (size_t a = 0; a < cl.size(); ++a) {
            bool dup = false;
            for (size_t b = 0; b < a; ++b) if (ends[cl[b]].seg == ends[cl[a]].seg) { dup = true; break; }
            if (!dup) ++seen;
        }
        return seen;
    };

    // â”€â”€â”€ 2. Network-continuous texture distance v â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    std::vector<float> vStart(S, 0.0f);
    std::vector<bool>  vDone(S, false);
    for (int s = 0; s < S && !vDone[s]; ++s) {
        if (segments[s].closed) { vDone[s] = true; continue; }
        vStart[s] = 0.0f; vDone[s] = true;
        bool ch = true;
        while (ch) {
            ch = false;
            for (int c = 0; c < nc; ++c) {
                if (countRealSegsInCluster(cluster[c]) != 2) continue;
                int known = -1;
                for (int id : cluster[c])
                    if (vDone[ends[id].seg]) { known = id; break; }
                if (known < 0) continue;
                const EndRef& ek = ends[known];
                float vK = vStart[ek.seg] + (ek.isBack ? segLen(segments[ek.seg]) : 0.0f);
                for (int id : cluster[c]) {
                    const EndRef& eu = ends[id];
                    if (vDone[eu.seg] || segments[eu.seg].closed) continue;
                    vStart[eu.seg] = vK - (eu.isBack ? segLen(segments[eu.seg]) : 0.0f);
                    vDone[eu.seg] = true;
                    ch = true;
                }
            }
        }
    }

    int capCount = 0, jnCount = 0, jointCount = 0, crossPadCount = 0;
    std::vector<uint32_t> firstRingBase(S, 0);
    std::vector<uint32_t> lastRingBase(S, 0);

    // -- 2b. Merge-zone detection: samples buried inside a MORE MAJOR road ----
    // street.frag paints the curb and the white edge lines procedurally from u
    // (mark(u, 0.028) / mark(u, 0.085)), so they land on whichever fragment is
    // visible. When one ribbon lies on top of another, the upper road's rim
    // bands slice a bright border straight across the road underneath. Every
    // sample inside a more major corridor therefore drops all paint (u = 0.35,
    // the same trick the junction pads already use) and reads as bare asphalt.
    // Ranking is strict (lower RoadType wins, ties broken by id) so two roads
    // of equal rank can never suppress each other's markings.
    std::vector<std::vector<char>> bareAt(S);
    int bareSamples = 0;
    {
        struct AABB2 { glm::vec2 lo, hi; };
        std::vector<AABB2> box(S);
        for (int s = 0; s < S; ++s) {
            glm::vec2 lo(1e9f), hi(-1e9f);
            for (const auto& p : segments[s].splinePoints) {
                lo = glm::min(lo, glm::vec2(p.x, p.z));
                hi = glm::max(hi, glm::vec2(p.x, p.z));
            }
            const float pad = segments[s].width * 0.5f + 1.0f;
            box[s] = { lo - glm::vec2(pad), hi + glm::vec2(pad) };
        }

        auto outranks = [&](int me, int host) {
            const int tm = (int)segments[me].type, th = (int)segments[host].type;
            if (th != tm) return th < tm;
            return segments[host].id < segments[me].id;
        };

        // Distance to host centreline: coarse stride, then exact local refine.
        auto inCorridor = [](const std::vector<glm::vec3>& poly, float hw,
                             const glm::vec2& q) -> bool {
            const int n = (int)poly.size();
            if (n < 2) return false;
            const float lim = hw * 0.98f;
            const int stride = std::max(1, n / 96);
            auto hit = [&](int k) {
                const glm::vec2 a(poly[k].x, poly[k].z);
                const glm::vec2 ab(poly[k + 1].x - a.x, poly[k + 1].z - a.y);
                const float len2 = glm::dot(ab, ab);
                float t = (len2 > 1e-9f) ? glm::dot(q - a, ab) / len2 : 0.0f;
                t = std::clamp(t, 0.0f, 1.0f);
                const glm::vec2 d = q - (a + ab * t);
                return glm::dot(d, d) < lim * lim;
            };
            for (int k = 0; k + 1 < n; k += stride) {
                if (!hit(k)) continue;
                for (int k2 = std::max(0, k - stride); k2 < std::min(n - 1, k + stride); ++k2)
                    if (hit(k2)) return true;
            }
            return false;
        };

        for (int s = 0; s < S; ++s) {
            const int n = (int)segments[s].splinePoints.size();
            bareAt[s].assign(std::max(n, 0), 0);
            if (n < 2) continue;

            std::vector<std::pair<int, int>> cand;   // (rank, host)
            for (int h = 0; h < S; ++h) {
                if (h == s || !outranks(s, h)) continue;
                if (box[s].lo.x > box[h].hi.x || box[s].hi.x < box[h].lo.x ||
                    box[s].lo.y > box[h].hi.y || box[s].hi.y < box[h].lo.y) continue;
                cand.push_back({ (int)segments[h].type, h });
            }
            if (cand.empty()) continue;
            std::sort(cand.begin(), cand.end());

            const int kMaxHosts = 8;   // bound per-sample cost on dense grids
            for (int i = 0; i < n; ++i) {
                const glm::vec2 q(segments[s].splinePoints[i].x,
                                  segments[s].splinePoints[i].z);
                for (int ci = 0; ci < (int)cand.size() && ci < kMaxHosts; ++ci) {
                    if (!inCorridor(segments[cand[ci].second].splinePoints,
                                    segments[cand[ci].second].width * 0.5f, q)) continue;
                    bareAt[s][i] = 1;
                    ++bareSamples;
                    break;
                }
            }
        }
    }

    // -- 2c. Mid-span crossings: the junction case 4b/5 structurally cannot see -
    // Sections 4b and 5 only ever look at segment ENDPOINTS, so two roads that
    // cross in the middle of their spans produced no junction at all: coplanar
    // ribbons z-fight and each road's paint cuts across the other. Contacts are
    // found with a uniform spatial hash over the sampled centrelines (O(samples)
    // instead of an O(S^2 * n^2) sweep), near-parallel overlaps are rejected
    // (they are merges, handled by step 0 and 2b, not crossings), and every road
    // involved drops its paint around the crossing. The pad itself is emitted in
    // section 5b.
    {
        const float kCell = 8.0f;
        const float kCrossCos = kJunctionApproachCos;   // |cos| below this = at least ~30 deg
        const int kMaxContacts = 40000;

        std::unordered_map<long long, std::vector<std::pair<int, int>>> grid;
        auto cellKey = [](int cx, int cz) -> long long {
            return (long long)cx * 1000003LL + (long long)cz;
        };
        for (int s = 0; s < S; ++s) {
            const auto& pts = segments[s].splinePoints;
            for (int i = 0; i < (int)pts.size(); ++i)
                grid[cellKey((int)std::floor(pts[i].x / kCell),
                             (int)std::floor(pts[i].z / kCell))].push_back({ s, i });
        }

        struct Contact { glm::vec2 p; int a, ai, b, bi; };
        std::vector<Contact> contacts;
        for (int s = 0; s < S && (int)contacts.size() < kMaxContacts; ++s) {
            const auto& pts = segments[s].splinePoints;
            if (pts.size() < 2) continue;
            const float hwS = segments[s].width * 0.5f;
            for (int i = 0; i < (int)pts.size() && (int)contacts.size() < kMaxContacts; ++i) {
                const glm::vec2 p(pts[i].x, pts[i].z);
                const int cx = (int)std::floor(p.x / kCell);
                const int cz = (int)std::floor(p.y / kCell);
                for (int ox = -1; ox <= 1; ++ox) {
                    for (int oz = -1; oz <= 1; ++oz) {
                        auto it = grid.find(cellKey(cx + ox, cz + oz));
                        if (it == grid.end()) continue;
                        for (const auto& pr : it->second) {
                            const int h = pr.first, j = pr.second;
                            if (h <= s) continue;                 // one direction only
                            const auto& hp = segments[h].splinePoints;
                            if (j < 0 || j >= (int)hp.size() || hp.size() < 2) continue;
                            const glm::vec2 q(hp[j].x, hp[j].z);
                            const float lim = hwS + segments[h].width * 0.5f;
                            const glm::vec2 d = p - q;
                            if (glm::dot(d, d) >= lim * lim) continue;

                            glm::vec2 t0(segments[s].tangents[i].x, segments[s].tangents[i].z);
                            glm::vec2 t1(segments[h].tangents[j].x, segments[h].tangents[j].z);
                            if (glm::dot(t0, t0) < 1e-9f || glm::dot(t1, t1) < 1e-9f) continue;
                            t0 = glm::normalize(t0);
                            t1 = glm::normalize(t1);
                            if (std::fabs(glm::dot(t0, t1)) > kCrossCos) continue;

                            contacts.push_back({ (p + q) * 0.5f, s, i, h, j });
                            if ((int)contacts.size() >= kMaxContacts) break;
                        }
                    }
                }
            }
        }

        // Grouping: a crossing yields one contact per sample pair inside the
        // overlap diamond, so contacts are clustered by single linkage. The
        // threshold has to cover that diamond (up to hwA + hwB across for a
        // right-angle crossing), hence generous: two intersections closer than
        // this share one paved pad, which reads as a plaza rather than a defect.
        // Contacts are paired through a coarse grid so this stays linear.
        const float kMergeR = 48.0f;
        std::unordered_map<long long, std::vector<int>> cgrid;
        for (size_t i = 0; i < contacts.size(); ++i)
            cgrid[cellKey((int)std::floor(contacts[i].p.x / kMergeR),
                          (int)std::floor(contacts[i].p.y / kMergeR))].push_back((int)i);

        std::vector<int> parent(contacts.size());
        for (size_t i = 0; i < contacts.size(); ++i) parent[i] = (int)i;
        auto find = [&parent](int a) {
            auto self = [&parent](int x) -> int {
                while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
                return x;
            };
            return self(a);
        };
        for (size_t i = 0; i < contacts.size(); ++i) {
            const int cx = (int)std::floor(contacts[i].p.x / kMergeR);
            const int cz = (int)std::floor(contacts[i].p.y / kMergeR);
            for (int ox = -1; ox <= 1; ++ox) {
                for (int oz = -1; oz <= 1; ++oz) {
                    auto it = cgrid.find(cellKey(cx + ox, cz + oz));
                    if (it == cgrid.end()) continue;
                    for (int j : it->second) {
                        if (j <= (int)i) continue;
                        if (glm::length(contacts[i].p - contacts[j].p) > kMergeR) continue;
                        const int ra = find((int)i), rb = find(j);
                        if (ra != rb) parent[ra] = rb;
                    }
                }
            }
        }

        std::unordered_map<int, CrossSite> grouped;
        for (size_t i = 0; i < contacts.size(); ++i) {
            CrossSite& site = grouped[find((int)i)];
            site.c += contacts[i].p;
            site.members.push_back({ contacts[i].a, contacts[i].ai });
            site.members.push_back({ contacts[i].b, contacts[i].bi });
            bareAt[contacts[i].a][contacts[i].ai] = 1;
            bareAt[contacts[i].b][contacts[i].bi] = 1;
            ++bareSamples;
        }

        for (auto& kv : grouped) {
            CrossSite site = kv.second;
            site.c /= (float)std::max<size_t>(1, site.members.size() / 2);

            // Keep only each road's sample closest to the site centre.
            std::vector<std::pair<int, int>> keep;
            for (const auto& m : site.members) {
                const auto& sp = segments[m.first].splinePoints;
                if (m.second < 0 || m.second >= (int)sp.size()) continue;
                const float d = glm::length(glm::vec2(sp[m.second].x, sp[m.second].z) - site.c);
                bool seen = false;
                for (auto& k : keep) {
                    if (k.first != m.first) continue;
                    const auto& kp = segments[k.first].splinePoints;
                    const float kd = glm::length(glm::vec2(kp[k.second].x, kp[k.second].z) - site.c);
                    if (d < kd) k.second = m.second;
                    seen = true;
                    break;
                }
                if (!seen) keep.push_back(m);
            }
            site.members = keep;
            if (site.members.size() < 2) continue;
            for (const auto& m : site.members)
                site.r = std::max(site.r, segments[m.first].width * 0.5f);
            crossSites.push_back(site);
        }
    }

    // The sample-pair crossing pass can miss an endpoint landing exactly on the
    // host's outer edge (the endpoints are not evenly aligned to host samples).
    // Add a fallback pad only when that road pair does not already have a nearby
    // crossing pad, avoiding the duplicate coplanar surfaces that caused blobs.
    for (const BranchContact& contact : branchContacts) {
        const float mergeDistance = std::max(segments[contact.seg].width,
                                             segments[contact.host].width) * 1.5f;
        bool hasPad = false;
        for (const CrossSite& site : crossSites) {
            bool hasBranch = false, hasHost = false;
            for (const auto& member : site.members) {
                hasBranch |= member.first == contact.seg;
                hasHost |= member.first == contact.host;
            }
            if (hasBranch && hasHost && glm::length(site.c - contact.position) <= mergeDistance) {
                hasPad = true;
                break;
            }
        }
        if (hasPad) continue;

        const int endIdx = contact.end == 0 ? 0 : (int)segments[contact.seg].splinePoints.size() - 1;
        CrossSite site;
        site.c = contact.position;
        site.members = { { contact.seg, endIdx }, { contact.host, contact.hostSample } };
        site.r = std::max(segments[contact.seg].width * 0.5f,
                          segments[contact.host].width * 0.5f);
        crossSites.push_back(site);
        bareAt[contact.seg][endIdx] = 1;
        bareAt[contact.host][contact.hostSample] = 1;
        ++bareSamples;
    }

    // â”€â”€â”€ 3. Ribbon rings per segment â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    for (int s = 0; s < S; ++s) {
        const RoadSegment& seg = segments[s];
        int n = (int)seg.splinePoints.size();
        if (n < 2) continue;

        const bool closed = seg.closed;
        const float hw = seg.width * 0.5f;
        const float rtype = float((int)seg.type);
        const float vOff = vStart[s];
        const float stripLift = 0.0f;
        const uint32_t baseIdx = (uint32_t)m_vertices.size();
        firstRingBase[s] = baseIdx;

        // One cross-section ring. The terrain is sampled ONCE, at the centreline, and
        // the rim pillars only partially adopt their own ground height (EDGE_CONFORM).
        // Sampling every pillar independently let the two rims ride separate terrain
        // heights, which is what twisted and warped the road edges across every side
        // slope and hill. The surface normal is then taken from the rim positions that
        // were actually emitted, so the residual cross-slope is lit correctly.
        auto emitRing = [&](int i, float v, bool wrap) {
            const glm::vec3& center  = seg.splinePoints[i];
            const glm::vec3& right   = seg.rightVecs[i];
            const glm::vec3& tangent = seg.tangents[i];
            const float yC = center.y;
            // Fade markings before an attached endpoint so its straight edge lines
            // flow into the curved corner connectors emitted below.
            float mark = 1.0f;
            const float along = (i < (int)seg.cumulativeDist.size())
                ? seg.cumulativeDist[i] : 0.0f;
            for (const BranchContact& contact : branchContacts) {
                if (contact.seg != s) continue;
                const float endDist = contact.end == 0
                    ? along - seg.cumulativeDist.front()
                    : seg.cumulativeDist.back() - along;
                const float r = branchFilletRadius(contact);
                const float t = glm::clamp(endDist / r, 0.0f, 1.0f);
                const float fade = t * t * (3.0f - 2.0f * t);
                mark = std::min(mark, fade);
            }

            glm::vec3 rimL(0.0f), rimR(0.0f);
            for (int j = 0; j < kCrossCount; ++j) {
                const float uc  = kCross[j];
                const float off = uc * 2.0f - 1.0f;
                const float useHw = roadMeshHalfWidth(seg, i, wrap, off, hw);

                glm::vec3 pos = center + right * (off * useHw);
                float y = yC;
                if (sampler.isValid())
                    y = yC + (sampler.sample(pos.x, pos.z) - yC) * EDGE_CONFORM;
                if (deformer && deformer->isBuilt())
                    y += deformer->getDeformation(pos.x, pos.z);
                pos.y = y + Z_BIAS + CAMBER + stripLift;

                if (j == 0)                    rimL = pos;
                if (j == kCrossCount - 1)      rimR = pos;
                m_vertices.push_back({ pos, glm::vec3(0.0f, 1.0f, 0.0f),
                                        glm::vec2(uc, v), rtype,
                                        std::max(0.0f, seg.width) * 0.1f + std::max(0.0f, 5.0f - rtype),
                                        mark });
            }

            glm::vec3 across = rimR - rimL;
            if (glm::dot(across, across) < 1e-9f) across = right * (2.0f * hw);
            glm::vec3 surfNorm = glm::cross(tangent, across);
            const float nl = glm::length(surfNorm);
            surfNorm = (nl > 1e-6f) ? surfNorm / nl : glm::vec3(0.0f, 1.0f, 0.0f);
            if (surfNorm.y < 0.0f) surfNorm = -surfNorm;
            for (int j = 0; j < kCrossCount; ++j)
                m_vertices[m_vertices.size() - kCrossCount + j].normal = surfNorm;
        };

        if (closed) {
            float closingDist = glm::length(seg.splinePoints[0] - seg.splinePoints[n - 1]);
            float totalDist = seg.cumulativeDist[n - 1] + closingDist;
            const float dashCycle = 2.3f;
            float targetCycles = std::max(1.0f, std::round((totalDist * TEX_SCALE) / dashCycle));
            float ringTexScale = (targetCycles * dashCycle) / totalDist;

            for (int i = 0; i < n; ++i)
                emitRing(i, seg.cumulativeDist[i] * ringTexScale, true);

            // Closing ring n: identical position and normal to ring 0, but
            // v = totalDist * ringTexScale so the dash pattern wraps seamlessly.
            lastRingBase[s] = (uint32_t)m_vertices.size();
            emitRing(0, totalDist * ringTexScale, true);

            // Build quads for i = 0 .. n - 1 (last connects ring n-1 to closing ring n)
            for (int i = 0; i < n; ++i) {
                uint32_t r0 = baseIdx + (uint32_t)i * kCrossCount;
                uint32_t r1 = baseIdx + (uint32_t)(i + 1) * kCrossCount;
                for (int j = 0; j < kCrossCount - 1; ++j) {
                    uint32_t a0 = r0 + j, a1 = r0 + j + 1;
                    uint32_t b0 = r1 + j, b1 = r1 + j + 1;
                    m_indices.push_back(a0); m_indices.push_back(b0); m_indices.push_back(a1);
                    m_indices.push_back(a1); m_indices.push_back(b0); m_indices.push_back(b1);
                }
            }
            continue; // Closed loop is complete and self-contained!
        }

        // Open road: emit rings 0 .. n - 1
        lastRingBase[s] = baseIdx + (uint32_t)(n - 1) * kCrossCount;
        for (int i = 0; i < n; ++i) {
            const float v = (seg.cumulativeDist[i] - seg.cumulativeDist[0] + vOff) * TEX_SCALE;
            emitRing(i, v, false);
        }


        // Quads for open road: i = 0 .. n - 2
        for (int i = 0; i < n - 1; ++i) {
            uint32_t r0 = baseIdx + (uint32_t)i * kCrossCount;
            uint32_t r1 = baseIdx + (uint32_t)(i + 1) * kCrossCount;
            for (int j = 0; j < kCrossCount - 1; ++j) {
                uint32_t a0 = r0 + j, a1 = r0 + j + 1;
                uint32_t b0 = r1 + j, b1 = r1 + j + 1;
                m_indices.push_back(a0); m_indices.push_back(b0); m_indices.push_back(a1);
                m_indices.push_back(a1); m_indices.push_back(b0); m_indices.push_back(b1);
            }
        }

        // â”€â”€â”€ 4. Rounded end caps for true dead ends â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        for (int end = 0; end < 2; ++end) {
            int refId = (end == 0) ? segEndRef[s].first : segEndRef[s].second;
            if (refId < 0) continue;
            int c = cid[refId];
            if (c >= 0 && countRealSegsInCluster(cluster[c]) > 1) continue; // connects to another road!
            const unsigned char endBit = (end == 0) ? 0x1 : 0x2;
            if (branchAttachedEnds[s] & endBit) continue; // only this mouth meets the host

            const int i0 = (end == 0) ? 0 : n - 1;
            const glm::vec3& P = seg.splinePoints[i0];
            glm::vec3 tang = seg.tangents[i0];
            glm::vec3 right = seg.rightVecs[i0];
            glm::vec3 outward = (end == 0) ? -tang : tang;
            // A cap is a fan, not a cross-section: sweep u from the centre outwards
            // so the arc reads as a continuation of the road surface, and let the
            // marking mask come from the end ring like every other ring.
            const float mark = 1.0f;

            const int M = 12;
            const float vEnd = (seg.cumulativeDist[i0] - seg.cumulativeDist[0] + vOff) * TEX_SCALE;
            const float yC = P.y;

            glm::vec3 capO = P; capO.y = yC + Z_BIAS + CAMBER + stripLift;
            const uint32_t capBase = (uint32_t)m_vertices.size();
            m_vertices.push_back({capO, glm::vec3(0.0f, 1.0f, 0.0f),
                                   glm::vec2(0.5f, vEnd), rtype,
                                   std::max(0.0f, seg.width) * 0.1f + std::max(0.0f, 5.0f - rtype), mark});

            glm::vec3 arcL(0.0f), arcR(0.0f);
            for (int k = 0; k <= M; ++k) {
                float ang = float(k) / float(M) * PI;
                glm::vec3 d = -right * hw * std::cos(ang) + outward * hw * std::sin(ang);
                glm::vec3 pos = P + d;
                float y = yC;
                if (sampler.isValid())
                    y = yC + (sampler.sample(pos.x, pos.z) - yC) * EDGE_CONFORM;
                if (deformer && deformer->isBuilt()) y += deformer->getDeformation(pos.x, pos.z);
                pos.y = y + Z_BIAS + CAMBER + stripLift;
                if (k == 0)    arcL = pos;
                if (k == M)    arcR = pos;
                const float uu = 0.5f - 0.5f * std::cos(ang);   // 0.5 -> 0 -> 0.5 across the arc
                const float vv = vEnd + (hw * std::sin(ang)) * TEX_SCALE;
                m_vertices.push_back({pos, glm::vec3(0.0f, 1.0f, 0.0f),
                                      glm::vec2(uu, vv), rtype,
                                      std::max(0.0f, seg.width) * 0.1f + std::max(0.0f, 5.0f - rtype), mark});
            }

            glm::vec3 across = arcR - arcL;
            if (glm::dot(across, across) < 1e-9f) across = right * (2.0f * hw);
            glm::vec3 surfNorm = glm::cross(tang, across);
            const float nl = glm::length(surfNorm);
            surfNorm = (nl > 1e-6f) ? surfNorm / nl : glm::vec3(0.0f, 1.0f, 0.0f);
            if (surfNorm.y < 0.0f) surfNorm = -surfNorm;
            for (int k = 0; k <= M + 1; ++k)
                m_vertices[capBase + k].normal = surfNorm;

            for (int k = 0; k < M; ++k) {
                m_indices.push_back(capBase);
                m_indices.push_back(capBase + 1 + k);
                m_indices.push_back(capBase + 1 + k + 1);
            }
            ++capCount;
        }
    }

    // â”€â”€â”€ 4b. End-to-End Joints (connecting two roads cleanly) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // Connects matching cross-section pillars between two adjacent segments.
    // Preserves lane lines, center double-yellows, and curbs without any grey blotches.
    for (int c = 0; c < nc; ++c) {
        if (countRealSegsInCluster(cluster[c]) != 2) continue;
        int idA = cluster[c][0];
        int idB = cluster[c][1];
        int segA = ends[idA].seg;
        int segB = ends[idB].seg;
        if (segA == segB || segments[segA].closed || segments[segB].closed) continue;

        bool isBackA = ends[idA].isBack;
        bool isBackB = ends[idB].isBack;
        uint32_t ringBaseA = isBackA ? lastRingBase[segA] : firstRingBase[segA];
        uint32_t ringBaseB = isBackB ? lastRingBase[segB] : firstRingBase[segB];

        // Harmonize pillar alignment:
        // When one segment enters and the other leaves (isBackA != isBackB),
        // left meets left (j connects to j).
        // When both enter or both leave, directions are opposite, so left meets right
        // relative to the segment's local frame: j connects to (kCrossCount - 1 - j).
        bool flipB = (isBackA == isBackB);

        // Bridge the two sampled cross-sections directly, even when widths differ.
        // Each strip is one surface: duplicate reverse-wound faces here were
        // coplanar with the forward faces and caused z-fighting across the join.
        for (int j = 0; j < kCrossCount - 1; ++j) {
            uint32_t a0 = ringBaseA + j;
            uint32_t a1 = ringBaseA + j + 1;
            uint32_t b0, b1;
            if (!flipB) {
                b0 = ringBaseB + j;
                b1 = ringBaseB + j + 1;
            } else {
                b0 = ringBaseB + (kCrossCount - 1 - j);
                b1 = ringBaseB + (kCrossCount - 2 - j);
            }
            m_indices.push_back(a0); m_indices.push_back(b0); m_indices.push_back(a1);
            m_indices.push_back(a1); m_indices.push_back(b0); m_indices.push_back(b1);
        }
        ++jointCount;
    }

    // â”€â”€â”€ 5. Branching Junction Pads (2+ roads meeting) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // Old-strategy (Caesar) roof-fix: every place where road mouths come
    // together gets a paved pad covering the void between the ribbons, so
    // angled/unequal-width connections read as one continuous intersection
    // instead of a gap or a stack of overlapping mouth discs.
    for (int c = 0; c < nc; ++c) {
        if (countRealSegsInCluster(cluster[c]) < 2) continue;

        glm::vec2 C(0.0f);
        float maxHw = 0.0f;
        int widestType = -1;
        for (int id : cluster[c]) {
            const EndRef& e = ends[id];
            C += e.pos;
            maxHw = std::max(maxHw, e.hw);
            if (e.hw >= maxHw || widestType < 0) widestType = (int)segments[e.seg].type;
        }
        C /= (float)cluster[c].size();

        int jnType = isPaved(widestType) ? widestType : (int)segments[ends[cluster[c][0]].seg].type;
        for (int id : cluster[c]) if (isPaved((int)segments[ends[id].seg].type)) { jnType = (int)segments[ends[id].seg].type; break; }
        if (jnType < 0) jnType = 1;
        struct JnM { glm::vec2 rimL, rimR, tan; float hw; };
        std::vector<JnM> mem;
        mem.reserve(cluster[c].size());
        for (int id : cluster[c]) {
            const EndRef& e = ends[id];
            const RoadSegment& s = segments[e.seg];
            int n = (int)s.splinePoints.size();
            if (n < 2) continue;
            const int i0 = e.isBack ? n - 1 : 0;
            glm::vec2 p(s.splinePoints[i0].x, s.splinePoints[i0].z);
            glm::vec3 r3 = s.rightVecs[i0];
            glm::vec2 r2(r3.x, r3.z);
            float lenR = glm::length(r2);
            if (lenR > 0.01f) r2 /= lenR; else r2 = glm::vec2(1, 0);
            glm::vec3 t3 = s.tangents[i0];
            glm::vec2 tanOut = e.isBack ? -glm::vec2(t3.x, t3.z) : glm::vec2(t3.x, t3.z);
            if (glm::length(tanOut) > 0.01f) tanOut = glm::normalize(tanOut); else tanOut = glm::vec2(1, 0);

            JnM m = { p - r2 * e.hw, p + r2 * e.hw, tanOut, e.hw };
            mem.push_back(m);
        }
        const int M = (int)mem.size();
        if (M < 2) continue;
        if (M == 2) {
            // Straight continuation: skip ONLY when the two mouths run along
            // nearly the same line (head-on OR same direction) at similar width.
            // The exact shared-vertex quad bridge handles those; an |dot| check
            // covers the head-on case that an angle<20deg test would leak through.
            bool parallel = std::fabs(glm::dot(mem[0].tan, mem[1].tan)) > 0.94f;
            bool wideStep = std::fabs(mem[0].hw - mem[1].hw) >
                            0.2f * std::max(mem[0].hw, mem[1].hw);
            if (parallel && !wideStep) continue;
        }

        std::vector<glm::vec2> boundary;
        boundary.reserve(M * 2);
        for (int i = 0; i < M; ++i) {
            boundary.push_back(mem[i].rimR);
            boundary.push_back(mem[i].rimL);
        }
        // Build one simple outer boundary for the whole junction. Sorting all road
        // edges by angle and filling only alternating wedges left slivers of the
        // overlapping ribbons exposed, which showed up as dark seams and spikes.
        std::sort(boundary.begin(), boundary.end(), [](const glm::vec2& a, const glm::vec2& b) {
            return a.x != b.x ? a.x < b.x : a.y < b.y;
        });
        boundary.erase(std::unique(boundary.begin(), boundary.end(), [](const glm::vec2& a, const glm::vec2& b) {
            return std::fabs(a.x - b.x) < 1e-4f && std::fabs(a.y - b.y) < 1e-4f;
        }), boundary.end());
        auto cross2 = [](const glm::vec2& o, const glm::vec2& a, const glm::vec2& b) {
            return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
        };
        std::vector<glm::vec2> hull(2 * boundary.size());
        int hullN = 0;
        for (const auto& p : boundary) {
            while (hullN >= 2 && cross2(hull[hullN - 2], hull[hullN - 1], p) <= 0.0f) --hullN;
            hull[hullN++] = p;
        }
        const int lowerHullN = hullN + 1;
        for (int i = (int)boundary.size() - 2; i >= 0; --i) {
            while (hullN >= lowerHullN && cross2(hull[hullN - 2], hull[hullN - 1], boundary[i]) <= 0.0f) --hullN;
            hull[hullN++] = boundary[i];
        }
        hull.resize(hullN > 0 ? hullN - 1 : 0);
        if (hull.size() < 3) continue;

        // The roads now meet by their own ribbons. A raised convex-hull overlay
        // here looks like a dark patch and can cover the road texture at the join.
        continue;

        const glm::vec3 up(0, 1, 0);

        auto emitPoly = [&](glm::vec2 origin, const std::vector<glm::vec2>& bnd, float lift = 0.0f) {
            if (bnd.size() < 2) return;
            uint32_t wBase = (uint32_t)m_vertices.size();
            auto add = [&](const glm::vec2& p2) {
                float y = sampler.isValid() ? sampler.sample(p2.x, p2.y) : 0.0f;
                if (deformer && deformer->isBuilt()) y += deformer->getDeformation(p2.x, p2.y);
                m_vertices.push_back({ glm::vec3(p2.x, y + Z_BIAS + CAMBER + lift, p2.y),
                                       glm::vec3(0.0f, 1.0f, 0.0f),
                                       glm::vec2(0.5f, glm::length(p2 - origin) * TEX_SCALE),
                                       (float)jnType,
                                       std::max(0.0f, 5.0f - (float)jnType), 0.0f });
            };
            add(origin);
            for (const auto& p2 : bnd) add(p2);
            uint32_t wN = (uint32_t)bnd.size() + 1;
            for (uint32_t k = 1; k + 1 < wN; ++k) {
                m_indices.push_back(wBase);
                m_indices.push_back(wBase + k);
                m_indices.push_back(wBase + k + 1);
            }
            ++jnCount;
        };

        // Lift above the road ribbons so the junction surface wins the depth test
        // instead of z-fighting against the roads it connects.
        const float padLift = 0.03f;
        glm::vec2 padCenter(0.0f);
        for (const auto& p : hull) padCenter += p;
        padCenter /= (float)hull.size();
        emitPoly(padCenter, hull, padLift);
    }

    // -- 5b. Mid-span crossing pads --------------------------------------------
    // One flat paved quad per crossing, sized to the participants' rims and
    // lifted clear of both ribbons. It hides the coplanar overlap (the source of
    // the z-fighting) and, being bare asphalt like the roads around it, reads as
    // a real intersection: both roads' markings stop at the kerb line and
    // resume on the far side.
    for (const auto& site : crossSites) {
        // Crossings are formed by the road ribbon edges; do not stamp a surface
        // over them. This keeps the edge-to-edge join free of overlay patches.
        continue;

        std::vector<glm::vec2> rim;
        for (const auto& m : site.members) {
            const auto& seg = segments[m.first];
            const int i = m.second;
            if (i < 0 || i >= (int)seg.splinePoints.size()) continue;
            const glm::vec2 c(seg.splinePoints[i].x, seg.splinePoints[i].z);
            glm::vec2 r(seg.rightVecs[i].x, seg.rightVecs[i].z);
            if (glm::dot(r, r) < 1e-9f) continue;
            r = glm::normalize(r);
            const float hw = seg.width * 0.5f + 0.5f;   // 0.5 m overlap guard
            rim.push_back(c - r * hw);
            rim.push_back(c + r * hw);
        }
        if (rim.size() < 3) continue;

        // Convex hull (monotone chain) so the pad hugs the ribbons instead of
        // stamping a disc over the tarmac.
        std::sort(rim.begin(), rim.end(), [](const glm::vec2& a, const glm::vec2& b) {
            return a.x != b.x ? a.x < b.x : a.y < b.y;
        });
        rim.erase(std::unique(rim.begin(), rim.end(),
                              [](const glm::vec2& a, const glm::vec2& b) {
                                  return std::fabs(a.x - b.x) < 1e-4f &&
                                         std::fabs(a.y - b.y) < 1e-4f;
                              }),
                  rim.end());
        if (rim.size() < 3) continue;
        auto cross2 = [](const glm::vec2& o, const glm::vec2& a, const glm::vec2& b) {
            return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
        };
        std::vector<glm::vec2> hull(2 * rim.size());
        int k = 0;
        for (int i = 0; i < (int)rim.size(); ++i) {
            while (k >= 2 && cross2(hull[k - 2], hull[k - 1], rim[i]) <= 0.0f) --k;
            hull[k++] = rim[i];
        }
        const int lower = k + 1;
        for (int i = (int)rim.size() - 2; i >= 0; --i) {
            while (k >= lower && cross2(hull[k - 2], hull[k - 1], rim[i]) <= 0.0f) --k;
            hull[k++] = rim[i];
        }
        hull.resize(k > 0 ? k - 1 : 0);
        if (hull.size() < 3) continue;

        // The highest-priority road supplies the surface and marking frame.
        int padHost = site.members[0].first;
        for (const auto& m : site.members) {
            if ((int)segments[m.first].type < (int)segments[padHost].type ||
                ((int)segments[m.first].type == (int)segments[padHost].type &&
                 segments[m.first].width > segments[padHost].width))
                padHost = m.first;
        }
        const int padType = (int)segments[padHost].type;
        const glm::vec3 up(0, 1, 0);
        const float padLift = 0.03f;
        glm::vec2 mid(0.0f);
        for (const auto& p2 : hull) mid += p2;
        mid /= (float)hull.size();

        auto padY = [&](const glm::vec2& p2) {
            float y = sampler.isValid() ? sampler.sample(p2.x, p2.y) : 0.0f;
            if (deformer && deformer->isBuilt()) y += deformer->getDeformation(p2.x, p2.y);
            return y + Z_BIAS + CAMBER + padLift;
        };

        const uint32_t pBase = (uint32_t)m_vertices.size();
        m_vertices.push_back({ glm::vec3(mid.x, padY(mid), mid.y), up,
                               glm::vec2(0.5f, 0.0f), (float)padType,
                               std::max(0.0f, 5.0f - (float)padType), 0.0f });
        for (const auto& p2 : hull) {
            m_vertices.push_back({ glm::vec3(p2.x, padY(p2), p2.y), up,
                                   glm::vec2(0.5f, glm::length(p2 - mid) * TEX_SCALE), (float)padType,
                                   std::max(0.0f, 5.0f - (float)padType), 0.0f });
        }
        for (uint32_t i = 0; i < (uint32_t)hull.size(); ++i) {
            const uint32_t a = pBase + 1 + i;
            const uint32_t b = pBase + 1 + (i + 1) % (uint32_t)hull.size();
            m_indices.push_back(pBase);
            m_indices.push_back(a);
            m_indices.push_back(b);
        }
        ++crossPadCount;
    }

    // -- 5c. Shader-space rounded T-junction borders ---------------------------
    // Pass curved border paths to street.frag so the existing road material paints
    // the stripe and curb on the dominant road surface. No separate line/pad mesh
    // is emitted, which keeps the join at the same height and texture as its roads.
    constexpr float kBezierQuarter = 0.55228475f;
    for (const BranchContact& contact : branchContacts) {
        const RoadSegment& branch = segments[contact.seg];
        const RoadSegment& host = segments[contact.host];
        if (branch.splinePoints.size() < 2 || host.splinePoints.size() < 2) continue;
        const int endIndex = contact.end == 0 ? 0 : (int)branch.splinePoints.size() - 1;
        glm::vec2 branchRight(branch.rightVecs[endIndex].x, branch.rightVecs[endIndex].z);
        glm::vec2 hostT = glm::normalize(contact.hostTangent);
        if (glm::length(branchRight) < 1e-4f || glm::length(hostT) < 1e-4f) continue;
        const glm::vec2 hostRight(hostT.y, -hostT.x);
        const float radius = branchFilletRadius(contact);

        // Sample the actual curved road frame where its painted edge enters the
        // fillet. Extrapolating the endpoint tangent put the shader curve in a
        // different place from the existing stripe on bent approach roads.
        const auto& distance = branch.cumulativeDist;
        if (distance.size() != branch.splinePoints.size() ||
            distance.size() != branch.rightVecs.size() ||
            distance.size() != branch.tangents.size() || distance.size() < 2) continue;
        const float targetDistance = contact.end == 0
            ? distance.front() + radius : distance.back() - radius;
        int startRing = 0;
        for (int i = 0; i + 1 < (int)distance.size(); ++i) {
            if (distance[i] <= targetDistance && targetDistance <= distance[i + 1]) {
                startRing = i;
                break;
            }
            if (targetDistance > distance.back()) startRing = (int)distance.size() - 2;
        }
        const float ringSpan = distance[startRing + 1] - distance[startRing];
        const float ringT = ringSpan > 1e-5f
            ? glm::clamp((targetDistance - distance[startRing]) / ringSpan, 0.0f, 1.0f)
            : 0.0f;
        glm::vec2 branchStartTangent = glm::mix(
            glm::vec2(branch.tangents[startRing].x, branch.tangents[startRing].z),
            glm::vec2(branch.tangents[startRing + 1].x, branch.tangents[startRing + 1].z), ringT);
        if (glm::length(branchStartTangent) < 1e-4f) continue;
        branchStartTangent = glm::normalize(branchStartTangent);
        // The curve runs from the road interior toward the attached endpoint.
        // That is opposite the spline tangent at a start endpoint and aligned at
        // an end endpoint. Using the unflipped tangent on start joins sent the
        // Bezier control point back into the branch, creating the stray arcs seen
        // in the game screenshot.
        if (contact.end == 0) branchStartTangent = -branchStartTangent;
        // Match the shader's full outer pavement band: u=[0, .067] on either side.
        constexpr float pavementBandCenterU = 0.0335f;
        const float hostCurbOffset = host.width * (0.5f - pavementBandCenterU);
        const float hostWhiteOffset = roadWhiteEdgeLineOffset(host.width);
        const float turnOrientation = glm::dot(branchRight, hostT) >= 0.0f ? 1.0f : -1.0f;

        // Derive the curve anchors from the exact cross-section positions used by
        // the road ribbon. rightVecs are miter-compensated (often longer than 1),
        // and sharp bends can further change the inner-edge width. Normalizing the
        // frame here moved the shader-painted lines away from the real road edges,
        // leaving the detached arcs and black gaps visible at T joins.
        const float branchHalfWidth = branch.width * 0.5f;
        auto ribbonEdgeAt = [&](int ring, float u) {
            const float off = u * 2.0f - 1.0f;
            const float edgeWidth = roadMeshHalfWidth(branch, ring, false, off, branchHalfWidth);
            const glm::vec3 p = branch.splinePoints[ring] +
                branch.rightVecs[ring] * (off * edgeWidth);
            return glm::vec2(p.x, p.z);
        };
        auto ribbonEdgeAtFilletStart = [&](float u) {
            return glm::mix(ribbonEdgeAt(startRing, u),
                            ribbonEdgeAt(startRing + 1, u), ringT);
        };

        float whiteMouthHalfSpan = 0.0f;
        float pavementMouthHalfSpan = 0.0f;
        for (float side : { -1.0f, 1.0f }) {
            const float edgeU = side < 0.0f ? 0.085f : 0.915f;
            const float curbU = side < 0.0f ? pavementBandCenterU : 1.0f - pavementBandCenterU;
            whiteMouthHalfSpan = std::max(whiteMouthHalfSpan,
                std::fabs(glm::dot(ribbonEdgeAt(endIndex, edgeU) - contact.position, hostT)));
            pavementMouthHalfSpan = std::max(pavementMouthHalfSpan,
                std::fabs(glm::dot(ribbonEdgeAt(endIndex, curbU) - contact.position, hostT)));
        }
        m_joinShaderContacts.push_back({ contact.hostPosition, hostT, contact.hostSide,
                                         host.width,
                                         whiteMouthHalfSpan + radius,
                                         pavementMouthHalfSpan + radius, radius });

        for (float side : { -1.0f, 1.0f }) {
            const glm::vec2 turn = hostT * (side * turnOrientation);
            const float edgeU = side < 0.0f ? 0.085f : 0.915f;
            const float curbU = side < 0.0f ? pavementBandCenterU : 1.0f - pavementBandCenterU;
            const glm::vec2 whiteAtJoin = ribbonEdgeAt(endIndex, edgeU);
            const glm::vec2 whiteStart = ribbonEdgeAtFilletStart(edgeU);
            // Land on the host's actual painted edge stripe. The branch endpoint
            // is snapped to that stripe, but its mitered frame can shift each edge
            // slightly along the host tangent; preserve that offset while joining.
            const glm::vec2 hostWhite = contact.hostPosition +
                hostRight * (contact.hostSide * hostWhiteOffset);
            const float whiteAlongHost = glm::dot(whiteAtJoin - hostWhite, hostT);
            const glm::vec2 whiteEnd = hostWhite + hostT *
                (whiteAlongHost + side * turnOrientation * radius);
            m_joinShaderStrokes.push_back({ whiteStart,
                whiteStart + branchStartTangent * (radius * kBezierQuarter),
                whiteEnd - turn * (radius * kBezierQuarter), whiteEnd,
                branch.width * 0.018f, false });

            const glm::vec2 curbAtBranch = ribbonEdgeAtFilletStart(curbU);
            const glm::vec2 curbAtBranchJoin = ribbonEdgeAt(endIndex, curbU);
            const glm::vec2 curbAtHost = contact.hostPosition +
                hostRight * (contact.hostSide * hostCurbOffset);
            const float curbAlongHost = glm::dot(curbAtBranchJoin - curbAtHost, hostT);
            const glm::vec2 curbStart = curbAtBranch;
            const glm::vec2 curbEnd = curbAtHost + hostT *
                (curbAlongHost + side * turnOrientation * radius);
            m_joinShaderStrokes.push_back({ curbStart,
                curbStart + branchStartTangent * (radius * kBezierQuarter),
                curbEnd - turn * (radius * kBezierQuarter), curbEnd,
                branch.width * pavementBandCenterU, true });
        }
    }

    m_indexCount = (int)m_indices.size();
    std::cout << "[RoadMeshBuilder] Generated " << m_vertices.size() << " vertices, "
              << (m_indexCount / 3) << " triangles (" << capCount << " caps, "
              << jointCount << " joints, " << jnCount << " junction wedges, "
              << branchContacts.size() << " T-joins, " << m_joinShaderStrokes.size()
              << " shader border curves, " << m_joinShaderContacts.size()
              << " shader contact masks, "
              << crossSites.size() << " crossings, " << crossPadCount
              << " crossing pads, "
              << bareSamples << " bare merge samples)" << std::endl;
}

void RoadMeshBuilder::upload() {
    if (m_vertices.empty() || m_indices.empty()) return;

    // Cleanup previous
    if (m_VAO) glDeleteVertexArrays(1, &m_VAO);
    if (m_VBO) glDeleteBuffers(1, &m_VBO);
    if (m_EBO) glDeleteBuffers(1, &m_EBO);

    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glGenBuffers(1, &m_EBO);

    glBindVertexArray(m_VAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, m_vertices.size() * sizeof(RoadVertex),
                 m_vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_indices.size() * sizeof(uint32_t),
                 m_indices.data(), GL_STATIC_DRAW);

    // Layout 0: vec3 position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RoadVertex),
                          (void*)offsetof(RoadVertex, position));

    // Layout 1: vec3 normal
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(RoadVertex),
                          (void*)offsetof(RoadVertex, normal));

    // Layout 2: vec2 uv
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(RoadVertex),
                          (void*)offsetof(RoadVertex, uv));

    // Layout 3: float roadType
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(RoadVertex),
                          (void*)offsetof(RoadVertex, roadType));

    // Layout 5: per-road overlap priority (wider roads dominate at crossings).
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(RoadVertex),
                          (void*)offsetof(RoadVertex, surfacePriority));

    // Layout 4: float markMask (1 = paint markings, 0 = bare asphalt in a junction)
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(RoadVertex),
                          (void*)offsetof(RoadVertex, markMask));

    glBindVertexArray(0);

    std::cout << "[RoadMeshBuilder] Uploaded to GPU: VAO=" << m_VAO << std::endl;
}

void RoadMeshBuilder::render(Shader& shader) const {
    if (m_VAO == 0 || m_indexCount == 0) return;

    shader.use();
    constexpr int kMaxJoinStrokes = 32;
    std::array<glm::vec4, kMaxJoinStrokes * 4> curvePoints{};
    std::array<glm::vec4, kMaxJoinStrokes> strokeInfo{};
    constexpr int kMaxJoinContacts = 8;
    std::array<glm::vec4, kMaxJoinContacts> contactCenterT{};
    std::array<glm::vec4, kMaxJoinContacts> contactInfo{};
    std::array<glm::vec4, kMaxJoinContacts> contactDims{};
    const int joinCount = std::min((int)m_joinShaderStrokes.size(), kMaxJoinStrokes);
    const int contactCount = std::min((int)m_joinShaderContacts.size(), kMaxJoinContacts);
    for (int i = 0; i < joinCount; ++i) {
        const RoadJoinShaderStroke& stroke = m_joinShaderStrokes[i];
        curvePoints[i * 4 + 0] = glm::vec4(stroke.p0, 0.0f, 0.0f);
        curvePoints[i * 4 + 1] = glm::vec4(stroke.p1, 0.0f, 0.0f);
        curvePoints[i * 4 + 2] = glm::vec4(stroke.p2, 0.0f, 0.0f);
        curvePoints[i * 4 + 3] = glm::vec4(stroke.p3, 0.0f, 0.0f);
        strokeInfo[i] = glm::vec4(stroke.halfWidth, stroke.curb ? 1.0f : 0.0f, 0.0f, 0.0f);
    }
    for (int i = 0; i < contactCount; ++i) {
        const RoadJoinShaderContact& contact = m_joinShaderContacts[i];
        contactCenterT[i] = glm::vec4(contact.hostCenter, contact.hostTangent);
        contactInfo[i] = glm::vec4(contact.hostSide, contact.whiteSpan,
                                   contact.pavementSpan, contact.radius);
        contactDims[i] = glm::vec4(contact.hostWidth, 0.0f, 0.0f, 0.0f);
    }
    shader.setInt("u_joinStrokeCount", joinCount);
    shader.setInt("u_joinContactCount", contactCount);
    glUniform4fv(shader.getUniformLocation("u_joinCurvePoints[0]"),
                 (GLsizei)curvePoints.size(), glm::value_ptr(curvePoints[0]));
    glUniform4fv(shader.getUniformLocation("u_joinStrokeInfo[0]"),
                 (GLsizei)strokeInfo.size(), glm::value_ptr(strokeInfo[0]));
    glUniform4fv(shader.getUniformLocation("u_joinContactCenterT[0]"),
                 (GLsizei)contactCenterT.size(), glm::value_ptr(contactCenterT[0]));
    glUniform4fv(shader.getUniformLocation("u_joinContactInfo[0]"),
                 (GLsizei)contactInfo.size(), glm::value_ptr(contactInfo[0]));
    glUniform4fv(shader.getUniformLocation("u_joinContactDims[0]"),
                 (GLsizei)contactDims.size(), glm::value_ptr(contactDims[0]));

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-3.0f, -3.0f);
    // No alpha blending. The shoulder used to be an alpha-blended fade that still
    // wrote depth, so the semi-transparent fringe fought with the terrain underneath
    // and produced flickering speckle along both road edges. street.frag now
    // dithers the shoulder away with discard() instead, so the ribbon can be drawn
    // fully opaque and every fragment it shades is a fragment it owns.
    glDisable(GL_BLEND);

    glBindVertexArray(m_VAO);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    glDisable(GL_POLYGON_OFFSET_FILL);
}
