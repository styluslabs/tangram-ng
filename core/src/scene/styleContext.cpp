#include "scene/styleContext.h"

#include "data/propertyItem.h"
#include "data/tileData.h"
#include "js/JavaScript.h"
#include "log.h"
#include "platform.h"
#include "scene/filters.h"
#include "scene/scene.h"
#include "util/mapProjection.h"
#include "util/builders.h"
#include "util/yamlUtil.h"

namespace Tangram {

#ifdef TANGRAM_JS_TRACING
void reportJSTrace(uint32_t _id, double secs);
struct JSTracer {
    std::chrono::steady_clock::time_point m_t0 = std::chrono::steady_clock::now();
    uint32_t m_id;
    JSTracer(uint32_t _id) : m_id(_id) {}
    ~JSTracer() {
        auto t1 = std::chrono::steady_clock::now();
        reportJSTrace(m_id, std::chrono::duration<double>(t1 - m_t0).count());
    }
};
#endif

static const std::vector<std::string> s_geometryStrings = {
    "", // unknown
    "point",
    "line",
    "polygon",
};

StyleContext::StyleContext() {
    m_jsContext = std::make_unique<JSContext>();
}

StyleContext::~StyleContext() = default;

void StyleContext::setSceneGlobals(const YAML::Node& sceneGlobals) {

    if (!sceneGlobals) { return; }

    JSScope jsScope(*m_jsContext);

    auto jsValue = YamlUtil::toJSValue(jsScope, sceneGlobals);

    m_jsContext->setGlobalValue("global", std::move(jsValue));
}

void StyleContext::initFunctions(const Scene& _scene) {

    if (_scene.id == m_sceneId) {
        return;
    }
    m_sceneId = _scene.id;

    setSceneGlobals(_scene.config()["global"]);
    setFunctions(_scene.functions());
#ifdef TANGRAM_NATIVE_STYLE_FNS
    m_nativeFns = &_scene.nativeFns();
#endif
}

bool StyleContext::setFunctions(const std::vector<std::string>& _functions) {
    uint32_t id = 0;
    bool success = true;
    for (auto& function : _functions) {
        success &= m_jsContext->setFunction(id++, function);
    }

    m_functionCount = id;
    return success;
}

bool StyleContext::addFunction(const std::string& _function) {
    bool success = m_jsContext->setFunction(m_functionCount++, _function);
    return success;
}

void StyleContext::setFeature(const Feature& _feature) {

    m_feature = &_feature;

    if (m_keywordGeometry != m_feature->geometryType) {
        setKeyword(FilterKeyword::geometry, s_geometryStrings[m_feature->geometryType]);
        m_keywordGeometry = m_feature->geometryType;
    }

    m_jsContext->setCurrentFeature(&_feature);
}

void StyleContext::setTileID(TileID _tileId) {
    if (m_tileID == _tileId) return;
    LngLat center = MapProjection::projectedMetersToLngLat(MapProjection::tileCenter(_tileId));
    setKeyword(FilterKeyword::zoom, _tileId.s);
    setKeyword(FilterKeyword::latitude, center.latitude);
    setKeyword(FilterKeyword::longitude, center.longitude);
    // When new zoom is set, meters_per_pixel must be updated too.
    double meters_per_pixel = MapProjection::metersPerPixelAtZoom(_tileId.s);
    setKeyword(FilterKeyword::meters_per_pixel, meters_per_pixel);
    m_tileID = _tileId;
}

void StyleContext::setKeyword(FilterKeyword keyword, Value value) {

    Value& entry = m_keywordValues[static_cast<uint8_t>(keyword)];
    if (entry == value) {
        return;
    }

    const std::string& keywordString = filterKeywordToString(keyword);

    {
        JSScope jsScope(*m_jsContext);
        JSValue jsValue;
        if (value.is<std::string>()) {
            jsValue = jsScope.newString(value.get<std::string>());
        } else if (value.is<double>()) {
            jsValue = jsScope.newNumber(value.get<double>());
        }
        m_jsContext->setGlobalValue(keywordString, std::move(jsValue));
    }

    entry = std::move(value);
}

double StyleContext::getPixelAreaScale() {
    // scale the filter value with pixelsPerMeter
    // used with `px2` area filtering
    double metersPerPixel = MapProjection::metersPerPixelAtZoom(m_tileID.s);
    return metersPerPixel * metersPerPixel;
}

void StyleContext::clear() {
    m_jsContext->setCurrentFeature(nullptr);
}

bool StyleContext::evalFilter(FunctionID _id) {
#ifdef TANGRAM_JS_TRACING
    JSTracer _jsTracer(_id);
#endif
    bool result = m_jsContext->evaluateBooleanFunction(_id);
    return result;
}

bool StyleContext::evalStyle(FunctionID _id, StyleParamKey _key, StyleParam::Value& _val) {
    _val = none_type{};

#ifdef TANGRAM_JS_TRACING
    JSTracer _jsTracer(_id);
#endif

#ifdef TANGRAM_NATIVE_STYLE_FNS
    if (m_nativeFns && _id < m_nativeFns->size() && m_nativeFns->at(_id)) {
        return m_nativeFns->at(_id)(*m_feature, _val);
    }
#endif

    JSScope jsScope(*m_jsContext);
    auto jsValue = jsScope.getFunctionResult(_id);
    if (!jsValue) {
        return false;
    }

    if (jsValue.isString()) {
        std::string value = jsValue.toString();

        switch (_key) {
            case StyleParamKey::outline_style:
            case StyleParamKey::repeat_group:
            case StyleParamKey::sprite:
            case StyleParamKey::sprite_default:
            case StyleParamKey::style:
            case StyleParamKey::text_align:
            case StyleParamKey::text_repeat_group:
            case StyleParamKey::text_source:
            case StyleParamKey::text_source_left:
            case StyleParamKey::text_source_right:
            case StyleParamKey::text_transform:
            case StyleParamKey::texture:
                _val = value;
                break;
            case StyleParamKey::color:
            case StyleParamKey::outline_color:
            case StyleParamKey::text_font_fill:
            case StyleParamKey::text_font_stroke_color: {
                Color result;
                if (StyleParam::parseColor(value, result)) {
                    _val = result.abgr;
                } else {
                    LOGW("Invalid color value: %s", value.c_str());
                }
                break;
            }
            default:
                _val = StyleParam::parseString(_key, value);
                break;
        }

    } else if (jsValue.isBoolean()) {
        bool value = jsValue.toBool();

        switch (_key) {
            case StyleParamKey::flat:
            case StyleParamKey::interactive:
            case StyleParamKey::text_interactive:
            case StyleParamKey::tile_edges:
            case StyleParamKey::visible:
            case StyleParamKey::outline_visible:
            case StyleParamKey::text_visible:
            case StyleParamKey::collide:
            case StyleParamKey::text_collide:
            case StyleParamKey::text_optional:
                _val = value;
                break;
            case StyleParamKey::extrude:
                if (value) {
                    _val = StyleParam::TextSource({"min_height", "height"});
                } else {
                    _val = glm::vec2(0.0f, 0.0f);
                }
                break;
            default:
                LOGW("Unused bool return type from Javascript style function for %d.", _key);
                break;
        }

    } else if (jsValue.isArray()) {
        auto len = jsValue.getLength();

        switch (_key) {
            case StyleParamKey::extrude: {
                if (len != 2) {
                    LOGW("Wrong array size for extrusion: '%d'.", len);
                    break;
                }

                double v1 = jsValue.getValueAtIndex(0).toDouble();
                double v2 = jsValue.getValueAtIndex(1).toDouble();

                _val = glm::vec2(v1, v2);
                break;
            }
            case StyleParamKey::color:
            case StyleParamKey::outline_color:
            case StyleParamKey::text_font_fill:
            case StyleParamKey::text_font_stroke_color: {
                if (len < 3 || len > 4) {
                    LOGW("Wrong array size for color: '%d'.", len);
                    break;
                }
                double r = jsValue.getValueAtIndex(0).toDouble();
                double g = jsValue.getValueAtIndex(1).toDouble();
                double b = jsValue.getValueAtIndex(2).toDouble();
                double a = 1.0;
                if (len == 4) {
                    a = jsValue.getValueAtIndex(3).toDouble();
                }
                _val = ColorF(r, g, b, a).toColor().abgr;
                break;
            }
            case StyleParamKey::size: {
                if (len != 2) {
                    LOGW("Wrong array size for style parameter 'size': '%d'.", len);
                    break;
                }
                StyleParam::SizeValue vec;
                vec.x.value = static_cast<float>(jsValue.getValueAtIndex(0).toDouble());
                vec.y.value = static_cast<float>(jsValue.getValueAtIndex(1).toDouble());
                _val = vec;
                break;
            }
            default:
                LOGW("Unused array return type from Javascript style function for %d.", _key);
                break;
        }
    } else if (jsValue.isNumber()) {
        double number = jsValue.toDouble();
        if (std::isnan(number)) {
            LOGD("duk evaluates JS method to NAN.\n");
        }
        switch (_key) {
            case StyleParamKey::text_source:
            case StyleParamKey::text_source_left:
            case StyleParamKey::text_source_right:
                _val = doubleToString(number);
                break;
            case StyleParamKey::extrude:
                _val = glm::vec2(0.f, number);
                break;
            case StyleParamKey::placement_spacing: {
                _val = StyleParam::Width{static_cast<float>(number), Unit::pixel};
                break;
            }
            case StyleParamKey::width:
            case StyleParamKey::outline_width: {
                // TODO more efficient way to return pixels.
                // atm this only works by return value as string
                _val = StyleParam::Width{static_cast<float>(number)};
                break;
            }
            case StyleParamKey::alpha:
            case StyleParamKey::angle:
            case StyleParamKey::outline_alpha:
            case StyleParamKey::priority:
            case StyleParamKey::text_font_alpha:
            case StyleParamKey::text_font_stroke_alpha:
            case StyleParamKey::text_priority:
            case StyleParamKey::text_font_stroke_width:
            case StyleParamKey::placement_min_length_ratio: {
                _val = static_cast<float>(number);
                break;
            }
            case StyleParamKey::size: {
                StyleParam::SizeValue vec;
                vec.x.value = static_cast<float>(number);
                _val = vec;
                break;
            }
            case StyleParamKey::order:
            case StyleParamKey::outline_order:
            case StyleParamKey::color:
            case StyleParamKey::outline_color:
            case StyleParamKey::text_font_fill:
            case StyleParamKey::text_font_stroke_color: {
                _val = static_cast<uint32_t>(number);
                break;
            }
            default:
                LOGW("Unused numeric return type from Javascript style function for %d.", _key);
                break;
        }
    } else if (jsValue.isUndefined()) {
        // Explicitly set value as 'undefined'. This is important for some styling rules.
        _val = Undefined();
    } else {
        LOGW("Unhandled return type from Javascript style function for %d.", _key);
    }
    return !_val.is<none_type>();
}

} // namespace Tangram
