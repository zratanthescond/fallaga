// Standalone regression test for the road JSON reader.
//
//   vcvars64.bat && cl /nologo /std:c++17 /EHsc /O1 /DNOMINMAX /I src /I %VCPKG%\include
//       roadjson_test.cpp /Fe:roadjson_test.exe /Fo:%TEMP%\ /link opengl32.lib glew32.lib
//   roadjson_test.exe
//
// The reader lives in an anonymous namespace inside RoadSystem.cpp, so the TU is
// included directly to reach RoadGraph::loadFromJSON. Linking needs the same
// OpenGL/GLEW libraries as the engine (loadFromJSON itself never calls them).
//
// Every malformed case here used to hang the engine or crash it: the previous
// scanner could not consume an unknown key's value, mis-handled escapes, and
// threw an uncaught std::out_of_range on an out-of-range literal. A regression
// shows up as this binary never terminating.
#include "src/RoadSystem.cpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                      \
    } while (0)

static const char* kScratch = "C:/Users/honco/AppData/Local/Temp/opencode/roadjson_case.json";

static void writeCase(const std::string& body) {
    std::ofstream f(kScratch, std::ios::binary);
    f << body;
}

// A load must fail cleanly: false returned, previous graph untouched. Any other
// outcome (success on garbage, or a half-applied graph) is a failure.
static void expectRejected(const std::string& label, const std::string& body) {
    writeCase(body);
    RoadGraph g;
    g.segments.resize(1);                       // sentinel: must survive a failed load
    g.segments[0].id = 999;
    const bool ok = g.loadFromJSON(kScratch);
    ++g_checks;
    if (ok) {
        ++g_failures;
        std::printf("FAIL %s: malformed JSON was accepted\n", label.c_str());
    }
    ++g_checks;
    if (g.segments.size() != 1 || g.segments[0].id != 999) {
        ++g_failures;
        std::printf("FAIL %s: failed load mutated the graph (%zu segments)\n",
                    label.c_str(), g.segments.size());
    }
}

// The per-road corner radius must survive a save/load cycle, and a bad value must be
// rejected by the strict reader rather than silently clamped.
static void testCornerRadiusRoundTrip() {
    writeCase(R"({"roads":[
        {"id":7,"type":"urban_street","width":8,"corner":12.5,
         "points":[{"x":0,"z":0},{"x":30,"z":0},{"x":30,"z":30}]}
    ]})");
    RoadGraph g;
    CHECK(g.loadFromJSON(kScratch));
    CHECK(g.segments.size() == 1);
    CHECK(g.segments[0].cornerRadius == 12.5f);

    // Rejected: negative.
    writeCase(R"({"roads":[
        {"id":7,"type":"urban_street","corner":-1,
         "points":[{"x":0,"z":0},{"x":30,"z":0}]}
    ]})");
    RoadGraph bad;
    CHECK(!bad.loadFromJSON(kScratch));

    // Rejected: absurd.
    writeCase(R"({"roads":[
        {"id":7,"type":"urban_street","corner":9999,
         "points":[{"x":0,"z":0},{"x":30,"z":0}]}
    ]})");
    RoadGraph huge;
    CHECK(!huge.loadFromJSON(kScratch));

    // Accepted: exactly 0 means automatic.
    writeCase(R"({"roads":[
        {"id":7,"type":"urban_street","corner":0,
         "points":[{"x":0,"z":0},{"x":30,"z":0}]}
    ]})");
    RoadGraph zero;
    CHECK(zero.loadFromJSON(kScratch));
    CHECK(zero.segments.size() == 1);
    CHECK(zero.segments[0].cornerRadius == 0.0f);
}

// ── 1. The happy path, with every optional key present ────────────────────────
static void testValidFile() {
    writeCase(R"({"roads":[
        {"id":3,"type":"colonial_avenue","width":11.5,"shoulder":2.0,"closed":true,
         "points":[{"x":1.5,"z":-2.25},{"x":3,"z":4},{"x":10,"z":0.5}]},
        {"id":4,"type":"medina","points":[{"x":0,"z":0},{"x":1,"z":1}]}
    ]})");
    RoadGraph g;
    CHECK(g.loadFromJSON(kScratch));
    CHECK(g.segments.size() == 2);

    const RoadSegment& a = g.segments[0];
    CHECK(a.id == 3);
    CHECK(a.type == RoadType::ColonialAvenue);
    CHECK(a.width == 11.5f);
    CHECK(a.shoulderWidth == 2.0f);
    CHECK(a.closed);
    // "corner" is optional: absent means "automatic", never a silent zero.
    CHECK(a.cornerRadius == 0.0f);
    CHECK(a.controlPoints.size() == 3);
    CHECK(a.controlPoints[0].pos.x == 1.5f);
    CHECK(a.controlPoints[0].pos.y == -2.25f);   // pos.y carries world Z
    CHECK(a.controlPoints[2].pos.y == 0.5f);

    // Missing width/shoulder fall back to the road-type hierarchy.
    const RoadSegment& b = g.segments[1];
    CHECK(b.id == 4);
    CHECK(b.type == RoadType::Medina);
    CHECK(b.width == 5.0f);
    CHECK(b.shoulderWidth == 0.75f);
    CHECK(!b.closed);
    CHECK(b.controlPoints.size() == 2);
}

