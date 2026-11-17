#include "catch.hpp"

#include "util/builders.h"
#include "glm/glm.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace Tangram;

// Checks for Builders::buildPolyLine: each mesh is extruded at half-width W (pos = coord + extrude*W,
//  v = uv.y + vshift*W, as the polyline shader does) and checked for folded triangles and, with tex coords,
//  for v being an affine function of position with unit gradient over each triangle (no texture distortion)

namespace {

struct PLVertex {
    glm::vec2 coord;
    glm::vec2 extrude;
    glm::vec2 uv;
    float vshift;
};

struct PLMesh {
    std::vector<PLVertex> vertices;
    std::vector<uint16_t> indices;
};

PLMesh buildLine(const Line& _line, JoinTypes _join, CapTypes _cap, bool _texCoords, float _maxWidth) {
    PLMesh mesh;
    PolyLineBuilder builder([&](const glm::vec2& coord, const glm::vec2& enormal, const glm::vec2& uv,
                                float vshift) {
        mesh.vertices.push_back({coord, enormal, uv, vshift});
    }, _cap, _join);
    builder.useTexCoords = _texCoords;
    builder.maxWidth = _maxWidth;
    Builders::buildPolyLine(_line, builder);
    REQUIRE(builder.numVertices == mesh.vertices.size());
    mesh.indices = builder.indices;
    return mesh;
}

struct MeshCheck {
    int numFolded = 0;  // triangles with negative area
    int numDistorted = 0;  // triangles where |grad v| differs from 1 by more than tolerance
    float maxGradErr = 0;
    float minArea = 0;
};

MeshCheck checkMesh(const PLMesh& _mesh, float _width, bool _texCoords, float _gradTol) {
    MeshCheck res;
    REQUIRE(_mesh.indices.size() % 3 == 0);
    for (size_t ii = 0; ii < _mesh.indices.size(); ii += 3) {
        glm::vec2 p[3];
        float v[3];
        for (int jj = 0; jj < 3; jj++) {
            uint16_t idx = _mesh.indices[ii + jj];
            REQUIRE(idx < _mesh.vertices.size());
            const PLVertex& vtx = _mesh.vertices[idx];
            p[jj] = vtx.coord + vtx.extrude*_width;
            v[jj] = vtx.uv.y + vtx.vshift*_width;
        }
        glm::vec2 e1 = p[1] - p[0], e2 = p[2] - p[0];
        float cross = e1.x*e2.y - e1.y*e2.x;  // twice signed area; CCW positive
        res.minArea = std::min(res.minArea, cross/2);
        if (cross < -1e-6f) { res.numFolded++; }
        // gradient of v: solve [e1; e2] * g = [v1 - v0; v2 - v0]; skip (near) degenerate triangles
        if (!_texCoords || std::abs(cross) < 1e-4f) { continue; }
        float dv1 = v[1] - v[0], dv2 = v[2] - v[0];
        glm::vec2 g((dv1*e2.y - dv2*e1.y)/cross, (e1.x*dv2 - e2.x*dv1)/cross);
        float err = std::abs(glm::length(g) - 1);
        res.maxGradErr = std::max(res.maxGradErr, err);
        if (err > _gradTol) { res.numDistorted++; }
    }
    return res;
}

struct TestLine {
    const char* name;
    Line line;
    float maxWidth;
    float gradTol;  // shallow miter joins share vertices w/ vshift 0, which allows small texture distortion
};

Line arc(float _radius, float _stepDeg, int _steps) {
    Line line;
    for (int ii = 0; ii <= _steps; ii++) {
        float a = ii*_stepDeg*3.14159265f/180;
        line.push_back({_radius*std::sin(a), _radius*(1 - std::cos(a))});
    }
    return line;
}

std::vector<TestLine> testLines() {
    return {
        { "straight", {{0, 0}, {10, 0}}, 0.5f, 1e-3f },
        { "collinear", {{0, 0}, {5, 0}, {10, 0}}, 0.5f, 1e-3f },
        { "repeated", {{0, 0}, {1, 0}, {1, 0}, {2, 1}}, 0.5f, 1e-3f },
        { "zigzag", {{0, 0}, {2, 1}, {4, 0}, {6, 1}, {8, 0}}, 0.5f, 1e-3f },
        { "zigzag-wide", {{0, 0}, {2, 1}, {4, 0}, {6, 1}, {8, 0}}, 2.f, 1e-3f },
        { "sharp", {{0, 0}, {5, 0}, {0, 0.5f}, {5, 1}}, 0.5f, 1e-3f },
        { "short", {{0, 0}, {1, 0}, {1.1f, 0.1f}, {1.2f, 0}, {1.3f, 0.15f}, {2.3f, 0.15f}}, 0.5f, 1e-3f },
        { "uturn", {{0, 0}, {5, 0}, {5, 0.4f}, {0, 0.4f}}, 0.5f, 1e-3f },
        { "reverse", {{0, 0}, {5, 0}, {1, 0}}, 0.5f, 1e-3f },
        // segments 0.87 long, W = 0.5: v error at shared vertices <= tan(2.5 deg)*W = 0.022
        { "arc", arc(10, 5, 18), 0.5f, 0.06f },
        { "arc-cw", [](){ Line l = arc(10, 5, 18); for (auto& p : l) { p.y = -p.y; } return l; }(), 0.5f, 0.06f },
    };
}

const char* joinName(JoinTypes j) {
    return j == JoinTypes::miter ? "miter" : j == JoinTypes::bevel ? "bevel" : "round";
}
const char* capName(CapTypes c) {
    return c == CapTypes::butt ? "butt" : c == CapTypes::square ? "square" : "round";
}

const JoinTypes allJoins[] = { JoinTypes::miter, JoinTypes::bevel, JoinTypes::round };
const CapTypes allCaps[] = { CapTypes::butt, CapTypes::square, CapTypes::round };

}  // namespace

