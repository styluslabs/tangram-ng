#include "style/contourTextStyle.h"

#include "data/propertyItem.h"
#include "labels/textLabels.h"
#include "scene/scene.h"
#include "scene/drawRule.h"
#include "style/textStyleBuilder.h"
#include "tile/tile.h"
#include "util/elevationManager.h"
#include "log.h"

namespace Tangram {

class ContourTextStyleBuilder : public TextStyleBuilder {

public:

    ContourTextStyleBuilder(const TextStyle& _style) : TextStyleBuilder(_style) {}

    void setup(const Tile& _tile) override;

    void setup(const Marker& _marker, int zoom) override {
        LOGE("ContourTextStyle cannot be used with markers!");
    }

    bool addFeature(const Feature& _feat, const DrawRule& _rule) override;

    std::unique_ptr<StyledMesh> build() override;

    static constexpr int gridSize = 4;

private:
    TileID m_tileId = {-1, -1, -1};
    std::shared_ptr<Texture> m_texture;
};


void ContourTextStyleBuilder::setup(const Tile& _tile) {

    // nothing to do if no elevation data
    if (_tile.rasters().empty() || _tile.rasters().front().texture->width() <= 1) { return; }

    m_tileId = _tile.getID();
    m_texture = _tile.rasters().front().texture;
    TextStyleBuilder::setup(_tile);
}

static float getContourLine(Texture& tex, TileID& tileId, glm::vec2 pos, float elevStep, Line& line) {

    const float tileSize = 256.0f * std::exp2(tileId.s - tileId.z);
    const float maxPosErr = 0.25f/tileSize;
    const float labelLen = 32/tileSize;
    const float stepSize = 2/tileSize;
    const size_t numLinePts = int(1.25f*labelLen/stepSize);

    float level = NAN;
    while (1) {
        float step, prevElev = 0, lowerElev = NAN, upperElev = NAN;
        glm::vec2 grad, prevPos, lowerPos, upperPos;
        int niter = 0;
        do {
            float elev = ElevationManager::elevationLerp(tex, pos, &grad);
            if (std::isnan(level)) {
                level = std::round(elev/elevStep)*elevStep;
                if (level <= 0) { return NAN; }
            }

            if (elev < level && !(elev < lowerElev)) {  // use false compare to handle NAN
                lowerElev = elev;
                lowerPos = pos;
            } else if (elev > level && !(elev > upperElev)) {
                upperElev = elev;
                upperPos = pos;
            }

            // handle zero gradient case ... I think this could be fairly common so don't just abort
            if (grad.x == 0 && grad.y == 0) {
                if (niter == 0 || prevElev == elev || pos == prevPos) { return NAN; }
                float dr = glm::length(pos - prevPos);
                grad = (pos - prevPos)*(elev - prevElev)/(dr*dr);
            }
            prevElev = elev;
            prevPos = pos;

            float gradlen = glm::length(grad);
            step = std::abs(level - elev)/gradlen;
            if (level < elev) { gradlen = -gradlen; }

            if (std::isnan(lowerElev) || std::isnan(upperElev)) {
                float toedge = std::min({pos.x, pos.y, 1 - pos.x, 1 - pos.y});  // distance to nearest edge
                pos += std::min(step, std::max(0.025f, toedge))*(grad/gradlen);  // limit step size
            } else {
                pos = (upperPos*(level - lowerElev) + lowerPos*(upperElev - level))/(upperElev - lowerElev);
            }

            // abort if outside tile or too many iterations; exit on false compare to handle NAN in pos
            if(++niter > 12 || !(pos.x >= 0 && pos.y >= 0 && pos.x <= 1 && pos.y <= 1)) { return NAN; }

        } while (step > maxPosErr);

        line.push_back(pos);
        if(line.size() >= numLinePts) { return level; }
        glm::vec2 tangent = glm::normalize(glm::vec2(grad.y, -grad.x));
        //if(glm::dot(tangent, prevTangent ... check for excess angle
        pos = glm::clamp(pos + tangent*stepSize, glm::vec2(0.f), glm::vec2(1.f));
    }
}

bool ContourTextStyleBuilder::addFeature(const Feature& _feat, const DrawRule& _rule) {

    if (!m_texture || !checkRule(_rule)) { return false; }

    bool metricUnits = static_cast<const ContourTextStyle&>(m_style).m_metricUnits;
    // text_source: units to append units to label; '_' since applyRule() will fail if params.text is empty
    Properties props = {{{"name", "_"}, {"units", metricUnits ? "_m": "_ft"}}};  //
    TextStyle::Parameters params = applyRule(_rule, props, false);  //_feat.props
    if (!params.font) { return false; }
    auto repeatGroupHash = params.labelOptions.repeatGroup;
    // 'angle: auto' -> labelOptions.angle = NAN to force text to always be oriented uphill
    _rule.get(StyleParamKey::angle, params.labelOptions.angle);
    params.wordWrap = false;
    std::string suffix = params.text.substr(1);

    // this obviously needs to match values in contour line shader ... we could consider defining steps w/ a
    //  YAML array which can become a shader uniform and also can be read from Scene in Style::build()
    float elevStep = metricUnits ? (m_tileId.s >= 14 ? 100 : m_tileId.s >= 12 ? 200 : 500)
                                 : (m_tileId.s >= 14 ? 500 : m_tileId.s >= 12 ? 1000 : 2000)/3.28084f;

    // Keep start position of new quads
    size_t quadsStart = m_quads.size(), numLabels = m_labels.size();

    // idea here is to align grid points between zoom levels to keep labels in same position
    //  when zooming ... mostly defeated unfortunately by curved label placement, repeat distance
    int gridmult = std::max(0, std::min(int(m_tileId.s), 15) - m_tileId.z);
    int ngrid = gridSize << gridmult;
    float gridstart = 0.5f/(1 << std::max(0, 15 - m_tileId.z));
    glm::vec2 pos;
    for (int col = 0; col < ngrid; col++) {
        pos.y = (col + gridstart)/ngrid;
        for (int row = 0; row < ngrid; row++) {
            pos.x = (row + gridstart)/ngrid;

            Line line;
            float level = getContourLine(*m_texture, m_tileId, pos, elevStep, line);
            if (std::isnan(level)) { continue; }

            LabelAttributes attrib;
            params.text = std::to_string(int(metricUnits ? level : std::round(level*3.28084f))) + suffix;
            // make sure different levels are in different repeat groups (which is the behavior in the normal
            //  case where applyRule() is called for every label)
            params.labelOptions.repeatGroup = repeatGroupHash;
            hash_combine(params.labelOptions.repeatGroup, params.text);

            if (!prepareLabel(params, Label::Type::line, attrib)) { return false; }

            //size_t prevlabels = m_labels.size();
            addCurvedTextLabels(line, params, attrib, _rule);
            //LOGD("Placed %d contour label(s) for %s", int(m_labels.size() - prevlabels), m_tileId.toString().c_str());
        }
    }

    if (numLabels == m_labels.size()) { m_quads.resize(quadsStart); }

    return true;
}

std::unique_ptr<StyledMesh> ContourTextStyleBuilder::build() {
    m_texture.reset();
    return TextStyleBuilder::build();
}

void ContourTextStyle::build(const Scene& _scene) {
    m_metricUnits = _scene.options().metricUnits;
    Style::build(_scene);
}

std::unique_ptr<StyleBuilder> ContourTextStyle::createBuilder() const {
    return std::make_unique<ContourTextStyleBuilder>(*this);
}

#ifdef TANGRAM_CONTOUR_DEBUG

class ContourDebugStyleBuilder : public StyleBuilder {

public:
    ContourDebugStyleBuilder(const ContourDebugStyle& _style) : m_style(_style) {}
    const Style& style() const override { return m_style; }

