#include "util/builders.h"

#include "util/geom.h"
#include "log.h"
#include "glm/gtx/rotate_vector.hpp"
#include "glm/gtx/norm.hpp"

namespace mapbox { namespace util {
template <>
struct nth<0, Tangram::Point> {
    inline static float get(const Tangram::Point &t) { return t.x; };
};
template <>
struct nth<1, Tangram::Point> {
    inline static float get(const Tangram::Point &t) { return t.y; };
};
}}

namespace {
// Tests if a line segment (from point A to B) is outside the edge of a tile
bool isOutsideTile(const glm::vec2& _a, const glm::vec2& _b) {

    // tweak this adjust if catching too few/many line segments near tile edges
    // TODO: make tolerance configurable by source if necessary
    float tolerance = 0.0005f;
    float tile_min = 0.0f + tolerance;
    float tile_max = 1.0f - tolerance;

    if ( (_a.x < tile_min && _b.x < tile_min) ||
         (_a.x > tile_max && _b.x > tile_max) ||
         (_a.y < tile_min && _b.y < tile_min) ||
         (_a.y > tile_max && _b.y > tile_max) ) {
        return true;
    }

    return false;
}

bool isOutsideTile(const glm::vec2& p) {
    float tolerance = 0.0005f;
    float tile_min = 0.0f + tolerance;
    float tile_max = 1.0f - tolerance;
    return ((p.x < tile_min) || (p.x > tile_max) ||
            (p.y < tile_min) || (p.y > tile_max));
}
}