TEST_CASE("Polyline builder: no folds, affine tex coords", "[polyline][builders]") {
    for (const TestLine& tl : testLines()) {
        for (JoinTypes join : allJoins) {
            for (CapTypes cap : allCaps) {
                for (bool tex : {false, true}) {
                    PLMesh mesh = buildLine(tl.line, join, cap, tex, tl.maxWidth);
                    for (float frac : {0.25f, 0.5f, 1.f}) {
                        float width = frac*tl.maxWidth;
                        MeshCheck res = checkMesh(mesh, width, tex, tl.gradTol);
                        INFO(tl.name << " join " << joinName(join) << " cap " << capName(cap)
                             << " tex " << tex << " W " << width << ": min area " << res.minArea
                             << ", max |grad v| error " << res.maxGradErr);
                        CHECK(res.numFolded == 0);
                        CHECK(res.numDistorted == 0);
                    }
                }
            }
        }
    }
}

TEST_CASE("Polyline builder: inner miter fallback prevents folding", "[polyline][builders]") {
    // two 90 deg turns joined by a segment shorter than the inner miters at W = 0.5
    Line uturn = {{0, 0}, {5, 0}, {5, 0.4f}, {0, 0.4f}};
    for (JoinTypes join : allJoins) {
        INFO("join " << joinName(join));
        PLMesh unlimited = buildLine(uturn, join, CapTypes::butt, false, 0);
        CHECK(checkMesh(unlimited, 0.5f, false, 0).numFolded > 0);
        PLMesh limited = buildLine(uturn, join, CapTypes::butt, false, 0.5f);
        CHECK(checkMesh(limited, 0.5f, false, 0).numFolded == 0);
    }
}

