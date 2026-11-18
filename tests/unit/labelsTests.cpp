#include "catch.hpp"
#include "gl/dynamicQuadMesh.h"
#include "labels/labelManager.h"
#include "labels/obbBuffer.h"
#include "labels/textLabel.h"
#include "labels/textLabels.h"
#include "map.h"
#include "platform.h"
#include "scene/scene.h"
#include "style/style.h"
#include "style/textStyle.h"
#include "tile/tile.h"
#include "view/view.h"

#include <memory>

namespace Tangram {

TextStyle dummyStyle("textStyle");
TextLabels dummy(dummyStyle);
Label::AABB* bounds = nullptr;

std::unique_ptr<TextLabel> makeLabel(glm::vec2 _transform, Label::Type _type, std::string id) {
    Label::Options options;
    options.offset = {0.0f, 0.0f};
    //options.properties = std::make_shared<Properties>();
    //options.properties->set("id", id);
    options.anchors.anchor[0] = LabelProperty::Anchor::center;
    options.anchors.count = 1;

    return std::unique_ptr<TextLabel>(new TextLabel({{glm::vec3(_transform, 0)}}, _type, options,
                                                    {}, {10, 10}, dummy, {},
                                                    TextLabelProperty::Align::none));
}

TextLabel makeLabelWithAnchorFallbacks(glm::vec2 _transform, glm::vec2 _offset = {0, 0}) {
    Label::Options options;

    // options.anchors.anchor[0] = LabelProperty::Anchor::center;
    options.anchors.anchor[0] = LabelProperty::Anchor::right;
    options.anchors.anchor[1] = LabelProperty::Anchor::bottom;
    options.anchors.anchor[2] = LabelProperty::Anchor::left;
    options.anchors.anchor[3] = LabelProperty::Anchor::top;
    options.anchors.count = 4;

    options.offset = _offset;

    TextRange textRanges;

    return TextLabel({{glm::vec3(_transform, 0)}}, Label::Type::point, options,
            {}, {10, 10}, dummy, textRanges, TextLabelProperty::Align::none);
}

#if 0
TEST_CASE("Test getFeaturesAtPoint", "[Labels][FeaturePicking]") {
    std::unique_ptr<Labels> labels(new Labels());

    View view(256, 256);
    view.setPosition(0, 0);
    view.setZoom(0);
    view.update(false);

    struct TestLabelMesh : public LabelSet {
        void addLabel(std::unique_ptr<Label> _label) { m_labels.push_back(std::move(_label)); }
    };

    auto labelMesh = std::unique_ptr<TestLabelMesh>(new TestLabelMesh());
    auto textStyle = std::unique_ptr<TextStyle>(new TextStyle("test", nullptr, false));
    textStyle->setID(0);

    labelMesh->addLabel(makeLabel(glm::vec2{.5f,.5f}, Label::Type::point, "0"));
    labelMesh->addLabel(makeLabel(glm::vec2{1,0}, Label::Type::point, "1"));
    labelMesh->addLabel(makeLabel(glm::vec2{1,1}, Label::Type::point, "2"));

    std::shared_ptr<Tile> tile(new Tile({0,0,0}, view.getMapProjection()));
    tile->initGeometry(1);
    tile->setMesh(*textStyle.get(), std::move(labelMesh));
    tile->update(0, view);

    std::vector<std::unique_ptr<Style>> styles;
    styles.push_back(std::move(textStyle));

    std::vector<std::shared_ptr<Tile>> tiles;

    tiles.push_back(tile);
    {
        auto& items = labels->getFeaturesAtPoint(view.state(), 0, styles, tiles, 128, 128, false);
        REQUIRE(items.size() == 1);
        REQUIRE(items[0].properties->getString("id") == "0");
    }
    {
        auto& items = labels->getFeaturesAtPoint(view.state(), 0, styles, tiles, 256, 256, false);
        REQUIRE(items.size() == 1);
        REQUIRE(items[0].properties->getString("id") == "1");
    }

    {
        auto& items = labels->getFeaturesAtPoint(view.state(), 0, styles, tiles, 256, 0, false);
        REQUIRE(items.size() == 1);
        REQUIRE(items[0].properties->getString("id") == "2");
    }
}
#endif

struct TestTransform {
    ScreenTransform transform;
    TestTransform(ScreenTransform::Buffer& _buffer, Range& _range) : transform(_buffer, _range) {}
};

// runs LabelManager::handleOcclusions() on labels in insertion order (stands in for priority order)
class TestLabels : public LabelManager {
public:
    TestLabels(View& _v) {
        m_isect2d.resize({1, 1}, {_v.getWidth(), _v.getHeight()});
        // returned transforms reference entries of both vectors, so they must not reallocate
        m_labels.reserve(16);
        tmpTransforms.reserve(16);
    }

