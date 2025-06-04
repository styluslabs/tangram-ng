#include "labels/spriteLabel.h"

#include "gl/dynamicQuadMesh.h"
#include "labels/screenTransform.h"
#include "labels/obbBuffer.h"
#include "log.h"
#include "scene/spriteAtlas.h"
#include "style/pointStyle.h"
#include "util/geom.h"
#include "util/elevationManager.h"
#include "view/view.h"

namespace Tangram {

using namespace LabelProperty;

const float SpriteVertex::alpha_scale = 65535.0f;
const float SpriteVertex::texture_scale = 32767.0f;

struct BillboardTransform {
    ScreenTransform& m_transform;

    BillboardTransform(ScreenTransform& _transform)
        : m_transform(_transform) {}

    void set(glm::vec2 _position, glm::vec3 _projected, glm::vec2 _screenSize, float _fractZoom) {

        m_transform.push_back(_position);
        m_transform.push_back(_projected);
        m_transform.push_back(glm::vec3(_screenSize, _fractZoom));
    }

    glm::vec2 position() const { return glm::vec2(m_transform[0]); }
    glm::vec3 projected() const { return m_transform[1]; }
    glm::vec2 screenSize() const { return glm::vec2(m_transform[2]); }
    float fractZoom() const { return m_transform[2].z; }
};

struct FlatTransform {
    ScreenTransform& m_transform;

    FlatTransform(ScreenTransform& _transform)
        : m_transform(_transform) {}

    void set(const std::array<glm::vec2, 4>& _position,
             const std::array<glm::vec4, 4>& _projected) {
        for (size_t i = 0; i < 4; i++) {
            m_transform.push_back(glm::vec3(_position[i], _projected[i].w));
        }
        for (size_t i = 0; i < 4; i++) {
            m_transform.push_back(glm::vec3(_projected[i]));
        }
    }