// Vertex and index counts per case, to catch accidental geometry growth.  To regenerate after an intended
//  change, run tests.out "[polylinecounts]" and copy the printed table.
TEST_CASE("Polyline builder: vertex and index counts", "[polyline][builders][polylinecounts]") {
    // {vertices, indices} for each line, then join (miter, bevel, round), cap (butt, square, round), tex (off, on)
    static const int expected[][3][3][2][2] = {
    { // straight
      { {{4, 6}, {4, 6}}, {{8, 18}, {8, 18}}, {{20, 42}, {20, 42}} },
      { {{4, 6}, {4, 6}}, {{8, 18}, {8, 18}}, {{20, 42}, {20, 42}} },
      { {{4, 6}, {4, 6}}, {{8, 18}, {8, 18}}, {{20, 42}, {20, 42}} }
    },
    { // collinear
      { {{6, 12}, {6, 12}}, {{10, 24}, {10, 24}}, {{22, 48}, {22, 48}} },
      { {{9, 15}, {11, 15}}, {{13, 27}, {15, 27}}, {{25, 51}, {27, 51}} },
      { {{13, 27}, {15, 27}}, {{17, 39}, {19, 39}}, {{29, 63}, {31, 63}} }
    },
    { // repeated
      { {{6, 12}, {8, 12}}, {{10, 24}, {12, 24}}, {{22, 48}, {24, 48}} },
      { {{10, 15}, {11, 15}}, {{14, 27}, {15, 27}}, {{26, 51}, {27, 51}} },
      { {{14, 27}, {15, 27}}, {{18, 39}, {19, 39}}, {{30, 63}, {31, 63}} }
    },
    { // zigzag
      { {{10, 24}, {16, 24}}, {{14, 36}, {20, 36}}, {{26, 60}, {32, 60}} },
      { {{22, 33}, {25, 33}}, {{26, 45}, {29, 45}}, {{38, 69}, {41, 69}} },
      { {{34, 69}, {37, 69}}, {{38, 81}, {41, 81}}, {{50, 105}, {53, 105}} }
    },
    { // zigzag-wide
      { {{10, 24}, {16, 24}}, {{14, 36}, {20, 36}}, {{26, 60}, {32, 60}} },
      { {{22, 33}, {25, 33}}, {{26, 45}, {29, 45}}, {{38, 69}, {41, 69}} },
      { {{34, 69}, {37, 69}}, {{38, 81}, {41, 81}}, {{50, 105}, {53, 105}} }
    },
    { // sharp
      { {{17, 24}, {18, 24}}, {{21, 36}, {22, 36}}, {{33, 60}, {34, 60}} },
      { {{17, 24}, {18, 24}}, {{21, 36}, {22, 36}}, {{33, 60}, {34, 60}} },
      { {{25, 48}, {26, 48}}, {{29, 60}, {30, 60}}, {{41, 84}, {42, 84}} }
    },
    { // short
      { {{16, 30}, {20, 30}}, {{20, 42}, {24, 42}}, {{32, 66}, {36, 66}} },
      { {{32, 42}, {32, 42}}, {{36, 54}, {36, 54}}, {{48, 78}, {48, 78}} },
      { {{48, 90}, {48, 90}}, {{52, 102}, {52, 102}}, {{64, 126}, {64, 126}} }
    },
    { // uturn
      { {{10, 18}, {12, 18}}, {{14, 30}, {16, 30}}, {{26, 54}, {28, 54}} },
      { {{18, 24}, {18, 24}}, {{22, 36}, {22, 36}}, {{34, 60}, {34, 60}} },
      { {{26, 48}, {26, 48}}, {{30, 60}, {30, 60}}, {{42, 84}, {42, 84}} }
    },
    { // reverse
      { {{11, 15}, {11, 15}}, {{15, 27}, {15, 27}}, {{27, 51}, {27, 51}} },
      { {{11, 15}, {11, 15}}, {{15, 27}, {15, 27}}, {{27, 51}, {27, 51}} },
      { {{15, 27}, {15, 27}}, {{19, 39}, {19, 39}}, {{31, 63}, {31, 63}} }
    },
    { // arc
      { {{38, 108}, {38, 108}}, {{42, 120}, {42, 120}}, {{54, 144}, {54, 144}
} },
      { {{106, 159}, {123, 159}}, {{110, 171}, {127, 171}}, {{122, 195},
{139, 195}} },
      { {{174, 363}, {191, 363}}, {{178, 375}, {195, 375}}, {{190, 399},
{207, 399}} }
    },
    { // arc-cw
      { {{38, 108}, {38, 108}}, {{42, 120}, {42, 120}}, {{54, 144}, {54, 144}
} },
      { {{106, 159}, {123, 159}}, {{110, 171}, {127, 171}}, {{122, 195},
{139, 195}} },
      { {{174, 363}, {191, 363}}, {{178, 375}, {195, 375}}, {{190, 399},
{207, 399}} }
    },
    };
    auto lines = testLines();
    REQUIRE(sizeof(expected)/sizeof(expected[0]) == lines.size());
    std::string table;
    std::string errors;
    for (size_t li = 0; li < lines.size(); li++) {
        table += "    { // " + std::string(lines[li].name) + "\n";
        for (int ji = 0; ji < 3; ji++) {
            table += "      {";
            for (int ci = 0; ci < 3; ci++) {
                table += " {";
                for (int ti = 0; ti < 2; ti++) {
                    PLMesh mesh = buildLine(lines[li].line, allJoins[ji], allCaps[ci], ti, lines[li].maxWidth);
                    int nv = int(mesh.vertices.size()), ni = int(mesh.indices.size());
                    const int* exp = expected[li][ji][ci][ti];
                    char buf[256];
                    if (nv != exp[0] || ni != exp[1]) {
                        snprintf(buf, sizeof(buf), "%s join %s cap %s tex %d: %d vertices, %d indices;"
                                 " expected %d, %d\n", lines[li].name, joinName(allJoins[ji]),
                                 capName(allCaps[ci]), ti, nv, ni, exp[0], exp[1]);
                        errors += buf;
                    }
                    snprintf(buf, sizeof(buf), "{%d, %d}%s", nv, ni, ti ? "" : ", ");
                    table += buf;
                }
                table += ci < 2 ? "}," : "}";
            }
            table += ji < 2 ? " },\n" : " }\n";
        }
        table += "    },\n";
    }
    // print table directly (Catch wraps long message lines)
    if (!errors.empty()) { printf("Polyline builder actual counts:\n%s", table.c_str()); }
    INFO(errors);
    CHECK(errors.empty());
}
