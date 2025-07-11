#pragma once

#include "style/style.h"
#include "style/pointStyle.h"
#include "style/textStyleBuilder.h"
#include <map>
#include <memory>
#include <vector>
#include <random>

namespace Tangram {

struct IconMesh : LabelSet {

    std::unique_ptr<StyledMesh> textLabels;
    std::unique_ptr<StyledMesh> spriteLabels;

    void setTextLabels(std::unique_ptr<StyledMesh> _textLabels);
};

struct PointStyleBuilder : public StyleBuilder {

    struct Parameters {
        bool interactive = false;
        bool keepTileEdges = false;
        bool autoAngle = false;
        bool dynamicTexture = false;
        std::string sprite;
        std::string spriteDefault;
        std::string texture;
        glm::vec2 size = { 16.f, 16.f };
        uint32_t color = 0xffffffff;
        float outlineWidth = 0.f;
        uint32_t outlineColor = 0x00000000;
        uint16_t antialiasFactor = 0;
        Label::Options labelOptions;
        LabelProperty::Placement placement = LabelProperty::Placement::vertex;
        float extrudeScale = 1.f;
        float placementMinLengthRatio = 1.0f;
        float placementSpacing = PointStyle::DEFAULT_PLACEMENT_SPACING;
    };

    const PointStyle& m_style;


    void setup(const Tile& _tile) override;
    void setup(const Marker& _marker, int zoom) override;

    bool checkRule(const DrawRule& _rule) const override;

    bool addPolygon(const Polygon& _polygon, const Properties& _props, const DrawRule& _rule) override;
    bool addLine(const Line& _line, const Properties& _props, const DrawRule& _rule) override;
    bool addPoint(const Point& _line, const Properties& _props, const DrawRule& _rule) override;

    std::unique_ptr<StyledMesh> build() override;

    const Style& style() const override { return m_style; }

    PointStyleBuilder(const PointStyle& _style) : m_style(_style) {
        m_textStyleBuilder = m_style.textStyle().createBuilder();
    }

    Parameters applyRule(const DrawRule& _rule) const;

    // Gets points for label placement and appropriate angle for each label (if `auto` angle is set)
    void labelPointsPlacing(const Line& _line, const glm::vec4& _quad, Texture* _texture,
                            Parameters& _params, const DrawRule& _rule);

    void addLabel(const Point& _point, const glm::vec4& _quad, Texture* _texture,
                  const Parameters& _params, const DrawRule& _rule);

    void addLayoutItems(LabelCollider& _layout) override;

    bool addFeature(const Feature& _feat, const DrawRule& _rule) override;

private:

    /*
     * Resolves the apt texture used by the point style builder
     * Texture could be specified by:
     * - explicit marker texture
     * - texture name specified in the draw rules
     * - default texture specified for the style
     */
    bool getTexture(const Parameters& _params, Texture** _texture) const;

    /*
     * Evaluates the size the point sprite can be drawn with:
     * - size specified in the draw rule
     * - Texture density information could be required if:
     *      - no size is specified in the draw rule
     *      - draw rule size uses '%' units, which scale the sprite texture based on the texture density
     * - size specified in the draw rule uses "auto" which means width/height of the sprite is evaluated, keeping the
     * same aspect ratio.
     */
    bool evalSizeParam(const DrawRule& _rule, Parameters& _params, const Texture* _texture) const;
    bool getUVQuad(Parameters& _params, glm::vec4& _quad, const Texture* _texture) const;

    std::vector<std::unique_ptr<Label>> m_labels;
    std::vector<SpriteQuad> m_quads;

    std::unique_ptr<IconMesh> m_iconMesh;

    float m_zoom = 0;
    float m_styleZoom = 0;
    float m_tileScale = 1;
    std::unique_ptr<SpriteLabels> m_spriteLabels;
    std::unique_ptr<StyleBuilder> m_textStyleBuilder;

    // Non-owning reference to a texture to use for the current feature.
    Texture* m_texture = nullptr;

    std::mt19937 m_randGen;
};

}

namespace std {
    template <>
    struct hash<Tangram::PointStyleBuilder::Parameters> {
        size_t operator() (const Tangram::PointStyleBuilder::Parameters& p) const {
            std::hash<Tangram::Label::Options> optionsHash;
            std::size_t seed = 0;
            hash_combine(seed, p.sprite);
            hash_combine(seed, p.color);
            hash_combine(seed, p.size.x);
            hash_combine(seed, p.size.y);
            hash_combine(seed, (int)p.placement);
            hash_combine(seed, optionsHash(p.labelOptions));
            return seed;
        }
    };
}