    ScreenTransform& addLabel(Label* _l, Tile* _t) {
        m_labels.push_back({_l, nullptr, _t, nullptr, false, {}});
        tmpTransforms.emplace_back(m_transforms, m_labels.back().transformRange);
        return tmpTransforms.back().transform;
    }
    void run(View& _v) { handleOcclusions(_v.state()); }
    void clear() { m_labels.clear(); }

    std::vector<TestTransform> tmpTransforms;

};

// 10x10 point label with given anchors (in fallback order)
TextLabel makePointLabel(glm::vec2 _transform, std::vector<LabelProperty::Anchor> _anchors,
                         float _radialOffset = 0) {
    Label::Options options;
    options.radialOffset = _radialOffset;
    for (size_t ii = 0; ii < _anchors.size(); ++ii) { options.anchors.anchor[ii] = _anchors[ii]; }
    options.anchors.count = _anchors.size();
    return TextLabel({{glm::vec3(_transform, 0)}}, Label::Type::point, options,
            {}, {10, 10}, dummy, {}, TextLabelProperty::Align::none);
}

glm::vec2 obbCenter(const Label& _l, ScreenTransform& _transform) {
    std::vector<isect2d::OBB<glm::vec2>> obbs;
    Range range;
    OBBBuffer buffer{obbs, range};
    _l.obbs(_transform, buffer);
    return obbs.at(range.start).getCentroid();
}

TEST_CASE( "Test anchor fallback behavior", "[Labels][AnchorFallback]" ) {

    View view(256, 256);
    view.setConstrainToWorldBounds(false);
    view.setPosition(0, 0);
    view.setZoom(0);
    view.update();

    Tile tile({0,0,0});
    tile.update(view, 0);

    {
        TestLabels labels(view);
        TextLabel l1 = makeLabelWithAnchorFallbacks(glm::vec2{0.5,0.5});
        auto& t1 = labels.addLabel(&l1, &tile);
        l1.update(tile.mvp(), view.state(), bounds, t1);

        TextLabel l2 = makeLabelWithAnchorFallbacks(glm::vec2{0.5,0.5});
        auto& t2 = labels.addLabel(&l2, &tile);
        l2.update(tile.mvp(), view.state(), bounds, t2);

        labels.run(view);
        REQUIRE(l1.isOccluded() == false);
        REQUIRE(l2.isOccluded() == true);

        REQUIRE(l1.anchorType() == LabelProperty::Anchor::right);
        REQUIRE(l2.anchorType() == LabelProperty::Anchor::right);
    }

    {
        TestLabels labels(view);
        TextLabel l1 = makeLabelWithAnchorFallbacks(glm::vec2{0.5,0.5});
        auto& t1 = labels.addLabel(&l1, &tile);
        l1.update(tile.mvp(), view.state(), bounds, t1);

        // Second label is one pixel left of L1
        TextLabel l2 = makeLabelWithAnchorFallbacks(glm::vec2{0.5 - 1./256,0.5});
        auto& t2 = labels.addLabel(&l2, &tile);
        l2.update(tile.mvp(), view.state(), bounds, t2);

        labels.run(view);
        // l1.print();
        // l2.print();
        REQUIRE(l1.isOccluded() == false);
        REQUIRE(l2.isOccluded() == false);

        REQUIRE(l1.anchorType() == LabelProperty::Anchor::right);
        // Check that left-of anchor fallback is used
        REQUIRE(l2.anchorType() == LabelProperty::Anchor::left);
    }

    {
        TestLabels labels(view);
        TextLabel l1 = makeLabelWithAnchorFallbacks(glm::vec2{0.5,0.5});
        auto& t1 = labels.addLabel(&l1, &tile);
        l1.update(tile.mvp(), view.state(), bounds, t1);

        // Second label is 10 pixel top of L1
        TextLabel l2 = makeLabelWithAnchorFallbacks(glm::vec2{0.5,0.5 + 10./256});
        auto& t2 = labels.addLabel(&l2, &tile);
        l2.update(tile.mvp(), view.state(), bounds, t2);

        labels.run(view);
        REQUIRE(l1.isOccluded() == false);
        REQUIRE(l2.isOccluded() == false);

        REQUIRE(l1.anchorType() == LabelProperty::Anchor::right);
        // Check that anchor fallback is used
        REQUIRE(l2.anchorType() == LabelProperty::Anchor::top);
    }

    {
        TestLabels labels(view);
        TextLabel l1 = makeLabelWithAnchorFallbacks(glm::vec2{0.5,0.5});
        auto& t1 = labels.addLabel(&l1, &tile);
        l1.update(tile.mvp(), view.state(), bounds, t1);

        // Second label is 10 pixel below of L1
        TextLabel l2 = makeLabelWithAnchorFallbacks(glm::vec2{0.5,0.5 - 10./256});
        auto& t2 = labels.addLabel(&l2, &tile);
        l2.update(tile.mvp(), view.state(), bounds, t2);

        labels.run(view);
        REQUIRE(l1.isOccluded() == false);
        REQUIRE(l2.isOccluded() == false);

        REQUIRE(l1.anchorType() == LabelProperty::Anchor::right);
        // Check that anchor fallback is used
        REQUIRE(l2.anchorType() == LabelProperty::Anchor::bottom);
    }

}
// attached label (e.g. text attached to an icon) must be placed relative to its parent's current anchor,
//  including the anchor chosen by fallback, not as if the parent were centered on the point
TEST_CASE( "Attached label follows parent anchor", "[Labels][AnchorFallback]" ) {

    View view(256, 256);
    view.setConstrainToWorldBounds(false);
    view.setPosition(0, 0);
    view.setZoom(0);
    view.update();

    Tile tile({0,0,0});
    tile.update(view, 0);

    using Anchor = LabelProperty::Anchor;
    const float px = 1.f/256;  // 1 tile unit = 256 px

    // parent at its first anchor (right): child to the right of parent
    {
        TestLabels labels(view);
        TextLabel parent = makePointLabel({0.5, 0.5}, {Anchor::right, Anchor::left});
        auto& tp = labels.addLabel(&parent, &tile);
        parent.update(tile.mvp(), view.state(), bounds, tp);

        TextLabel child = makePointLabel({0.5, 0.5}, {Anchor::right});
        child.setRelative(parent, false, false);
        auto& tc = labels.addLabel(&child, &tile);
        child.update(tile.mvp(), view.state(), bounds, tc);

        labels.run(view);
        REQUIRE(!parent.isOccluded());
        REQUIRE(!child.isOccluded());
        REQUIRE(parent.anchorType() == Anchor::right);

        glm::vec2 c0 = parent.screenCenter();
        glm::vec2 pc = obbCenter(parent, tp), cc = obbCenter(child, tc);
        REQUIRE(pc.x == Approx(c0.x + 5));
        // parent occupies [0, 10] right of point, so child centered at 15 (was 10, overlapping parent)
        REQUIRE(cc.x == Approx(c0.x + 15));
        REQUIRE(cc.y == Approx(c0.y));
    }

    // blocker right of point: parent falls back to left; child must follow and avoid the blocker
    {
        TestLabels labels(view);
        // 10x10 centered 12px right of point: [7, 17]
        TextLabel blocker = makePointLabel({0.5 + 12*px, 0.5}, {Anchor::center});
        auto& tb = labels.addLabel(&blocker, &tile);
        blocker.update(tile.mvp(), view.state(), bounds, tb);

        TextLabel parent = makePointLabel({0.5, 0.5}, {Anchor::right, Anchor::left});
        auto& tp = labels.addLabel(&parent, &tile);
        parent.update(tile.mvp(), view.state(), bounds, tp);

        TextLabel child = makePointLabel({0.5, 0.5}, {Anchor::right, Anchor::left});
        child.setRelative(parent, false, false);
        auto& tc = labels.addLabel(&child, &tile);
        child.update(tile.mvp(), view.state(), bounds, tc);

        labels.run(view);
        REQUIRE(!blocker.isOccluded());
        REQUIRE(!parent.isOccluded());
        REQUIRE(!child.isOccluded());
        REQUIRE(parent.anchorType() == Anchor::left);

        glm::vec2 c0 = parent.screenCenter();
        glm::vec2 pc = obbCenter(parent, tp), cc = obbCenter(child, tc);
        REQUIRE(pc.x == Approx(c0.x - 5));
        // child right of parent ([0, 10]) hits blocker, so falls back to left of parent: [-20, -10]
        REQUIRE(child.anchorType() == Anchor::left);
        REQUIRE(cc.x == Approx(c0.x - 15));
        REQUIRE(cc.y == Approx(c0.y));
    }
}

// radial offset (scalar `offset`) pushes label away from point along its current anchor's direction, incl.
//  fallback anchors; attached text follows parent's radial offset and adds its own relative to parent
TEST_CASE( "Radial offset follows anchor", "[Labels][AnchorFallback]" ) {

    View view(256, 256);
    view.setConstrainToWorldBounds(false);
    view.setPosition(0, 0);
    view.setZoom(0);
    view.update();

    Tile tile({0,0,0});
    tile.update(view, 0);

    using Anchor = LabelProperty::Anchor;
    const float px = 1.f/256;

    // first anchor (right), radial 4 on parent, 2 on child: parent [4, 14], child [16, 26]
    {
        TestLabels labels(view);
        TextLabel parent = makePointLabel({0.5, 0.5}, {Anchor::right, Anchor::left}, 4);
        auto& tp = labels.addLabel(&parent, &tile);
        parent.update(tile.mvp(), view.state(), bounds, tp);

        TextLabel child = makePointLabel({0.5, 0.5}, {Anchor::right, Anchor::left}, 2);
        child.setRelative(parent, false, false);
        auto& tc = labels.addLabel(&child, &tile);
        child.update(tile.mvp(), view.state(), bounds, tc);

        labels.run(view);
        REQUIRE(!parent.isOccluded());
        REQUIRE(!child.isOccluded());
        REQUIRE(parent.anchorType() == Anchor::right);

        glm::vec2 c0 = parent.screenCenter();
        glm::vec2 pc = obbCenter(parent, tp), cc = obbCenter(child, tc);
        REQUIRE(pc.x == Approx(c0.x + 9));
        REQUIRE(pc.y == Approx(c0.y));
        REQUIRE(cc.x == Approx(c0.x + 21));
        REQUIRE(cc.y == Approx(c0.y));
    }

    // blocker right of point: parent falls back to left, so radial offset must push left: parent [-14, -4]
    {
        TestLabels labels(view);
        TextLabel blocker = makePointLabel({0.5 + 12*px, 0.5}, {Anchor::center});
        auto& tb = labels.addLabel(&blocker, &tile);
        blocker.update(tile.mvp(), view.state(), bounds, tb);

        TextLabel parent = makePointLabel({0.5, 0.5}, {Anchor::right, Anchor::left}, 4);
        auto& tp = labels.addLabel(&parent, &tile);
        parent.update(tile.mvp(), view.state(), bounds, tp);

        TextLabel child = makePointLabel({0.5, 0.5}, {Anchor::left, Anchor::right}, 2);
        child.setRelative(parent, false, false);
        auto& tc = labels.addLabel(&child, &tile);
        child.update(tile.mvp(), view.state(), bounds, tc);

        labels.run(view);
        REQUIRE(!blocker.isOccluded());
        REQUIRE(!parent.isOccluded());
        REQUIRE(!child.isOccluded());
        REQUIRE(parent.anchorType() == Anchor::left);

        glm::vec2 c0 = parent.screenCenter();
        glm::vec2 pc = obbCenter(parent, tp), cc = obbCenter(child, tc);
        REQUIRE(pc.x == Approx(c0.x - 9));
        // child left of parent: [-26, -16]
        REQUIRE(child.anchorType() == Anchor::left);
        REQUIRE(cc.x == Approx(c0.x - 21));
    }

    // diagonal anchor: radial offset applies on both axes (box clears point by 4 in x and y)
    {
        TestLabels labels(view);
        TextLabel label = makePointLabel({0.5, 0.5}, {Anchor::bottom_right}, 4);
        auto& tl = labels.addLabel(&label, &tile);
        label.update(tile.mvp(), view.state(), bounds, tl);

        labels.run(view);
        REQUIRE(!label.isOccluded());
        glm::vec2 c0 = label.screenCenter();
        glm::vec2 lc = obbCenter(label, tl);
        REQUIRE(lc.x == Approx(c0.x + 9));
        REQUIRE(std::abs(lc.y - c0.y) == Approx(9));
    }
}

}
