#include "RoadSystem.h"
#include <algorithm>
#include <functional>
#include <array>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <limits>
#include <sstream>
#include <unordered_map>


namespace {
// Graded road bed: the strip either side of the asphalt held at the road profile.
// Real roads are cut and filled flat before the surface goes on; if this strip is
// narrower than a terrain triangle the triangle bridges over the road at natural
// height and shows through the asphalt. At the shipped 22.25 m terrain cell the bed
// therefore has to be at least a cell diagonal wide. Measured with the ribbon
// tracking the bed (below): meanGap 0.196 -> 0.082 m, worst gap 9.75 -> 1.39 m,
// 7185 -> 14 vertices more than 0.5 m off.
constexpr float kRoadBedFlatReach = 34.0f;
// How far back from a junction the shared arm height is blended into each road's own
// profile, in metres. Long enough to be a gentle approach rather than a kink at the
// mouth; short enough that it does not reshape a road.
constexpr float kJunctionHeightBlend = 18.0f;

// How far the brick sidewalk band sits above the carriageway and the terrain, metres.
// The ribbon already has vertices on both sides of each kerb line (kCross 0.146 / 0.154),
// so this lift turns the kerb into a real step with a lit face. street.vert must NOT lift
// the band again (set its CURB_HEIGHT_M to 0.0). TerrainManager::conformTerrainToRoadMesh
// subtracts this lift again (kSidewalkRaise there must match) so the ground is graded to the
// road bed, not to the kerb step.
constexpr float kSidewalkRaise = RoadSpline::kSidewalkRaise;

// Fraction of ribbon width taken by the raised sidewalk on EACH side
// (matches kCross 0.146/0.154 and 0.846/0.854, and CURB_U in street.frag).
constexpr float kRoadCurbU = RoadSpline::kCurbU;

// How much of its own ground height the ribbon adopts across its width.
//
// 1.0 means the carriageway follows the graded bed exactly, which is what makes the
// asphalt sit ON the prepared bed instead of crossing it. The old value, 0.02, left
// the ribbon almost dead flat at the centreline height while the bed was graded to
// the road profile over a bed of a different width -- the two surfaces disagreed
// across the whole carriageway, which is the terrain-lying-on-the-road defect.
// Twisting on cross-slopes is not a concern once the bed itself is graded flat: the
// ground the ribbon follows is the bed, not the natural hillside.
constexpr float kRoadEdgeConform = 1.0f;

// Horizontal road frame helper. The spline tangents have already been smoothed;
// derive every cross-section side from that tangent with the same fixed XZ
// rotation. A second smoothing/miter pass made the frame disagree with the
// tangent and could turn open-end rings or tight bends into folded quads.
void computeRoadFrames(std::vector<glm::vec3> &rightVecs,
                       const std::vector<glm::vec3> &tangents, bool closed) {
  (void)closed;
  const int m = (int)tangents.size();
  if (m <= 0)
    return;
  rightVecs.resize(m);
  for (int i = 0; i < m; ++i) {
    glm::vec2 tangent(tangents[i].x, tangents[i].z);
    const float length = glm::length(tangent);
    if (length > 1e-5f)
      tangent /= length;
    else
      tangent = glm::vec2(1.0f, 0.0f);
    rightVecs[i] = glm::vec3(tangent.y, 0.0f, -tangent.x);
  }
}

// Backward-compatible forwarder
void computeParallelFrames(std::vector<glm::vec3> &rightVecs,
                           const std::vector<glm::vec3> &tangents, int from) {
  (void)from;
  computeRoadFrames(rightVecs, tangents, false);
}
} // namespace

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  HeightSampler
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

void HeightSampler::init(const TerrainCoordinateMapper &mapper,
                         const float *heights, int w, int h) {
  m_mapper = mapper;
  m_heights = heights;
  m_w = w;
  m_h = h;
}

float HeightSampler::sample(float x, float z) const {
  if (!m_heights || m_w <= 0 || m_h <= 0)
    return 0.0f;

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

  return h00 * (1.0f - tx) * (1.0f - ty) + h10 * tx * (1.0f - ty) +
         h01 * (1.0f - tx) * ty + h11 * tx * ty;
}

float HeightSampler::renderedGround(float x, float z) const {
  if (!m_heights || m_w < 2 || m_h < 2)
    return 0.0f;

  const glm::vec2 uv = m_mapper.worldToUV(x, z);
  if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) {
    return 0.0f;
  }

  // Keep one cell of headroom so the +1 corner lookups below stay in range.
  const float fx = uv.x * float(m_w - 1);
  const float fy = uv.y * float(m_h - 1);
  const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, m_w - 2);
  const int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, m_h - 2);
  const float tx = std::clamp(fx - float(x0), 0.0f, 1.0f);
  const float ty = std::clamp(fy - float(y0), 0.0f, 1.0f);

  const float h00 = m_heights[y0 * m_w + x0];
  const float h10 = m_heights[y0 * m_w + x0 + 1];
  const float h01 = m_heights[(y0 + 1) * m_w + x0];
  const float h11 = m_heights[(y0 + 1) * m_w + x0 + 1];

  // Triangle (tl, bl, tr) covers tx + ty <= 1; triangle (tr, bl, br) covers the rest.
  // Both are planar interpolations of their three corners, matching the terrain
  // index buffer term for term.
  if (tx + ty <= 1.0f)
    return h00 + (h10 - h00) * tx + (h01 - h00) * ty;
  return h11 + (h01 - h11) * (1.0f - tx) + (h10 - h11) * (1.0f - ty);
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
  explicit JsonReader(const std::string &text) : m_text(text) {}

  // â”€â”€ scanning
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  void skipSpace() {
    while (m_pos < m_text.size()) {
      const char c = m_text[m_pos];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        m_pos++;
        continue;
      }
      break;
    }
  }

  char peek() const { return m_pos < m_text.size() ? m_text[m_pos] : '\0'; }
  bool atEnd() const { return m_pos >= m_text.size(); }

  bool consume(char c) {
    skipSpace();
    if (peek() != c)
      return false;
    m_pos++;
    return true;
  }

  bool expect(char c) {
    if (consume(c))
      return true;
    return fail(std::string("expected '") + c + "'");
  }

  // â”€â”€ diagnostics
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  // Only the first error is kept: it is the one that explains the rest.
  bool fail(const std::string &what) {
    if (m_error.empty()) {
      size_t line = 1, column = 1;
      for (size_t i = 0; i < m_pos && i < m_text.size(); ++i) {
        if (m_text[i] == '\n') {
          line++;
          column = 1;
        } else {
          column++;
        }
      }
      m_error = what + " (line " + std::to_string(line) + ", column " +
                std::to_string(column) + ")";
    }
    return false;
  }

  bool failed() const { return !m_error.empty(); }
  const std::string &error() const { return m_error; }

  // â”€â”€ structure
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  // Reads the next "key": of an object whose '{' was already consumed. Returns
  // false at the closing '}' or on error, so callers must test failed() to tell
  // "object finished" from "object broken". A trailing comma is a syntax
  // error, as in JSON.
  bool memberKey(std::string &key, bool &first) {
    if (consume('}'))
      return false;
    if (!first && !consume(',')) {
      fail("expected ',' or '}'");
      return false;
    }
    first = false;
    if (!readString(key))
      return false;
    if (!expect(':'))
      return false;
    return true;
  }

  // â”€â”€ values
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  bool readString(std::string &out) {
    if (!expect('"'))
      return false;
    out.clear();
    while (true) {
      if (atEnd())
        return fail("unterminated string");
      const char c = m_text[m_pos++];
      if (c == '"')
        return true;
      if (c != '\\') {
        // Compared as unsigned: a plain char is signed on MSVC, so any
        // UTF-8 byte >= 0x80 would look like a control character.
        if ((unsigned char)c < 0x20)
          return fail("unescaped control character in string");
        out += c;
        continue;
      }
      if (atEnd())
        return fail("unterminated escape sequence");
      switch (m_text[m_pos++]) {
      case '"':
        out += '"';
        break;
      case '\\':
        out += '\\';
        break;
      case '/':
        out += '/';
        break;
      case 'b':
        out += '\b';
        break;
      case 'f':
        out += '\f';
        break;
      case 'n':
        out += '\n';
        break;
      case 'r':
        out += '\r';
        break;
      case 't':
        out += '\t';
        break;
      case 'u': {
        uint32_t code = 0;
        if (!readHex4(code))
          return false;
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
      default:
        return fail("unknown escape sequence");
      }
    }
  }

  bool readFloat(float &out) {
    double value = 0.0;
    if (!readNumber(value))
      return false;
    if (std::abs(value) > (double)std::numeric_limits<float>::max())
      return fail("number out of range for a float");
    out = (float)value;
    return true;
  }

  // Reads a JSON number as a double. Public because "id" must be validated as
  // an exact integer rather than a float. Strict grammar, and strtod turns an
  // overflow into infinity which the isfinite test rejects — std::stof used to
  // throw an uncaught std::out_of_range on such a literal.
  bool readNumber(double &out) {
    skipSpace();
    const size_t start = m_pos;
    if (peek() == '-')
      m_pos++;
    if (peek() == '0') {
      m_pos++;
    } else if (isDigit(peek())) {
      while (isDigit(peek()))
        m_pos++;
    } else {
      m_pos = start;
      return fail("expected a number");
    }
    if (peek() == '.') {
      m_pos++;
      if (!isDigit(peek())) {
        m_pos = start;
        return fail("expected digits after '.'");
      }
      while (isDigit(peek()))
        m_pos++;
    }
    if (peek() == 'e' || peek() == 'E') {
      m_pos++;
      if (peek() == '+' || peek() == '-')
        m_pos++;
      if (!isDigit(peek())) {
        m_pos = start;
        return fail("expected digits in exponent");
      }
      while (isDigit(peek()))
        m_pos++;
    }
    const std::string token = m_text.substr(start, m_pos - start);
    char *end = nullptr;
    const double value = std::strtod(token.c_str(), &end);
    if (end != token.c_str() + token.size() || !std::isfinite(value))
      return fail("number '" + token + "' is out of range");
    out = value;
    return true;
  }

  bool readBool(bool &out) {
    if (match("true")) {
      out = true;
      return true;
    }
    if (match("false")) {
      out = false;
      return true;
    }
    return fail("expected true or false");
  }

  // Consumes a whole value whatever its type. This is what keeps an unknown
  // key from stalling the reader on its value.
  bool skipValue(int depth = 0) {
    if (depth > 64)
      return fail("JSON nested too deeply");
    skipSpace();
    switch (peek()) {
    case '{':
      return skipContainer('}', depth, true);
    case '[':
      return skipContainer(']', depth, false);
    case '"': {
      std::string ignored;
      return readString(ignored);
    }
    case 't':
      if (match("true"))
        return true;
      return fail("invalid value");
    case 'f':
      if (match("false"))
        return true;
      return fail("invalid value");
    case 'n':
      if (match("null"))
        return true;
      return fail("invalid value");
    default: {
      double ignored;
      return readNumber(ignored);
    }
    }
  }

private:
  bool isDigit(char c) const { return c >= '0' && c <= '9'; }

  bool match(const char *literal) {
    skipSpace();
    const size_t n = std::strlen(literal);
    if (m_text.compare(m_pos, n, literal) != 0)
      return false;
    m_pos += n;
    return true;
  }

  bool readHex4(uint32_t &out) {
    if (m_pos + 4 > m_text.size())
      return fail("truncated \\u escape");
    out = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = m_text[m_pos++];
      out <<= 4;
      if (c >= '0' && c <= '9')
        out |= (uint32_t)(c - '0');
      else if (c >= 'a' && c <= 'f')
        out |= (uint32_t)(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F')
        out |= (uint32_t)(c - 'A' + 10);
      else
        return fail("invalid \\u escape");
    }
    return true;
  }

  static void appendUtf8(uint32_t code, std::string &out) {
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
    m_pos++; // opening bracket
    if (consume(close))
      return true;
    while (true) {
      if (isObject) {
        bool first = true;
        std::string key;
        if (!memberKey(key, first)) {
          if (failed())
            return false; // broken object
          return true;    // closing '}' reached
        }
        if (!skipValue(depth + 1))
          return false;
      } else if (!skipValue(depth + 1)) {
        return false;
      }
      if (consume(close))
        return true;
      if (!consume(','))
        return fail(std::string("expected ',' or '") + close + "'");
    }
  }

  const std::string &m_text;
  size_t m_pos = 0;
  std::string m_error;
};

} // namespace

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

static bool parseRoadType(const std::string &s, RoadType &out) {
  if (s == "colonial_avenue") {
    out = RoadType::ColonialAvenue;
    return true;
  }
  if (s == "urban_street") {
    out = RoadType::UrbanStreet;
    return true;
  }
  if (s == "country_highway") {
    out = RoadType::CountryHighway;
    return true;
  }
  if (s == "country_dirt") {
    out = RoadType::CountryDirt;
    return true;
  }
  if (s == "port") {
    out = RoadType::Port;
    return true;
  }
  if (s == "medina") {
    out = RoadType::Medina;
    return true;
  }
  return false;
}

// Hierarchy-based road dimensions (world meters).  Used when a JSON segment
// omits width/shoulder so road sizes stay consistent with the road type.
static float roadTypeDefaultWidth(RoadType t) {
  switch (t) {
  case RoadType::ColonialAvenue:
    return 11.5f;
  case RoadType::UrbanStreet:
    return 8.0f;
  case RoadType::CountryHighway:
    return 8.5f;
  case RoadType::CountryDirt:
    return 6.0f;
  case RoadType::Port:
    return 10.0f;
  case RoadType::Medina:
    return 5.0f;
  }
  return 6.0f;
}

static float roadTypeDefaultShoulder(RoadType t) {
  return (t == RoadType::Medina) ? 0.75f : 1.5f;
}

static const char *roadTypeToString(RoadType t) {
  switch (t) {
  case RoadType::ColonialAvenue:
    return "colonial_avenue";
  case RoadType::UrbanStreet:
    return "urban_street";
  case RoadType::CountryHighway:
    return "country_highway";
  case RoadType::CountryDirt:
    return "country_dirt";
  case RoadType::Port:
    return "port";
  case RoadType::Medina:
    return "medina";
  }
  return "country_dirt";
}

// Reads one "points" array into control points. Requires both x and z on every
// point, so a typo can no longer be silently read as a point on the origin.
static bool parsePointList(JsonReader &json, std::vector<RoadPoint> &out) {
  if (!json.expect('['))
    return false;
  if (json.consume(']'))
    return true;
  while (true) {
    if (!json.expect('{'))
      return false;
    bool hasX = false, hasZ = false;
    float x = 0.0f, z = 0.0f;
    bool first = true;
    std::string pkey;
    while (json.memberKey(pkey, first)) {
      if (pkey == "x") {
        if (!json.readFloat(x))
          return false;
        hasX = true;
      } else if (pkey == "z") {
        if (!json.readFloat(z))
          return false;
        hasZ = true;
      } else if (!json.skipValue()) {
        return false;
      }
    }
    if (json.failed())
      return false;
    if (!hasX || !hasZ)
      return json.fail("a point needs both \"x\" and \"z\"");

    RoadPoint pt;
    pt.pos.x = x;
    pt.pos.y = z; // pos.y stores world Z
    out.push_back(pt);

    if (json.consume(','))
      continue;
    return json.expect(']');
  }
}

static bool parseRoadObject(JsonReader &json, RoadSegment &seg) {
  if (!json.expect('{'))
    return false;
  seg.width = 6.0f;
  seg.shoulderWidth = 1.5f;
  bool hasWidth = false, hasShoulder = false;
  bool first = true;
  std::string key;
  while (json.memberKey(key, first)) {
    if (key == "id") {
      double id = 0.0;
      if (!json.readNumber(id))
        return false;
      if (id < 0.0 || id > 4294967295.0 || id != std::floor(id))
        return json.fail("\"id\" must be an integer between 0 and 4294967295");
      seg.id = (uint32_t)id;
    } else if (key == "type") {
      std::string name;
      if (!json.readString(name))
        return false;
      if (!parseRoadType(name, seg.type))
        return json.fail("unknown road type \"" + name + "\"");
    } else if (key == "width") {
      if (!json.readFloat(seg.width))
        return false;
      // A zero or negative width produces inverted, self-crossing ribbons and
      // degenerate junction hulls further down the pipeline.
      if (!(seg.width > 0.0f) || seg.width > 1000.0f)
        return json.fail("\"width\" must be greater than 0 and at most 1000 metres");
      hasWidth = true;
    } else if (key == "shoulder") {
      if (!json.readFloat(seg.shoulderWidth))
        return false;
      if (!(seg.shoulderWidth >= 0.0f) || seg.shoulderWidth > 1000.0f)
        return json.fail("\"shoulder\" must be between 0 and 1000 metres");
      hasShoulder = true;
    } else if (key == "corner") {
      if (!json.readFloat(seg.cornerRadius))
        return false;
      if (!(seg.cornerRadius >= 0.0f) || seg.cornerRadius > 1000.0f)
        return json.fail("\"corner\" must be between 0 and 1000 metres");
    } else if (key == "lanesPerDirection") {
      double value = 0.0;
      if (!json.readNumber(value))
        return false;
      if (value < 0.0 || value > 8.0 || value != std::floor(value))
        return json.fail("\"lanesPerDirection\" must be an integer from 0 to 8");
      seg.lanesPerDirection = (uint8_t)value;
    } else if (key == "oneWay") {
      if (!json.readBool(seg.oneWay))
        return false;
    } else if (key == "speedLimitMps") {
      if (!json.readFloat(seg.speedLimitMps))
        return false;
      if (!(seg.speedLimitMps >= 0.0f) || seg.speedLimitMps > 120.0f)
        return json.fail("\"speedLimitMps\" must be between 0 and 120 metres per second");
    } else if (key == "sidewalks") {
      if (!json.readBool(seg.sidewalksEnabled))
        return false;
    } else if (key == "sidewalkWidth") {
      if (!json.readFloat(seg.sidewalkWidth))
        return false;
      if (!(seg.sidewalkWidth >= 0.0f) || seg.sidewalkWidth > 50.0f)
        return json.fail("\"sidewalkWidth\" must be between 0 and 50 metres");
    } else if (key == "closed") {
      if (!json.readBool(seg.closed))
        return false;
    } else if (key == "points") {
      if (!parsePointList(json, seg.controlPoints))
        return false;
    } else if (!json.skipValue()) {
      return false;
    }
  }
  if (json.failed())
    return false;

  // Resolve missing width/shoulder from the road-type hierarchy so sizes
  // are always well-defined in world meters.
  if (!hasWidth)
    seg.width = roadTypeDefaultWidth(seg.type);
  if (!hasShoulder)
    seg.shoulderWidth = roadTypeDefaultShoulder(seg.type);
  return true;
}

static bool parseRoadsArray(JsonReader &json, std::vector<RoadSegment> &out) {
  if (!json.expect('['))
    return false;
  if (json.consume(']'))
    return true;
  while (true) {
    RoadSegment seg;
    if (!parseRoadObject(json, seg))
      return false;
    if (seg.controlPoints.size() >= 2) {
      out.push_back(std::move(seg));
    } else {
      std::cerr << "[RoadGraph] ignoring road " << seg.id << " with only "
                << seg.controlPoints.size() << " point(s)" << std::endl;
    }
    if (json.consume(','))
      continue;
    return json.expect(']');
  }
}

bool RoadGraph::loadFromJSON(const std::string &path) {
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
        if (!parseRoadsArray(json, loaded))
          return report();
        foundRoads = true;
      } else if (!json.skipValue()) {
        return report();
      }
    }
  }
  if (json.failed())
    return report();
  if (!foundRoads) {
    std::cerr << "[RoadGraph] No 'roads' key found in " << path << std::endl;
    return false;
  }
  json.skipSpace();
  if (!json.atEnd())
    std::cerr << "[RoadGraph] " << path
              << ": ignoring trailing content after the JSON object"
              << std::endl;

  // Duplicate ids make outranks() false in both directions, so neither road
  // suppresses the other's paint and the junction surfaces z-fight. Treat them
  // as a data error rather than silently building broken geometry.
  {
    std::unordered_map<uint32_t, size_t> seenIds;
    for (size_t i = 0; i < loaded.size(); ++i) {
      auto ins = seenIds.emplace(loaded[i].id, i);
      if (!ins.second) {
        std::cerr << "[RoadGraph] " << path << ": duplicate road id "
                  << loaded[i].id << " (roads " << ins.first->second << " and "
                  << i << ")" << std::endl;
        return false;
      }
    }
  }

  // Commit only once the whole document has parsed. Committing before the
  // success check meant a valid file with an empty "roads" array wiped the graph
  // while still reporting failure to the caller.
  segments = std::move(loaded);
  nodes.clear();

  std::cout << "[RoadGraph] Loaded " << segments.size()
            << " road segments from " << path << std::endl;
  return true;
}

