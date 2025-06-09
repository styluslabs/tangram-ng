//
// Created by Matt Blair on 9/15/18.
//

#include "yamlUtil.h"
#include "log.h"
#include "util/floatFormatter.h"
#include "js/JavaScript.h"
#include "csscolorparser.hpp"
#include <cmath>

namespace Tangram {
namespace YamlUtil {

glm::vec4 getColorAsVec4(const YAML::Node& node) {
    double val;
    if (getDouble(node, val, false)) {
        return glm::vec4(val, val, val, 1.0);
    }
    if (node.IsSequence()) {
        glm::vec4 vec;
        if (parseVec(node, vec)) {
            if (node.size() < 4) {
                vec.w = 1.0;
            }
            return vec;
        }
    }
    if (node.IsScalar()) {
        auto c = CSSColorParser::parse(node.Scalar());
        return glm::vec4(c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a);
    }
    return glm::vec4();
}

bool getInt(const YAML::Node& node, int& result, bool allowTrailingJunk) {
    double value;
    if (getDouble(node, value, allowTrailingJunk)) {
        result = static_cast<int>(std::round(value));
        return true;
    }
    return false;
}

int getIntOrDefault(const YAML::Node& node, int defaultValue, bool allowTrailingJunk) {
    getInt(node, defaultValue, allowTrailingJunk);
    return defaultValue;
}

bool getFloat(const YAML::Node& node, float& result, bool allowTrailingJunk) {
    if (node.isNumber()) { result = float(node.getNumber()); return true; }
    if (node.IsScalar()) {
        const std::string& scalar = node.Scalar();
        int size = static_cast<int>(scalar.size());
        int count = 0;
        float value = ff::stof(scalar.data(), size, &count);
        if (count > 0 && (count == size || allowTrailingJunk)) {
            result = value;
            return true;
        }
    }
    return false;
}

float getFloatOrDefault(const YAML::Node& node, float defaultValue, bool allowTrailingJunk) {
    getFloat(node, defaultValue, allowTrailingJunk);
    return defaultValue;
}

bool getDouble(const YAML::Node& node, double& result, bool allowTrailingJunk) {
    if (node.isNumber()) { result = node.getNumber(); return true; }
    if (node.IsScalar()) {
        const std::string& scalar = node.Scalar();
        int size = static_cast<int>(scalar.size());
        int count = 0;
        double value = ff::stod(scalar.data(), size, &count);
        if (count > 0 && (count == size || allowTrailingJunk)) {
            result = value;
            return true;
        }
    }
    return false;
}

double getDoubleOrDefault(const YAML::Node& node, double defaultValue, bool allowTrailingJunk) {
    getDouble(node, defaultValue, allowTrailingJunk);
    return defaultValue;
}

bool getBool(const YAML::Node& node, bool& result) {
    bool ok = false;
    result = node.as<bool>(result, &ok);  //YAML::convert<bool>::decode(node, result);
    return ok;
}

bool getBoolOrDefault(const YAML::Node& node, bool defaultValue) {
    getBool(node, defaultValue);
    return defaultValue;
}

void mergeMapFields(YAML::Node& target, YAML::Node&& import) {
    if (!target.IsMap() || !import.IsMap()) {

        if (target && (target.Type() != import.Type())) {
            LOGN("Merging different node types: \n'%s'\n<--\n'%s'",
                 Dump(target).c_str(), Dump(import).c_str());
        }

        target = std::move(import);

    } else {
        for (auto entry : import.pairs()) {

            const auto& key = entry.first.Scalar();
            auto& source = entry.second;
            auto& dest = target[key];
            //if(dest.isMap() && source.IsNull) continue;  -- don't replace map w/ empty node?
            mergeMapFields(dest, std::move(source));
        }
    }
}

// Convert a scalar node to a boolean, double, or string (in that order)
// and for the first conversion that works, push it to the top of the JS stack.
static JSValue pushYamlScalarAsJsPrimitive(JSScope& jsScope, const YAML::Node& node) {
    bool booleanValue = false;
    double numberValue = 0.;
    if (YamlUtil::getBool(node, booleanValue)) {
        return jsScope.newBoolean(booleanValue);
    } else if (YamlUtil::getDouble(node, numberValue)) {
        return jsScope.newNumber(numberValue);
    } else {
        return jsScope.newString(node.Scalar());
    }
}

static JSValue pushYamlScalarAsJsFunctionOrString(JSScope& jsScope, const YAML::Node& node) {
    auto value = jsScope.newFunction(node.Scalar());
    if (value) {
        return value;
    }
    return jsScope.newString(node.Scalar());
}

JSValue toJSValue(JSScope& jsScope, const YAML::Node& node) {
    switch(node.Type()) {
    case YAML::NodeType::Scalar: {
        auto& scalar = node.Scalar();
        if (scalar.compare(0, 8, "function") == 0) {
            return pushYamlScalarAsJsFunctionOrString(jsScope, node);
        }
        return pushYamlScalarAsJsPrimitive(jsScope, node);
    }
    case YAML::NodeType::Sequence: {
        auto jsArray = jsScope.newArray();
        for (int i = 0; i < node.size(); i++) {
            jsArray.setValueAtIndex(i, toJSValue(jsScope, node[i]));
        }
        return jsArray;
    }
    case YAML::NodeType::Map: {
        auto jsObject = jsScope.newObject();
        for (const auto& entry : node.pairs()) {
            if (!entry.first.IsScalar()) {
                continue; // Can't put non-scalar keys in JS objects.
            }
            jsObject.setValueForProperty(entry.first.Scalar(), toJSValue(jsScope, entry.second));
        }
        return jsObject;
    }
    default:
        return jsScope.newNull();
    }
}

} // namespace YamlUtil
} // namespace Tangram