    MeshData<DebugStyle::Vertex> m_meshData;
    const ContourDebugStyle& m_style;
    double m_tileScale;

    void setup(const Tile& _tile) override;
    void setup(const Marker& _marker, int zoom) override {}
    bool addFeature(const Feature& _feat, const DrawRule& _rule) override;
    std::unique_ptr<StyledMesh> build() override;

private:
    TileID m_tileId = {-1, -1, -1};
    std::shared_ptr<Texture> m_texture;
};


void ContourDebugStyleBuilder::setup(const Tile& _tile) {

    if (_tile.rasters().empty() || _tile.rasters().front().texture->width() <= 1) { return; }

    m_tileId = _tile.getID();
    m_texture = _tile.rasters().front().texture;
    m_tileScale = _tile.getScale();
}

bool ContourDebugStyleBuilder::addFeature(const Feature& _feat, const DrawRule& _rule) {

    if (!m_texture) { return false; }

    float elevStep = m_style.m_metricUnits ? (m_tileId.z >= 14 ? 100 : m_tileId.z >= 12 ? 200 : 500)
            : (m_tileId.z >= 14 ? 500 : m_tileId.z >= 12 ? 1000 : 2000)/3.28084f;

    //int ngrid = ContourTextStyleBuilder::gridSize + (m_tileId.s - m_tileId.z);
    int gridmult = std::max(0, std::min(int(m_tileId.s), 15) - m_tileId.z);
    int ngrid = (ContourTextStyleBuilder::gridSize << gridmult);
    float gridstart = 0.5f/(1 << std::max(0, 15 - m_tileId.z));
    glm::vec2 pos;
    for (int col = 0; col < ngrid; col++) {
        pos.y = (col + gridstart)/ngrid;
        for (int row = 0; row < ngrid; row++) {
            pos.x = (row + gridstart)/ngrid;

            Line line;
            float level = getContourLine(*m_texture, m_tileId, pos, elevStep, line);
            if(line.empty()) { continue; }

            GLuint abgr = std::isnan(level) ? 0xFF00FF00 : 0xFF0000FF;
            for (size_t ii = 0; ii < line.size(); ++ii) {
                auto& pt = line[ii];
                float elev = m_style.m_terrain3d ? ElevationManager::elevationLerp(*m_texture, pt)/m_tileScale : 0;
                m_meshData.vertices.push_back({glm::vec3(pt, elev), abgr});
                if (ii == 0) continue;
                m_meshData.indices.push_back(ii-1);
                m_meshData.indices.push_back(ii);
            }
            m_meshData.offsets.emplace_back(2*line.size()-2, line.size());
        }
    }
    return true;
}

std::unique_ptr<StyledMesh> ContourDebugStyleBuilder::build() {
    m_texture.reset();
    if (m_meshData.vertices.empty()) { return nullptr; }
    auto mesh = std::make_unique<Mesh<DebugStyle::Vertex>>(
            m_style.vertexLayout(), m_style.drawMode());
    mesh->compile(m_meshData);
    m_meshData.clear();
    return std::move(mesh);
}

void ContourDebugStyle::build(const Scene& _scene) {
    m_metricUnits = _scene.options().metricUnits;
    m_terrain3d = bool(_scene.elevationManager());
    Style::build(_scene);
}

std::unique_ptr<StyleBuilder> ContourDebugStyle::createBuilder() const {
    return std::make_unique<ContourDebugStyleBuilder>(*this);
}

#endif

}