bool RoadGraph::saveToJSON(const std::string &path) const {
  std::ofstream file(path);
  if (!file.is_open())
    return false;

  file << "{\n  \"roads\": [\n";
  for (size_t i = 0; i < segments.size(); ++i) {
    const auto &seg = segments[i];
    file << "    {\n";
    file << "      \"id\": " << seg.id << ",\n";
    file << "      \"type\": \"" << roadTypeToString(seg.type) << "\",\n";
    file << "      \"width\": " << seg.width << ",\n";
    file << "      \"shoulder\": " << seg.shoulderWidth << ",\n";
    if (seg.cornerRadius > 0.0f)
      file << "      \"corner\": " << seg.cornerRadius << ",\n";
    if (seg.closed)
      file << "      \"closed\": true,\n";
    if (seg.lanesPerDirection > 0)
      file << "      \"lanesPerDirection\": " << (int)seg.lanesPerDirection << ",\n";
    if (seg.oneWay)
      file << "      \"oneWay\": true,\n";
    if (seg.speedLimitMps > 0.0f)
      file << "      \"speedLimitMps\": " << seg.speedLimitMps << ",\n";
    if (!seg.sidewalksEnabled)
      file << "      \"sidewalks\": false,\n";
    if (seg.sidewalkWidth > 0.0f)
      file << "      \"sidewalkWidth\": " << seg.sidewalkWidth << ",\n";
    file << "      \"points\": [\n";
    for (size_t j = 0; j < seg.controlPoints.size(); ++j) {
      file << "        {\"x\": " << seg.controlPoints[j].pos.x
           << ", \"z\": " << seg.controlPoints[j].pos.y << "}";
      if (j + 1 < seg.controlPoints.size())
        file << ",";
      file << "\n";
    }
    file << "      ]\n";
    file << "    }";
    if (i + 1 < segments.size())
      file << ",";
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

  for (RoadSegment &seg : segments) {
    seg.startNodeId = kInvalidRoadNodeId;
    seg.endNodeId = kInvalidRoadNodeId;
  }

  // Collect all endpoints
  struct EndpointRef {
    uint32_t segIdx;
    bool isEnd; // false = start, true = end
    glm::vec2 pos;
  };
  std::vector<EndpointRef> endpoints;

  for (uint32_t i = 0; i < (uint32_t)segments.size(); ++i) {
    auto &seg = segments[i];
    if (seg.controlPoints.empty())
      continue;
    endpoints.push_back({i, false, seg.controlPoints.front().pos});
    endpoints.push_back({i, true, seg.controlPoints.back().pos});
  }

  // Cluster endpoints within tolerance
  std::vector<int> clusterID(endpoints.size(), -1);
  int nextCluster = 0;

  for (size_t i = 0; i < endpoints.size(); ++i) {
    if (clusterID[i] >= 0)
      continue;
    clusterID[i] = nextCluster;

    for (size_t j = i + 1; j < endpoints.size(); ++j) {
      if (clusterID[j] >= 0)
        continue;
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
    RoadSegment &seg = segments[endpoints[i].segIdx];
    if (endpoints[i].isEnd)
      seg.endNodeId = (uint32_t)c;
    else
      seg.startNodeId = (uint32_t)c;
  }

  for (int i = 0; i < nextCluster; ++i) {
    nodes[i].id = (uint32_t)i;
    glm::vec2 avg = clusterSum[i] / float(clusterCount[i]);
    nodes[i].position = glm::vec3(avg.x, 0.0f, avg.y);
  }

  // Count junctions (nodes with 3+ connections)
  int junctions = 0;
  for (auto &n : nodes) {
    if (n.connectedSegments.size() >= 3)
      junctions++;
  }

  std::cout << "[RoadGraph] Topology: " << nodes.size() << " nodes, "
            << junctions << " junctions" << std::endl;
}

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  RoadSpline â€” Catmull-Rom evaluation
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

namespace {
struct RoadNavigationDefaults {
  int lanesPerDirection = 1;
  float speedLimit = 8.3f;
  float sidewalkWidth = 1.8f;
  int priority = 1;
  bool sidewalksEnabled = true;
};

RoadNavigationDefaults roadNavigationDefaults(const RoadSegment &seg) {
  RoadNavigationDefaults d;
  d.lanesPerDirection = std::clamp((int)(seg.width / 6.0f), 1, 4);
  switch (seg.type) {
  case RoadType::ColonialAvenue:
    d.speedLimit = 13.9f;
    d.sidewalkWidth = 2.4f;
    d.priority = 5;
    break;
  case RoadType::UrbanStreet:
    d.speedLimit = 11.1f;
    d.sidewalkWidth = 2.0f;
    d.priority = 4;
    break;
  case RoadType::CountryHighway:
    d.speedLimit = 16.7f;
    d.sidewalkWidth = 1.8f;
    d.priority = 3;
    break;
  case RoadType::CountryDirt:
    d.speedLimit = 8.3f;
    d.sidewalkWidth = 1.4f;
    d.priority = 1;
    break;
  case RoadType::Port:
    d.speedLimit = 5.6f;
    d.sidewalkWidth = 2.2f;
    d.priority = 2;
    break;
  case RoadType::Medina:
    d.speedLimit = 5.6f;
    d.sidewalkWidth = 1.2f;
    d.priority = 1;
    break;
  }
  if (seg.lanesPerDirection > 0)
    d.lanesPerDirection = seg.lanesPerDirection;
  if (seg.speedLimitMps > 0.0f)
    d.speedLimit = seg.speedLimitMps;
  if (seg.sidewalkWidth > 0.0f)
    d.sidewalkWidth = seg.sidewalkWidth;
  d.sidewalksEnabled = seg.sidewalksEnabled;
  return d;
}

float navCross2(const glm::vec2 &a, const glm::vec2 &b) {
  return a.x * b.y - a.y * b.x;
}

bool navSegmentsCross(const glm::vec2 &a, const glm::vec2 &b,
                      const glm::vec2 &c, const glm::vec2 &d) {
  const glm::vec2 r = b - a, s = d - c;
  const float denom = navCross2(r, s);
  if (std::fabs(denom) < 1e-5f)
    return false;
  const float t = navCross2(c - a, s) / denom;
  const float u = navCross2(c - a, r) / denom;
  return t > 0.03f && t < 0.97f && u > 0.03f && u < 0.97f;
}

bool navSegmentIntersection(const glm::vec2 &a, const glm::vec2 &b,
                            const glm::vec2 &c, const glm::vec2 &d,
                            float &t, float &u) {
  const glm::vec2 r = b - a, s = d - c;
  const float denom = navCross2(r, s);
  if (std::fabs(denom) < 1e-5f)
    return false;
  t = navCross2(c - a, s) / denom;
  u = navCross2(c - a, r) / denom;
  return t > 0.02f && t < 0.98f && u > 0.02f && u < 0.98f;
}

RoadNavigationSample navSample(float distance, const glm::vec2 &position,
                               const glm::vec2 &direction,
                               float curvature = 0.0f) {
  RoadNavigationSample sample;
  sample.distance = distance;
  sample.position = position;
  const float length = glm::length(direction);
  sample.direction = length > 1e-5f ? direction / length : glm::vec2(1, 0);
  sample.curvature = curvature;
  return sample;
}

std::vector<RoadNavigationSample>
finalizeNavPolyline(const std::vector<glm::vec2> &positions, bool closeLoop = false) {
  std::vector<RoadNavigationSample> samples;
  if (positions.size() < 2)
    return samples;

  std::vector<float> arcLength(positions.size(), 0.0f);
  for (size_t i = 1; i < positions.size(); ++i)
    arcLength[i] = arcLength[i - 1] +
                   glm::length(positions[i] - positions[i - 1]);
  const float closingLength = closeLoop
      ? glm::length(positions.front() - positions.back()) : 0.0f;

  samples.reserve(positions.size() + (closeLoop ? 1u : 0u));
  for (size_t i = 0; i < positions.size(); ++i) {
    const size_t previous = closeLoop
        ? (i + positions.size() - 1) % positions.size()
        : (i > 0 ? i - 1 : i);
    const size_t next = closeLoop
        ? (i + 1) % positions.size()
        : std::min(i + 1, positions.size() - 1);
    glm::vec2 direction = positions[next] - positions[previous];
    if (glm::length(direction) < 1e-5f) {
      direction = i + 1 < positions.size()
          ? positions[i + 1] - positions[i]
          : positions[i] - positions[i - 1];
    }

    float curvature = 0.0f;
    if (positions.size() >= 3) {
      // Use the nearest valid triple at open endpoints, and a wrapped triple
      // for closed paths. This gives lanes and Bézier connectors the same
      // signed discrete-curvature convention.
      const size_t center = closeLoop
          ? i
          : std::clamp(i, size_t(1), positions.size() - 2);
      const size_t before = closeLoop
          ? (center + positions.size() - 1) % positions.size() : center - 1;
      const size_t after = closeLoop
          ? (center + 1) % positions.size() : center + 1;
      const glm::vec2 a = positions[center] - positions[before];
      const glm::vec2 b = positions[after] - positions[center];
      const float denominator = glm::length(a) * glm::length(b) *
                                glm::length(positions[after] - positions[before]);
      if (denominator > 1e-5f)
        curvature = 2.0f * navCross2(a, b) / denominator;
    }
    samples.push_back(navSample(arcLength[i], positions[i], direction,
                                curvature));
  }

  if (closeLoop) {
    const RoadNavigationSample &first = samples.front();
    samples.push_back(navSample(arcLength.back() + closingLength,
                                first.position, first.direction,
                                first.curvature));
  }
  return samples;
}

std::vector<RoadNavigationSample> navBezier(const glm::vec2 &p0,
                                            const glm::vec2 &p1,
                                            const glm::vec2 &p2,
                                            const glm::vec2 &p3,
                                            int minimumCount) {
  const float controlLength = glm::length(p1 - p0) +
                               glm::length(p2 - p1) +
                               glm::length(p3 - p2);
  // Parameter-space steps are not arc-length steps, but using the control
  // polygon length gives a stable ~1 m density for the short road curves.
  const int adaptiveCount = (int)std::ceil(controlLength / 1.0f) + 1;
  const int count = std::clamp(std::max(minimumCount, adaptiveCount), 2, 4097);
  std::vector<glm::vec2> positions;
  positions.reserve((size_t)count);
  for (int i = 0; i < count; ++i) {
    const float t = float(i) / float(count - 1), u = 1.0f - t;
    const glm::vec2 p = u * u * u * p0 + 3.0f * u * u * t * p1 +
                        3.0f * u * t * t * p2 + t * t * t * p3;
    positions.push_back(p);
  }
  return finalizeNavPolyline(positions);
}
} // namespace

void RoadNavigationGraph::clear() {
  m_lanes.clear();
  m_laneConnectors.clear();
  m_sidewalks.clear();
  m_walkConnectors.clear();
  m_nodes.clear();
  m_built = false;
}

void RoadNavigationGraph::build(const RoadGraph &roads) {
  clear();

  struct SegmentAnchor {
    float distance;
    uint32_t nodeId;
  };
  struct CrossingCandidate {
    uint32_t segmentA, segmentB;
    float distanceA, distanceB;
    glm::vec2 position;
  };
  std::vector<std::vector<SegmentAnchor>> anchors(roads.segments.size());
  std::vector<float> centerlineLengths(roads.segments.size(), 0.0f);
  for (uint32_t i = 0; i < roads.nodes.size(); ++i) {
    const RoadNode &node = roads.nodes[i];
    m_nodes.push_back({node.id, glm::vec2(node.position.x, node.position.z), false});
  }
  for (uint32_t i = 0; i < roads.segments.size(); ++i) {
    const RoadSegment &seg = roads.segments[i];
    const size_t n = seg.splinePoints.size();
    if (n < 2 || seg.cumulativeDist.size() != n)
      continue;
    centerlineLengths[i] = seg.cumulativeDist.back();
    if (!seg.closed) {
      anchors[i].push_back({0.0f, seg.startNodeId});
      anchors[i].push_back({centerlineLengths[i], seg.endNodeId});
    }
  }

  // A branch often terminates at the edge of a host road, so its endpoint is
  // not close enough to the host's centerline to be found by endpoint clustering.
  // Project transverse endpoints onto the nearest interior spline and reuse the
  // branch endpoint node ID on both roads.
  int endpointToInteriorLinks = 0;
  for (uint32_t branchIndex = 0; branchIndex < roads.segments.size(); ++branchIndex) {
    const RoadSegment &branch = roads.segments[branchIndex];
    if (branch.closed || branch.splinePoints.size() < 2 || branch.tangents.size() < 2)
      continue;
    for (int endpointIndex = 0; endpointIndex < 2; ++endpointIndex) {
      const bool atStart = endpointIndex == 0;
      const uint32_t nodeId = atStart ? branch.startNodeId : branch.endNodeId;
      if (nodeId == kInvalidRoadNodeId)
        continue;
      const size_t branchSample = atStart ? 0 : branch.splinePoints.size() - 1;
      const glm::vec2 branchPos(branch.splinePoints[branchSample].x,
                                branch.splinePoints[branchSample].z);
      const glm::vec2 rawBranchDir(branch.tangents[branchSample].x,
                                   branch.tangents[branchSample].z);
      const float branchDirLen = glm::length(rawBranchDir);
      if (branchDirLen < 1e-5f)
        continue;
      const glm::vec2 branchDir = rawBranchDir / branchDirLen;
      float bestDistance = std::numeric_limits<float>::infinity();
      uint32_t bestHost = kInvalidRoadNodeId;
      float bestHostDistance = 0.0f;

      for (uint32_t hostIndex = 0; hostIndex < roads.segments.size(); ++hostIndex) {
        if (hostIndex == branchIndex)
          continue;
        const RoadSegment &host = roads.segments[hostIndex];
        const size_t hostCount = host.splinePoints.size();
        if (host.closed || hostCount < 2 || host.tangents.size() != hostCount ||
            host.cumulativeDist.size() != hostCount)
          continue;
        const float hostLength = centerlineLengths[hostIndex];
        const float clearance = std::max(4.0f,
            std::min(host.width * 0.5f, 15.0f));
        const float maxDistance = glm::clamp(
            host.width * 0.5f + branch.width * 0.25f + 1.0f, 5.0f, 24.0f);
        for (size_t k = 0; k + 1 < hostCount; ++k) {
          const glm::vec2 a(host.splinePoints[k].x, host.splinePoints[k].z);
          const glm::vec2 b(host.splinePoints[k + 1].x, host.splinePoints[k + 1].z);
          const glm::vec2 edge = b - a;
          const float edgeLen2 = glm::dot(edge, edge);
          if (edgeLen2 < 1e-8f)
            continue;
          const float t = glm::clamp(glm::dot(branchPos - a, edge) / edgeLen2,
                                     0.0f, 1.0f);
          const glm::vec2 projected = a + edge * t;
          const float gap = glm::length(branchPos - projected);
          if (gap >= bestDistance || gap > maxDistance)
            continue;
          const float s = glm::mix(host.cumulativeDist[k],
                                   host.cumulativeDist[k + 1], t);
          if (s <= clearance || hostLength - s <= clearance)
            continue;
          glm::vec2 tangent = glm::mix(
              glm::vec2(host.tangents[k].x, host.tangents[k].z),
              glm::vec2(host.tangents[k + 1].x, host.tangents[k + 1].z), t);
          const float tangentLen = glm::length(tangent);
          if (tangentLen < 1e-5f)
            continue;
          tangent /= tangentLen;
          if (std::fabs(glm::dot(branchDir, tangent)) > kRoadJunctionApproachCos)
            continue;
          bestDistance = gap;
          bestHost = hostIndex;
          bestHostDistance = s;
        }
      }
      if (bestHost != kInvalidRoadNodeId) {
        anchors[bestHost].push_back({bestHostDistance, nodeId});
        ++endpointToInteriorLinks;
      }
    }
  }

  // Detect centerline crossings where neither road ends. Adjacent spline samples
  // are clustered into one navigation junction; existing endpoint nodes are
  // reused when a crossing lands close to one.
  std::vector<CrossingCandidate> interiorCrossings;
  for (uint32_t aIndex = 0; aIndex < roads.segments.size(); ++aIndex) {
    const RoadSegment &aRoad = roads.segments[aIndex];
    if (aRoad.closed || aRoad.splinePoints.size() < 2 ||
        aRoad.cumulativeDist.size() != aRoad.splinePoints.size())
      continue;
    for (uint32_t bIndex = aIndex + 1; bIndex < roads.segments.size(); ++bIndex) {
      const RoadSegment &bRoad = roads.segments[bIndex];
      if (bRoad.closed || bRoad.splinePoints.size() < 2 ||
          bRoad.cumulativeDist.size() != bRoad.splinePoints.size())
        continue;
      for (size_t ai = 0; ai + 1 < aRoad.splinePoints.size(); ++ai) {
        const glm::vec2 a0(aRoad.splinePoints[ai].x, aRoad.splinePoints[ai].z);
        const glm::vec2 a1(aRoad.splinePoints[ai + 1].x, aRoad.splinePoints[ai + 1].z);
        for (size_t bi = 0; bi + 1 < bRoad.splinePoints.size(); ++bi) {
          const glm::vec2 b0(bRoad.splinePoints[bi].x, bRoad.splinePoints[bi].z);
          const glm::vec2 b1(bRoad.splinePoints[bi + 1].x, bRoad.splinePoints[bi + 1].z);
          float ta = 0.0f, tb = 0.0f;
          if (!navSegmentIntersection(a0, a1, b0, b1, ta, tb))
            continue;
          glm::vec2 dirA = a1 - a0, dirB = b1 - b0;
          const float lenA = glm::length(dirA), lenB = glm::length(dirB);
          if (lenA < 1e-5f || lenB < 1e-5f)
            continue;
          dirA /= lenA;
          dirB /= lenB;
          if (std::fabs(glm::dot(dirA, dirB)) > kRoadJunctionApproachCos)
            continue;
          const float sA = glm::mix(aRoad.cumulativeDist[ai],
                                    aRoad.cumulativeDist[ai + 1], ta);
          const float sB = glm::mix(bRoad.cumulativeDist[bi],
                                    bRoad.cumulativeDist[bi + 1], tb);
          const float clearA = std::max(2.0f, std::min(aRoad.width * 0.25f, 10.0f));
          const float clearB = std::max(2.0f, std::min(bRoad.width * 0.25f, 10.0f));
          if (sA <= clearA || centerlineLengths[aIndex] - sA <= clearA ||
              sB <= clearB || centerlineLengths[bIndex] - sB <= clearB)
            continue;
          const glm::vec2 position = a0 + (a1 - a0) * ta;
          uint32_t existingNode = kInvalidRoadNodeId;
          float nearestNodeDistance = kRoadJunctionTolerance;
          for (const RoadNode &node : roads.nodes) {
            const float d = glm::length(position - glm::vec2(node.position.x, node.position.z));
            if (d < nearestNodeDistance) {
              nearestNodeDistance = d;
              existingNode = node.id;
            }
          }
          if (existingNode != kInvalidRoadNodeId) {
            anchors[aIndex].push_back({sA, existingNode});
            anchors[bIndex].push_back({sB, existingNode});
          } else {
            interiorCrossings.push_back({aIndex, bIndex, sA, sB, position});
          }
        }
      }
    }
  }

  struct CrossingCluster {
    glm::vec2 sum{0.0f};
    uint32_t count = 0;
    std::vector<CrossingCandidate> candidates;
  };
  std::vector<CrossingCluster> crossingClusters;
  for (const CrossingCandidate &candidate : interiorCrossings) {
    size_t clusterIndex = crossingClusters.size();
    for (size_t i = 0; i < crossingClusters.size(); ++i) {
      const glm::vec2 center = crossingClusters[i].sum /
                               float(crossingClusters[i].count);
      if (glm::length(candidate.position - center) <= kRoadJunctionTolerance) {
        clusterIndex = i;
        break;
      }
    }
    if (clusterIndex == crossingClusters.size())
      crossingClusters.emplace_back();
    CrossingCluster &cluster = crossingClusters[clusterIndex];
    cluster.sum += candidate.position;
    ++cluster.count;
    cluster.candidates.push_back(candidate);
  }
  for (CrossingCluster &cluster : crossingClusters) {
    const uint32_t nodeId = (uint32_t)m_nodes.size();
    m_nodes.push_back({nodeId, cluster.sum / float(cluster.count), true});
    for (const CrossingCandidate &candidate : cluster.candidates) {
      anchors[candidate.segmentA].push_back({candidate.distanceA, nodeId});
      anchors[candidate.segmentB].push_back({candidate.distanceB, nodeId});
    }
  }

  for (auto &segmentAnchors : anchors) {
    std::stable_sort(segmentAnchors.begin(), segmentAnchors.end(),
        [](const SegmentAnchor &a, const SegmentAnchor &b) {
          return a.distance < b.distance;
        });
    std::vector<SegmentAnchor> unique;
    for (const SegmentAnchor &anchor : segmentAnchors) {
      if (!unique.empty() && std::fabs(unique.back().distance - anchor.distance) < 0.5f) {
        if (unique.back().nodeId == kInvalidRoadNodeId)
          unique.back().nodeId = anchor.nodeId;
        continue;
      }
      unique.push_back(anchor);
    }
    segmentAnchors.swap(unique);
  }

  struct RawRoadSample {
    glm::vec3 center;
    glm::vec3 right;
    glm::vec3 tangent;
    float curvature;
  };
  auto sourceAt = [](const RoadSegment &seg, float distance) {
    const size_t n = seg.splinePoints.size();
    auto upper = std::upper_bound(seg.cumulativeDist.begin(),
                                  seg.cumulativeDist.end(), distance);
    size_t i1 = upper == seg.cumulativeDist.end()
                    ? n - 1 : (size_t)(upper - seg.cumulativeDist.begin());
    size_t i0 = i1 > 0 ? i1 - 1 : 0;
    float span = seg.cumulativeDist[i1] - seg.cumulativeDist[i0];
    float t = span > 1e-6f
                  ? glm::clamp((distance - seg.cumulativeDist[i0]) / span, 0.0f, 1.0f)
                  : 0.0f;
    RawRoadSample raw;
    raw.center = glm::mix(seg.splinePoints[i0], seg.splinePoints[i1], t);
    raw.right = glm::mix(seg.rightVecs[i0], seg.rightVecs[i1], t);
    raw.tangent = glm::mix(seg.tangents[i0], seg.tangents[i1], t);
    raw.curvature = i0 < seg.curvature.size() && i1 < seg.curvature.size()
                        ? glm::mix(seg.curvature[i0], seg.curvature[i1], t) : 0.0f;
    const float rightLength = glm::length(glm::vec2(raw.right.x, raw.right.z));
    if (rightLength > 1e-5f) raw.right /= rightLength;
    const float tangentLength = glm::length(glm::vec2(raw.tangent.x, raw.tangent.z));
    if (tangentLength > 1e-5f) raw.tangent /= tangentLength;
    return raw;
  };
  auto makeOffsetPath = [&](const RoadSegment &seg, float startDistance,
                            float endDistance, float lateralOffset,
                            bool reverse, bool closeLoop) {
    std::vector<float> distances;
    distances.push_back(startDistance);
    // Navigation paths need a finer arc-length table than the road mesh. Keep
    // every evaluated spline sample and add 1 m stations between them so the
    // offset curves do not inherit the mesh's coarser straight-line spacing.
    for (float d = startDistance + 1.0f; d < endDistance - 1e-4f; d += 1.0f)
      distances.push_back(d);
    auto firstInterior = std::upper_bound(seg.cumulativeDist.begin(),
        seg.cumulativeDist.end(), startDistance + 1e-4f);
    auto endInterior = std::lower_bound(seg.cumulativeDist.begin(),
        seg.cumulativeDist.end(), endDistance - 1e-4f);
    for (auto it = firstInterior; it != endInterior; ++it)
      distances.push_back(*it);
    distances.push_back(endDistance);
    std::sort(distances.begin(), distances.end());
    distances.erase(std::unique(distances.begin(), distances.end(),
        [](float a, float b) { return std::fabs(a - b) < 0.05f; }),
        distances.end());
    if (reverse)
      std::reverse(distances.begin(), distances.end());

    std::vector<glm::vec2> positions;
    positions.reserve(distances.size());
    for (float distance : distances) {
      RawRoadSample raw = sourceAt(seg, distance);
      // On bends the mesh narrows the inner side (meshHalfWidth). Squeeze the lane
      // offset by the same ratio so lane centres stay on the carriageway there too.
      float bendScale = 1.0f;
      if (lateralOffset != 0.0f && seg.width > 0.1f && seg.splinePoints.size() >= 3) {
        const size_t sn = seg.splinePoints.size();
        auto up = std::upper_bound(seg.cumulativeDist.begin(),
                                   seg.cumulativeDist.end(), distance);
        const size_t i1 = up == seg.cumulativeDist.end() ? sn - 1
                                                         : size_t(up - seg.cumulativeDist.begin());
        const size_t i0 = i1 > 0 ? i1 - 1 : 0;
        const float fullHalf = seg.width * 0.5f;
        const float sideSign = lateralOffset < 0.0f ? -1.0f : 1.0f;
        const float hwLocal = std::min(
            RoadSpline::meshHalfWidth(seg, int(i0), seg.closed, sideSign, fullHalf),
            RoadSpline::meshHalfWidth(seg, int(i1), seg.closed, sideSign, fullHalf));
        bendScale = glm::clamp(hwLocal / fullHalf, 0.1f, 1.0f);
      }
      const float laneOffset = lateralOffset * bendScale;
      const glm::vec2 pos(raw.center.x + raw.right.x * laneOffset,
                          raw.center.z + raw.right.z * laneOffset);
      if (!positions.empty() && glm::length(pos - positions.back()) < 0.05f)
        continue;
      // A tight inner offset can fold back on itself even when the centreline
      // remains smooth. Remove the cusp sample (and retry the same candidate)
      // until consecutive offset edges no longer reverse direction.
      while (positions.size() >= 2) {
        const glm::vec2 incoming = positions.back() - positions[positions.size() - 2];
        const glm::vec2 outgoing = pos - positions.back();
        if (glm::dot(incoming, outgoing) >= 0.0f)
          break;
        positions.pop_back();
      }
      if (positions.empty() || glm::length(pos - positions.back()) >= 0.05f)
        positions.push_back(pos);
    }
    if (positions.size() < 2)
      return std::vector<RoadNavigationSample>{};

    return finalizeNavPolyline(positions, closeLoop);
  };

  for (uint32_t segmentIndex = 0; segmentIndex < roads.segments.size(); ++segmentIndex) {
    const RoadSegment &seg = roads.segments[segmentIndex];
    const size_t count = seg.splinePoints.size();
    if (count < 2 || seg.tangents.size() != count || seg.rightVecs.size() != count ||
        seg.cumulativeDist.size() != count)
      continue;
    const RoadNavigationDefaults defaults = roadNavigationDefaults(seg);
    const float roadWidth = std::max(seg.width, 2.0f);
    const int lanesPerDirection = std::clamp(defaults.lanesPerDirection, 1, 8);
    const int directionCount = seg.oneWay ? 1 : 2;
    const float carriageWidth = roadWidth * (1.0f - 2.0f * kRoadCurbU);
    const float laneWidth = carriageWidth / float(lanesPerDirection * directionCount);
    const float fullLength = centerlineLengths[segmentIndex];

    std::vector<std::pair<SegmentAnchor, SegmentAnchor>> sections;
    if (seg.closed) {
      sections.push_back({{0.0f, kInvalidRoadNodeId},
                          {fullLength, kInvalidRoadNodeId}});
    } else {
      const auto &segmentAnchors = anchors[segmentIndex];
      for (size_t i = 1; i < segmentAnchors.size(); ++i) {
        if (segmentAnchors[i].distance - segmentAnchors[i - 1].distance > 0.25f)
          sections.push_back({segmentAnchors[i - 1], segmentAnchors[i]});
      }
    }

    for (uint32_t sectionIndex = 0; sectionIndex < sections.size(); ++sectionIndex) {
      const SegmentAnchor &low = sections[sectionIndex].first;
      const SegmentAnchor &high = sections[sectionIndex].second;
      for (int directionIndex = 0; directionIndex < directionCount; ++directionIndex) {
        const bool forward = directionIndex == 0;
        for (int laneIndex = 0; laneIndex < lanesPerDirection; ++laneIndex) {
          const float lateralOffset = seg.oneWay
              ? (-carriageWidth * 0.5f + (float(laneIndex) + 0.5f) * laneWidth)
              : (float(laneIndex) + 0.5f) * laneWidth * (forward ? 1.0f : -1.0f);
          RoadLane lane;
          lane.id = (uint32_t)m_lanes.size();
          lane.segmentIndex = segmentIndex;
          lane.sectionIndex = sectionIndex;
          lane.startNodeId = seg.closed ? kInvalidRoadNodeId
              : (forward ? low.nodeId : high.nodeId);
          lane.endNodeId = seg.closed ? kInvalidRoadNodeId
              : (forward ? high.nodeId : low.nodeId);
          lane.laneIndex = (uint8_t)laneIndex;
          lane.forward = forward;
          lane.closed = seg.closed;
          lane.speedLimit = defaults.speedLimit;
          lane.samples = makeOffsetPath(seg, low.distance, high.distance,
                                        lateralOffset, !forward, seg.closed);
          if (lane.samples.size() >= 2) {
            lane.length = lane.samples.back().distance;
            m_lanes.push_back(std::move(lane));
          }
        }
      }

      if (!defaults.sidewalksEnabled)
        continue;
      for (int side : {-1, 1}) {
        RoadSidewalk walk;
        walk.id = (uint32_t)m_sidewalks.size();
        walk.segmentIndex = segmentIndex;
        walk.sectionIndex = sectionIndex;
        walk.closed = seg.closed;
        walk.startNodeId = seg.closed ? kInvalidRoadNodeId : low.nodeId;
        walk.endNodeId = seg.closed ? kInvalidRoadNodeId : high.nodeId;
        walk.side = (int8_t)side;
        const float offset = float(side) * roadWidth * (0.5f - 0.5f * kRoadCurbU);
        walk.samples = makeOffsetPath(seg, low.distance, high.distance,
                                      offset, false, seg.closed);
        if (walk.samples.size() >= 2) {
          walk.length = walk.samples.back().distance;
          m_sidewalks.push_back(std::move(walk));
        }
      }
    }
  }

  // Smooth directed connectors between lane sections at endpoint and
  // mid-segment junctions.
  std::unordered_map<uint32_t, std::vector<uint32_t>> incomingLanes;
  std::unordered_map<uint32_t, std::vector<uint32_t>> outgoingLanes;
  for (const RoadLane &lane : m_lanes) {
    if (lane.closed)
      continue;
    if (lane.startNodeId != kInvalidRoadNodeId)
      outgoingLanes[lane.startNodeId].push_back(lane.id);
    if (lane.endNodeId != kInvalidRoadNodeId)
      incomingLanes[lane.endNodeId].push_back(lane.id);
  }
  for (const RoadNavigationNode &node : m_nodes) {
    const auto incomingIt = incomingLanes.find(node.id);
    const auto outgoingIt = outgoingLanes.find(node.id);
    if (incomingIt == incomingLanes.end() || outgoingIt == outgoingLanes.end())
      continue;
    for (uint32_t fromIndex : incomingIt->second) {
      const RoadLane &from = m_lanes[fromIndex];
      if (from.samples.empty()) continue;
      for (uint32_t toIndex : outgoingIt->second) {
        const RoadLane &to = m_lanes[toIndex];
        if (to.samples.empty() ||
            (from.segmentIndex == to.segmentIndex &&
             from.sectionIndex == to.sectionIndex))
          continue;
        const glm::vec2 start = from.samples.back().position;
        const glm::vec2 end = to.samples.front().position;
        const float chord = glm::length(end - start);
        if (chord < 0.05f ||
            glm::dot(from.samples.back().direction,
                     to.samples.front().direction) < -0.86f)
          continue;
        const float width = std::max(roads.segments[from.segmentIndex].width, 2.0f);
        const float handle = glm::clamp(chord * 0.42f, 1.0f,
                                        std::max(2.0f, width * 1.5f));
        RoadLaneConnector connector;
        connector.id = (uint32_t)m_laneConnectors.size();
        connector.nodeId = node.id;
        connector.fromLaneId = from.id;
        connector.toLaneId = to.id;
        connector.priority = roadNavigationDefaults(
            roads.segments[from.segmentIndex]).priority;
        connector.turnAngleRadians = std::atan2(
            navCross2(from.samples.back().direction,
                      to.samples.front().direction),
            glm::dot(from.samples.back().direction,
                     to.samples.front().direction));
        connector.samples = navBezier(start,
            start + from.samples.back().direction * handle,
            end - to.samples.front().direction * handle, end, 9);
        if (connector.samples.size() >= 2) {
          m_lanes[fromIndex].outgoingConnectorIds.push_back(connector.id);
          m_laneConnectors.push_back(std::move(connector));
        }
      }
    }
  }

  // Conflicting lane movements are symmetric references for the later traffic
  // reservation system. Shared endpoints are ignored as they are intentional.
  for (size_t a = 0; a < m_laneConnectors.size(); ++a) {
    RoadLaneConnector &ca = m_laneConnectors[a];
    for (size_t b = a + 1; b < m_laneConnectors.size(); ++b) {
      RoadLaneConnector &cb = m_laneConnectors[b];
      if (ca.nodeId != cb.nodeId)
        continue;
      bool conflict = false;
      for (size_t i = 1; i < ca.samples.size() && !conflict; ++i) {
        for (size_t j = 1; j < cb.samples.size(); ++j) {
          if (navSegmentsCross(ca.samples[i - 1].position, ca.samples[i].position,
                               cb.samples[j - 1].position, cb.samples[j].position)) {
            conflict = true;
            break;
          }
        }
      }
      if (conflict) {
        ca.conflictingConnectorIds.push_back(cb.id);
        cb.conflictingConnectorIds.push_back(ca.id);
      }
    }
  }

  struct WalkEndpoint {
    uint32_t sidewalkId, segmentIndex, nodeId;
    bool atStart;
    int8_t side;
    glm::vec2 position, pathDirection;
    float angle;
  };
  std::unordered_map<uint32_t, std::vector<WalkEndpoint>> endpointsByNode;
  for (const RoadSidewalk &walk : m_sidewalks) {
    if (walk.closed || walk.samples.size() < 2)
      continue;
    if (walk.startNodeId != kInvalidRoadNodeId) {
      const auto &sample = walk.samples.front();
      endpointsByNode[walk.startNodeId].push_back(
          {walk.id, walk.segmentIndex, walk.startNodeId, true,
           walk.side, sample.position, sample.direction, 0.0f});
    }
    if (walk.endNodeId != kInvalidRoadNodeId) {
      const auto &sample = walk.samples.back();
      endpointsByNode[walk.endNodeId].push_back(
          {walk.id, walk.segmentIndex, walk.endNodeId, false,
           walk.side, sample.position, sample.direction, 0.0f});
    }
  }
  for (const RoadNavigationNode &node : m_nodes) {
    auto endpointsIt = endpointsByNode.find(node.id);
    if (endpointsIt == endpointsByNode.end())
      continue;
    std::vector<WalkEndpoint> atNode = std::move(endpointsIt->second);
    const glm::vec2 center = node.position;
    for (WalkEndpoint &endpoint : atNode) {
      const glm::vec2 radial = endpoint.position - center;
      endpoint.angle = std::atan2(radial.y, radial.x);
    }
    if (atNode.size() < 2)
      continue;
    std::sort(atNode.begin(), atNode.end(), [](const WalkEndpoint &a,
                                               const WalkEndpoint &b) {
      return a.angle < b.angle;
    });
    const size_t pairCount = atNode.size() == 2 ? 1 : atNode.size();
    for (size_t i = 0; i < pairCount; ++i) {
      const WalkEndpoint &a = atNode[i];
      const WalkEndpoint &b = atNode[(i + 1) % atNode.size()];
      const bool crossing = a.segmentIndex == b.segmentIndex && a.side != b.side;
      const float gap = glm::length(b.position - a.position);
      if (gap < 0.1f || gap > 40.0f)
        continue;
      RoadWalkConnector connector;
      connector.id = (uint32_t)m_walkConnectors.size();
      connector.nodeId = node.id;
      connector.fromSidewalkId = a.sidewalkId;
      connector.toSidewalkId = b.sidewalkId;
      connector.fromAtStart = a.atStart;
      connector.toAtStart = b.atStart;
      connector.crossing = crossing;
      if (crossing) {
        connector.samples = navBezier(a.position,
            glm::mix(a.position, b.position, 1.0f / 3.0f),
            glm::mix(a.position, b.position, 2.0f / 3.0f), b.position, 3);
      } else {
        const float handle = glm::clamp(gap * 0.35f, 0.6f, 6.0f);
        const glm::vec2 aHeading = a.atStart ? -a.pathDirection : a.pathDirection;
        const glm::vec2 bHeading = b.atStart ? b.pathDirection : -b.pathDirection;
        connector.samples = navBezier(a.position, a.position + aHeading * handle,
                                      b.position - bHeading * handle, b.position, 7);
      }
      if (connector.samples.size() >= 2)
        m_walkConnectors.push_back(std::move(connector));
    }
  }

  m_built = true;
  std::cout << "[RoadNavigation] Built " << m_lanes.size() << " lanes, "
            << m_laneConnectors.size() << " lane connectors, "
            << m_sidewalks.size() << " sidewalks, "
            << m_walkConnectors.size() << " sidewalk links" << std::endl;
}

static glm::vec2 catmullRom(const glm::vec2 &p0, const glm::vec2 &p1,
                            const glm::vec2 &p2, const glm::vec2 &p3, float t) {
  float t2 = t * t;
  float t3 = t2 * t;
  return 0.5f * ((2.0f * p1) + (-p0 + p2) * t +
                 (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                 (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

void RoadSpline::evaluate(RoadSegment &seg, const HeightSampler &sampler,
                          float samplesPerMeter) {
  const auto &cps = seg.controlPoints;
  int n = (int)cps.size();
  seg.surfaceBoundsValid = false;
  // Re-evaluation is also used while editing. Never leave a previously valid
  // spline attached to a segment whose control points have since been removed.
  seg.splinePoints.clear();
  seg.tangents.clear();
  seg.rightVecs.clear();
  seg.curvature.clear();
  seg.cumulativeDist.clear();
  if (n < 2)
    return;

  // Collect 2D control points
  std::vector<glm::vec2> pts(n);
  for (int i = 0; i < n; ++i)
    pts[i] = cps[i].pos;

  // Detect a closed loop (round road / roundabout): first control point lands
  // back on the last one. Such roads are rendered as continuous rings.
  //
  // The authored `closed` flag from the JSON wins: a closed segment takes a
  // completely different mesh path (ring, no end caps) and is EXCLUDED from
  // endpoint clustering, so silently promoting an open road to a ring severs it
  // from every junction it was supposed to meet. Only auto-detect a genuine
  // loop, i.e. one where the author actually duplicated the first point at the
  // end.
  const float loopTol = glm::max(1.0f, seg.width * 0.5f);
  if (!seg.closed) {
    seg.closed = (n >= 4 && glm::length(pts.front() - pts.back()) < loopTol);
  }

  // â”€â”€ Adaptive sampling
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  // Straight stretches resample at the requested base step (samplesPerMeter);
  // curved sections (the rounded corners) tessellate by turn angle instead, so
  // tight Medina turns get far more samples than long straight avenues without
  // wasting triangle density on the straights.
  const float baseStep = glm::max(0.5f, 1.0f / glm::max(samplesPerMeter, 0.1f));
  const float maxArcAngle =
      0.045f; // one sample per ~2.6 degrees of corner turn

  auto rsewTo = [&](std::vector<glm::vec2> &out, const glm::vec2 &p,
                    float step) {
    if (out.empty()) {
      out.push_back(p);
      return;
    }
    glm::vec2 a = out.back();
    float len = glm::length(p - a);
    if (len < 0.01f)
      return;
    int c = std::max(1, (int)std::ceil(len / glm::max(step, 0.25f)));
    for (int k = 1; k <= c; ++k) {
      glm::vec2 q = glm::mix(a, p, float(k) / float(c));
      if (glm::length(q - out.back()) > 0.01f)
        out.push_back(q);
    }
  };

  // â”€â”€ Build the smooth centerline path
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  //  * open roads : circular fillet arcs round every interior corner, so the
  //    ribbon edge-lines and curbs curve through the turn like real streets.
  //  * closed loops: sampled around the ring continuously; the seam corner
  //    (between the last and first anchors) is rounded exactly like the others
  //    instead of letting the closing sliver cut straight across the corner.
  std::vector<glm::vec2> path;
  auto emitCorner = [&](std::vector<glm::vec2> &out, const glm::vec2 &a,
                        const glm::vec2 &b, const glm::vec2 &c, float step) {
    // Returns true when the corner at b is rounded by a fillet arc.
    if (glm::length(b - a) < 1e-4f || glm::length(c - b) < 1e-4f)
      return false;
    glm::vec2 u1 = glm::normalize(b - a);
    glm::vec2 u2 = glm::normalize(c - b);
    const float dotC = glm::clamp(glm::dot(u1, u2), -1.0f, 1.0f);
    // The fillet's tangent run. A per-road cornerRadius (the editor's CORNER
    // RADIUS slider) wins when set; otherwise fall back to the automatic
    // half-width so every road that never opts in keeps rounding exactly as it
    // did before. Either way it is capped at half of each adjacent segment,
    // which is what stops two neighbouring corners from eating each other on a
    // short zig-zag.
    float r = 0.0f;
    if (dotC < 0.93f) { // ~21.5 deg: straighter than this is already smooth
      const float cap =
          glm::min(glm::length(b - a) * 0.5f, glm::length(c - b) * 0.5f);
      // Keep the inner road edge from folding into itself. A radius of only
      // half the width collapses the inside edge on a 90-degree bend.
      const float want =
          (seg.cornerRadius > 0.0f) ? seg.cornerRadius : seg.width * 1.5f;
      r = glm::min(want, cap);
    }
    if (r < 0.15f)
      r = 0.0f;
    if (r <= 0.0f)
      return false;
    const glm::vec2 T1 = b - u1 * r; // tangent point on incoming edge
    const glm::vec2 T2 = b + u2 * r; // tangent point on outgoing edge
    rsewTo(out, T1, step);
    const float theta = std::acos(dotC);
    // `r` is the tangent run, not the circle radius. They are equal only at
    // a 90-degree turn. Using r as the arc radius made shallow bends miss T2
    // and created the sharp kinks visible where roads curve and join.
    const float tanHalf = std::tan(theta * 0.5f);
    if (tanHalf < 1e-4f)
      return false;
    const float arcRadius = r / tanHalf;
    // Circle center from the chord T1T2, exact for every turn angle.
    const glm::vec2 mid = (T1 + T2) * 0.5f;
    const glm::vec2 chord = T2 - T1;
    const float halfChord = glm::length(chord) * 0.5f;
    glm::vec2 C;
    if (halfChord <= arcRadius) {
      const float h = std::sqrt(
          std::max(arcRadius * arcRadius - halfChord * halfChord, 0.0f));
      glm::vec2 perp = glm::dot(chord, chord) > 1e-8f
                           ? glm::normalize(glm::vec2(-chord.y, chord.x))
                           : glm::vec2(0.0f);
      glm::vec2 c1 = mid + perp * h;
      glm::vec2 c2 = mid - perp * h;
      C = (glm::dot(c1 - b, c1 - b) > glm::dot(c2 - b, c2 - b))
              ? c1
              : c2; // bulge away from apex
    } else {
      C = mid; // degenerate: straight across
    }
    const float a0 = std::atan2(T1.y - C.y, T1.x - C.x);
    const float ccw = (u1.x * u2.y - u1.y * u2.x >= 0.0f) ? 1.0f : -1.0f;
    const int na = std::max(2, (int)std::ceil(theta / maxArcAngle));
    for (int k = 1; k <= na; ++k) {
      const float an = a0 + ccw * (theta * float(k) / float(na));
      const glm::vec2 p = C + glm::vec2(std::cos(an), std::sin(an)) * arcRadius;
      if (glm::length(p - out.back()) > 0.01f)
        out.push_back(p);
    }
    return true;
  };

  if (seg.closed) {
    // Collect unique loop control points (discard duplicate closing point if
    // present)
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
        const glm::vec2 &a = loopPts[(k - 1 + N) % N];
        const glm::vec2 &b = loopPts[k];
        const glm::vec2 &c = loopPts[(k + 1) % N];
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
      for (const auto &p : pts)
        path.push_back(p);
    }
  } else {
    path.push_back(pts[0]);
    for (int j = 1; j < n - 1; ++j) {
      const glm::vec2 &a = pts[j - 1];
      const glm::vec2 &b = pts[j];
      const glm::vec2 &c = pts[j + 1];
      if (!emitCorner(path, a, b, c, baseStep))
        rsewTo(path, b, baseStep);
    }
    rsewTo(path, pts.back(), baseStep);
  }

  // Sampling at >0.5/m is already done above; store as sampled geometry.
  // Sample heightmap for elevation directly without alteration
  int m = (int)path.size();
  // Duplicate or nearly coincident authored points can collapse during
  // resampling. Such a path has no usable tangent and must not reach the
  // endpoint tangent code below (which requires at least two samples).
  if (m < 2)
    return;
  seg.splinePoints.resize(m);
  for (int i = 0; i < m; ++i) {
    float y = sampler.isValid() ? sampler.renderedGround(path[i].x, path[i].y) : 0.0f;
    seg.splinePoints[i] = glm::vec3(path[i].x, y, path[i].y);
  }

  // Grade the road like a built surface: remove short DEM bumps along the
  // centerline, then enforce a realistic maximum longitudinal grade. The same
  // sampled profile drives the asphalt mesh and terrain bed below it.
  smoothElevation(seg.splinePoints, 0.08f, 5.0f, seg.closed);

  // Compute tangents (central differences, wrapping for closed rings).
  // Keep each tangent oriented with the ordered centreline before smoothing;
  // otherwise a noisy central difference can reverse one frame and flip its
  // perpendicular, folding the road strip between adjacent rings.
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
    glm::vec3 tangent = (len > 1e-6f)
                            ? glm::normalize(glm::vec3(fwd.x, 0.0f, fwd.z))
                            : glm::vec3(1, 0, 0);

    glm::vec3 along;
    if (seg.closed) {
      const int nx = (i + 1) % m;
      along = seg.splinePoints[nx] - seg.splinePoints[i];
    } else if (i + 1 < m) {
      along = seg.splinePoints[i + 1] - seg.splinePoints[i];
    } else {
      along = seg.splinePoints[i] - seg.splinePoints[i - 1];
    }
    if (glm::dot(glm::vec2(tangent.x, tangent.z), glm::vec2(along.x, along.z)) <
        0.0f)
      tangent = -tangent;
    seg.tangents[i] = tangent;
  }

  // Smooth tangents with a wider 5-tap Gaussian-like window. Two passes make
  // the offset frame follow the curve without the short-period waviness that
  // the old 3-tap filter left in the road edges.
  static constexpr float kTangentWeights[5] = {1.0f, 2.0f, 3.0f, 2.0f, 1.0f};
  for (int pass = 0; pass < 2; ++pass) {
    std::vector<glm::vec3> smoothed = seg.tangents;
    for (int i = 0; i < m; ++i) {
      glm::vec3 sum(0.0f);
      float weightSum = 0.0f;
      for (int tap = -2; tap <= 2; ++tap) {
        int sample = i + tap;
        if (seg.closed)
          sample = (sample % m + m) % m;
        else
          sample = std::clamp(sample, 0, m - 1);

        glm::vec3 candidate = seg.tangents[sample];
        // Compare directions against this ring before averaging. This
        // prevents a reversed tangent from cancelling the frame vector.
        if (glm::dot(candidate, seg.tangents[i]) < 0.0f)
          candidate = -candidate;
        const float weight = kTangentWeights[tap + 2];
        sum += candidate * weight;
        weightSum += weight;
      }
      if (weightSum > 0.0f)
        sum /= weightSum;
      const float tLen = glm::length(glm::vec2(sum.x, sum.z));
      smoothed[i] = (tLen > 1e-6f)
                        ? glm::normalize(glm::vec3(sum.x, 0.0f, sum.z))
                        : seg.tangents[i];
    }
    seg.tangents = smoothed;
  }

  // Compute right vectors with horizontal perpendicular (no holonomy, no twist)
  computeRoadFrames(seg.rightVecs, seg.tangents, seg.closed);

  // Cumulative distance
  seg.cumulativeDist.resize(m);
  seg.cumulativeDist[0] = 0.0f;
  for (int i = 1; i < m; ++i) {
    seg.cumulativeDist[i] =
        seg.cumulativeDist[i - 1] +
        glm::length(seg.splinePoints[i] - seg.splinePoints[i - 1]);
  }

  if (!seg.splinePoints.empty()) {
    glm::vec2 minPoint(seg.splinePoints.front().x, seg.splinePoints.front().z);
    glm::vec2 maxPoint = minPoint;
    for (const glm::vec3 &point : seg.splinePoints) {
      minPoint.x = std::min(minPoint.x, point.x);
      minPoint.y = std::min(minPoint.y, point.z);
      maxPoint.x = std::max(maxPoint.x, point.x);
      maxPoint.y = std::max(maxPoint.y, point.z);
    }
    seg.surfaceBounds =
        glm::vec4(minPoint.x, minPoint.y, maxPoint.x, maxPoint.y);
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

void RoadSpline::smoothElevation(std::vector<glm::vec3> &positions,
                                 float maxGrade, float smoothSigma,
                                 bool closed) {
  int n = (int)positions.size();
  if (n < 3)
    return;

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
  const float totalLength =
      arc.back() +
      (closed ? glm::length(glm::vec2(positions.front().x - positions.back().x,
                                      positions.front().z - positions.back().z))
              : 0.0f);
  std::vector<float> smoothedY(n);

  for (int i = 0; i < n; ++i) {
    float sumW = 0.0f, sumY = 0.0f;
    for (int j = 0; j < n; ++j) {
      float d = std::abs(arc[j] - arc[i]);
      if (closed && totalLength > 0.01f)
        d = std::min(d, totalLength - d);
      if (d > radius)
        continue;
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
      if (dist <= 0.01f)
        continue;
      const float maxDy = maxGrade * dist;
      smoothedY[i] = std::clamp(smoothedY[i], smoothedY[i - 1] - maxDy,
                                smoothedY[i - 1] + maxDy);
    }
    const int backwardStart = closed ? 0 : 1;
    for (int i = n - 2; i >= backwardStart; --i) {
      const float dist = arc[i + 1] - arc[i];
      if (dist <= 0.01f)
        continue;
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

// Helper: find nearest point on a polyline and return distance + nearest
// elevation
struct RoadBedSample {
  float centerDistance = 1e9f;
  float edgeDistance = 1e9f;
  float halfWidth = 0.0f;
  float elevation = 0.0f;
};

float RoadSpline::meshHalfWidth(const RoadSegment &road, int idx, bool wrap,
                                float side, float fullHalfWidth) {
  const int n = (int)road.splinePoints.size();
  if (!wrap && (idx <= 0 || idx + 1 >= n))
    return fullHalfWidth;
  const int pr = wrap ? (idx - 1 + n) % n : idx - 1;
  const int nx = wrap ? (idx + 1) % n : idx + 1;
  const glm::vec2 a(road.splinePoints[pr].x, road.splinePoints[pr].z);
  const glm::vec2 b(road.splinePoints[idx].x, road.splinePoints[idx].z);
  const glm::vec2 c(road.splinePoints[nx].x, road.splinePoints[nx].z);
  const glm::vec2 ab = b - a, ac = c - a, bc = c - b;
  const float turn = ab.x * bc.y - ab.y * bc.x;
  if (std::fabs(turn) < 1e-5f || !(side * turn < 0.0f))
    return fullHalfWidth;
  const float area2 = 0.5f * std::fabs(ab.x * ac.y - ab.y * ac.x);
  if (area2 < 1e-6f)
    return fullHalfWidth;
  const float la = glm::length(b - c), lb = glm::length(c - a),
              lc = glm::length(a - b);
  const float rho = (la * lb * lc) / (4.0f * area2);
  // The inside offset must stay inside the curve radius to avoid folding,
  // but it must never exceed the authored half-width. The previous formula
  // returned rho - 0.3 m all the way up to 2x half-width, making a tight
  // curve's inner edge nearly twice as wide and creating the pointed road
  // spikes visible at bends and junctions.
  return std::min(fullHalfWidth, std::max(0.3f, rho - 0.3f));
}

static void accumulateLowestRoadBed(float baseHeight, float candidateBed,
                                    float influence, float &bedOffset,
                                    float &accumulatedInfluence) {
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

static float roadTerrainFalloffWidth(const RoadSegment &road) {
  // Keep the terrain transition proportional to how engineered each road is:
  // major paved routes need a wider tie-in, while narrow alleys and dirt tracks
  // should leave more of the surrounding ground untouched.
  switch (road.type) {
  case RoadType::ColonialAvenue:
    return 5.0f;
  case RoadType::UrbanStreet:
    return 4.0f;
  case RoadType::CountryHighway:
    return 6.0f;
  case RoadType::CountryDirt:
    return 2.5f;
  case RoadType::Port:
    return 4.0f;
  case RoadType::Medina:
    return 1.5f;
  }
  return 4.0f;
}

// Query the ribbon using the same perpendicular frame and bend-width rule as
// RoadMeshBuilder. A centerline-radius test misses the width on outside bends
// and therefore leaves untouched terrain poking over the rendered asphalt at
// corners.
static RoadBedSample nearestRoadBed(const RoadSegment &road, float qx,
                                    float qz) {
  RoadBedSample best;
  const int n = (int)road.splinePoints.size();
  if (n < 2 || road.rightVecs.size() != road.splinePoints.size())
    return best;
  const int edgeCount = road.closed ? n : n - 1;
  for (int i = 0; i < edgeCount; ++i) {
    const int j = (i + 1) % n;
    const glm::vec2 a(road.splinePoints[i].x, road.splinePoints[i].z);
    const glm::vec2 b(road.splinePoints[j].x, road.splinePoints[j].z);
    const glm::vec2 d = b - a;
    const float len2 = glm::dot(d, d);
    if (len2 < 1e-8f)
      continue;
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
    if (rLen < 1e-5f)
      continue;
    const float signedLateral = glm::dot(delta, r2 / rLen);
    const float side = signedLateral < 0.0f ? -1.0f : 1.0f;
    const float hw0 = RoadSpline::meshHalfWidth(road, i, road.closed, side,
                                                road.width * 0.5f);
    const float hw1 = RoadSpline::meshHalfWidth(road, j, road.closed, side,
                                                road.width * 0.5f);
    // The rendered ribbon interpolates two perpendicular cross-sections while
    // this raster query projects onto their centerline chord. A small overlap
    // absorbs that sub-metre projection error so no terrain sliver survives
    // at a sharp inner fold.
    constexpr float kRasterOverlap = 0.8f;
    const float halfWidth = glm::mix(hw0, hw1, t) * rLen + kRasterOverlap;
    const float lateral = std::fabs(signedLateral);
    // Interior spline-ring ends are shared with the adjacent ribbon quad.
    // Only apply longitudinal end-cap distance at the road's true endpoints.
    float capOutside = 0.0f;
    if (!road.closed && i == 0 && rawT < 0.0f)
      capOutside = -rawT * len;
    if (!road.closed && i == n - 2 && rawT > 1.0f)
      capOutside = (rawT - 1.0f) * len;
    const float outsideLateral = std::max(0.0f, lateral - halfWidth);
    const float edgeDistance =
        capOutside > 0.0f
            ? std::max(0.0f,
                       std::sqrt(lateral * lateral + capOutside * capOutside) -
                           halfWidth)
            : outsideLateral;
    best.centerDistance = std::min(best.centerDistance, centerDistance);
    if (edgeDistance < best.edgeDistance) {
      best.edgeDistance = edgeDistance;
      best.halfWidth = halfWidth;
      best.elevation =
          road.splinePoints[i].y * (1.0f - t) + road.splinePoints[j].y * t;
    }
  }
  return best;
}

void RoadDeformer::build(const std::vector<RoadSegment> &segments,
                         const HeightSampler &sampler,
                         const TerrainCoordinateMapper &mapper, int deformW,
                         int deformH) {
  m_mapper = mapper;
  m_mapW = deformW;
  m_mapH = deformH;
  m_deformMap.assign(deformW * deformH, 0.0f);
  m_influenceMap.assign(deformW * deformH, 0.0f);
  m_surfaceMask.assign(deformW * deformH, 0);

  std::cout << "[RoadDeformer] Building deformation field (" << deformW << "x"
            << deformH << ")..." << std::endl;

  float worldW = mapper.bounds.maxX - mapper.bounds.minX;
  float worldH = mapper.bounds.maxZ - mapper.bounds.minZ;
  float pixelSizeX = worldW / float(deformW - 1);
  float pixelSizeZ = worldH / float(deformH - 1);

  // Segment-centric approach: for each road segment, only rasterize pixels
  // within its bounding box + influence radius.  This avoids the O(pixels Ã—
  // segments) brute-force that was causing 60+ second startup times.
  for (const auto &seg : segments) {
    if (seg.splinePoints.empty())
      continue;

    const float halfWidth = seg.width * 0.5f;
    const float shoulderWidth = std::max(seg.shoulderWidth, 0.0f);
    const float falloffWidth = roadTerrainFalloffWidth(seg);
    // The graded bed spans at least a terrain cell either side of the asphalt, so no
    // terrain triangle can span the road from ungraded ground. Overridable for
    // measurement. See kRoadBedFlatReach.
    float roadFlatReach = kRoadBedFlatReach;
    if (const char* env = std::getenv("FALLAGA_ROAD_FLAT")) {
      const float requested = std::strtof(env, nullptr);
      if (requested >= 0.0f) roadFlatReach = requested;
    }
    const float bedHalfWidth = std::max(shoulderWidth, roadFlatReach);
    const float maxReach = halfWidth + bedHalfWidth + falloffWidth;

    // Compute world-space AABB of this segment's spline points + margin
    float segMinX = 1e9f, segMaxX = -1e9f;
    float segMinZ = 1e9f, segMaxZ = -1e9f;
    for (const auto &pt : seg.splinePoints) {
      segMinX = std::min(segMinX, pt.x);
      segMaxX = std::max(segMaxX, pt.x);
      segMinZ = std::min(segMinZ, pt.z);
      segMaxZ = std::max(segMaxZ, pt.z);
    }
    segMinX -= maxReach;
    segMaxX += maxReach;
    segMinZ -= maxReach;
    segMaxZ += maxReach;

    // Convert AABB to pixel range in the deformation grid
    // Column: u = (wx - minX) / worldW  â†’  col = u * (deformW - 1)
    int colMin = std::max(0, (int)floorf((segMinX - mapper.bounds.minX) /
                                         worldW * (deformW - 1)));
    int colMax =
        std::min(deformW - 1, (int)ceilf((segMaxX - mapper.bounds.minX) /
                                         worldW * (deformW - 1)));
    // Row: TerrainManager's row 0 is the south edge (minZ).
    int rowMin = std::max(0, (int)floorf((segMinZ - mapper.bounds.minZ) /
                                         worldH * (deformH - 1)));
    int rowMax =
        std::min(deformH - 1, (int)ceilf((segMaxZ - mapper.bounds.minZ) /
                                         worldH * (deformH - 1)));

    // Rasterize only the local pixel region around this road segment
    for (int row = rowMin; row <= rowMax; ++row) {
      for (int col = colMin; col <= colMax; ++col) {
        // Pixel â†’ World
        float u = float(col) / float(deformW - 1);
        float v = float(row) / float(deformH - 1);
        float wx = mapper.bounds.minX + u * worldW;
        float wz = mapper.bounds.minZ + v * worldH;

        const RoadBedSample bed = nearestRoadBed(seg, wx, wz);
        if (bed.centerDistance > maxReach)
          continue;
        const int idx = row * deformW + col;

        // The terrain shader uses a centre-to-edge material profile.
        // Derive it from the same nearest spline sample and falloff
        // radius as the heightfield so the color boundary cannot drift
        // away from the road grading boundary.
        const float surfaceReach = bed.halfWidth + shoulderWidth + falloffWidth;
        if (surfaceReach > 1e-4f &&
            bed.edgeDistance <= shoulderWidth + falloffWidth) {
          const float centerProfile =
              std::clamp(1.0f - bed.centerDistance / surfaceReach, 0.0f, 1.0f);
          const auto mask = static_cast<unsigned char>(
              std::lround((0.06f + 0.94f * centerProfile) * 255.0f));
          m_surfaceMask[idx] = std::max(m_surfaceMask[idx], mask);
        }

        // Hold the ribbon and authored shoulder at the road-bed profile, then return
        // smoothly to untouched terrain over a road-type-sized falloff. This is the
        // "grade the road bed first, then build the road on it" step: inside the flat
        // zone the ground is set to the road profile LESS a clearance, so the asphalt
        // has prepared earth under it rather than natural terrain crossing it. The
        // ribbon adopts the same profile (kRoadEdgeConform = 1.0), so the two agree
        // across the whole carriageway.
        float influence = 0.0f;
        if (bed.edgeDistance <= bedHalfWidth) {
          influence = 1.0f;
        } else if (falloffWidth > 1e-4f &&
                   bed.edgeDistance <= bedHalfWidth + falloffWidth) {
          float t = (bed.edgeDistance - bedHalfWidth) / falloffWidth;
          t = std::clamp(t, 0.0f, 1.0f);
          influence = 1.0f - (t * t * (3.0f - 2.0f * t)); // smoothstep
        }

        if (influence > 0.001f) {
          float baseH = sampler.renderedGround(wx, wz);
          // Match the actual road ribbon cross-section. RoadMeshBuilder
          // keeps 30% of the DEM's cross-slope at the asphalt edge; using
          // the centreline height here instead flattened the terrain to a
          // different profile than the road and left a visible ledge/seam.
          // Keep the terrain bed slightly below asphalt. Exact coplanar
          // surfaces still let the terrain win depth tests at grazing
          // angles, which shows up as terrain crawling over the road.
          // RoadMeshBuilder adds a 1 cm depth-safe surface bias. Keep
          // the terrain bed at the matching unsurfaced road profile.
          // How far the graded road bed is held below the asphalt. This is the lever
          // that lets a road sit on the ground WITHOUT floating: raising it cuts the bed
          // deeper, so terrain triangles stop leaning through the ribbon, and the road
          // can then be laid on a surface that is genuinely clear of the ground.
          //
          // At 4 cm it was far too small for a 22 m terrain cell. Enlarging the ribbon's
          // clearance clamp instead is what produced floating roads (a mean of 1.17 m,
          // up to 9.95 m), because the clamp lifts each vertex to the highest ground
          // within its reach. Cutting the bed is the correct direction: it removes the
          // ground that would poke through rather than standing the road on stilts.
          //   FALLAGA_ROAD_BED_CLEARANCE=<metres>
          static const float kRoadBedClearance = [] {
            if (const char* e = std::getenv("FALLAGA_ROAD_BED_CLEARANCE")) {
              const float v = std::strtof(e, nullptr);
              if (v >= 0.0f) return v;
            }
            return 0.04f;
          }();
          const float roadBedH = bed.elevation +
                                 (baseH - bed.elevation) * kRoadEdgeConform -
                                 kRoadBedClearance;
          // At crossings, constrain the terrain below the lowest
          // overlapping road surface. Choosing only the first/equal
          // influence made the bed alternate above and below roads
          // depending on segment order.
          accumulateLowestRoadBed(baseH, roadBedH, influence, m_deformMap[idx],
                                  m_influenceMap[idx]);
        }
      }
    }
  }

  // Count affected pixels
  int affected = 0;
  for (int i = 0; i < deformW * deformH; ++i) {
    if (m_influenceMap[i] > 0.001f)
      affected++;
  }

  std::cout << "[RoadDeformer] Done. " << affected << " pixels affected ("
            << (100.0f * affected / (deformW * deformH)) << "%)" << std::endl;
}

float RoadDeformer::getDeformation(float x, float z) const {
  if (m_deformMap.empty())
    return 0.0f;

  float ux = (x - m_mapper.bounds.minX) /
             (m_mapper.bounds.maxX - m_mapper.bounds.minX);
  float uz = (z - m_mapper.bounds.minZ) /
             (m_mapper.bounds.maxZ - m_mapper.bounds.minZ);
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

  return d00 * (1 - tx) * (1 - tz) + d10 * tx * (1 - tz) + d01 * (1 - tx) * tz +
         d11 * tx * tz;
}

float RoadDeformer::getRoadInfluence(float x, float z) const {
  if (m_influenceMap.empty())
    return 0.0f;

  float ux = (x - m_mapper.bounds.minX) /
             (m_mapper.bounds.maxX - m_mapper.bounds.minX);
  float uz = (z - m_mapper.bounds.minZ) /
             (m_mapper.bounds.maxZ - m_mapper.bounds.minZ);
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

  return i00 * (1 - tx) * (1 - tz) + i10 * tx * (1 - tz) + i01 * (1 - tx) * tz +
         i11 * tx * tz;
}

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  RoadTileField â€” 4-direction auto-tiling field derived from road splines
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

void RoadTileField::build(const std::vector<RoadSegment> &segments,
                          const TerrainCoordinateMapper &mapper, float cellSize,
                          int maxResolution) {
  m_cellSize = std::max(0.5f, cellSize);
  m_map = {mapper.bounds.minX, mapper.bounds.maxX, mapper.bounds.minZ,
           mapper.bounds.maxZ, mapper.flipHeightmapZ};

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
  struct SpanRect {
    int c0, r0, c1, r1;
  };
  std::vector<SpanRect> dirty;
  dirty.reserve(segments.size() * 4);
  for (const auto &seg : segments) {
    const int n = (int)seg.splinePoints.size();
    if (n < 2)
      continue;

    const uint8_t stamp = (uint8_t)((int)seg.type + 1);
    const int spanCount = seg.closed ? n : n - 1;
    for (int i = 0; i < spanCount; ++i) {
      const glm::vec3 &a = seg.splinePoints[i];
      const glm::vec3 &b = seg.splinePoints[(i + 1) % n];
      int c0 = INT_MAX, r0 = INT_MAX, c1 = INT_MIN, r1 = INT_MIN;
      RoadTile::walkRibbon(m_map, w, h, a.x, a.z, b.x, b.z,
                           [&](int col, int row) {
                             c0 = std::min(c0, col);
                             c1 = std::max(c1, col);
                             r0 = std::min(r0, row);
                             r1 = std::max(r1, row);
                             m_grid.stampOccupancy(col, row, stamp);
                           });
      if (c1 >= c0)
        dirty.push_back({c0, r0, c1, r1}); // empty if out of bounds
    }
  }

  for (const SpanRect &r : dirty)
    m_grid.recomputeMasksInRect(r.c0 - 1, r.r0 - 1, r.c1 + 1, r.r1 + 1);
  m_grid.rebuildHistogram();

  std::cout << "[RoadTileField] " << w << "x" << h << " cells (" << m_cellSize
            << " m), " << m_grid.roadCellCount()
            << " road cells: " << shapeSummary() << std::endl;

  if (std::getenv("FALLAGA_ROADTILES"))
    m_grid.debugPrint();
}

bool RoadTileField::isRoadAt(float x, float z) const {
  if (!isBuilt())
    return false;
  return m_grid.isRoad(m_map.colFor(x, m_grid.width()),
                       m_map.rowFor(z, m_grid.height()));
}

uint8_t RoadTileField::maskAt(float x, float z) const {
  if (!isBuilt())
    return 0;
  return m_grid.maskAt(m_map.colFor(x, m_grid.width()),
                       m_map.rowFor(z, m_grid.height()));
}

std::optional<RoadTile::Shape> RoadTileField::shapeAt(float x, float z) const {
  if (!isBuilt())
    return std::nullopt;
  return m_grid.shapeAt(m_map.colFor(x, m_grid.width()),
                        m_map.rowFor(z, m_grid.height()));
}

int RoadTileField::armCountAt(float x, float z) const {
  return RoadTile::armCount(maskAt(x, z));
}

bool RoadTileField::isConnectedTowards(float x, float z,
                                       uint8_t direction) const {
  return (maskAt(x, z) & (direction & RoadTile::All)) != 0;
}

std::string RoadTileField::shapeSummary() const {
  auto n = [this](RoadTile::Shape s) { return m_grid.countShape(s); };
  std::string out;
  auto add = [&out](const char *label, int count) {
    if (count <= 0)
      return;
    if (!out.empty())
      out += ", ";
    out += std::string(label) + " " + std::to_string(count);
  };
  add("straights",
      n(RoadTile::Shape::Straight_NS) + n(RoadTile::Shape::Straight_EW));
  add("corners", n(RoadTile::Shape::Corner_NE) + n(RoadTile::Shape::Corner_SE) +
                     n(RoadTile::Shape::Corner_SW) +
                     n(RoadTile::Shape::Corner_NW));
  add("T-junctions", n(RoadTile::Shape::TJunction_NoN) +
                         n(RoadTile::Shape::TJunction_NoE) +
                         n(RoadTile::Shape::TJunction_NoS) +
                         n(RoadTile::Shape::TJunction_NoW));
  add("crossroads", n(RoadTile::Shape::Crossroad));
  add("dead-ends",
      n(RoadTile::Shape::DeadEnd_N) + n(RoadTile::Shape::DeadEnd_E) +
          n(RoadTile::Shape::DeadEnd_S) + n(RoadTile::Shape::DeadEnd_W));
  add("isolated", n(RoadTile::Shape::Single));
  if (out.empty())
    out = "no road cells";
  return out;
}

// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  RoadMeshBuilder â€” Runtime ribbon mesh generation
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

RoadMeshBuilder::~RoadMeshBuilder() { destroy(); }

// ── Rounded junction correction ──────────────────────────────────────────────
// A road meeting another road is not a question of yes or no, it is a question
// of how much the direction has to change. Given a branch that arrives at
// direction d1 and a host travelling at d2, the correction angle is the turn
// between them:
//
//     theta = 0    the two are already aligned    -> no correction, run
//     straight in theta = 90   a square T-junction           -> a tight fillet
//     theta -> 180 a hairpin / parallel overlap  -> a very large, sweeping
//     fillet
//
// The fillet radius follows the standard road-corner relation: a turn of theta
// is rounded over a tangent run of t = R * tan(theta/2). Holding that tangent
// run to a fraction of the road width - rather than holding R - is what keeps
// the correction "small": a sharp corner gets a tight arc, a shallow one gets a
// long shallow sweep, and neither can grow without bound. R is then clamped so
// a near-straight join degenerates gracefully to no arc at all.
static float junctionFilletRadius(float theta, float tangentRun) {
  const float half = theta * 0.5f;
  const float t = std::tan(half);
  if (t < 1e-4f)
    return tangentRun * 4.0f; // ~straight: no meaningful fillet
  return tangentRun / t;
}

// Sample a circular arc from a to b, bulging toward `bulge`, into `out`. Used
// to replace the sharp corner between a branch and its host with a rounded one.
static void appendCornerArc(std::vector<glm::vec2> &out, const glm::vec2 &a,
                            const glm::vec2 &b, const glm::vec2 &bulge,
                            int steps) {
  // Circumcentre of the circle through a, b with the given bulge direction.
  const glm::vec2 mid = (a + b) * 0.5f;
  const glm::vec2 chord = b - a;
  const float chordLen = glm::length(chord);
  if (chordLen < 1e-4f) {
    out.push_back(b);
    return;
  }
  const glm::vec2 perp = glm::vec2(-chord.y, chord.x) / chordLen;

  // Sagitta sets the radius. Positive sagitta bulges toward `perp`; the
  // caller's bulge vector only chooses the side.
  const float side = (glm::dot(bulge, perp) >= 0.0f) ? 1.0f : -1.0f;
  const float sag = chordLen * 0.18f;
  const float R = (sag * sag + 0.25f * chordLen * chordLen) / (2.0f * sag);
  const glm::vec2 centre = mid - perp * (side * (R - sag));

  const glm::vec2 v0 = a - centre, v1 = b - centre;
  constexpr float kTwoPi = 6.28318530718f;
  float a0 = std::atan2(v0.y, v0.x), a1 = std::atan2(v1.y, v1.x);
  float da = a1 - a0;
  while (da > 3.14159265f)
    da -= kTwoPi;
  while (da < -3.14159265f)
    da += kTwoPi;

  for (int i = 1; i <= steps; ++i) {
    const float ang = a0 + da * (float(i) / float(steps));
    out.push_back(centre + glm::vec2(std::cos(ang), std::sin(ang)) * R);
  }
}

void RoadMeshBuilder::destroy() {
  if (m_VAO) {
    glDeleteVertexArrays(1, &m_VAO);
    m_VAO = 0;
  }
  if (m_VBO) {
    glDeleteBuffers(1, &m_VBO);
    m_VBO = 0;
  }
  if (m_EBO) {
    glDeleteBuffers(1, &m_EBO);
    m_EBO = 0;
  }
  m_indexCount = 0;
  m_vertices.clear();
  m_indices.clear();
}

// Pull the ends of every road in a junction cluster to one shared height, blending back
// to the road's own profile over a short distance.
//
// Why: the ribbon takes each endpoint's height from the terrain under that endpoint, and
// junction pads, gap-splice joints and caps each compute their height independently. Two
// roads meeting at a junction therefore arrive at whatever height the ground happens to
// be at each of their ends. Measured on the shipped network the spread reaches 90.5 mm at
// the 4-way junction, which is well beyond the 35 mm Z_BIAS and so shows as a step or a
// patch at the join.
//
// The shared height is the HIGHEST end in the cluster rather than the mean. Nothing may
// be lowered: the ribbon keeps a minimum clearance over the drawn terrain, so an arm
// pulled below its neighbour would leave that neighbour's asphalt proud, and the clamp
// would push it back up anyway. Taking the maximum makes every arm meet the highest one,
// which is also the direction the clamp already moves.
//
// Road positions are untouched -- only Y is adjusted, and only within `blend` metres of
// the shared endpoint -- so the authored layout, widths and topology are unchanged.
void unifyJunctionHeights(std::vector<RoadSegment> &segments, float tolerance,
                          float blend) {
  struct End {
    int seg;
    bool back;
  };
  std::vector<End> ends;
  for (int s = 0; s < (int)segments.size(); ++s) {
    if (segments[s].splinePoints.size() < 2)
      continue;
    ends.push_back({s, false});
    ends.push_back({s, true});
  }
  if (ends.empty())
    return;

  auto posOf = [&](const End &e) -> const glm::vec3 & {
    return e.back ? segments[e.seg].splinePoints.back()
                  : segments[e.seg].splinePoints.front();
  };

  // Cluster by proximity, the same rule the topology and the mesh already use.
  std::vector<int> parent(ends.size());
  for (size_t i = 0; i < parent.size(); ++i)
    parent[i] = (int)i;
  std::function<int(int)> find = [&](int a) {
    while (parent[a] != a) {
      parent[a] = parent[parent[a]];
      a = parent[a];
    }
    return a;
  };
  for (size_t i = 0; i < ends.size(); ++i)
    for (size_t j = i + 1; j < ends.size(); ++j) {
      const glm::vec3 &a = posOf(ends[i]);
      const glm::vec3 &b = posOf(ends[j]);
      const float d = std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z));
      if (d <= tolerance)
        parent[find((int)i)] = find((int)j);
    }

  // One shared height per multi-arm cluster.
  std::vector<float> shared(ends.size(), 0.0f);
  std::vector<char> hasShared(ends.size(), 0);
  for (size_t i = 0; i < ends.size(); ++i) {
    if (find((int)i) != (int)i)
      continue;
    float hi = -1e30f;
    int members = 0;
    for (size_t j = 0; j < ends.size(); ++j) {
      if (find((int)j) != (int)i)
        continue;
      hi = std::max(hi, posOf(ends[j]).y);
      ++members;
    }
    if (members < 2)
      continue; // a dead end has nothing to reconcile
    for (size_t j = 0; j < ends.size(); ++j)
      if (find((int)j) == (int)i) {
        shared[j] = hi;
        hasShared[j] = 1;
      }
  }

  // Apply, blending to the road's own profile so no kink is introduced.
  for (size_t i = 0; i < ends.size(); ++i) {
    if (!hasShared[i])
      continue;
    std::vector<glm::vec3> &pts = segments[ends[i].seg].splinePoints;
    const int n = (int)pts.size();
    const float delta = shared[i] - (ends[i].back ? pts.back().y : pts.front().y);
    if (std::fabs(delta) < 1e-6f)
      continue;
    // Metres from this end to point k, using the segment's own cumulative distances
    // when they are available (they are, after RoadSpline::evaluate), so the blend
    // length is a real distance rather than a point count.
    const std::vector<float> &cum = segments[ends[i].seg].cumulativeDist;
    const bool haveCum = cum.size() == (size_t)n && n > 1;
    const float endDist = ends[i].back ? (haveCum ? cum[n - 1] : 0.0f) : 0.0f;
    for (int k = 0; k < n; ++k) {
      const int idx = ends[i].back ? n - 1 - k : k;
      const float metres = haveCum ? std::fabs(cum[idx] - endDist) : (float)k;
      if (metres >= blend)
        break;
      const float w = 1.0f - metres / blend;           // 1 at the end, 0 at `blend`
      const float smooth = w * w * (3.0f - 2.0f * w);  // smoothstep, no kink
      pts[idx].y += delta * smooth;
    }
  }
}

void RoadMeshBuilder::build(std::vector<RoadSegment> segments,
                            const HeightSampler &sampler,
                            const RoadDeformer *deformer, float snapMargin,
                            bool addCrown) {
  m_vertices.clear();
  m_indices.clear();
  m_joinShaderStrokes.clear();
  m_joinShaderContacts.clear();
  m_ribbonMetricUV.clear();
  m_shoulderQuads = 0;

  // Bring every junction's arms to one height before anything is emitted, so the ribbon,
  // the caps, the pads and the joint bridges all read the same value. This only adjusts
  // Y, within kJunctionHeightBlend metres of a shared endpoint; the authored layout is
  // untouched.
  unifyJunctionHeights(segments, kRoadJunctionTolerance, kJunctionHeightBlend);

  // Roads share a height at crossings, so their fragments can have identical
  // depth. With GL_LEQUAL the later ribbon owns those pixels. Draw
  // lower-priority surface styles first and the dominant road last to avoid
  // texture z-fighting. RoadType ordering is used only for render priority; it
  // does not affect joins.
  std::stable_sort(segments.begin(), segments.end(),
                   [](const RoadSegment &a, const RoadSegment &b) {
                     if (a.type != b.type)
                       return (int)a.type > (int)b.type;
                     return a.id < b.id;
                   });

  // Keep the road bed close to the sampled terrain. Road-over-road depth
  // ordering is handled in clip space by street.vert; lifting the whole ribbon
  // by 25 cm left an obvious raised slab along otherwise flat ground. A 1 cm
  // lift is enough because the terrain bed is conformed just below the ribbon
  // and the draw pass also uses polygon offset to resolve depth ties.
  const float Z_BIAS = 0.035f;
  const float crownHeight = addCrown ? RoadSpline::kCrownHeight : 0.0f;
  const float TEX_SCALE = 0.20f; // UV repeat scale along road
  // How much of the terrain cross-slope the road bed adopts. 1.0 makes the
  // ribbon twist with the ground (both rims ride separate terrain heights ->
  // the visible "melted edge" on every side slope); 0.0 leaves the road dead
  // flat and floating above cuts. 0.3 keeps the bed close to the ground while
  // cutting the twist ~3x.
  const float EDGE_CONFORM = [] {
    if (const char* env = std::getenv("FALLAGA_ROAD_EDGE_CONFORM")) {
      const float requested = std::strtof(env, nullptr);
      if (requested >= 0.0f && requested <= 1.0f) return requested;
    }
    return kRoadEdgeConform;
  }();

  // Minimum vertical clearance the ribbon keeps over the sampled ground. A terrain
  // cell is ~22 m across on the shipped DEM while roads are 8-40 m wide, and a rim
  // pillar only adopts EDGE_CONFORM of its own ground height, so a terrain triangle
  // leaning over the asphalt can still stand above the ribbon. That is the "roads
  // going under the terrain" defect: the ground renders over the asphalt.
  const float kMinClearance = 0.02f;
  // Lowest y a road vertex may take at this XZ so the asphalt stays visible.
  auto minSurfaceY = [&](const glm::vec3 &at, float surfaceY) {
    if (!sampler.isValid())
      return surfaceY;
    // renderedGround is the drawn terrain surface and already contains the baked road
    // bed offset, so there is no deformation term here. Adding one double-counted the
    // offset and pushed the asphalt under the ground.
    const float ground = sampler.renderedGround(at.x, at.z);
    return std::max(surfaceY, ground + kMinClearance);
  };

  // Lowest y a road vertex may take so that the terrain cannot stand above ANY part of
  // its triangles, not just the vertex point.
  //
  // Why the vertex test is not enough: the ribbon is generated from the drawn terrain
  // at its own vertex positions, but a road triangle spans several 22 m terrain cells.
  // Measured on the shipped DEM, the drawn terrain rises up to 0.53 m above the road
  // triangle at 0.37% of sample points even with the vertex clamp working, because a
  // terrain triangle leaning through the ribbon's interior is higher than the ground
  // sampled under the ribbon's own corners.
  //
  // The clearance must therefore be taken over the triangle's footprint. For a triangle
  // the ground maximum cannot exceed the maximum over its vertices, so sampling the
  // three vertices plus the centroid is sufficient; the caller passes the half-extents
  // of the triangles the vertex belongs to, and the offsets below are scaled to them.
  auto groundMaxOverFootprint = [&](const glm::vec3 &at, float halfAlong, float halfAcross) {
  if (!sampler.isValid())
    return at.y;
  float g = sampler.renderedGround(at.x, at.z);
  const float r = std::max(halfAlong, halfAcross);
  if (r <= 1e-4f)
    return g;
  for (int k = -4; k <= 4; ++k)
    for (int m = -4; m <= 4; ++m) {
      if (k == 0 && m == 0) continue;
      g = std::max(g, sampler.renderedGround(at.x + 0.25f * (float)m * halfAcross,
                                             at.z + 0.25f * (float)k * halfAlong));
    }
  return g;
  };
  // Clamp a ribbon vertex against its whole footprint. halfAlong is roughly the ring
  // spacing along the road, halfAcross roughly the pillar gap across it.
  //
  // The reach is floored at half a terrain cell. That is the number that matters: the
  // terrain cell is ~22.25 m, so a terrain triangle leaning through the ribbon's
  // interior can be a full cell away from the vertex being clamped. With a reach that
  // followed only the ribbon's own ring/pillar spacing, 2640 of 1.6 M sample points
  // still had ground above the asphalt; covering a cell is what actually removes it.
  auto minSurfaceYFootprint = [&](const glm::vec3 &at, float surfaceY,
                                  float halfAlong, float halfAcross) {
    if (!sampler.isValid())
      return surfaceY;
    // The search reach trades two opposite failure modes against each other, so it is
    // the one number that decides whether roads look buried or look like they float:
    //   too small -> a terrain triangle leaning through the ribbon's interior stands
    //                above the asphalt ("terrain over the road");
    //   too large -> the vertex is raised to the highest ground within the reach, so a
    //                road on a slope is lifted onto a plane above its surroundings and
    //                visibly hovers.
    //
    // A reach of half a terrain cell (11 m) is far too large: measured on the shipped
    // network it lifted the ribbon by a mean of 1.17 m and up to 9.95 m, which is the
    // floating road. The ribbon's own triangle extent is the ring step along the road and
    // the pillar step across it, so that is the honest search distance -- extending
    // beyond the triangles a vertex belongs to raises it for terrain it never covers.
    // A terrain triangle can lean into the ribbon from up to one cell away. The DEM is
    // ~11 m per cell, so a footprint smaller than half a cell can miss the high corner
    // that buries the asphalt on a coastal cut (the port rim measured 0.41 m under).
    const float cellReach = 0.5f * std::max(sampler.cellSizeX(), sampler.cellSizeZ());
    const float ground = groundMaxOverFootprint(
        at, std::max(halfAlong, cellReach), std::max(halfAcross, cellReach));
    return std::max(surfaceY, ground + kMinClearance);
  };
  // Endpoint clustering tolerance, metres. Shared with RoadGraph::buildTopology
  // so the reported topology and the drawn junctions always agree.
  const float JN_TOL = kRoadJunctionTolerance;
  // A road only forms a junction with a host it arrives at transversely, the
  // same test the mid-span crossing detector uses. See the constant for the
  // reasoning.
  const float kJunctionApproachCos = kRoadJunctionApproachCos;
  const float PI = 3.14159265f;

  // Dense cross-section so lane markings / edges get crisp geometry.
  // Vertex pillars sit exactly on marking boundaries (u across road).
  static const float kCross[] = {
      0.000f, // left rim
      0.055f, // left edge-line outer
      0.105f, // left edge-line inner
      0.146f, // left kerb, sidewalk side (raised)   -- brackets CURB_U = 0.15
      0.154f, // left kerb, carriageway side (ground)
      0.435f, // center zone
      0.455f, // double-yellow left outer
      0.495f, // double-yellow left inner
      0.505f, // double-yellow right inner
      0.545f, // double-yellow right outer
      0.565f, // center zone
      0.846f, // right kerb, carriageway side (ground)
      0.854f, // right kerb, sidewalk side (raised)
      0.895f, // right edge-line inner
      0.945f, // right edge-line outer
      1.000f  // right rim
  };
  static const int kCrossCount = (int)(sizeof(kCross) / sizeof(kCross[0]));

  const int S = (int)segments.size();
  if (S == 0) {
    m_indexCount = 0;
    return;
  }

  auto segLen = [&](const RoadSegment &s) {
    if (s.cumulativeDist.size() < 2)
      return 0.0f;
    return s.cumulativeDist.back() - s.cumulativeDist[0];
  };
  auto isPaved = [&](int t) { return t == 0 || t == 1 || t == 4; };

  // Bit 0/1 marks the specific start/end that joins a host road, so only that
  // endpoint loses its dead-end cap.
  std::vector<unsigned char> branchAttachedEnds(S, 0);
  struct BranchContact {
    int seg, end, host, hostSample;
    glm::vec2 position;     // branch centreline at the host edge stripe
    glm::vec2 hostPosition; // closest point on host centreline
    glm::vec2 hostTangent;
    float hostSide;
  };
  std::vector<BranchContact> branchContacts;
  auto branchFilletRadius = [&](const BranchContact &contact) {
    const float branchWidth = segments[contact.seg].width;
    const float hostWidth = segments[contact.host].width;
    const RoadSegment &branch = segments[contact.seg];
    const float branchLength =
        branch.cumulativeDist.size() >= 2
            ? branch.cumulativeDist.back() - branch.cumulativeDist.front()
            : 0.0f;
    // Match the round footprint and the border curves to one shared quarter-
    // circle. A radius no larger than the branch half-width keeps the two
    // corners from crossing through one another at a narrow T mouth.
    const float designRadius = std::min(branchWidth * 0.48f, hostWidth * 0.45f);
    const float availableRun = std::max(0.25f, branchLength * 0.45f);
    return std::min({designRadius, availableRun, 8.0f});
  };

  // A paved junction pad: the flat polygon that fills the void where ribbons
  // meet. Emitted for mid-span crossings AND for every branch mouth retracted
  // onto a host in step 0, so a connection is never left as a bare gap.
  struct CrossSite {
    glm::vec2 c;
    float r = 0.0f;
    std::vector<std::pair<int, int>> members; // (segment, nearest sample)
    bool midSpan = false;
  };
  std::vector<CrossSite> crossSites;

  // â”€â”€â”€ 0. Branch alignment to host edge
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  // An open endpoint that lands INSIDE another road's corridor is aligned
  // to that road's near edge corridor, so the branch fuses edge-to-edge
  // with the host without burying or twisting its ribbon.
  // Road type describes surface style, not connectivity priority. Each endpoint
  // may join any road it approaches transversely.
  for (int s = 0; s < S; ++s) {
    RoadSegment &seg = segments[s];
    const int n = (int)seg.splinePoints.size();
    if (seg.closed || n < 2)
      continue;

    for (int end = 0; end < 2; ++end) {
      const glm::vec2 pos = (end == 0) ? glm::vec2(seg.splinePoints.front().x,
                                                   seg.splinePoints.front().z)
                                       : glm::vec2(seg.splinePoints.back().x,
                                                   seg.splinePoints.back().z);

      // Check if already shared with another endpoint
      bool sharedEnd = false;
      for (int h = 0; h < S && !sharedEnd; ++h) {
        const RoadSegment &ho = segments[h];
        if (h == s || ho.splinePoints.size() < 2)
          continue;
        glm::vec2 a(ho.splinePoints.front().x, ho.splinePoints.front().z);
        glm::vec2 b(ho.splinePoints.back().x, ho.splinePoints.back().z);
        if (glm::length(a - pos) <= JN_TOL + 0.05f ||
            glm::length(b - pos) <= JN_TOL + 0.05f)
          sharedEnd = true;
      }
      if (sharedEnd)
        continue;

      // Check if endpoint lands inside another road's corridor (T-junction)
      // Host = closest matching spline sample across ALL candidates,
      // with no road-type restriction; type controls appearance only.
      //
      // The reach is the host's own width, so a street that stops a lane short
      // of a boulevard still welds on instead of ending in mid-air with a
      // rounded cap. Reach alone is not enough: it also catches a road merely
      // running ALONGSIDE another. A junction only exists where the branch
      // arrives transversely, so the approach angle decides it.
      const glm::vec2 branchOut =
          (end == 0)
              ? -glm::vec2(seg.tangents.front().x, seg.tangents.front().z)
              : glm::vec2(seg.tangents.back().x, seg.tangents.back().z);
      const float branchLen = glm::length(branchOut);
      if (branchLen < 1e-4f)
        continue;
      const glm::vec2 branchDir = branchOut / branchLen;

      // A junction exists where the two roads' paved ends actually overlap.
      // Reach is the host's own half-width plus a hair - the distance at which
      // the branch's tarmac meets the host's tarmac. Testing against a road's
      // full width instead would join two roads merely running side by side,
      // which overlap along their whole length and meet at no point on it.
      // Require a transverse approach. End-to-end continuations are handled
      // by endpoint clustering and gap splicing instead.
      const float kJunctionEpsilon = 0.5f;
      int hSeg = -1;
      float hDist = 1e9f;
      int hSample = -1;
      glm::vec2 hostPosition(0.0f);
      glm::vec2 hostRight(1.0f, 0.0f);
      glm::vec2 hostTangent(0.0f, 1.0f);
      for (int h = 0; h < S; ++h) {
        if (h == s)
          continue;
        const RoadSegment &ho = segments[h];
        if (ho.splinePoints.size() < 2)
          continue;
        // Allow the branch endpoint to be one branch-width short of the
        // host curb. The old half-width-only radius left a visible capped
        // stub when the pavement was close but its centreline was outside
        // the corridor test. The transverse test below prevents side-by-side
        // roads from snapping together.
        const float reach = ho.width * 0.5f + seg.width + kJunctionEpsilon;
        const int hostPointCount = (int)ho.splinePoints.size();
        const int hostEdgeCount =
            ho.closed ? hostPointCount : hostPointCount - 1;
        for (int k = 0; k < hostEdgeCount; ++k) {
          const int next = (k + 1) % hostPointCount;
          const glm::vec2 a(ho.splinePoints[k].x, ho.splinePoints[k].z);
          const glm::vec2 b(ho.splinePoints[next].x, ho.splinePoints[next].z);
          const glm::vec2 ab = b - a;
          const float ab2 = glm::dot(ab, ab);
          if (ab2 < 1e-8f)
            continue;
          const float t = glm::clamp(glm::dot(pos - a, ab) / ab2, 0.0f, 1.0f);
          const glm::vec2 closest = a + ab * t;
          const float d = glm::length(closest - pos);
          if (d >= reach || d >= hDist)
            continue;
          const glm::vec2 ht = glm::normalize(ab);
          if (std::fabs(glm::dot(branchDir, ht)) > kJunctionApproachCos)
            continue;
          hDist = d;
          hSeg = h;
          hSample = (t < 0.5f) ? k : next;
          hostPosition = closest;
          // Match computeRoadFrames' XZ right-vector convention.
          hostRight = glm::vec2(ht.y, -ht.x);
          hostTangent = ht;
        }
      }
      if (hSeg < 0)
        continue;

      // branchDir is the outward direction beyond the road endpoint, so the
      // branch's interior lies in the opposite direction. Use that interior
      // direction to select the host's near edge; using branchDir itself
      // selects the far edge and drags the branch across the whole host road.
      const glm::vec2 branchInteriorDir = -branchDir;
      const float side =
          glm::dot(branchInteriorDir, hostRight) >= 0.0f ? 1.0f : -1.0f;
      const glm::vec2 hostEdge =
          hostPosition +
          hostRight * (side * roadWhiteEdgeLineOffset(segments[hSeg].width));
      if (end == 0)
        seg.controlPoints.front().pos = hostEdge;
      else
        seg.controlPoints.back().pos = hostEdge;
      RoadSpline::evaluate(seg, sampler, 0.5f);
      const glm::vec2 joinedPos =
          end == 0
              ? glm::vec2(seg.splinePoints.front().x,
                          seg.splinePoints.front().z)
              : glm::vec2(seg.splinePoints.back().x, seg.splinePoints.back().z);
      branchAttachedEnds[s] |= (end == 0) ? 0x1 : 0x2;
      branchContacts.push_back(
          {s, end, hSeg, hSample, joinedPos, hostPosition, hostTangent, side});
    }
  }

  // Attaching a branch moves and re-evaluates a host endpoint (line above), which
  // can change a host segment's sample count. Any hostSample captured for that
  // segment before this point may now be out of range, and bareAt[] is sized from
  // the final counts, so every captured host index has to be re-validated here.
  // (bareAt[host][stale] was an out-of-bounds heap write.)
  for (BranchContact &contact : branchContacts) {
    if (contact.host < 0 || contact.host >= S) {
      contact.host = -1;
      contact.hostSample = -1;
      continue;
    }
    const int hostSamples = (int)segments[contact.host].splinePoints.size();
    if (contact.hostSample < 0 || contact.hostSample >= hostSamples) {
      // Re-point at the nearest still-valid sample rather than dropping the join.
      contact.hostSample = glm::clamp(contact.hostSample, 0, std::max(0, hostSamples - 1));
      if (hostSamples < 2) {
        contact.host = -1;
        contact.hostSample = -1;
      }
    }
  }

  // â”€â”€â”€ 1. Endpoint clustering
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  // ── 0c. Automatic gap splicing ───────────────────────────────────────────
  // Ends that face each other across a gap are extended along their own axes
  // until the axes meet, so a road drawn in two strokes becomes one road
  // instead of two end caps facing a hole. See the header for why each guard is
  // needed. The spliced ends land within kRoadJunctionTolerance of each other,
  // so the clustering below turns them into a proper shared-vertex junction.
  {
    struct GapEnd {
      int seg;
      bool back;
      glm::vec2 pos;
      glm::vec2 out;
    };
    std::vector<GapEnd> ge;
    for (int s = 0; s < S; ++s) {
      RoadSegment &seg = segments[s];
      if (seg.closed || seg.splinePoints.size() < 2)
        continue;
      glm::vec2 oF(-seg.tangents.front().x, -seg.tangents.front().z);
      glm::vec2 oB(seg.tangents.back().x, seg.tangents.back().z);
      if (glm::dot(oF, oF) > 1e-8f)
        oF = glm::normalize(oF);
      if (glm::dot(oB, oB) > 1e-8f)
        oB = glm::normalize(oB);
      ge.push_back(
          {s, false,
           glm::vec2(seg.splinePoints.front().x, seg.splinePoints.front().z),
           oF});
      ge.push_back(
          {s, true,
           glm::vec2(seg.splinePoints.back().x, seg.splinePoints.back().z),
           oB});
    }

    // True when some third road's corridor covers the straight gap, which would
    // mean the splice reaches across the network rather than along it.
    auto gapIsBlocked = [&](const glm::vec2 &a, const glm::vec2 &b, int s1,
                            int s2) {
      for (int h = 0; h < S; ++h) {
        if (h == s1 || h == s2)
          continue;
        const auto &hp = segments[h].splinePoints;
        const float hw = segments[h].width * 0.5f;
        const size_t stride = (hp.size() > 64) ? (hp.size() / 64) : 1;
        for (int t = 0; t <= 24; ++t) {
          const glm::vec2 m = glm::mix(a, b, t / 24.0f);
          for (size_t k = 0; k + 1 < hp.size(); k += stride) {
            const glm::vec2 p(hp[k].x, hp[k].z);
            const glm::vec2 ab(hp[k + 1].x - p.x, hp[k + 1].z - p.y);
            const float l2 = glm::dot(ab, ab);
            if (l2 < 1e-9f)
              continue;
            const float u = glm::clamp(glm::dot(m - p, ab) / l2, 0.0f, 1.0f);
            if (glm::length(m - (p + ab * u)) <= hw)
              return true;
          }
        }
      }
      return false;
    };

    std::vector<bool> done(ge.size(), false);
    int spliceCount = 0;
    int deadEndsBefore = 0, deadEndsAfter = 0;
    if (std::getenv("FALLAGA_ROADGAP")) {
      for (const GapEnd &e : ge) {
        bool joined = false;
        for (const GapEnd &o : ge) {
          if (o.seg == e.seg) continue;
          if (glm::length(o.pos - e.pos) <= kRoadJunctionTolerance) { joined = true; break; }
        }
        if (!joined) ++deadEndsBefore;
      }
    }
    for (size_t i = 0; i < ge.size(); ++i) {
      if (done[i])
        continue;
      for (size_t j = i + 1; j < ge.size(); ++j) {
        if (done[j] || ge[i].seg == ge[j].seg)
          continue;
        const glm::vec2 delta = ge[j].pos - ge[i].pos;
        const float d = glm::length(delta);
        if (d <= kRoadJunctionTolerance || d > kMaxRoadGapSplice)
          continue;

        const glm::vec2 u = delta / d;
        const float aimI = glm::dot(ge[i].out, u);
        const float aimJ = glm::dot(ge[j].out, -u);
        // Two ways an end may be closed. Either both axes aim at each other, which
        // is one road drawn in two strokes, or the ends are close enough that a
        // direct span between them reads as a continuation of both. The second case
        // is what a hand- or auto-traced network usually leaves behind, and without
        // it the asphalt simply stops and terrain shows through the gap.
        const bool facingPair = aimI >= kRoadGapSpliceFacing &&
                                aimJ >= kRoadGapSpliceFacing;
        constexpr float kDirectJoinFacing = -0.30f; // up to ~107 deg off-axis
        const bool directJoin = d <= kDirectJoinRange &&
                                aimI >= kDirectJoinFacing &&
                                aimJ >= kDirectJoinFacing;
        if (!facingPair && !directJoin)
          continue;

        // Where the two axes meet, and how cleanly they meet there.
        const glm::vec2 mI =
            ge[i].pos + ge[i].out * glm::dot(ge[j].pos - ge[i].pos, ge[i].out);
        const glm::vec2 mJ =
            ge[j].pos + ge[j].out * glm::dot(ge[i].pos - ge[j].pos, ge[j].out);
        if (facingPair && glm::length(mI - mJ) > kRoadGapSpliceLock)
          continue;
        if (gapIsBlocked(ge[i].pos, ge[j].pos, ge[i].seg, ge[j].seg))
          continue;

        // Extend each end along its own axis to the shared meeting point, so
        // the road keeps the direction it was drawn with instead of kinking
        // sideways. When the axes do not meet cleanly the span between the ends is
        // used instead, which keeps the join short and centred on the gap.
        const glm::vec2 joint = (facingPair && glm::length(mI - mJ) <= kRoadGapSpliceLock)
                                    ? (mI + mJ) * 0.5f
                                    : (ge[i].pos + ge[j].pos) * 0.5f;
        if (glm::dot(joint - ge[i].pos, ge[i].out) <= 0.0f)
          continue;
        if (glm::dot(joint - ge[j].pos, ge[j].out) <= 0.0f)
          continue;

        const float jy =
            sampler.isValid() ? sampler.renderedGround(joint.x, joint.y) : 0.0f;
        RoadSegment &si = segments[ge[i].seg];
        RoadSegment &sj = segments[ge[j].seg];
        if (ge[i].back)
          si.splinePoints.back() = glm::vec3(joint.x, jy, joint.y);
        else
          si.splinePoints.front() = glm::vec3(joint.x, jy, joint.y);
        if (ge[j].back)
          sj.splinePoints.back() = glm::vec3(joint.x, jy, joint.y);
        else
          sj.splinePoints.front() = glm::vec3(joint.x, jy, joint.y);

        done[i] = done[j] = true;
        ++spliceCount;
        break;
      }
    }
    if (std::getenv("FALLAGA_ROADGAP")) {
      for (const GapEnd &e : ge) {
        bool joined = false;
        for (const GapEnd &o : ge) {
          if (o.seg == e.seg) continue;
          if (glm::length(o.pos - e.pos) <= kRoadJunctionTolerance) { joined = true; break; }
        }
        if (!joined) ++deadEndsAfter;
      }
      std::cout << "[RoadGap] dead ends: " << deadEndsBefore << " before splice, "
                << deadEndsAfter << " after (" << spliceCount << " joined)"
                << std::endl;
    }
    if (spliceCount > 0)
      std::cout << "[RoadMeshBuilder] Auto-spliced " << spliceCount
                << " road gap(s) up to " << (int)kMaxRoadGapSplice << " m"
                << std::endl;
  }

  // Two ends join only when they are within JN_TOL of each other. Note that a
  // wider "near-collinear continuation" rule is tempting here - a road drawn in
  // two strokes that stopped short leaves two parallel-pointing ends a
  // road-width apart - but it is not distinguishable from two genuinely
  // parallel roads at that distance, and clustering those welds a pavement
  // wedge between roads that must stay separate. Bridging a deliberate
  // authoring gap is the road editor's CONNECT ROADS job.
  struct EndRef {
    int seg;
    bool isBack;
    glm::vec2 pos;
    float hw;
  };
  std::vector<EndRef> ends;
  std::vector<std::pair<int, int>> segEndRef(S, {-1, -1});
  for (int s = 0; s < S; ++s) {
    const RoadSegment &seg = segments[s];
    if (seg.splinePoints.size() < 2)
      continue;
    const int fi = (int)ends.size();
    ends.push_back(
        {s, false,
         glm::vec2(seg.splinePoints.front().x, seg.splinePoints.front().z),
         seg.width * 0.5f});
    ends.push_back(
        {s, true,
         glm::vec2(seg.splinePoints.back().x, seg.splinePoints.back().z),
         seg.width * 0.5f});
    segEndRef[s] = {fi, fi + 1};
  }
  const int E = (int)ends.size();
  std::vector<int> cid(E, -1);
  int nc = 0;
  for (int i = 0; i < E; ++i) {
    if (cid[i] >= 0)
      continue;
    cid[i] = nc;
    for (int j = i + 1; j < E; ++j)
      if (cid[j] < 0 && glm::length(ends[i].pos - ends[j].pos) <= JN_TOL)
        cid[j] = nc;
    ++nc;
  }
  std::vector<std::vector<int>> cluster(nc);
  for (int i = 0; i < E; ++i)
    cluster[cid[i]].push_back(i);

  auto countRealSegsInCluster = [&](const std::vector<int> &cl) {
    int seen = 0;
    for (size_t a = 0; a < cl.size(); ++a) {
      bool dup = false;
      for (size_t b = 0; b < a; ++b)
        if (ends[cl[b]].seg == ends[cl[a]].seg) {
          dup = true;
          break;
        }
      if (!dup)
        ++seen;
    }
    return seen;
  };

  // â”€â”€â”€ 2. Network-continuous texture distance v
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  std::vector<float> vStart(S, 0.0f);
  std::vector<bool> vDone(S, false);
  for (int s = 0; s < S && !vDone[s]; ++s) {
    if (segments[s].closed) {
      vDone[s] = true;
      continue;
    }
    vStart[s] = 0.0f;
    vDone[s] = true;
    bool ch = true;
    while (ch) {
      ch = false;
      for (int c = 0; c < nc; ++c) {
        if (countRealSegsInCluster(cluster[c]) != 2)
          continue;
        int known = -1;
        for (int id : cluster[c])
          if (vDone[ends[id].seg]) {
            known = id;
            break;
          }
        if (known < 0)
          continue;
        const EndRef &ek = ends[known];
        float vK =
            vStart[ek.seg] + (ek.isBack ? segLen(segments[ek.seg]) : 0.0f);
        for (int id : cluster[c]) {
          const EndRef &eu = ends[id];
          if (vDone[eu.seg] || segments[eu.seg].closed)
            continue;
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
    struct AABB2 {
      glm::vec2 lo, hi;
    };
    std::vector<AABB2> box(S);
    for (int s = 0; s < S; ++s) {
      glm::vec2 lo(1e9f), hi(-1e9f);
      for (const auto &p : segments[s].splinePoints) {
        lo = glm::min(lo, glm::vec2(p.x, p.z));
        hi = glm::max(hi, glm::vec2(p.x, p.z));
      }
      const float pad = segments[s].width * 0.5f + 1.0f;
      box[s] = {lo - glm::vec2(pad), hi + glm::vec2(pad)};
    }

    auto outranks = [&](int me, int host) {
      const int tm = (int)segments[me].type, th = (int)segments[host].type;
      if (th != tm)
        return th < tm;
      return segments[host].id < segments[me].id;
    };

    // Distance to host centreline: coarse stride, then exact local refine.
    auto inCorridor = [](const std::vector<glm::vec3> &poly, float hw,
                         const glm::vec2 &q) -> bool {
      const int n = (int)poly.size();
      if (n < 2)
        return false;
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
        if (!hit(k))
          continue;
        for (int k2 = std::max(0, k - stride); k2 < std::min(n - 1, k + stride);
             ++k2)
          if (hit(k2))
            return true;
      }
      return false;
    };

    for (int s = 0; s < S; ++s) {
      const int n = (int)segments[s].splinePoints.size();
      bareAt[s].assign(std::max(n, 0), 0);
      if (n < 2)
        continue;

      std::vector<std::pair<int, int>> cand; // (rank, host)
      for (int h = 0; h < S; ++h) {
        if (h == s || !outranks(s, h))
          continue;
        if (box[s].lo.x > box[h].hi.x || box[s].hi.x < box[h].lo.x ||
            box[s].lo.y > box[h].hi.y || box[s].hi.y < box[h].lo.y)
          continue;
        cand.push_back({(int)segments[h].type, h});
      }
      if (cand.empty())
        continue;
      std::sort(cand.begin(), cand.end());

      const int kMaxHosts = 8; // bound per-sample cost on dense grids
      for (int i = 0; i < n; ++i) {
        const glm::vec2 q(segments[s].splinePoints[i].x,
                          segments[s].splinePoints[i].z);
        for (int ci = 0; ci < (int)cand.size() && ci < kMaxHosts; ++ci) {
          if (!inCorridor(segments[cand[ci].second].splinePoints,
                          segments[cand[ci].second].width * 0.5f, q))
            continue;
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
    const float kCrossCos =
        kJunctionApproachCos; // |cos| below this = at least ~30 deg
    const int kMaxContacts = 40000;

    std::unordered_map<long long, std::vector<std::pair<int, int>>> grid;
    auto cellKey = [](int cx, int cz) -> long long {
      return (long long)cx * 1000003LL + (long long)cz;
    };
    for (int s = 0; s < S; ++s) {
      const auto &pts = segments[s].splinePoints;
      for (int i = 0; i < (int)pts.size(); ++i)
        grid[cellKey((int)std::floor(pts[i].x / kCell),
                     (int)std::floor(pts[i].z / kCell))]
            .push_back({s, i});
    }

    struct Contact {
      glm::vec2 p;
      int a, ai, b, bi;
    };
    std::vector<Contact> contacts;
    for (int s = 0; s < S && (int)contacts.size() < kMaxContacts; ++s) {
      const auto &pts = segments[s].splinePoints;
      if (pts.size() < 2)
        continue;
      const float hwS = segments[s].width * 0.5f;
      for (int i = 0;
           i < (int)pts.size() && (int)contacts.size() < kMaxContacts; ++i) {
        const glm::vec2 p(pts[i].x, pts[i].z);
        const int cx = (int)std::floor(p.x / kCell);
        const int cz = (int)std::floor(p.y / kCell);
        for (int ox = -1; ox <= 1; ++ox) {
          for (int oz = -1; oz <= 1; ++oz) {
            auto it = grid.find(cellKey(cx + ox, cz + oz));
            if (it == grid.end())
              continue;
            for (const auto &pr : it->second) {
              const int h = pr.first, j = pr.second;
              if (h <= s)
                continue; // one direction only
              const auto &hp = segments[h].splinePoints;
              if (j < 0 || j >= (int)hp.size() || hp.size() < 2)
                continue;
              const glm::vec2 q(hp[j].x, hp[j].z);
              const float lim = hwS + segments[h].width * 0.5f;
              const glm::vec2 d = p - q;
              if (glm::dot(d, d) >= lim * lim)
                continue;

              glm::vec2 t0(segments[s].tangents[i].x,
                           segments[s].tangents[i].z);
              glm::vec2 t1(segments[h].tangents[j].x,
                           segments[h].tangents[j].z);
              if (glm::dot(t0, t0) < 1e-9f || glm::dot(t1, t1) < 1e-9f)
                continue;
              t0 = glm::normalize(t0);
              t1 = glm::normalize(t1);
              if (std::fabs(glm::dot(t0, t1)) > kCrossCos)
                continue;

              contacts.push_back({(p + q) * 0.5f, s, i, h, j});
              if ((int)contacts.size() >= kMaxContacts)
                break;
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
    constexpr float kMergeCell = 16.0f;
    std::unordered_map<long long, std::vector<int>> cgrid;
    for (size_t i = 0; i < contacts.size(); ++i)
      cgrid[cellKey((int)std::floor(contacts[i].p.x / kMergeCell),
                    (int)std::floor(contacts[i].p.y / kMergeCell))]
          .push_back((int)i);

    std::vector<int> parent(contacts.size());
    for (size_t i = 0; i < contacts.size(); ++i)
      parent[i] = (int)i;
    auto find = [&parent](int a) {
      auto self = [&parent](int x) -> int {
        while (parent[x] != x) {
          parent[x] = parent[parent[x]];
          x = parent[x];
        }
        return x;
      };
      return self(a);
    };
    for (size_t i = 0; i < contacts.size(); ++i) {
      const int cx = (int)std::floor(contacts[i].p.x / kMergeCell);
      const int cz = (int)std::floor(contacts[i].p.y / kMergeCell);
      const float maxMergeDistance =
          std::max(4.0f, 0.75f * (segments[contacts[i].a].width +
                                  segments[contacts[i].b].width));
      const int searchCells =
          std::min(8, (int)std::ceil(maxMergeDistance / kMergeCell));
      for (int ox = -searchCells; ox <= searchCells; ++ox) {
        for (int oz = -searchCells; oz <= searchCells; ++oz) {
          auto it = cgrid.find(cellKey(cx + ox, cz + oz));
          if (it == cgrid.end())
            continue;
          for (int j : it->second) {
            if (j <= (int)i)
              continue;
            const float mergeDistance =
                std::max(4.0f, 0.75f *
                                   (segments[contacts[i].a].width +
                                    segments[contacts[i].b].width +
                                    segments[contacts[j].a].width +
                                    segments[contacts[j].b].width) *
                                   0.5f);
            if (glm::length(contacts[i].p - contacts[j].p) > mergeDistance)
              continue;
            const int ra = find((int)i), rb = find(j);
            if (ra != rb)
              parent[ra] = rb;
          }
        }
      }
    }

    std::unordered_map<int, CrossSite> grouped;
    for (size_t i = 0; i < contacts.size(); ++i) {
      CrossSite &site = grouped[find((int)i)];
      site.c += contacts[i].p;
      site.members.push_back({contacts[i].a, contacts[i].ai});
      site.members.push_back({contacts[i].b, contacts[i].bi});
      bareAt[contacts[i].a][contacts[i].ai] = 1;
      bareAt[contacts[i].b][contacts[i].bi] = 1;
      ++bareSamples;
    }

    for (auto &kv : grouped) {
      CrossSite site = kv.second;
      site.c /= (float)std::max<size_t>(1, site.members.size() / 2);
      site.midSpan = true;

      // Keep only each road's sample closest to the site centre.
      std::vector<std::pair<int, int>> keep;
      for (const auto &m : site.members) {
        const auto &sp = segments[m.first].splinePoints;
        if (m.second < 0 || m.second >= (int)sp.size())
          continue;
        const float d =
            glm::length(glm::vec2(sp[m.second].x, sp[m.second].z) - site.c);
        bool seen = false;
        for (auto &k : keep) {
          if (k.first != m.first)
            continue;
          const auto &kp = segments[k.first].splinePoints;
          const float kd =
              glm::length(glm::vec2(kp[k.second].x, kp[k.second].z) - site.c);
          if (d < kd)
            k.second = m.second;
          seen = true;
          break;
        }
        if (!seen)
          keep.push_back(m);
      }
      site.members = keep;
      if (site.members.size() < 2)
        continue;
      for (const auto &m : site.members)
        site.r = std::max(site.r, segments[m.first].width * 0.5f);
      crossSites.push_back(site);
    }
  }

  // The sample-pair crossing pass can miss an endpoint landing exactly on the
  // host's outer edge (the endpoints are not evenly aligned to host samples).
  // Add a fallback pad only when that road pair does not already have a nearby
  // crossing pad, avoiding the duplicate coplanar surfaces that caused blobs.
  for (const BranchContact &contact : branchContacts) {
    // Contacts whose host could not be re-validated after the attach pass are
    // dropped here; indexing segments[contact.host] with -1 would be fatal.
    if (contact.host < 0 || contact.host >= S ||
        contact.seg < 0 || contact.seg >= S)
      continue;
    const float mergeDistance =
        std::max(segments[contact.seg].width, segments[contact.host].width) *
        1.5f;
    bool hasPad = false;
    for (const CrossSite &site : crossSites) {
      bool hasBranch = false, hasHost = false;
      for (const auto &member : site.members) {
        hasBranch |= member.first == contact.seg;
        hasHost |= member.first == contact.host;
      }
      if (hasBranch && hasHost &&
          glm::length(site.c - contact.position) <= mergeDistance) {
        hasPad = true;
        break;
      }
    }
    if (hasPad)
      continue;

    const int endIdx = contact.end == 0
                           ? 0
                           : (int)segments[contact.seg].splinePoints.size() - 1;
    CrossSite site;
    site.c = contact.position;
    site.members = {{contact.seg, endIdx}, {contact.host, contact.hostSample}};
    site.r = std::max(segments[contact.seg].width * 0.5f,
                      segments[contact.host].width * 0.5f);
    crossSites.push_back(site);
    bareAt[contact.seg][endIdx] = 1;
    // Guarded: the sample index is validated when the contact is captured, but
    // bareAt[] is sized from the final sample counts, so never write blind here.
    if (contact.host >= 0 && contact.host < S &&
        contact.hostSample >= 0 &&
        contact.hostSample < (int)bareAt[contact.host].size()) {
      bareAt[contact.host][contact.hostSample] = 1;
    }
    ++bareSamples;
  }

  // True mid-span crossings remove the overlapping strip spans. Store the
  // selected cut rings now so the strip indices and the patch use identical
  // boundary vertices later in this build.
  // Tangent run of the curb return at a junction mouth. The arms are cut this
  // far BEYOND the point where the two road edges would meet, and the gap
  // between the cut rings is then filled with a tangent fillet (see 5b) instead
  // of a miter.
  auto junctionFilletRun = [](float widthA, float widthB) {
    return std::clamp(0.45f * std::min(widthA, widthB), 0.8f, 3.5f);
  };
  struct CrossingTrimRange {
    int start = 0, end = 0;
    bool wraps = false;
  };
  struct CrossingPatch {
    glm::vec2 center{0.0f};
    std::vector<std::pair<int, int>> centerSamples;
    std::vector<std::pair<int, int>> boundaryRings;
  };
  std::vector<std::vector<CrossingTrimRange>> crossingTrimRanges(S);
  std::vector<CrossingPatch> crossingPatches;
  for (const CrossSite &site : crossSites) {
    if (!site.midSpan || site.members.size() < 2)
      continue;

    CrossingPatch patch;
    patch.center = site.c;
    std::vector<std::pair<int, CrossingTrimRange>> pending;
    bool valid = true;
    for (const auto &member : site.members) {
      const int s = member.first;
      const RoadSegment &seg = segments[s];
      const int n = (int)seg.splinePoints.size();
      if (n < 3 || member.second < 0 || member.second >= n ||
          seg.cumulativeDist.size() != seg.splinePoints.size() ||
          seg.tangents.size() != seg.splinePoints.size()) {
        valid = false;
        break;
      }

      const int centerIndex = member.second;
      glm::vec2 tangent(segments[s].tangents[centerIndex].x,
                        segments[s].tangents[centerIndex].z);
      if (glm::dot(tangent, tangent) < 1e-8f) {
        valid = false;
        break;
      }
      tangent = glm::normalize(tangent);
      const float ownHalfWidth = std::max(0.0f, seg.width * 0.5f);
      float trimDistance = 0.0f;
      for (const auto &other : site.members) {
        if (other.first == s)
          continue;
        const RoadSegment &otherSeg = segments[other.first];
        if (other.second < 0 || other.second >= (int)otherSeg.tangents.size())
          continue;
        glm::vec2 otherTangent(otherSeg.tangents[other.second].x,
                               otherSeg.tangents[other.second].z);
        if (glm::dot(otherTangent, otherTangent) < 1e-8f)
          continue;
        otherTangent = glm::normalize(otherTangent);
        const float sine =
            std::fabs(tangent.x * otherTangent.y - tangent.y * otherTangent.x);
        if (sine < 1e-3f)
          continue;
        const float cosine = std::fabs(glm::dot(tangent, otherTangent));
        const float otherHalfWidth = std::max(0.0f, otherSeg.width * 0.5f);
        // Include the full overlap of both width corridors at oblique
        // angles, plus a tiny seam allowance.
        trimDistance =
            std::max(trimDistance,
                     (otherHalfWidth + ownHalfWidth * cosine) / sine + 0.02f +
                         junctionFilletRun(seg.width, otherSeg.width));
      }
      if (trimDistance <= 0.0f) {
        valid = false;
        break;
      }

      CrossingTrimRange range;
      if (!seg.closed) {
        const float station = seg.cumulativeDist[centerIndex];
        const float startTarget = station - trimDistance;
        const float endTarget = station + trimDistance;
        range.start = centerIndex;
        range.end = centerIndex;
        while (range.start > 0 && seg.cumulativeDist[range.start] > startTarget)
          --range.start;
        while (range.end + 1 < n && seg.cumulativeDist[range.end] < endTarget)
          ++range.end;
        // A near-end crossing belongs to endpoint/junction handling;
        // cutting it here would leave no closed patch boundary.
        if (range.start == 0 || range.end == n - 1 ||
            range.start >= range.end) {
          valid = false;
          break;
        }
      } else {
        const float closingDistance =
            glm::length(seg.splinePoints.front() - seg.splinePoints.back());
        const float totalDistance = seg.cumulativeDist.back() + closingDistance;
        if (totalDistance <= 1e-4f) {
          valid = false;
          break;
        }
        trimDistance = std::min(trimDistance, totalDistance * 0.24f);
        range.start = centerIndex;
        range.end = centerIndex;
        float backDistance = 0.0f, forwardDistance = 0.0f;
        int steps = 0;
        while (backDistance < trimDistance && steps++ < n - 1) {
          const int prev = (range.start - 1 + n) % n;
          const float step =
              range.start == 0
                  ? totalDistance - seg.cumulativeDist[prev]
                  : seg.cumulativeDist[range.start] - seg.cumulativeDist[prev];
          backDistance += step;
          range.start = prev;
        }
        steps = 0;
        while (forwardDistance < trimDistance && steps++ < n - 1) {
          const int next = (range.end + 1) % n;
          const float step =
              range.end == n - 1
                  ? totalDistance - seg.cumulativeDist.back()
                  : seg.cumulativeDist[next] - seg.cumulativeDist[range.end];
          forwardDistance += step;
          range.end = next;
        }
        range.wraps = range.start > range.end;
        if (range.start == range.end) {
          valid = false;
          break;
        }
      }
      pending.push_back({s, range});
      patch.centerSamples.push_back({s, centerIndex});
      patch.boundaryRings.push_back({s, range.start});
      patch.boundaryRings.push_back({s, range.end});
    }
    if (!valid || pending.size() < 2)
      continue;

    for (const auto &item : pending) {
      const int s = item.first;
      const CrossingTrimRange &range = item.second;
      crossingTrimRanges[s].push_back(range);
      const int n = (int)segments[s].splinePoints.size();
      int i = range.start;
      for (int guard = 0; guard < n; ++guard) {
        bareAt[s][i] = 1;
        if (i == range.end)
          break;
        i = (i + 1) % n;
      }
    }
    crossingPatches.push_back(std::move(patch));
  }

  // -- 2d. Endpoint-cluster junctions: trim the arms, share one patch
  // ------------- Roads whose ends meet at one point used to run to the cluster
  // centre and get a convex-hull pad dropped on top (section 5): coplanar
  // overlap, a hull that bulges past the road edges, and a ribbon/pad mismatch
  // in normals and paint. They now go through the same machinery as mid-span
  // crossings: every arm is cut back to a computed station, the cut rings
  // become the patch boundary (5b), and the quads under the junction are
  // dropped so nothing overlaps.
  //   station(arm) = where its edge meets the neighbouring arm's edge
  //                  + the fillet run for the curb return.
  std::vector<char> clusterTrimmed(nc, 0);
  for (int c = 0; c < nc; ++c) {
    if (countRealSegsInCluster(cluster[c]) < 2)
      continue;

    struct ClusterArm {
      int seg = -1;
      bool isBack = false;
      int endIdx = 0;
      glm::vec2 pos{0.0f}, dir{1.0f, 0.0f}; // dir leaves the junction
      float hw = 0.0f, angle = 0.0f, station = 0.0f;
      int ring = -1;
    };
    std::vector<ClusterArm> arms;
    bool ok = true;
    for (int id : cluster[c]) {
      const EndRef &e = ends[id];
      const RoadSegment &sg = segments[e.seg];
      const int n = (int)sg.splinePoints.size();
      if (sg.closed || n < 3 || sg.tangents.size() != sg.splinePoints.size() ||
          sg.cumulativeDist.size() != sg.splinePoints.size()) {
        ok = false;
        break;
      }
      const int i0 = e.isBack ? n - 1 : 0;
      glm::vec2 t(sg.tangents[i0].x, sg.tangents[i0].z);
      if (glm::dot(t, t) < 1e-8f) {
        ok = false;
        break;
      }
      t = glm::normalize(t);
      ClusterArm a;
      a.seg = e.seg;
      a.isBack = e.isBack;
      a.endIdx = i0;
      a.pos = e.pos;
      a.dir = e.isBack ? -t : t;
      a.hw = e.hw;
      a.angle = std::atan2(a.dir.y, a.dir.x);
      arms.push_back(a);
    }
    if (!ok || arms.size() < 2)
      continue;
    // A straight continuation is the shared-vertex joint of 4b, not a junction.
    if (arms.size() == 2 &&
        std::fabs(glm::dot(arms[0].dir, arms[1].dir)) > 0.94f)
      continue;

    std::sort(arms.begin(), arms.end(),
              [](const ClusterArm &a, const ClusterArm &b) {
                return a.angle < b.angle;
              });
    const int A = (int)arms.size();
    auto cr2 = [](const glm::vec2 &a, const glm::vec2 &b) {
      return a.x * b.y - a.y * b.x;
    };

    for (int i = 0; i < A && ok; ++i) {
      ClusterArm &a = arms[i];
      float st = 0.0f;
      bool any = false;
      for (int dn : {1, -1}) {
        if (A == 2 && dn == -1)
          continue;
        const ClusterArm &o = arms[(i + dn + A) % A];
        const float sn = std::fabs(cr2(a.dir, o.dir));
        if (sn < 0.12f)
          continue; // collinear/hairpin: edges never meet
        const float cs = glm::dot(a.dir, o.dir);
        const float meet = std::max(0.0f, (o.hw + a.hw * cs) / sn);
        st = std::max(st, meet + junctionFilletRun(a.hw * 2.0f, o.hw * 2.0f));
        any = true;
      }
      const float cap = std::min(25.0f, 0.45f * segLen(segments[a.seg]));
      if (!any || st > cap) {
        ok = false;
        break;
      }
      a.station = st;

      const RoadSegment &sg = segments[a.seg];
      const int n = (int)sg.splinePoints.size();
      if (!a.isBack) {
        int k = 1;
        while (k < n - 1 && sg.cumulativeDist[k] - sg.cumulativeDist[0] < st)
          ++k;
        if (k >= n - 1) {
          ok = false;
          break;
        }
        a.ring = k;
      } else {
        int k = n - 2;
        while (k > 0 && sg.cumulativeDist[n - 1] - sg.cumulativeDist[k] < st)
          --k;
        if (k <= 0) {
          ok = false;
          break;
        }
        a.ring = k;
      }
    }
    if (!ok)
      continue;
    // A road with both ends in this cluster needs its two cut rings in order.
    for (int i = 0; i < A && ok; ++i)
      for (int j = i + 1; j < A; ++j)
        if (arms[i].seg == arms[j].seg) {
          const ClusterArm &f = arms[i].isBack ? arms[j] : arms[i];
          const ClusterArm &b = arms[i].isBack ? arms[i] : arms[j];
          if (f.isBack == b.isBack || f.ring >= b.ring) {
            ok = false;
            break;
          }
        }
    if (!ok)
      continue;

    CrossingPatch patch;
    for (const ClusterArm &a : arms)
      patch.center += a.pos;
    patch.center /= (float)A;
    for (const ClusterArm &a : arms) {
      const int n = (int)segments[a.seg].splinePoints.size();
      CrossingTrimRange range;
      range.start = a.isBack ? a.ring : 0;
      range.end = a.isBack ? n - 1 : a.ring;
      crossingTrimRanges[a.seg].push_back(range);
      for (int i = range.start; i <= range.end; ++i)
        bareAt[a.seg][i] = 1;
      patch.centerSamples.push_back({a.seg, a.endIdx});
      patch.boundaryRings.push_back({a.seg, a.ring});
    }
    crossingPatches.push_back(std::move(patch));
    clusterTrimmed[c] = 1;
  }

  auto isCrossingTrimmedQuad = [&](int s, int quadIndex) {
    for (const CrossingTrimRange &range : crossingTrimRanges[s]) {
      if (!range.wraps) {
        if (quadIndex >= range.start && quadIndex < range.end)
          return true;
      } else if (quadIndex >= range.start || quadIndex < range.end) {
        return true;
      }
    }
    return false;
  };

  // Distance (along the road) from every ring to the nearest junction cut ring.
  // The cut ring keeps its curb and edge-line paint so they can run round the
  // corner; the centre-zone paint (double yellow) fades out over
  // kJunctionWearFade metres approaching it, so the ruts/dust the shader
  // derives from the same mask end softly instead of snapping to the flat
  // junction surface.
  constexpr float kJunctionWearFade = 3.0f;
  std::vector<std::vector<float>> cutDistAt(S);
  std::vector<std::vector<char>> isCutRing(S);
  for (int s = 0; s < S; ++s) {
    const RoadSegment &sg = segments[s];
    const int n = (int)sg.splinePoints.size();
    if (n < 2 || crossingTrimRanges[s].empty() || sg.closed ||
        sg.cumulativeDist.size() != sg.splinePoints.size())
      continue;
    cutDistAt[s].assign(n, 1e9f);
    isCutRing[s].assign(n, 0);
    for (const CrossingTrimRange &r : crossingTrimRanges[s]) {
      if (r.wraps)
        continue;
      for (int cut : {r.start, r.end}) {
        const bool real = (cut == r.start) ? (r.start > 0) : (r.end < n - 1);
        if (!real)
          continue;
        isCutRing[s][cut] = 1;
        for (int i = 0; i < n; ++i)
          cutDistAt[s][i] =
              std::min(cutDistAt[s][i], std::fabs(sg.cumulativeDist[i] -
                                                  sg.cumulativeDist[cut]));
      }
    }
  }

  // â”€â”€â”€ 3. Ribbon rings per segment
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  for (int s = 0; s < S; ++s) {
    const RoadSegment &seg = segments[s];
    int n = (int)seg.splinePoints.size();
    if (n < 2)
      continue;

    const bool closed = seg.closed;
    const float hw = seg.width * 0.5f;
    const float rtype = float((int)seg.type);
    const float vOff = vStart[s];
    const float stripLift = 0.0f;
    const uint32_t baseIdx = (uint32_t)m_vertices.size();
    firstRingBase[s] = baseIdx;

    // One cross-section ring. The terrain is sampled ONCE, at the centreline,
    // and the rim pillars only partially adopt their own ground height
    // (EDGE_CONFORM). Sampling every pillar independently let the two rims ride
    // separate terrain heights, which is what twisted and warped the road edges
    // across every side slope and hill. The surface normal is then taken from
    // the rim positions that were actually emitted, so the residual cross-slope
    // is lit correctly.
    auto emitRing = [&](int i, float v, bool wrap) {
      const glm::vec3 &center = seg.splinePoints[i];
      const glm::vec3 &right = seg.rightVecs[i];
      const glm::vec3 &tangent = seg.tangents[i];
      const float yC = center.y;
      // Fade markings before an attached endpoint so its straight edge lines
      // flow into the curved corner connectors emitted below.
      float mark = 1.0f;
      if (i < (int)bareAt[s].size() && bareAt[s][i])
        mark = 0.0f;
      const float along =
          (i < (int)seg.cumulativeDist.size()) ? seg.cumulativeDist[i] : 0.0f;
      // Distance along the segment in metres, origin at its first ring. Drives the
      // road-following v of the carriageway surface texture (see the ring emitter).
      const float alongMetres =
          along - (seg.cumulativeDist.empty() ? 0.0f
                                              : seg.cumulativeDist.front());
      for (const BranchContact &contact : branchContacts) {
        if (contact.seg != s)
          continue;
        const float endDist = contact.end == 0
                                  ? along - seg.cumulativeDist.front()
                                  : seg.cumulativeDist.back() - along;
        const float r = branchFilletRadius(contact);
        const float t = glm::clamp(endDist / r, 0.0f, 1.0f);
        const float fade = t * t * (3.0f - 2.0f * t);
        mark = std::min(mark, fade);
      }

      const uint32_t ringVertexBase = (uint32_t)m_vertices.size();
      std::array<glm::vec3, kCrossCount> ringPositions{};
      // The raised sidewalk meets the flat junction pad at a cut ring, so it comes back down to
      // the carriageway over the same kJunctionWearFade run the paint fades over, and is flat at
      // the cut ring itself (no step or crack against the pad).
      float cutLift = 1.0f;
      if (i < (int)cutDistAt[s].size() && cutDistAt[s][i] < kJunctionWearFade) {
        if (isCutRing[s][i] != 0) {
          cutLift = 0.0f;
        } else {
          const float x = std::clamp(cutDistAt[s][i] / kJunctionWearFade, 0.0f, 1.0f);
          cutLift = x * x * (3.0f - 2.0f * x);
        }
      }
      for (int j = 0; j < kCrossCount; ++j) {
        const float uc = kCross[j];
        const float off = uc * 2.0f - 1.0f;
        const float useHw = RoadSpline::meshHalfWidth(seg, i, wrap, off, hw);

        glm::vec3 pos = center + right * (off * useHw);
        float y = yC;
        // renderedGround is the drawn terrain and already carries the baked bed offset,
        // so no deformation term is added here.
        if (sampler.isValid())
          y = yC + (sampler.renderedGround(pos.x, pos.z) - yC) * EDGE_CONFORM;
        const float crown = crownHeight * (1.0f - off * off);
        // Clear the WHOLE footprint these vertices' triangles cover, not just the
        // vertex point: a terrain triangle leaning through the ribbon's interior can be
        // half a metre above the ground sampled under the ribbon's own corners.
        // A vertex belongs to the quads on BOTH sides of its ring and pillar, so the
        // half-extent of the geometry it brackets is the full ring spacing and the full
        // pillar gap. Passing only half of those left the high corner of the neighbouring
        // terrain cell outside the 5x5 search on steep ground (port rim, ~0.42 m buried).
        const float halfAlong = seg.cumulativeDist.size() > 1
                                    ? std::max(1.0f, seg.cumulativeDist[std::min(
                                          i + 1, (int)seg.cumulativeDist.size() - 1)] -
                                                         seg.cumulativeDist[i])
                                    : 2.0f;
        const float halfAcross = std::max(1.0f, useHw * 2.0f / (float)(kCrossCount - 1));
        pos.y = minSurfaceYFootprint(pos, y + Z_BIAS + crown + stripLift,
                                     halfAlong, halfAcross);

        // Raised brick sidewalk (urban / colonial streets only). Pillars j = 1..3 and
        // 12..14 sit on the sidewalk side of the kerb; the rims (j = 0, last) stay on the
        // ground so the outer edge slopes back down to the terrain instead of hovering.
        // The lift fades out with the paint mask (dead ends / attached branch ends) and
        // drops to zero across the mouth of a side street (a dropped kerb), mirroring
        // the pavement suppression street.frag applies there.
        if ((int)rtype <= 1) {
          const bool raisedPillar =
              (j >= 1 && j <= 3) || (j >= kCrossCount - 4 && j <= kCrossCount - 2);
          if (raisedPillar) {
            float mouth = 1.0f;
            for (const BranchContact &c : branchContacts) {
              if (c.host != s)
                continue;
              const glm::vec2 hostT = glm::normalize(c.hostTangent);
              const glm::vec2 hostR(hostT.y, -hostT.x);
              const glm::vec2 d(pos.x - c.hostPosition.x, pos.z - c.hostPosition.y);
              if (glm::dot(d, hostR) * c.hostSide <= 0.0f)
                continue; // other side of the host from the side street
              const float half =
                  segments[c.seg].width * 0.5f + branchFilletRadius(c);
              mouth = std::min(
                  mouth, glm::smoothstep(half, half + 2.0f,
                                         std::fabs(glm::dot(d, hostT))));
            }
            pos.y += kSidewalkRaise * mark * mouth * cutLift;
          }
        }

        ringPositions[j] = pos;
        // The carriageway surface texture follows the road: metricUV here is
        // (lateral distance in metres from the left rim, distance in metres along
        // the segment), so the cobble/asphalt courses run with the road instead of
        // staying locked to the world axes and shearing across every bend. It is
        // recorded and re-applied after stampMetricUV(), which fills the junction
        // pads / caps / arc fans with the world-XZ projection; those keep it.
        const glm::vec2 ribbonMetricUV(
            off * useHw + std::max(0.0f, seg.width) * 0.5f,
            alongMetres);
        m_ribbonMetricUV.emplace_back((uint32_t)m_vertices.size(), ribbonMetricUV);
        m_vertices.push_back(
            {pos, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(uc, v), rtype,
             std::max(0.0f, seg.width) * 0.1f + std::max(0.0f, 5.0f - rtype),
             mark, glm::vec2(0.0f)});
      }

      for (int j = 0; j < kCrossCount; ++j) {
        const glm::vec3 across =
            j == 0 ? ringPositions[1] - ringPositions[0]
                   : (j == kCrossCount - 1
                          ? ringPositions[j] - ringPositions[j - 1]
                          : ringPositions[j + 1] - ringPositions[j - 1]);
        glm::vec3 surfNorm = glm::cross(tangent, across);
        const float nl = glm::length(surfNorm);
        surfNorm = nl > 1e-6f ? surfNorm / nl : glm::vec3(0.0f, 1.0f, 0.0f);
        if (surfNorm.y < 0.0f)
          surfNorm = -surfNorm;
        m_vertices[ringVertexBase + (uint32_t)j].normal = surfNorm;
      }

      // Junction cut rings keep curb + edge-line paint (j = 0..2 and the mirror
      // side) and drop the centre-zone paint; rings approaching a cut ring fade
      // the centre zone out over kJunctionWearFade.
      if (i < (int)cutDistAt[s].size() && cutDistAt[s][i] < kJunctionWearFade) {
        const bool cutRing = isCutRing[s][i] != 0;
        const float x =
            std::clamp(cutDistAt[s][i] / kJunctionWearFade, 0.0f, 1.0f);
        const float fade = x * x * (3.0f - 2.0f * x);
        for (int j = 0; j < kCrossCount; ++j) {
          const bool edgeBand = (j <= 2) || (j >= kCrossCount - 3);
          float &mm = m_vertices[ringVertexBase + (uint32_t)j].markMask;
          if (cutRing)
            mm = edgeBand ? 1.0f : 0.0f;
          else if (!edgeBand)
            mm *= fade;
        }
      }
    };

    if (closed) {
      float closingDist =
          glm::length(seg.splinePoints[0] - seg.splinePoints[n - 1]);
      float totalDist = seg.cumulativeDist[n - 1] + closingDist;
      const float dashCycle = 2.3f;
      float targetCycles =
          std::max(1.0f, std::round((totalDist * TEX_SCALE) / dashCycle));
      float ringTexScale = (targetCycles * dashCycle) / totalDist;

      for (int i = 0; i < n; ++i)
        emitRing(i, seg.cumulativeDist[i] * ringTexScale, true);

      // Closing ring n: identical position and normal to ring 0, but
      // v = totalDist * ringTexScale so the dash pattern wraps seamlessly.
      lastRingBase[s] = (uint32_t)m_vertices.size();
      emitRing(0, totalDist * ringTexScale, true);

      // Build quads for i = 0 .. n - 1 (last connects ring n-1 to closing ring
      // n)
      for (int i = 0; i < n; ++i) {
        if (isCrossingTrimmedQuad(s, i))
          continue;
        uint32_t r0 = baseIdx + (uint32_t)i * kCrossCount;
        uint32_t r1 = baseIdx + (uint32_t)(i + 1) * kCrossCount;
        for (int j = 0; j < kCrossCount - 1; ++j) {
          uint32_t a0 = r0 + j, a1 = r0 + j + 1;
          uint32_t b0 = r1 + j, b1 = r1 + j + 1;
          m_indices.push_back(a0);
          m_indices.push_back(b0);
          m_indices.push_back(a1);
          m_indices.push_back(a1);
          m_indices.push_back(b0);
          m_indices.push_back(b1);
        }
      }
      continue; // Closed loop is complete and self-contained!
    }

    // Open road: emit rings 0 .. n - 1
    lastRingBase[s] = baseIdx + (uint32_t)(n - 1) * kCrossCount;
    for (int i = 0; i < n; ++i) {
      const float v =
          (seg.cumulativeDist[i] - seg.cumulativeDist[0] + vOff) * TEX_SCALE;
      emitRing(i, v, false);
    }

    // Quads for open road: i = 0 .. n - 2
    for (int i = 0; i < n - 1; ++i) {
      if (isCrossingTrimmedQuad(s, i))
        continue;
      uint32_t r0 = baseIdx + (uint32_t)i * kCrossCount;
      uint32_t r1 = baseIdx + (uint32_t)(i + 1) * kCrossCount;
      for (int j = 0; j < kCrossCount - 1; ++j) {
        uint32_t a0 = r0 + j, a1 = r0 + j + 1;
        uint32_t b0 = r1 + j, b1 = r1 + j + 1;
        m_indices.push_back(a0);
        m_indices.push_back(b0);
        m_indices.push_back(a1);
        m_indices.push_back(a1);
        m_indices.push_back(b0);
        m_indices.push_back(b1);
      }
    }

    // ── 3b. Graded shoulder: REMOVED ───────────────────────────────────────
    // A graded verge was implemented and measured, then reverted. It is a net
    // negative on this terrain: extending a 3.5 m strip horizontally to the natural
    // surface produces hard-edged fins and dark polygons wherever the ground drops
    // steeply beside the asphalt (measured 8044-8164 verge quads, visible as spikes
    // at junctions and along the coast road), and tapering the strip to nothing at
    // segment ends did not remove them. The step it was meant to hide is smaller
    // than the artefacts it introduces. If this is revisited, the verge has to
    // follow a real cross-section profile (cut/fill at a fixed slope) rather than a
    // horizontal run to the sampled ground.

    // â”€â”€â”€ 4. Rounded end caps for true dead ends
    // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    for (int end = 0; end < 2; ++end) {
      int refId = (end == 0) ? segEndRef[s].first : segEndRef[s].second;
      if (refId < 0)
        continue;
      int c = cid[refId];
      if (c >= 0 && countRealSegsInCluster(cluster[c]) > 1)
        continue; // connects to another road!
      const unsigned char endBit = (end == 0) ? 0x1 : 0x2;
      if (branchAttachedEnds[s] & endBit)
        continue; // only this mouth meets the host

      const int i0 = (end == 0) ? 0 : n - 1;
      const glm::vec3 &P = seg.splinePoints[i0];
      glm::vec3 tang = seg.tangents[i0];
      glm::vec3 right = seg.rightVecs[i0];
      glm::vec3 outward = (end == 0) ? -tang : tang;
      // A cap is a fan, not a cross-section: sweep u from the centre outwards
      // so the arc reads as a continuation of the road surface, and let the
      // marking mask come from the end ring like every other ring.
      const float mark = 1.0f;

      const int M = 12;
      const float vEnd =
          (seg.cumulativeDist[i0] - seg.cumulativeDist[0] + vOff) * TEX_SCALE;
      const float yC = P.y;

      glm::vec3 capO = P;
      capO.y = minSurfaceYFootprint(capO, yC + Z_BIAS + crownHeight + stripLift, hw, hw);
      const uint32_t capBase = (uint32_t)m_vertices.size();
      m_vertices.push_back(
          {capO, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(0.5f, vEnd), rtype,
           std::max(0.0f, seg.width) * 0.1f + std::max(0.0f, 5.0f - rtype),
           mark});

      glm::vec3 arcL(0.0f), arcR(0.0f);
      for (int k = 0; k <= M; ++k) {
        float ang = float(k) / float(M) * PI;
        glm::vec3 d =
            -right * hw * std::cos(ang) + outward * hw * std::sin(ang);
      glm::vec3 pos = P + d;
      float y = yC;
      // renderedGround already contains the baked bed offset; no deformation term.
      if (sampler.isValid())
        y = yC + (sampler.renderedGround(pos.x, pos.z) - yC) * EDGE_CONFORM;
      const float lateral = std::cos(ang);
      const float crown = crownHeight * (1.0f - lateral * lateral);
      pos.y = minSurfaceYFootprint(pos, y + Z_BIAS + crown + stripLift,
                                   std::max(hw * 0.5f, 2.0f), std::max(hw * 0.5f, 2.0f));
        if (k == 0)
          arcL = pos;
        if (k == M)
          arcR = pos;
        const float uu =
            0.5f - 0.5f * std::cos(ang); // 0.5 -> 0 -> 0.5 across the arc
        const float vv = vEnd + (hw * std::sin(ang)) * TEX_SCALE;
        m_vertices.push_back(
            {pos, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(uu, vv), rtype,
             std::max(0.0f, seg.width) * 0.1f + std::max(0.0f, 5.0f - rtype),
             mark});
      }

      glm::vec3 across = arcR - arcL;
      if (glm::dot(across, across) < 1e-9f)
        across = right * (2.0f * hw);
      glm::vec3 surfNorm = glm::cross(tang, across);
      const float nl = glm::length(surfNorm);
      surfNorm = (nl > 1e-6f) ? surfNorm / nl : glm::vec3(0.0f, 1.0f, 0.0f);
      if (surfNorm.y < 0.0f)
        surfNorm = -surfNorm;
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

  // â”€â”€â”€ 4b. End-to-End Joints (connecting two roads cleanly)
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ Connects matching
  // cross-section pillars between two adjacent segments. Preserves lane lines,
  // center double-yellows, and curbs without any grey blotches.
  for (int c = 0; c < nc; ++c) {
    if (countRealSegsInCluster(cluster[c]) != 2)
      continue;
    if (clusterTrimmed[c])
      continue; // trimmed + patched in 2d / 5b instead
    int idA = cluster[c][0];
    int idB = cluster[c][1];
    int segA = ends[idA].seg;
    int segB = ends[idB].seg;
    if (segA == segB || segments[segA].closed || segments[segB].closed)
      continue;

    bool isBackA = ends[idA].isBack;
    bool isBackB = ends[idB].isBack;
    uint32_t ringBaseA = isBackA ? lastRingBase[segA] : firstRingBase[segA];
    uint32_t ringBaseB = isBackB ? lastRingBase[segB] : firstRingBase[segB];

    // Harmonize pillar alignment:
    // When one segment enters and the other leaves (isBackA != isBackB),
    // left meets left (j connects to j).
    // When both enter or both leave, directions are opposite, so left meets
    // right relative to the segment's local frame: j connects to (kCrossCount -
    // 1 - j).
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
      m_indices.push_back(a0);
      m_indices.push_back(b0);
      m_indices.push_back(a1);
      m_indices.push_back(a1);
      m_indices.push_back(b0);
      m_indices.push_back(b1);
    }
    ++jointCount;
  }

  // â”€â”€â”€ 5. Branching Junction Pads (2+ roads meeting)
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  // Old-strategy (Caesar) roof-fix: every place where road mouths come
  // together gets a paved pad covering the void between the ribbons, so
  // angled/unequal-width connections read as one continuous intersection
  // instead of a gap or a stack of overlapping mouth discs.
  for (int c = 0; c < nc; ++c) {
    if (countRealSegsInCluster(cluster[c]) < 2)
      continue;
    if (clusterTrimmed[c])
      continue; // handled by 2d / 5b (trimmed arms + fillets)

    glm::vec2 C(0.0f);
    float maxHw = 0.0f;
    int widestType = -1;
    for (int id : cluster[c]) {
      const EndRef &e = ends[id];
      C += e.pos;
      maxHw = std::max(maxHw, e.hw);
      if (e.hw >= maxHw || widestType < 0)
        widestType = (int)segments[e.seg].type;
    }
    C /= (float)cluster[c].size();

    int jnType = isPaved(widestType)
                     ? widestType
                     : (int)segments[ends[cluster[c][0]].seg].type;
    for (int id : cluster[c])
      if (isPaved((int)segments[ends[id].seg].type)) {
        jnType = (int)segments[ends[id].seg].type;
        break;
      }
    if (jnType < 0)
      jnType = 1;
    struct JnM {
      glm::vec2 rimL, rimR, tan;
      float hw;
    };
    std::vector<JnM> mem;
    mem.reserve(cluster[c].size());
    for (int id : cluster[c]) {
      const EndRef &e = ends[id];
      const RoadSegment &s = segments[e.seg];
      int n = (int)s.splinePoints.size();
      if (n < 2)
        continue;
      const int i0 = e.isBack ? n - 1 : 0;
      glm::vec2 p(s.splinePoints[i0].x, s.splinePoints[i0].z);
      glm::vec3 r3 = s.rightVecs[i0];
      glm::vec2 r2(r3.x, r3.z);
      if (glm::dot(r2, r2) < 1e-4f)
        r2 = glm::vec2(1, 0);
      const float leftWidth =
          RoadSpline::meshHalfWidth(s, i0, s.closed, -1.0f, e.hw);
      const float rightWidth =
          RoadSpline::meshHalfWidth(s, i0, s.closed, 1.0f, e.hw);
      glm::vec3 t3 = s.tangents[i0];
      glm::vec2 tanOut =
          e.isBack ? -glm::vec2(t3.x, t3.z) : glm::vec2(t3.x, t3.z);
      if (glm::length(tanOut) > 0.01f)
        tanOut = glm::normalize(tanOut);
      else
        tanOut = glm::vec2(1, 0);

      // Use the exact cross-section points emitted by the road ribbon so
      // the junction boundary meets the road ends without a gap.
      JnM m = {p - r2 * leftWidth, p + r2 * rightWidth, tanOut, e.hw};
      mem.push_back(m);
    }
    const int M = (int)mem.size();
    if (M < 2)
      continue;
    if (M == 2) {
      // Straight continuation: skip ONLY when the two mouths run along
      // nearly the same line (head-on OR same direction) at similar width.
      // The exact shared-vertex quad bridge handles those; an |dot| check
      // covers the head-on case that an angle<20deg test would leak through.
      bool parallel = std::fabs(glm::dot(mem[0].tan, mem[1].tan)) > 0.94f;
      bool wideStep = std::fabs(mem[0].hw - mem[1].hw) >
                      0.2f * std::max(mem[0].hw, mem[1].hw);
      if (parallel && !wideStep)
        continue;
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
    std::sort(boundary.begin(), boundary.end(),
              [](const glm::vec2 &a, const glm::vec2 &b) {
                return a.x != b.x ? a.x < b.x : a.y < b.y;
              });
    boundary.erase(std::unique(boundary.begin(), boundary.end(),
                               [](const glm::vec2 &a, const glm::vec2 &b) {
                                 return std::fabs(a.x - b.x) < 1e-4f &&
                                        std::fabs(a.y - b.y) < 1e-4f;
                               }),
                   boundary.end());
    auto cross2 = [](const glm::vec2 &o, const glm::vec2 &a,
                     const glm::vec2 &b) {
      return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    };
    std::vector<glm::vec2> hull(2 * boundary.size());
    int hullN = 0;
    for (const auto &p : boundary) {
      while (hullN >= 2 && cross2(hull[hullN - 2], hull[hullN - 1], p) <= 0.0f)
        --hullN;
      hull[hullN++] = p;
    }
    const int lowerHullN = hullN + 1;
    for (int i = (int)boundary.size() - 2; i >= 0; --i) {
      while (hullN >= lowerHullN &&
             cross2(hull[hullN - 2], hull[hullN - 1], boundary[i]) <= 0.0f)
        --hullN;
      hull[hullN++] = boundary[i];
    }
    hull.resize(hullN > 0 ? hullN - 1 : 0);
    if (hull.size() < 3)
      continue;

    const glm::vec3 up(0, 1, 0);

    auto emitPoly = [&](glm::vec2 origin, const std::vector<glm::vec2> &bnd,
                        float lift = 0.0f) {
      if (bnd.size() < 2)
        return;
      uint32_t wBase = (uint32_t)m_vertices.size();
      auto add = [&](const glm::vec2 &p2, bool centerVertex) {
        // renderedGround already contains the baked bed offset; no deformation term.
        float y = sampler.isValid() ? sampler.renderedGround(p2.x, p2.y) : 0.0f;
        const float crown = centerVertex ? crownHeight : 0.0f;
        const glm::vec3 at(p2.x, 0.0f, p2.y);
        m_vertices.push_back(
            {glm::vec3(p2.x, minSurfaceYFootprint(at, y + Z_BIAS + crown + lift, 8.0f, 8.0f), p2.y),
             glm::vec3(0.0f, 1.0f, 0.0f),
             glm::vec2(0.5f, glm::length(p2 - origin) * TEX_SCALE),
             (float)jnType, std::max(0.0f, 5.0f - (float)jnType), 0.0f});
      };
      add(origin, true);
      for (const auto &p2 : bnd)
        add(p2, false);
      uint32_t wN = (uint32_t)bnd.size() + 1;
      for (uint32_t k = 1; k + 1 < wN; ++k) {
        m_indices.push_back(wBase);
        m_indices.push_back(wBase + k);
        m_indices.push_back(wBase + k + 1);
      }
      ++jnCount;
    };

    // The terrain is conformed 2 cm below the pre-snap ribbons. After the
    // road is snapped back to that heightfield, its 1 cm surface bias leaves
    // the fill flush at the joint without a raised slab.
    const float padLift = 0.0f;
    glm::vec2 padCenter(0.0f);
    for (const auto &p : hull)
      padCenter += p;
    padCenter /= (float)hull.size();
    emitPoly(padCenter, hull, padLift);
  }

  // -- 5b. Junction patches (mid-span crossings AND endpoint clusters)
  // ------------ The road strips end at cut rings. Each cut ring is an "arm" of
  // the patch. The patch is built from duplicated copies of the ring vertices
  // (same positions, so it is watertight against the ribbons) in three parts:
  //   1. a corner strip between every pair of neighbouring arms: three rows
  //   (rim,
  //      edge-line outer, edge-line inner) following a quadratic curve that is
  //      tangent to both roads' edge lines. The strip carries the same u values
  //      as the ribbon, so the curb and the white edge line run round the
  //      corner as real geometry instead of a shader-painted approximation;
  //   2. a fan from the patch centre to the remaining ring vertices and the
  //   inner
  //      row of every corner strip;
  //   3. normals taken from the ribbon rings, never a hard (0,1,0). UVs: u is
  //   folded
  //      (min(u, 1-u)) so interpolation never sweeps across the centre of the
  //      road, v is the distance from the patch centre so it no longer spans
  //      0..hundreds.
  {
    constexpr int kCornerSteps = 8;
    constexpr int kEdgeRows = 3; // rim, edge-line outer, edge-line inner
    auto cr2 = [](const glm::vec2 &a, const glm::vec2 &b) {
      return a.x * b.y - a.y * b.x;
    };
    // Emit a triangle with whichever winding faces up (+Y) in this renderer.
    auto triUp = [&](uint32_t i0, uint32_t i1, uint32_t i2) {
      const glm::vec3 &p0 = m_vertices[i0].position;
      const glm::vec3 &p1 = m_vertices[i1].position;
      const glm::vec3 &p2 = m_vertices[i2].position;
      const float cy =
          (p1.z - p0.z) * (p2.x - p0.x) - (p1.x - p0.x) * (p2.z - p0.z);
      if (std::fabs(cy) < 1e-10f)
        return;
      if (cy > 0.0f) {
        m_indices.push_back(i0);
        m_indices.push_back(i1);
        m_indices.push_back(i2);
      } else {
        m_indices.push_back(i0);
        m_indices.push_back(i2);
        m_indices.push_back(i1);
      }
    };

    struct PatchArm {
      int seg = -1, ring = -1;
      uint32_t base = 0; // first vertex (j = 0) of the cut ring
      glm::vec2 out{1.0f,
                    0.0f}; // unit direction leaving the patch along this arm
      bool ascCCW =
          true; // increasing j runs counter-clockwise around the patch
      float angle = 0.0f;
    };

    for (const CrossingPatch &patch : crossingPatches) {
      std::vector<PatchArm> arms;
      int padHost = -1;
      for (const auto &ring : patch.boundaryRings) {
        const int s = ring.first, i = ring.second;
        if (s < 0 || s >= S || i < 0 ||
            i >= (int)segments[s].splinePoints.size())
          continue;
        bool dup = false;
        for (const PatchArm &a : arms)
          dup |= (a.seg == s && a.ring == i);
        if (dup)
          continue;

        const uint32_t base = firstRingBase[s] + (uint32_t)i * kCrossCount;
        if (base + (uint32_t)kCrossCount > m_vertices.size())
          continue;
        const glm::vec3 &p0 = m_vertices[base].position;
        const glm::vec3 &p1 = m_vertices[base + kCrossCount - 1].position;
        const glm::vec2 q0(p0.x, p0.z), q1(p1.x, p1.z);
        glm::vec2 t(segments[s].tangents[i].x, segments[s].tangents[i].z);
        if (glm::dot(t, t) < 1e-8f)
          continue;
        t = glm::normalize(t);
        const glm::vec2 rel = (q0 + q1) * 0.5f - patch.center;
        if (glm::dot(rel, rel) < 1e-8f)
          continue;

        PatchArm arm;
        arm.seg = s;
        arm.ring = i;
        arm.base = base;
        arm.out = (glm::dot(rel, t) >= 0.0f) ? t : -t;
        arm.ascCCW = cr2(q0 - patch.center, q1 - patch.center) > 0.0f;
        arm.angle = std::atan2(rel.y, rel.x);
        arms.push_back(arm);

        if (padHost < 0 ||
            (int)segments[s].type < (int)segments[padHost].type ||
            ((int)segments[s].type == (int)segments[padHost].type &&
             segments[s].width > segments[padHost].width))
          padHost = s;
      }
      const int N = (int)arms.size();
      if (padHost < 0 || N < 2)
        continue;
      std::sort(arms.begin(), arms.end(),
                [](const PatchArm &a, const PatchArm &b) {
                  return a.angle < b.angle;
                });

      const RoadSegment &host = segments[padHost];
      const float hostType = (float)(int)host.type;
      const float hostPriority =
          std::max(0.0f, host.width) * 0.1f + std::max(0.0f, 5.0f - hostType);
      const glm::vec2 C = patch.center;

      // k = 0 .. kCrossCount-1 runs counter-clockwise around the patch.
      auto ringVertex = [&](const PatchArm &a, int k) -> uint32_t {
        return a.base + (uint32_t)(a.ascCCW ? k : (kCrossCount - 1 - k));
      };

      // Duplicate every ring vertex for the patch (folded u, local v).
      std::vector<std::array<uint32_t, kCrossCount>> armV(N);
      for (int ai = 0; ai < N; ++ai) {
        for (int k = 0; k < kCrossCount; ++k) {
          RoadVertex v = m_vertices[ringVertex(arms[ai], k)];
          v.uv.x = std::min(v.uv.x, 1.0f - v.uv.x);
          v.uv.y = glm::length(glm::vec2(v.position.x, v.position.z) - C) *
                   TEX_SCALE;
          v.roadType = hostType;
          v.surfacePriority = hostPriority;
          armV[ai][k] = (uint32_t)m_vertices.size();
          m_vertices.push_back(v);
        }
      }

      // Corner strips between neighbouring arms (arm ci -> arm ci+1, CCW).
      // Each row follows the road edge straight up to the corner, then rounds
      // it with an arc tangent to both edges:
      //     P_A --straight-- Q_A ~~arc~~ Q_B --straight-- P_B
      // where M (the point the two edge lines would meet at) is the arc's
      // control point. This works for concave corners (T/cross) and for the
      // outer corner of a bend alike; falls back to a straight chord when the
      // edges are near parallel or M is not a sensible distance from the patch.
      constexpr int kRowPoints =
          kCornerSteps + 3; // P_A, Q_A, arc(1..steps-1), Q_B, P_B
      struct Corner {
        std::array<std::vector<uint32_t>, kEdgeRows> row;
      };
      std::vector<Corner> corners(N);
      for (int ci = 0; ci < N; ++ci) {
        const int cj = (ci + 1) % N;
        const PatchArm &a = arms[ci];
        const PatchArm &b = arms[cj];
        const float d = cr2(a.out, b.out);
        const float run =
            junctionFilletRun(segments[a.seg].width, segments[b.seg].width);
        const float reach =
            2.2f * 0.5f *
                std::max(segments[a.seg].width, segments[b.seg].width) +
            2.0f;
        for (int r = 0; r < kEdgeRows; ++r) {
          const uint32_t ia = armV[ci][kCrossCount - 1 - r];
          const uint32_t ib = armV[cj][r];
          const RoadVertex va =
              m_vertices[ia]; // copies: push_back below reallocates
          const RoadVertex vb = m_vertices[ib];
          const glm::vec2 pa(va.position.x, va.position.z);
          const glm::vec2 pb(vb.position.x, vb.position.z);

          // Where the two edge lines meet; valid only BEHIND both cut points.
          bool haveM = false;
          glm::vec2 M(0.0f);
          if (std::fabs(d) > 0.05f) {
            const glm::vec2 ab = pb - pa;
            const float ta = cr2(ab, b.out) / d;
            const float tb = cr2(ab, a.out) / d;
            if (ta < -1e-3f && tb < -1e-3f) {
              M = pa + a.out * ta;
              haveM = glm::length(M - C) <= reach;
            }
          }

          std::array<glm::vec2, kRowPoints> pts;
          if (haveM) {
            const float legA = glm::length(pa - M), legB = glm::length(pb - M);
            const float rr = std::min({run, legA, legB});
            const glm::vec2 qa = M + (pa - M) * (rr / legA);
            const glm::vec2 qb = M + (pb - M) * (rr / legB);
            pts[0] = pa;
            pts[1] = qa;
            for (int st = 1; st < kCornerSteps; ++st) {
              const float t = (float)st / (float)kCornerSteps;
              pts[1 + st] = qa * ((1.0f - t) * (1.0f - t)) +
                            M * (2.0f * (1.0f - t) * t) + qb * (t * t);
            }
            pts[kCornerSteps + 1] = qb;
            pts[kCornerSteps + 2] = pb;
          } else {
            for (int q = 0; q < kRowPoints; ++q)
              pts[q] = glm::mix(pa, pb, (float)q / (float)(kRowPoints - 1));
          }

          auto &rowIdx = corners[ci].row[r];
          rowIdx.push_back(ia);
          for (int q = 1; q < kRowPoints - 1; ++q) {
            const float t = (float)q / (float)(kRowPoints - 1);
            RoadVertex v = (t < 0.5f) ? va : vb;
            v.position = glm::vec3(
                pts[q].x, glm::mix(va.position.y, vb.position.y, t), pts[q].y);
            glm::vec3 nrm = glm::mix(va.normal, vb.normal, t);
            const float nl = glm::length(nrm);
            v.normal = nl > 1e-6f ? nrm / nl : glm::vec3(0.0f, 1.0f, 0.0f);
            v.uv = glm::vec2(kCross[r], glm::length(pts[q] - C) * TEX_SCALE);
            v.markMask = glm::mix(va.markMask, vb.markMask, t);
            rowIdx.push_back((uint32_t)m_vertices.size());
            m_vertices.push_back(v);
          }
          rowIdx.push_back(ib);
        }
        for (int r = 0; r + 1 < kEdgeRows; ++r)
          for (int st = 0; st + 1 < kRowPoints; ++st) {
            const uint32_t i00 = corners[ci].row[r][st],
                           i01 = corners[ci].row[r][st + 1];
            const uint32_t i10 = corners[ci].row[r + 1][st],
                           i11 = corners[ci].row[r + 1][st + 1];
            triUp(i00, i01, i10);
            triUp(i10, i01, i11);
          }
      }

      // Fan boundary: per arm its inner vertices, then the inner row of the
      // corner.
      std::vector<uint32_t> loop;
      for (int ai = 0; ai < N; ++ai) {
        for (int k = kEdgeRows - 1; k <= kCrossCount - kEdgeRows; ++k)
          loop.push_back(armV[ai][k]);
        const auto &inner = corners[ai].row[kEdgeRows - 1];
        for (size_t q = 1; q + 1 < inner.size(); ++q)
          loop.push_back(inner[q]);
      }
      if (loop.size() < 3)
        continue;

      // Centre: height as before, normal = average of the ring normals.
      float baseCenterY = 0.0f;
      int centerSampleCount = 0;
      for (const auto &sample : patch.centerSamples) {
        const int s = sample.first, i = sample.second;
        if (s < 0 || s >= S || i < 0 ||
            i >= (int)segments[s].splinePoints.size())
          continue;
        baseCenterY += segments[s].splinePoints[i].y;
        ++centerSampleCount;
      }
      if (centerSampleCount > 0)
        baseCenterY /= (float)centerSampleCount;
      float centerY = baseCenterY;
      // renderedGround already contains the baked bed offset; no deformation term.
      if (sampler.isValid())
        centerY += (sampler.renderedGround(C.x, C.y) - baseCenterY) * EDGE_CONFORM;
      centerY = minSurfaceYFootprint(glm::vec3(C.x, 0.0f, C.y), centerY + Z_BIAS + crownHeight, 8.0f, 8.0f);

      glm::vec3 nSum(0.0f);
      for (int ai = 0; ai < N; ++ai)
        nSum += m_vertices[armV[ai][kCrossCount / 2]].normal;
      const float nsl = glm::length(nSum);
      const glm::vec3 centerNormal =
          nsl > 1e-6f ? nSum / nsl : glm::vec3(0.0f, 1.0f, 0.0f);

      RoadVertex cv = m_vertices[armV[0][kCrossCount / 2]];
      cv.position = glm::vec3(C.x, centerY, C.y);
      cv.normal = centerNormal;
      cv.uv = glm::vec2(0.5f, 0.0f);
      cv.roadType = hostType;
      cv.surfacePriority = hostPriority;
      cv.markMask = 0.0f;
      const uint32_t centerIndex = (uint32_t)m_vertices.size();
      m_vertices.push_back(cv);

      for (size_t i = 0; i < loop.size(); ++i)
        triUp(centerIndex, loop[i], loop[(i + 1) % loop.size()]);
      ++crossPadCount;
    }
  }

  // -- 5c. Shader-space rounded T-junction borders ---------------------------
  // Pass curved border paths to street.frag so the existing road material
  // paints the stripe and curb on the dominant road surface. No separate
  // line/pad mesh is emitted, which keeps the join at the same height and
  // texture as its roads.
  constexpr float kBezierQuarter = 0.55228475f;
  for (const BranchContact &contact : branchContacts) {
    const RoadSegment &branch = segments[contact.seg];
    const RoadSegment &host = segments[contact.host];
    if (branch.splinePoints.size() < 2 || host.splinePoints.size() < 2)
      continue;
    const int endIndex =
        contact.end == 0 ? 0 : (int)branch.splinePoints.size() - 1;
    glm::vec2 branchRight(branch.rightVecs[endIndex].x,
                          branch.rightVecs[endIndex].z);
    glm::vec2 hostT = glm::normalize(contact.hostTangent);
    if (glm::length(branchRight) < 1e-4f || glm::length(hostT) < 1e-4f)
      continue;
    const glm::vec2 hostRight(hostT.y, -hostT.x);
    const float radius = branchFilletRadius(contact);

    // Sample the actual curved road frame where its painted edge enters the
    // fillet. Extrapolating the endpoint tangent put the shader curve in a
    // different place from the existing stripe on bent approach roads.
    const auto &distance = branch.cumulativeDist;
    if (distance.size() != branch.splinePoints.size() ||
        distance.size() != branch.rightVecs.size() ||
        distance.size() != branch.tangents.size() || distance.size() < 2)
      continue;
    const float targetDistance =
        contact.end == 0 ? distance.front() + radius : distance.back() - radius;
    int startRing = 0;
    for (int i = 0; i + 1 < (int)distance.size(); ++i) {
      if (distance[i] <= targetDistance && targetDistance <= distance[i + 1]) {
        startRing = i;
        break;
      }
      if (targetDistance > distance.back())
        startRing = (int)distance.size() - 2;
    }
    const float ringSpan = distance[startRing + 1] - distance[startRing];
    const float ringT =
        ringSpan > 1e-5f
            ? glm::clamp((targetDistance - distance[startRing]) / ringSpan,
                         0.0f, 1.0f)
            : 0.0f;
    glm::vec2 branchStartTangent = glm::mix(
        glm::vec2(branch.tangents[startRing].x, branch.tangents[startRing].z),
        glm::vec2(branch.tangents[startRing + 1].x,
                  branch.tangents[startRing + 1].z),
        ringT);
    if (glm::length(branchStartTangent) < 1e-4f)
      continue;
    branchStartTangent = glm::normalize(branchStartTangent);
    // The curve runs from the road interior toward the attached endpoint.
    // That is opposite the spline tangent at a start endpoint and aligned at
    // an end endpoint. Using the unflipped tangent on start joins sent the
    // Bezier control point back into the branch, creating the stray arcs seen
    // in the game screenshot.
    if (contact.end == 0)
      branchStartTangent = -branchStartTangent;
    // Match the shader's full outer pavement band: u=[0, .067] on either side.
    constexpr float pavementBandCenterU = 0.0335f;
    const float hostCurbOffset = host.width * (0.5f - pavementBandCenterU);
    const float hostWhiteOffset = roadWhiteEdgeLineOffset(host.width);
    const float turnOrientation =
        glm::dot(branchRight, hostT) >= 0.0f ? 1.0f : -1.0f;

    // Derive the curve anchors from the exact cross-section positions used by
    // the road ribbon. The frame and bend-width helper must match here or the
    // shader-painted lines drift away from the real road edges at T joins.
    const float branchHalfWidth = branch.width * 0.5f;
    auto ribbonEdgeAt = [&](int ring, float u) {
      const float off = u * 2.0f - 1.0f;
      const float edgeWidth =
          RoadSpline::meshHalfWidth(branch, ring, false, off, branchHalfWidth);
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
    for (float side : {-1.0f, 1.0f}) {
      const float edgeU = side < 0.0f ? 0.085f : 0.915f;
      const float curbU =
          side < 0.0f ? pavementBandCenterU : 1.0f - pavementBandCenterU;
      whiteMouthHalfSpan = std::max(
          whiteMouthHalfSpan,
          std::fabs(glm::dot(ribbonEdgeAt(endIndex, edgeU) - contact.position,
                             hostT)));
      pavementMouthHalfSpan = std::max(
          pavementMouthHalfSpan,
          std::fabs(glm::dot(ribbonEdgeAt(endIndex, curbU) - contact.position,
                             hostT)));
    }
    m_joinShaderContacts.push_back(
        {contact.hostPosition, hostT, contact.hostSide, host.width,
         whiteMouthHalfSpan + radius, pavementMouthHalfSpan + radius, radius});

    for (float side : {-1.0f, 1.0f}) {
      const glm::vec2 turn = hostT * (side * turnOrientation);
      const float edgeU = side < 0.0f ? 0.085f : 0.915f;
      const float curbU =
          side < 0.0f ? pavementBandCenterU : 1.0f - pavementBandCenterU;
      const glm::vec2 whiteAtJoin = ribbonEdgeAt(endIndex, edgeU);
      const glm::vec2 whiteStart = ribbonEdgeAtFilletStart(edgeU);
      // Land on the host's actual painted edge stripe. The branch endpoint
      // is snapped to that stripe; preserve any offset along the host tangent
      // while joining.
      const glm::vec2 hostWhite =
          contact.hostPosition +
          hostRight * (contact.hostSide * hostWhiteOffset);
      const float whiteAlongHost = glm::dot(whiteAtJoin - hostWhite, hostT);
      const glm::vec2 whiteEnd =
          hostWhite +
          hostT * (whiteAlongHost + side * turnOrientation * radius);
      m_joinShaderStrokes.push_back(
          {whiteStart,
           whiteStart + branchStartTangent * (radius * kBezierQuarter),
           whiteEnd - turn * (radius * kBezierQuarter), whiteEnd,
           branch.width * 0.018f, false});

      const glm::vec2 curbAtBranch = ribbonEdgeAtFilletStart(curbU);
      const glm::vec2 curbAtBranchJoin = ribbonEdgeAt(endIndex, curbU);
      const glm::vec2 curbAtHost =
          contact.hostPosition +
          hostRight * (contact.hostSide * hostCurbOffset);
      const float curbAlongHost =
          glm::dot(curbAtBranchJoin - curbAtHost, hostT);
      const glm::vec2 curbStart = curbAtBranch;
      const glm::vec2 curbEnd =
          curbAtHost +
          hostT * (curbAlongHost + side * turnOrientation * radius);
      m_joinShaderStrokes.push_back(
          {curbStart,
           curbStart + branchStartTangent * (radius * kBezierQuarter),
           curbEnd - turn * (radius * kBezierQuarter), curbEnd,
           branch.width * pavementBandCenterU, true});
    }
  }

  // Single authoritative pass for the surface-texture coordinate.
  //
  // MetricUV must be the WORLD XZ position: street.frag samples the tarred gravel as
  // texture(roadSurfaceTex, MetricUV / roadSurfaceScale) and the same for the normal
  // map, so the parameterization has to be isotropic in world space and identical
  // everywhere or the texture jumps across a seam.
  //
  // It used to be set only in the ribbon ring emitter, from a per-segment across/along
  // pair -- and six other emissions (junction pads, end caps, both arc fans) pass a
  // 6-field aggregate initializer to a 7-member struct, so their metricUV
  // value-initialized to (0,0). With metricUV constant across a polygon its screen
  // derivative is zero, so every fragment there sampled LOD 0 of ONE texel: a flat,
  // untextured, uniformly lit polygon with hard straight edges sitting on the
  // junction. Stamping from the position here fixes all of them at once, and cannot
  // be regressed by a future emission forgetting the field.
  stampMetricUV();

  m_indexCount = (int)m_indices.size();
  std::cout << "[RoadMeshBuilder] Generated " << m_vertices.size()
            << " vertices, " << (m_indexCount / 3) << " triangles (" << capCount
            << " caps, " << jointCount << " joints, " << jnCount
            << " junction wedges, " << branchContacts.size() << " T-joins, "
            << m_joinShaderStrokes.size() << " shader border curves, "
            << m_joinShaderContacts.size() << " shader contact masks, "
            << crossSites.size() << " crossings, " << crossPadCount
            << " crossing pads, " << bareSamples << " bare merge samples, "
            << (m_shoulderQuads / 4) << " verge quads)"
            << std::endl;
}

// MetricUV is the world XZ position: street.frag samples the road surface and its
// normal map as texture(..., MetricUV / roadSurfaceScale), which only reads as a
// continuous material if the coordinate is world-space and shared by every vertex.
// Deriving it from the position rather than carrying it through each emitter makes
// that guarantee structural.
void RoadMeshBuilder::stampMetricUV() {
  // World-XZ projection first, for every vertex not emitted by the ribbon ring
  // emitter: junction pads, end caps and the arc fans. A constant coordinate would
  // sample a single texel and collapse them to a flat, untextured polygon (the old
  // junction-pad defect), so they need a real world-space coordinate.
  for (RoadVertex &v : m_vertices)
    v.metricUV = glm::vec2(v.position.x, v.position.z);

  // Then restore the road-following coordinate on the carriageway ribbons so the
  // surface texture turns with the road instead of staying locked to the world axes.
  for (const auto &ribbon : m_ribbonMetricUV) {
    if (ribbon.first < m_vertices.size())
      m_vertices[ribbon.first].metricUV = ribbon.second;
  }
}

void RoadMeshBuilder::upload() {
  // Create the GL objects only once and re-fill them in place afterwards. The old
  // version deleted and re-glGen'd the VAO/VBO/EBO on every upload, and the road
  // pipeline uploads more than once per frame while a control point is dragged.
  // An empty rebuild must still release the previous buffers: rendering stops
  // because m_VAO is 0, which removes the stale-geometry case the early return
  // used to leave behind.
  if (m_vertices.empty() || m_indices.empty()) {
    destroy();
    return;
  }

  if (m_VAO == 0) glGenVertexArrays(1, &m_VAO);
  if (m_VBO == 0) glGenBuffers(1, &m_VBO);
  if (m_EBO == 0) glGenBuffers(1, &m_EBO);
  if (m_VAO == 0 || m_VBO == 0 || m_EBO == 0) {
    std::cerr << "[RoadMeshBuilder] Failed to allocate GPU buffers" << std::endl;
    return;
  }

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
                        (void *)offsetof(RoadVertex, position));

  // Layout 1: vec3 normal
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(RoadVertex),
                        (void *)offsetof(RoadVertex, normal));

  // Layout 2: vec2 uv
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(RoadVertex),
                        (void *)offsetof(RoadVertex, uv));

  // Layout 3: float roadType
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(RoadVertex),
                        (void *)offsetof(RoadVertex, roadType));

  // Layout 5: per-road overlap priority (wider roads dominate at crossings).
  glEnableVertexAttribArray(5);
  glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(RoadVertex),
                        (void *)offsetof(RoadVertex, surfacePriority));

  // Layout 4: float markMask (1 = paint markings, 0 = bare asphalt in a
  // junction)
  glEnableVertexAttribArray(4);
  glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(RoadVertex),
                        (void *)offsetof(RoadVertex, markMask));

  // Layout 6: vec2 metricUV (metres across and along the road) for the surface
  // texture. Location 5 stays the overlap priority for the depth bias in street.vert.
  glEnableVertexAttribArray(6);
  glVertexAttribPointer(6, 2, GL_FLOAT, GL_FALSE, sizeof(RoadVertex),
                        (void *)offsetof(RoadVertex, metricUV));

  glBindVertexArray(0);

  std::cout << "[RoadMeshBuilder] Uploaded to GPU: VAO=" << m_VAO << std::endl;
}

void RoadMeshBuilder::render(Shader &shader) const {
  if (m_VAO == 0 || m_indexCount == 0)
    return;

  shader.use();
  constexpr int kMaxJoinStrokes = 32;
  std::array<glm::vec4, kMaxJoinStrokes * 4> curvePoints{};
  std::array<glm::vec4, kMaxJoinStrokes> strokeInfo{};
  constexpr int kMaxJoinContacts = 32;
  std::array<glm::vec4, kMaxJoinContacts> contactCenterT{};
  std::array<glm::vec4, kMaxJoinContacts> contactInfo{};
  std::array<glm::vec4, kMaxJoinContacts> contactDims{};
  const int joinCount =
      std::min((int)m_joinShaderStrokes.size(), kMaxJoinStrokes);
  const int contactCount =
      std::min((int)m_joinShaderContacts.size(), kMaxJoinContacts);
  for (int i = 0; i < joinCount; ++i) {
    const RoadJoinShaderStroke &stroke = m_joinShaderStrokes[i];
    curvePoints[i * 4 + 0] = glm::vec4(stroke.p0, 0.0f, 0.0f);
    curvePoints[i * 4 + 1] = glm::vec4(stroke.p1, 0.0f, 0.0f);
    curvePoints[i * 4 + 2] = glm::vec4(stroke.p2, 0.0f, 0.0f);
    curvePoints[i * 4 + 3] = glm::vec4(stroke.p3, 0.0f, 0.0f);
    strokeInfo[i] =
        glm::vec4(stroke.halfWidth, stroke.curb ? 1.0f : 0.0f, 0.0f, 0.0f);
  }
  for (int i = 0; i < contactCount; ++i) {
    const RoadJoinShaderContact &contact = m_joinShaderContacts[i];
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
               (GLsizei)contactCenterT.size(),
               glm::value_ptr(contactCenterT[0]));
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
  // wrote depth, so the semi-transparent fringe fought with the terrain
  // underneath and produced flickering speckle along both road edges.
  // street.frag now dithers the shoulder away with discard() instead, so the
  // ribbon can be drawn fully opaque and every fragment it shades is a fragment
  // it owns.
  glDisable(GL_BLEND);

  glBindVertexArray(m_VAO);
  glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);

  glDisable(GL_POLYGON_OFFSET_FILL);
}