    glm::vec2 position(size_t i) const { return glm::vec2(m_transform[i]); }
    glm::vec4 projected(size_t i) const { return glm::vec4(m_transform[4+i], m_transform[i].z); }
};

SpriteLabel::SpriteLabel(Coordinates _coordinates, float _zoom, glm::vec2 _size, Label::Options _options,
                         SpriteLabel::VertexAttributes _attrib, Texture* _texture,
                         SpriteLabels& _labels, size_t _labelsPos)
    : Label(_size, Label::Type::point, _options),
      m_coordinates(_coordinates),
      m_zoom(_zoom),
      m_labels(_labels),
      m_labelsPos(_labelsPos),
      m_texture(_texture),
      m_vertexAttrib(_attrib) {

    applyAnchor(m_options.anchors[0]);
}

void SpriteLabel::applyAnchor(LabelProperty::Anchor _anchor) {

    m_anchor = LabelProperty::anchorDirection(_anchor) * m_dim * 0.5f;
}

bool SpriteLabel::setElevation(ElevationManager& elevMgr, glm::dvec2 origin, double scale) {
    bool ok = true;
    auto r = origin + glm::dvec2(m_coordinates)*scale;
    m_coordinates.z = elevMgr.m_terrainScale * elevMgr.getElevation(r, ok)/scale;
    return ok;
}

bool SpriteLabel::updateScreenTransform(const glm::mat4& _mvp, const ViewState& _viewState,
                                        const AABB* _bounds, ScreenTransform& _transform) {

    glm::vec3 p0 = m_coordinates;

    glm::vec4 proj = worldToClipSpace(_mvp, glm::vec4(p0, 1.f));
    if (clipSpaceIsBehindCamera(proj)) { return false; }

    glm::vec3 ndc = clipSpaceToNdc(proj);
    if (ndc.z > 1.0f) { return false; }  // beyond far plane

    glm::vec2 pos = ndcToScreenSpace(ndc, _viewState.viewportSize);
    m_screenCenter = glm::vec4(pos, ndc.z, 1/proj.w);
    pos += m_options.offset;  // do not include offset in screenCenter

    if (m_options.flat) {

        std::array<glm::vec2, 4> positions;
        std::array<glm::vec4, 4> projected;

        float scale = std::pow(2, m_zoom) / (_viewState.zoomScale/proj.w * _viewState.tileSize);
        float zoomFactor = m_vertexAttrib.extrudeScale * _viewState.fractZoom;
        glm::vec2 dim = (m_dim + zoomFactor) * scale * 0.5f;

        positions[0] = -dim;
        positions[1] = glm::vec2(dim.x, -dim.y);
        positions[2] = glm::vec2(-dim.x, dim.y);
        positions[3] = dim;

        // Rotate in clockwise order on the ground plane
        if (m_options.angle != 0.f) {
            glm::vec2 rotation = rotation2dDeg(m_options.angle);
            for (size_t i = 0; i < 4; i++) {
                positions[i] = rotate2d(positions[i], rotation);
            }
        }

        AABB aabb;
        for (size_t i = 0; i < 4; i++) {
            projected[i] = worldToClipSpace(_mvp, glm::vec4(positions[i] + glm::vec2(p0), p0.z, 1.f));
            if (clipSpaceIsBehindCamera(projected[i])) { return false; }

            positions[i] = ndcToScreenSpace(clipSpaceToNdc(projected[i]), _viewState.viewportSize);
            aabb.include(positions[i].x, positions[i].y);
        }

        if (_bounds && !aabb.intersect(*_bounds)) { return false; }

        FlatTransform(_transform).set(positions, projected);
    } else {

        if (_bounds) {
            auto aabb = m_options.anchors.extents(m_dim);
            aabb.min += pos;
            aabb.max += pos;
            if (!aabb.intersect(*_bounds)) { return false; }
        }

        BillboardTransform(_transform).set(pos, ndc, _viewState.viewportSize, _viewState.fractZoom);
    }

    return true;
}

void SpriteLabel::obbs(ScreenTransform& _transform, OBBBuffer& _obbs) const {
    OBB obb;

    if (m_options.flat) {
        const float infinity = std::numeric_limits<float>::infinity();
        float minx = infinity, miny = infinity;
        float maxx = -infinity, maxy = -infinity;

        for (int i = 0; i < 4; ++i) {

            const auto& position = _transform[i];
            minx = std::min(minx, position.x);
            miny = std::min(miny, position.y);
            maxx = std::max(maxx, position.x);
            maxy = std::max(maxy, position.y);
        }

        glm::vec2 dim = glm::vec2(maxx - minx, maxy - miny);

        if (m_occludedLastFrame) { dim += Label::activation_distance_threshold; }

        glm::vec2 obbCenter = glm::vec2((minx + maxx) * 0.5f, (miny + maxy) * 0.5f);

        obb = OBB(obbCenter, {1, 0}, dim.x, dim.y);
    } else {

        BillboardTransform pointTransform(_transform);

        glm::vec2 dim = m_dim + glm::vec2(m_vertexAttrib.extrudeScale * pointTransform.fractZoom());

        if (m_occludedLastFrame) { dim += Label::activation_distance_threshold; }

        obb = OBB(pointTransform.position() + m_anchor, {1, 0}, dim.x, dim.y);
    }

    _obbs.append(obb);
}

void SpriteLabel::addVerticesToMesh(ScreenTransform& _transform, const glm::vec2& _screenSize) {

    if (!visibleState()) { return; }

    // TODO
    // if (a_extrude.x != 0.0) {
    //     float dz = u_map_position.z - abs(u_tile_origin.z);
    //     vertex_pos.xy += clamp(dz, 0.0, 1.0) * UNPACK_EXTRUDE(a_extrude.xy);
    // }

    auto& quad = m_labels.quads[m_labelsPos];

    SpriteVertex::State state {
        m_vertexAttrib.selectionColor,
        m_vertexAttrib.color,
        m_vertexAttrib.outlineColor,
        m_vertexAttrib.antialiasFactor,
        uint16_t(m_alpha * SpriteVertex::alpha_scale),
    };

    auto* quadVertices = m_labels.m_style.pushQuad(m_texture);

    if (m_options.flat) {
        FlatTransform transform(_transform);

        for (int i = 0; i < 4; i++) {
            SpriteVertex& vertex = quadVertices[i];

            vertex.pos = transform.projected(i);
            vertex.uv = quad.quad[i].uv;
            vertex.state = state;
        }

    } else {
        BillboardTransform transform(_transform);

        glm::vec2 pos = glm::vec2(transform.projected());
        glm::vec2 scale = 2.0f / transform.screenSize();
        scale.y *= -1;

        pos += m_options.offset * scale;
        pos += m_anchor * scale;

        for (int i = 0; i < 4; i++) {
            SpriteVertex& vertex = quadVertices[i];
            glm::vec2 coord = pos + quad.quad[i].pos * scale;

            vertex.pos.x = coord.x;
            vertex.pos.y = coord.y;
            vertex.pos.z = 0.f;
            vertex.pos.w = 1.f;
            vertex.uv = quad.quad[i].uv;
            vertex.state = state;
        }
    }
}

}