// ── 2. Tolerances the writer and hand edits rely on ──────────────────────────
static void testAcceptedInput() {
    // Unknown keys of every value type, extra top-level keys, reordered members,
    // single-line layout, and a segment with a single point (dropped, with a log).
    writeCase(R"({"version":3,"meta":"roads","roads":[
        {"points":[{"x":1,"z":2},{"x":3,"z":4}],"note":{"tags":[1,2,{"deep":true}]},
         "type":"port","id":0,"flags":null,"ratio":1.5e2,"live":false},
        {"type":"country_dirt","id":1,"points":[{"x":9,"z":9}]}
    ]})");
    RoadGraph g;
    CHECK(g.loadFromJSON(kScratch));
    CHECK(g.segments.size() == 1);
    CHECK(g.segments[0].type == RoadType::Port);
    CHECK(g.segments[0].controlPoints.size() == 2);

    // Escapes: \" must not end the string, \u must decode, \n must become a newline.
    writeCase(R"({"roads":[{"id":2,"type":"urban_street",
        "note":"say \"hi\" é\ttab","points":[{"x":1,"z":2},{"x":3,"z":4}]}]})");
    RoadGraph h;
    CHECK(h.loadFromJSON(kScratch));
    CHECK(h.segments.size() == 1);

    // A UTF-8 BOM, as left by some editors, must be ignored.
    writeCase("\xEF\xBB\xBF{\"roads\":[{\"id\":5,\"type\":\"medina\","
              "\"points\":[{\"x\":1,\"z\":2},{\"x\":3,\"z\":4}]}]}");
    RoadGraph b;
    CHECK(b.loadFromJSON(kScratch));
    CHECK(b.segments.size() == 1);
    CHECK(b.segments[0].id == 5);

    // An empty roads array is valid, just useless.
    writeCase(R"({"roads":[]})");
    RoadGraph e;
    CHECK(!e.loadFromJSON(kScratch));
    CHECK(e.segments.empty());
}