namespace Tangram {

CapTypes CapTypeFromString(const std::string& str) {
    if (str == "square") { return CapTypes::square; }
    if (str == "round") { return CapTypes::round; }
    return CapTypes::butt;
}

JoinTypes JoinTypeFromString(const std::string& str) {
    if (str == "bevel") { return JoinTypes::bevel; }
    if (str == "round") { return JoinTypes::round; }
    return JoinTypes::miter;
}

void Builders::buildPolygon(const Polygon& _polygon, float _height, PolygonBuilder& _ctx) {

    glm::vec2 min, max;
    if (_ctx.useTexCoords) {
        min = glm::vec2(std::numeric_limits<float>::max());
        max = glm::vec2(std::numeric_limits<float>::lowest());

        for (auto& p : _polygon[0]) {
            min.x = std::min(min.x, p.x);
            min.y = std::min(min.y, p.y);
            max.x = std::max(max.x, p.x);
            max.y = std::max(max.y, p.y);
        }
    }

    // Run earcut, triangles are stored in _ctx.earcut.indices
    _ctx.earcut(_polygon);

    size_t sumPoints = 0;
    for (auto& line : _polygon) {
        sumPoints += line.size();
    }

    // Mark the points that are referenced by indices as used.
    size_t sumVertices = 0;
    _ctx.used.assign(sumPoints, 0);
    for (auto i : _ctx.earcut.indices) {
        if (_ctx.used[i] == 0) {
            _ctx.used[i] = 1;
            sumVertices++;
        }
    }

    if(!sumVertices && sumPoints > 3) { LOGD("earcut failed for polygon!"); }

    uint16_t vertexDataOffset = _ctx.numVertices;
    _ctx.numVertices += sumVertices;

    size_t ring = 0;
    size_t offset = 0;

    // Go through all points of the polyon.
    for (size_t src = 0, dst = 0; src < sumPoints; src++) {
        // The points of the polygon rings are indexed linearly.
        // This maps the indices back to the original ring and point.
        if (src - offset >= _polygon[ring].size()) {
            offset += _polygon[ring].size();
            ring += 1;
        }

        // Add vertex only when the point is used.
        if (_ctx.used[src] == 0) { continue; }

        // Keep track of skipped points to update indices
        _ctx.used[src] = dst++;

        auto& p = _polygon[ring][src - offset];
        glm::vec3 coord(p.x, p.y, _height);

        if (_ctx.useTexCoords) {
            glm::vec2 uv(mapRange01(coord.x, min.x, max.x), mapRange01(coord.y, max.y, min.y));

            _ctx.addVertex(coord, glm::vec3(0.0, 0.0, 1.0), uv);
        } else {
            _ctx.addVertex(coord, glm::vec3(0.0, 0.0, 1.0), glm::vec2(0));
        }
    }

    for (auto i : _ctx.earcut.indices) {
        _ctx.indices.push_back(vertexDataOffset + _ctx.used[i]);
    }
}

void Builders::buildPolygonExtrusion(const Polygon& _polygon, float _minHeight, float _maxHeight, PolygonBuilder& _ctx) {

    auto vertexDataOffset = _ctx.numVertices;

    static const glm::vec3 upVector(0.0f, 0.0f, 1.0f);
    glm::vec3 normalVector;

    for (auto& line : _polygon) {

        size_t lineSize = line.size();

        for (size_t i = 0; i < lineSize - 1; i++) {

            glm::vec3 a(line[i], 0.f);
            glm::vec3 b(line[i+1], 0.f);

            if (!_ctx.keepTileEdges && isOutsideTile(a, b)) {
                continue;
            }
            normalVector = glm::cross(upVector, b - a);
            normalVector = glm::normalize(normalVector);

            if (std::isnan(normalVector.x)
             || std::isnan(normalVector.y)
             || std::isnan(normalVector.z)) {
                continue;
            }

            // 1st vertex top
            a.z = _maxHeight;
            _ctx.addVertex(a, normalVector, glm::vec2(1.,1.));

            // 2nd vertex top
            b.z = _maxHeight;
            _ctx.addVertex(b, normalVector, glm::vec2(0.,1.));

            // 1st vertex bottom
            a.z = _minHeight;
            _ctx.addVertex(a, normalVector, glm::vec2(1.,0.));

            // 2nd vertex bottom
            b.z = _minHeight;
            _ctx.addVertex(b, normalVector, glm::vec2(0.,0.));

            // Start the index from the previous state of the vertex Data
            _ctx.indices.push_back(vertexDataOffset);
            _ctx.indices.push_back(vertexDataOffset + 1);
            _ctx.indices.push_back(vertexDataOffset + 2);

            _ctx.indices.push_back(vertexDataOffset + 1);
            _ctx.indices.push_back(vertexDataOffset + 3);
            _ctx.indices.push_back(vertexDataOffset + 2);

            vertexDataOffset += 4;
        }

        _ctx.numVertices = vertexDataOffset;
    }
}

// Polyline tesselation: each segment is a quad (two triangles) and joins and caps are separate pieces of
//  geometry.  Texture coord v is distance along the centerline.  With tex coords, joins (except shallow miter
//  joins, which share vertices) follow a brush swept along the centerline that pivots around the join point:
//  segments are rectangles w/ butt ends (overlapping on the inner side), so v is an affine function of position
//  over each segment (no distortion of dashes and textures), and the outer join piece (fan or miter) and caps
//  have constant v, so v is also continuous across piece boundaries (no breaks or slivers of dashes at bends).
//  Without tex coords, vertices are shared between adjacent pieces where possible.

// Helper function for polyline tesselation; returns index of added vertex
static inline uint16_t addPolyLineVertex(const glm::vec2& _coord, const glm::vec2& _extrude, float _u, float _v,
                                         PolyLineBuilder& _ctx) {
    _ctx.addVertex(_coord, _extrude, glm::vec2(_u, _v));
    return uint16_t(_ctx.numVertices++);
}

// Add indices for quad with vertices r0, l0 at one end and r1, l1 at the other
static void addQuad(uint16_t _r0, uint16_t _l0, uint16_t _r1, uint16_t _l1, PolyLineBuilder& _ctx) {
    _ctx.indices.insert(_ctx.indices.end(), { _r0, _r1, _l0, _l0, _r1, _l1 });
}

// Add fan of triangles around _coord, with center vertex extrusion _ec, arc from extrusion _a to _b sweeping
//  signed angle _sweep (positive is CCW); tex coord u is interpolated from _ua to _ub along arc; _first and
//  _last are existing vertices to use for the ends of the arc (if >= 0; must have same attributes)
static void addFan(const glm::vec2& _coord, const glm::vec2& _ec, float _uc, const glm::vec2& _a,
                   const glm::vec2& _b, float _sweep, float _ua, float _ub, float _v, int _numTriangles,
                   PolyLineBuilder& _ctx, int _first = -1, int _last = -1) {
    uint16_t center = addPolyLineVertex(_coord, _ec, _uc, _v, _ctx);
    uint16_t prev = _first >= 0 ? uint16_t(_first) : addPolyLineVertex(_coord, _a, _ua, _v, _ctx);
    for (int ii = 1; ii <= _numTriangles; ii++) {
        float frac = ii/float(_numTriangles);
        glm::vec2 radial = ii < _numTriangles ? glm::rotate(_a, frac*_sweep) : _b;
        uint16_t next = ii == _numTriangles && _last >= 0 ? uint16_t(_last) :
            addPolyLineVertex(_coord, radial, (1.f - frac)*_ua + frac*_ub, _v, _ctx);
        // keep same (CCW) winding as quads
        if (_sweep > 0) {
            _ctx.indices.insert(_ctx.indices.end(), { center, prev, next });
        } else {
            _ctx.indices.insert(_ctx.indices.end(), { center, next, prev });
        }
        prev = next;
    }
}

static void buildPolyLineSegment(const Line& _line, PolyLineBuilder& _ctx, size_t _startIndex,
                                 size_t _endIndex, bool startCap = true, bool endCap = true) {

    size_t origLineSize = _line.size();
    // endIndex/startIndex could be wrapped values, calculate lineSize accordingly
    size_t lineSize = (_endIndex > _startIndex) ?
                      (_endIndex - _startIndex) : (origLineSize - _startIndex + _endIndex);

    // get points using wrapped index in the original line geometry, dropping repeated points
    auto& pts = _ctx.points;
    pts.clear();
    for (size_t ii = 0; ii < lineSize; ii++) {
        glm::vec2 p = _line[(_startIndex + ii) % origLineSize];
        if (pts.empty() || p != pts.back()) { pts.push_back(p); }
    }
    if (pts.size() < 2) { return; }

    const bool texCoords = _ctx.useTexCoords;
    const float w = _ctx.maxWidth;
    const float mlimsq = _ctx.miterLimit*_ctx.miterLimit;
    const int capTriangles = int(_ctx.cap);
    const int joinTriangles = _ctx.join == JoinTypes::round ? int(JoinTypes::round) : 1;

    // "right" side normal n = t rotated -90 deg (CCW positive) has u = 1, left side (-n) has u = 0
    float dist = 0;  // cumulative distance along the polyline
    glm::vec2 d01 = pts[1] - pts[0];
    float len01 = glm::length(d01);
    glm::vec2 t01 = d01/len01;
    glm::vec2 n01(t01.y, -t01.x);
    // length at start of current segment, on right and left sides, taken by inner miter of previous join
    float usedR = 0, usedL = 0;

    // first point, with start cap (caps have constant v and share their inner corners w/ the segment)
    glm::vec2 p0 = pts[0];
    uint16_t r0 = addPolyLineVertex(p0, n01, 1.f, dist, _ctx);
    uint16_t l0 = addPolyLineVertex(p0, -n01, 0.f, dist, _ctx);
    if (startCap && _ctx.cap == CapTypes::square) {
        uint16_t rc = addPolyLineVertex(p0, n01 - t01, 1.f, dist, _ctx);
        uint16_t lc = addPolyLineVertex(p0, -n01 - t01, 0.f, dist, _ctx);
        addQuad(rc, lc, r0, l0, _ctx);
    } else if (startCap && _ctx.cap == CapTypes::round) {
        addFan(p0, {0, 0}, 0.5f, n01, -n01, -float(PI), 1.f, 0.f, dist, capTriangles, _ctx, r0, l0);
    }

    // intermediate points (joins)
    for (size_t ii = 1; ii + 1 < pts.size(); ii++) {
        const glm::vec2& p1 = pts[ii];
        glm::vec2 d12 = pts[ii+1] - p1;
        float len12 = glm::length(d12);
        glm::vec2 t12 = d12/len12;
        glm::vec2 n12(t12.y, -t12.x);
        dist += len01;

        // miter extrusion (on right side); length is 1/cos(turn/2) and tangential component is tan(turn/2)
        float miterDenom = 1.f + glm::dot(n01, n12);
        bool miterOk = miterDenom > 1e-6f;
        glm::vec2 miter = (n01 + n12)/std::max(miterDenom, 1e-6f);
        float miterLenSq = glm::dot(miter, miter);
        float miterTan = std::abs(glm::dot(miter, t01));

        // outer side is right side (+n) for left (CCW) turn
        float sO = (t01.x*t12.y - t01.y*t12.x) > 0 ? 1.f : -1.f;
        float& usedI = sO > 0 ? usedL : usedR;
        // inner side uses the miter point unless it would extend past the end of either adjacent segment (or
        //  the inner miter point of previous join), in which case inner side has (overlapping) butt ends
        bool innerMiter = miterOk && (w > 0 ? (w*miterTan + usedI <= len01 && w*miterTan <= len12)
                                            : miterLenSq <= mlimsq);
        // miter outer join if requested and within miter limit, otherwise bevel or round
        bool outerMiter = miterOk && _ctx.join == JoinTypes::miter && miterLenSq <= mlimsq;

        // for shallow miter joins (most joins in typical map data), vertices are shared between segments, w/
        //  small distortion of texture (max shift along the line of maxSharedMiterTan * width)
        bool shareMiter = outerMiter && innerMiter && miterTan <= _ctx.maxSharedMiterTan;
        // other joins w/ tex coords: swept brush (see above) - butt ends and separate outer miter piece
        bool brush = texCoords && !shareMiter;
        if (brush) { innerMiter = false; }
        bool miterQuads = outerMiter && !brush;  // segment quads extend to outer miter point

        glm::vec2 eI01 = innerMiter ? -sO*miter : -sO*n01;
        glm::vec2 eI12 = innerMiter ? -sO*miter : -sO*n12;
        glm::vec2 eO01 = miterQuads ? sO*miter : sO*n01;
        glm::vec2 eO12 = miterQuads ? sO*miter : sO*n12;
        glm::vec2 eR01 = sO > 0 ? eO01 : eI01, eL01 = sO > 0 ? eI01 : eO01;
        glm::vec2 eR12 = sO > 0 ? eO12 : eI12, eL12 = sO > 0 ? eI12 : eO12;

        // end of segment 01
        uint16_t r1 = addPolyLineVertex(p1, eR01, 1.f, dist, _ctx);
        uint16_t l1 = addPolyLineVertex(p1, eL01, 0.f, dist, _ctx);
        addQuad(r0, l0, r1, l1, _ctx);

        // start of segment 12 - share vertices with end of segment 01 if possible (same attributes)
        r0 = eR01 == eR12 ? r1 : addPolyLineVertex(p1, eR12, 1.f, dist, _ctx);
        l0 = eL01 == eL12 ? l1 : addPolyLineVertex(p1, eL12, 0.f, dist, _ctx);

        // join; the outer corners of the join piece (extrusion sO*n01, sO*n12) are the outer end vertices of the
        //  segments, which have the same attributes, so they are shared
        float uO = sO > 0 ? 1.f : 0.f;
        uint16_t oEnd01 = sO > 0 ? r1 : l1, oStart12 = sO > 0 ? r0 : l0;
        if (brush && outerMiter) {
            // outer miter piece: center, end of segment 01, miter point, start of segment 12
            uint16_t c = addPolyLineVertex(p1, {0, 0}, 0.5f, dist, _ctx);
            uint16_t m = addPolyLineVertex(p1, sO*miter, uO, dist, _ctx);
            // keep same (CCW) winding as quads
            if (sO > 0) {
                _ctx.indices.insert(_ctx.indices.end(), { c, oEnd01, m, c, m, oStart12 });
            } else {
                _ctx.indices.insert(_ctx.indices.end(), { c, m, oEnd01, c, oStart12, m });
            }
        } else if (!outerMiter) {
            glm::vec2 a = sO*n01, b = sO*n12;
            float sweep = sO*std::acos(std::min(1.f, std::max(-1.f, glm::dot(a, b))));
            if (innerMiter) {
                addFan(p1, -sO*miter, 1.f - uO, a, b, sweep, uO, uO, dist, joinTriangles, _ctx,
                       oEnd01, oStart12);
            } else {
                addFan(p1, {0, 0}, 0.5f, a, b, sweep, uO, uO, dist, joinTriangles, _ctx,
                       oEnd01, oStart12);
            }
        }

        usedI = innerMiter ? w*miterTan : 0;
        (sO > 0 ? usedR : usedL) = 0;
        t01 = t12;
        n01 = n12;
        len01 = len12;
    }

    // last point, with end cap
    dist += len01;
    glm::vec2 p1 = pts.back();
    uint16_t r1 = addPolyLineVertex(p1, n01, 1.f, dist, _ctx);
    uint16_t l1 = addPolyLineVertex(p1, -n01, 0.f, dist, _ctx);
    addQuad(r0, l0, r1, l1, _ctx);
    if (endCap && _ctx.cap == CapTypes::square) {
        uint16_t rc = addPolyLineVertex(p1, n01 + t01, 1.f, dist, _ctx);
        uint16_t lc = addPolyLineVertex(p1, -n01 + t01, 0.f, dist, _ctx);
        addQuad(r1, l1, rc, lc, _ctx);
    } else if (endCap && _ctx.cap == CapTypes::round) {
        addFan(p1, {0, 0}, 0.5f, -n01, n01, -float(PI), 0.f, 1.f, dist, capTriangles, _ctx, l1, r1);
    }
}

void Builders::buildPolyLine(const Line& _line, PolyLineBuilder& _ctx) {

    size_t lineSize = _line.size();

    if (_ctx.keepTileEdges) {

        buildPolyLineSegment(_line, _ctx, 0, lineSize);

    } else {

        size_t cut = 0;
        size_t firstCutEnd = 0;

        // Determine cuts
        for (size_t i = 0; i < lineSize - 1; i++) {
            const glm::vec2& coordCurr = _line[i];
            const glm::vec2& coordNext = _line[i+1];
            if (isOutsideTile(coordCurr, coordNext)) {
                if (cut == 0) {
                    firstCutEnd = i + 1;
                }
                buildPolyLineSegment(_line, _ctx, cut, i + 1);
                cut = i + 1;
            } else if (!_ctx.closedPolygon) {
                // note that both endpoints can be outside tile even if the line segment intersects tile!
                bool currOutside = isOutsideTile(coordCurr);
                bool nextOutside = isOutsideTile(coordNext);

                if (currOutside || nextOutside) {
                    if (!currOutside) {
                        buildPolyLineSegment(_line, _ctx, cut, i + 1, true, false);
                    }
                    glm::vec2 a = coordCurr, b = coordNext;
                    if (clipLine(a, b, {0, 0}, {1, 1})) {
                        buildPolyLineSegment({ a, b }, _ctx, 0, 2,
                                             !currOutside && i == 0, !nextOutside && i+1 == lineSize-1);
                    }
                    cut = i + 1;
                }
            }
        }

        if (_ctx.closedPolygon) {
            if (cut == 0) {
                // no tile edge cuts!
                // loop and close the polygon with no endcaps
                buildPolyLineSegment(_line, _ctx, 0, lineSize+2, false, false);
            } else {
                // merge first and last cut line-segments together
                buildPolyLineSegment(_line, _ctx, cut, firstCutEnd);
            }
        } else {
            buildPolyLineSegment(_line, _ctx, cut, lineSize);
        }

    }

}

void Builders::buildQuadAtPoint(const glm::vec2& _screenPosition, const glm::vec2& _size, const glm::vec2& _uvBL, const glm::vec2& _uvTR, SpriteBuilder& _ctx) {
    float halfWidth = _size.x * .5f;
    float halfHeight = _size.y * .5f;

    _ctx.addVertex(glm::vec2(-halfWidth, -halfHeight), _screenPosition, {_uvBL.x, _uvBL.y});
    _ctx.addVertex(glm::vec2(-halfWidth, halfHeight), _screenPosition, {_uvBL.x, _uvTR.y});
    _ctx.addVertex(glm::vec2(halfWidth, -halfHeight), _screenPosition, {_uvTR.x, _uvBL.y});
    _ctx.addVertex(glm::vec2(halfWidth, halfHeight), _screenPosition, {_uvTR.x, _uvTR.y});

    _ctx.indices.push_back(_ctx.numVerts + 2);
    _ctx.indices.push_back(_ctx.numVerts + 0);
    _ctx.indices.push_back(_ctx.numVerts + 1);
    _ctx.indices.push_back(_ctx.numVerts + 1);
    _ctx.indices.push_back(_ctx.numVerts + 3);
    _ctx.indices.push_back(_ctx.numVerts + 2);

    _ctx.numVerts += 4;

}

}
