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
//  for v being an affine function of position w/ unit (or, in join pieces, zero) gradient over each triangle
//  (no texture distortion) and continuous across piece boundaries (no breaks in dashes at joins)

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
    int numDiscontinuous = 0;  // vertices on an edge of another triangle w/ different v than the edge there
    float maxJump = 0;
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
        // |grad v| is 1 (segments and caps) or 0 (join pieces, where the brush pivots around the join point)
        float glen = glm::length(g);
        float err = std::min(std::abs(glen - 1), glen);
        res.maxGradErr = std::max(res.maxGradErr, err);
        if (err > _gradTol) { res.numDistorted++; }
    }
    if (!_texCoords) { return res; }
    // continuity of v across piece boundaries: for each vertex lying on an edge of a triangle (incl. at its
    //  endpoints) at the same centerline point, v must match v interpolated along that edge (interiors of overlapping pieces, e.g. inner
    //  side of butt joins, aren't checked: both are drawn there)
    std::vector<glm::vec2> pos(_mesh.vertices.size());
    std::vector<float> vs(_mesh.vertices.size());
    for (size_t ii = 0; ii < pos.size(); ii++) {
        const PLVertex& vtx = _mesh.vertices[ii];
        pos[ii] = vtx.coord + vtx.extrude*_width;
        vs[ii] = vtx.uv.y + vtx.vshift*_width;
    }
    const float eps = 1e-4f;
    for (size_t pi = 0; pi < pos.size(); pi++) {
        float jump = 0;
        for (size_t ii = 0; ii < _mesh.indices.size(); ii += 3) {
            for (int jj = 0; jj < 3; jj++) {
                uint16_t ia = _mesh.indices[ii + jj], ib = _mesh.indices[ii + (jj+1)%3];
                if (ia == pi || ib == pi) { continue; }
                // only piece boundaries at the same join or cap (not other parts of a self-overlapping line)
                const glm::vec2& cp = _mesh.vertices[pi].coord;
                if (_mesh.vertices[ia].coord != cp || _mesh.vertices[ib].coord != cp) { continue; }
                glm::vec2 ab = pos[ib] - pos[ia];
                float lensq = glm::dot(ab, ab);
                float t = lensq > 0 ? glm::dot(pos[pi] - pos[ia], ab)/lensq : 0;
                if (t < -eps || t > 1 + eps) { continue; }
                if (glm::length(pos[ia] + t*ab - pos[pi]) > eps) { continue; }
                jump = std::max(jump, std::abs(vs[ia] + t*(vs[ib] - vs[ia]) - vs[pi]));
            }
        }
        res.maxJump = std::max(res.maxJump, jump);
        if (jump > 1e-3f) { res.numDiscontinuous++; }
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

TEST_CASE("Polyline builder: no folds, affine and continuous tex coords", "[polyline][builders]") {
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
                             << ", max |grad v| error " << res.maxGradErr << ", max v jump " << res.maxJump);
                        CHECK(res.numFolded == 0);
                        CHECK(res.numDistorted == 0);
                        CHECK(res.numDiscontinuous == 0);
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
      { {{7, 15}, {9, 15}}, {{11, 27}, {13, 27}}, {{23, 51}, {25, 51}} },
      { {{11, 27}, {13, 27}}, {{15, 39}, {17, 39}}, {{27, 63}, {29, 63}} }
    },
    { // repeated
      { {{6, 12}, {10, 18}}, {{10, 24}, {14, 30}}, {{22, 48}, {26, 54}} },
      { {{8, 15}, {9, 15}}, {{12, 27}, {13, 27}}, {{24, 51}, {25, 51}} },
      { {{12, 27}, {13, 27}}, {{16, 39}, {17, 39}}, {{28, 63}, {29, 63}} }
    },
    { // zigzag
      { {{10, 24}, {22, 42}}, {{14, 36}, {26, 54}}, {{26, 60}, {38, 78}} },
      { {{16, 33}, {19, 33}}, {{20, 45}, {23, 45}}, {{32, 69}, {35, 69}} },
      { {{28, 69}, {31, 69}}, {{32, 81}, {35, 81}}, {{44, 105}, {47, 105}} }
    },
    { // zigzag-wide
      { {{10, 24}, {22, 42}}, {{14, 36}, {26, 54}}, {{26, 60}, {38, 78}} },
      { {{16, 33}, {19, 33}}, {{20, 45}, {23, 45}}, {{32, 69}, {35, 69}} },
      { {{28, 69}, {31, 69}}, {{32, 81}, {35, 81}}, {{44, 105}, {47, 105}} }
    },
    { // sharp
      { {{13, 24}, {14, 24}}, {{17, 36}, {18, 36}}, {{29, 60}, {30, 60}} },
      { {{13, 24}, {14, 24}}, {{17, 36}, {18, 36}}, {{29, 60}, {30, 60}} },
      { {{21, 48}, {22, 48}}, {{25, 60}, {26, 60}}, {{37, 84}, {38, 84}} }
    },
    { // short
      { {{16, 30}, {28, 54}}, {{20, 42}, {32, 66}}, {{32, 66}, {44, 90}} },
      { {{24, 42}, {24, 42}}, {{28, 54}, {28, 54}}, {{40, 78}, {40, 78}} },
      { {{40, 90}, {40, 90}}, {{44, 102}, {44, 102}}, {{56, 126}, {56, 126}} }
    },
    { // uturn
      { {{10, 18}, {16, 30}}, {{14, 30}, {20, 42}}, {{26, 54}, {32, 66}} },
      { {{14, 24}, {14, 24}}, {{18, 36}, {18, 36}}, {{30, 60}, {30, 60}} },
      { {{22, 48}, {22, 48}}, {{26, 60}, {26, 60}}, {{38, 84}, {38, 84}} }
    },
    { // reverse
      { {{9, 15}, {9, 15}}, {{13, 27}, {13, 27}}, {{25, 51}, {25, 51}} },
      { {{9, 15}, {9, 15}}, {{13, 27}, {13, 27}}, {{25, 51}, {25, 51}} },
      { {{13, 27}, {13, 27}}, {{17, 39}, {17, 39}}, {{29, 63}, {29, 63}} }
    },
    { // arc
      { {{38, 108}, {38, 108}}, {{42, 120}, {42, 120}}, {{54, 144}, {54, 144}} },
      { {{72, 159}, {89, 159}}, {{76, 171}, {93, 171}}, {{88, 195}, {105, 195}} },
      { {{140, 363}, {157, 363}}, {{144, 375}, {161, 375}}, {{156, 399}, {173, 399}} }
    },
    { // arc-cw
      { {{38, 108}, {38, 108}}, {{42, 120}, {42, 120}}, {{54, 144}, {54, 144}} },
      { {{72, 159}, {89, 159}}, {{76, 171}, {93, 171}}, {{88, 195}, {105, 195}} },
      { {{140, 363}, {157, 363}}, {{144, 375}, {161, 375}}, {{156, 399}, {173, 399}} }
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
