#include "data/propertyItem.h"
#include "data/properties.h"
#include "rapidjson/writer.h"
#include <algorithm>
#include <cmath>

namespace Tangram {

std::string doubleToString(double _doubleValue) {
    std::string value = std::to_string(_doubleValue);

    // Remove trailing '0's
    value.erase(value.find_last_not_of('0') + 1, std::string::npos);
    // If last value is '.', clear it too
    value.erase(value.find_last_not_of('.') + 1, std::string::npos);

    return value;
}

Properties::Properties() : sourceId(0) {}

Properties::Properties(std::vector<Item>&& _items) : sourceId(0), props(_items) {}

Properties::~Properties() {}

Properties& Properties::operator=(Properties&& _other) {
    props = std::move(_other.props);
    sourceId = _other.sourceId;
    return *this;
}

void Properties::setSorted(std::vector<Item>&& _items) {
    props = std::move(_items);
}

const Value& Properties::get(const std::string& key) const {

    const auto it = std::find_if(props.begin(), props.end(),
                                 [&](const auto& item) {
                                     return item.key == key;
                                 });
    if (it == props.end()) {
        return NOT_A_VALUE;
    }

    // auto it = std::lower_bound(props.begin(), props.end(), key,
    //                            [](auto& item, auto& key) {
    //                                return keyComparator(item.key, key);
    //                            });
    // if (it == props.end() || it->key != key) {
    //     return NOT_FOUND;
    // }

    return it->value;
}

void Properties::clear() { props.clear(); }

bool Properties::contains(const std::string& key) const {
    return !get(key).is<none_type>();
}

bool Properties::getNumber(const std::string& key, double& value) const {
    auto& it = get(key);
    if (it.is<double>()) {
        value = it.get<double>();
        return true;
    }
    return false;
}

double Properties::getNumber(const std::string& key) const {
    auto& it = get(key);
    if (it.is<double>()) {
        return it.get<double>();
    }
    return 0;
}

bool Properties::getString(const std::string& key, std::string& value) const {
    auto& it = get(key);
    if (it.is<std::string>()) {
        value = it.get<std::string>();
        return true;
    }
    return false;
}

const std::string& Properties::getString(const std::string& key) const {
    const static std::string EMPTY_STRING = "";

    auto& it = get(key);
    if (it.is<std::string>()) {
        return it.get<std::string>();
    }
    return EMPTY_STRING;
}

bool Properties::getAsString(const std::string& key, std::string& value) const {
    auto& it = get(key);

    if (it.is<std::string>()) {
        value = it.get<std::string>();
        return true;
    } else if (it.is<double>()) {
        value = doubleToString(it.get<double>());
        return true;
    }

    return false;
}

std::string Properties::asString(const Value& value) {
    if (value.is<std::string>()) {
        return value.get<std::string>();
    } else if (value.is<double>()) {
        return doubleToString(value.get<double>());
    }
    return "";
}

std::string Properties::getAsString(const std::string& key) const {
    return asString(get(key));
}

void Properties::sort() {
    std::sort(props.begin(), props.end());
}

void Properties::setValue(std::string key, Value value) {

    auto it = std::lower_bound(props.begin(), props.end(), key,
        [](auto& item, auto& key) { return keyComparator(item.key, key); });

    if (it == props.end() || it->key != key) {
        props.emplace(it, std::move(key), std::move(value));
    } else {
        it->value = std::move(value);
    }
}

void Properties::set(std::string key, std::string value) {
    setValue(key, Value(std::move(value)));
}

void Properties::set(std::string key, double value) {
    setValue(key, Value(value));
}

std::string Properties::toJson() const {
    // use rapidjson to handle escaping " and \ in strings
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    writer.StartObject();
    for (const auto& item : props) {
        writer.String(item.key.c_str());
        if (item.value.is<std::string>()) {
            writer.String(item.value.get<std::string>().c_str());
        } else if (item.value.is<double>()) {
            double val = item.value.get<double>();
            if (std::isfinite(val)) { writer.Double(val); }
            else { writer.Null(); }
        } else {
            writer.String("");
        }
    }
    writer.EndObject();
    return sb.GetString();
}

}