// ── 3. Malformed input: reported, never hung on, never guessed ────────────────
static void testMalformedInput() {
    // A syntax error must fail cleanly: false returned, previous graph untouched.
    expectRejected("unquoted key",
        R"({"roads":[{id:1,"points":[{"x":1,"z":2}]}]})");
    expectRejected("non-numeric number",
        R"({"roads":[{"id":1,"points":[{"x":abc,"z":2},{"x":3,"z":4}]}]})");

    // std::stof threw an uncaught std::out_of_range on this one.
    expectRejected("number out of float range",
        R"({"roads":[{"id":1,"points":[{"x":1e999,"z":2},{"x":3,"z":4}]}]})");
    expectRejected("float range, finite double",
        R"({"roads":[{"id":1,"points":[{"x":1e300,"z":2},{"x":3,"z":4}]}]})");
    expectRejected("fractional id",
        R"({"roads":[{"id":1.5,"points":[{"x":1,"z":2},{"x":3,"z":4}]}]})");
    expectRejected("negative id",
        R"({"roads":[{"id":-1,"points":[{"x":1,"z":2},{"x":3,"z":4}]}]})");

    // A half-written file, e.g. an autosave interrupted mid-write. The old parser
    // "succeeded" with a fabricated point on the origin.
    expectRejected("truncated object",
        R"({"roads":[{"id":1,"points":[{"x":1,"z":2},{"x":3,"z")");
    expectRejected("truncated after a point",
        R"({"roads":[{"id":1,"points":[{"x":1,"z":2},{"x":3,"z");
    expectRejected("unterminated string",
        R"({"roads":[{"id":1,"type":"urban_street","points":[{"x":1,"z":2}]})");
    expectRejected("truncated number",
        R"({"roads":[{"id":1,"points":[{"x":1.,"z":2}]}]})");
    expectRejected("truncated exponent",
        R"({"roads":[{"id":1,"points":[{"x":1e,"z":2}]}]})");
    expectRejected("trailing comma in an object",
        R"({"roads":[{"id":1,"points":[{"x":1,"z":2},{"x":3,"z":4}],}]})");
    expectRejected("missing comma between members",
        R"({"roads":[{"id":1 "type":"medina"}]})");
    expectRejected("unquoted road type",
        R"({"roads":[{"id":1,"type":medina,"points":[{"x":1,"z":2},{"x":3,"z":4}]}]})");
    expectRejected("unknown road type",
        R"({"roads":[{"id":1,"type":"cobblestone","points":[{"x":1,"z":2},{"x":3,"z":4}]}]})");
    expectRejected("point without z",
        R"({"roads":[{"id":1,"points":[{"x":1},{"x":3,"z":4}]}]})");
    expectRejected("bare number where a point is expected",
        R"({"roads":[{"id":1,"points":[1,2]}]})");
    expectRejected("points is not an array",
        R"({"roads":[{"id":1,"points":{"x":1,"z":2}}]})");
    expectRejected("roads is not an array",
        R"({"roads":{"id":1}})");
    expectRejected("no roads key",
        R"({"segments":[{"id":1}]})");
    expectRejected("empty file", "");
    expectRejected("not an object", R"([{"id":1}])");
    expectRejected("raw control character in a string",
        "{\"roads\":[{\"id\":1,\"type\":\"urban\tstreet\",\"points\":[{\"x\":1,\"z\":2},{\"x\":3,\"z\":4}]}]}");

    // A missing file is a different failure from a broken one, and stays silent
    // on stdout so the caller can tell them apart.
    RoadGraph missing;
    CHECK(!missing.loadFromJSON("C:/Users/honco/AppData/Local/Temp/opencode/nope_does_not_exist.json"));
    CHECK(missing.segments.empty());

    // Valid JSON that holds no usable road is not an error: the file is the
    // source of truth, so the graph is emptied and the caller is told "none".
    writeCase(R"({"roads":[{"id":1,"points":[{"x":1,"z":2}]}]})");
    RoadGraph onlyOnePoint;
    onlyOnePoint.segments.resize(1);
    CHECK(!onlyOnePoint.loadFromJSON(kScratch));
    CHECK(onlyOnePoint.segments.empty());
}

// ── 4. Round trip through the writer ─────────────────────────────────────────
static void testRoundTrip() {
    RoadGraph src;
    src.segments.resize(2);
    src.segments[0].id = 7;
    src.segments[0].type = RoadType::Medina;
    src.segments[0].width = 5.0f;
    src.segments[0].shoulderWidth = 0.75f;
    src.segments[0].closed = true;
    src.segments[0].controlPoints.resize(3);
    src.segments[0].controlPoints[0].pos = { 1.5f, -2.5f };
    src.segments[0].controlPoints[1].pos = { 3.0f, 4.0f };
    src.segments[0].controlPoints[2].pos = { -7.25f, 0.0f };
    src.segments[1].id = 8;
    src.segments[1].type = RoadType::CountryHighway;
    src.segments[1].width = 8.5f;          // the type default, written explicitly
    src.segments[1].shoulderWidth = 1.5f;
    src.segments[1].controlPoints.resize(2);
    src.segments[1].controlPoints[0].pos = { 100.0f, 200.0f };
    src.segments[1].controlPoints[1].pos = { 300.0f, -400.0f };

    const char* out = "C:/Users/honco/AppData/Local/Temp/opencode/roadjson_roundtrip.json";
    CHECK(src.saveToJSON(out));

    RoadGraph back;
    CHECK(back.loadFromJSON(out));
    if (back.segments.size() != 2) {
        std::printf("FAIL round trip produced %zu segments\n", back.segments.size());
        ++g_failures;
        return;
    }
    CHECK(back.segments[0].id == 7);
    CHECK(back.segments[0].type == RoadType::Medina);
    CHECK(back.segments[0].width == 5.0f);
    CHECK(back.segments[0].shoulderWidth == 0.75f);
    CHECK(back.segments[0].closed);
    CHECK(back.segments[0].controlPoints.size() == 3);
    CHECK(back.segments[0].controlPoints[0].pos.x == 1.5f);
    CHECK(back.segments[0].controlPoints[0].pos.y == -2.5f);
    CHECK(back.segments[0].controlPoints[2].pos.x == -7.25f);
    // The writer always emits width/shoulder, so they survive verbatim.
    CHECK(back.segments[1].type == RoadType::CountryHighway);
    CHECK(back.segments[1].width == 8.5f);
    CHECK(back.segments[1].shoulderWidth == 1.5f);
    CHECK(back.segments[1].controlPoints[1].pos.y == -400.0f);
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    testValidFile();
    testCornerRadiusRoundTrip();
    testAcceptedInput();
    testMalformedInput();
    testRoundTrip();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
