#pragma once

#include "labels/label.h"
#include "labels/labelSet.h"

#include <array>

namespace Tangram {

class SpriteLabels;
class PointStyle;
class Texture;

struct SpriteVertex {
    glm::vec4 pos;
    glm::i16vec2 uv;
    struct State {
        uint32_t selection_color;
        uint32_t color;
        uint32_t outline_color;
        uint16_t antialias_factor;
        uint16_t alpha;
    } state;

    static const float alpha_scale;
    static const float texture_scale;
};

class SpriteLabel : public Label {
public:

    struct VertexAttributes {
        uint32_t color;
        uint32_t selectionColor;
        uint32_t outlineColor;
        uint16_t antialiasFactor;
        float extrudeScale;
    };

    using Coordinates = glm::vec3;

    SpriteLabel(Coordinates _coordinates, float _zoom, glm::vec2 _size, Label::Options _options,
                SpriteLabel::VertexAttributes _attrib, Texture* _texture,
                SpriteLabels& _labels, size_t _labelsPos);

    LabelType renderType() const override { return LabelType::icon; }

    bool updateScreenTransform(const glm::mat4& _mvp, const ViewState& _viewState,
                               const AABB* _bounds, ScreenTransform& _transform) override;

    bool setElevation(ElevationManager& elevMgr, glm::dvec2 origin, double scale) override;

    void obbs(ScreenTransform& _transform, OBBBuffer& _obbs) const override;

    void addVerticesToMesh(ScreenTransform& _transform, const glm::vec2& _screenSize) override;

    void applyAnchor(LabelProperty::Anchor _anchor) override;

    uint32_t selectionColor() override {
        return m_vertexAttrib.selectionColor;
    }

    glm::vec3 modelCenter() const override {
        return m_coordinates;
    }

    const Texture* texture() const override { return m_texture; }

private:

    Coordinates m_coordinates;
    float m_zoom;

    // Back-pointer to owning container and position
    const SpriteLabels& m_labels;
    const size_t m_labelsPos;

    // Non-owning reference to a texture that is specific to this label.
    // If non-null, this indicates a custom texture for a marker.
    Texture* m_texture;

    VertexAttributes m_vertexAttrib;
};

struct SpriteQuad {
    struct {
        glm::vec2 pos;
        glm::i16vec2 uv;
    } quad[4];
};

class SpriteLabels : public LabelSet {
public:
    SpriteLabels(const PointStyle& _style) : m_style(_style) {}

    void setQuads(std::vector<SpriteQuad>&& _quads) {
        quads = std::move(_quads);
    }

    // TODO: hide within class if needed
    const PointStyle& m_style;
    std::vector<SpriteQuad> quads;
};

}
