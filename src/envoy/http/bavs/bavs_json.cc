#include <string>
#include <cstring>
#include <iostream>
#include <stdio.h>
#include <cstdlib>

#include "bavs.h"

namespace Envoy {
namespace Http {

std::string dumpHeaders(Http::RequestOrResponseHeaderMap& hdrs) {
    std::stringstream ss;
    hdrs.iterate(
        [&ss](const HeaderEntry& header) -> HeaderMap::Iterate {
            ss << header.key().getStringView() << ":" << header.value().getStringView() << "\n";
            return HeaderMap::Iterate::Continue;
        }
    );
    return ss.str();
}

std::string jstringify(const std::string& st) {
    return "\"" + st + "\"";
}

std::string create_json_string(const std::map<std::string, std::string>& json_elements) {
    std::stringstream ss;
    size_t num_params = json_elements.size();
    ss << "{";
    size_t i = 0;
    for (const auto& pair : json_elements) {
        ss << "\"" << pair.first << "\": " << pair.second;
        if (i++ < num_params - 1) {
            ss  << ",";
        }
    }
    ss << "}";
    return ss.str();
}

std::string get_array_as_string(const Json::Object* json) {
    std::vector<Json::ObjectSharedPtr> objects = json->asObjectArray();
    std::stringstream ss;
    ss << "[";
    for (size_t i = 0; i < objects.size(); i++) {
        ss << get_object_as_string(objects[i].get());
        if (i < objects.size() - 1) {
            ss << ",";
        }
    }
    return ss.str();
}

std::string get_object_as_string(const Json::Object* json) {
    switch (json->type()) {
        case Json::Type::Array:
            return get_array_as_string(json);
        case Json::Type::Boolean:
            return json->asBoolean() ? "true" : "false";
        case Json::Type::Double:
            return std::to_string(json->asDouble());
        case Json::Type::Integer:
            return std::to_string(json->asInteger());
        case Json::Type::Null:
            return "null";
        case Json::Type::Object:
            return json->asJsonString();
        case Json::Type::String:
            return "\"" + json->asString() + "\"";
    }
}

std::string build_json_from_params(const Json::ObjectSharedPtr json_obj,
        std::vector<bavs::BAVSParameter> input_params) {

    std::map<std::string, std::string> json_elements;

    for (auto const& param : input_params) {
        if (!json_obj->hasObject(param.value())) {
            throw EnvoyException("Could not find param " + param.value());
            continue;
        }

        std::string element;
        if (param.type() == "STRING") {
            element = "\"" + json_obj->getString(param.value()) + "\"";
        } else if (param.type() == "BOOLEAN") {
            element = json_obj->getBoolean(param.value()) ? "true" : "false";
        } else if (param.type() == "DOUBLE") {
            element = std::to_string(json_obj->getDouble(param.value()));
        } else if (param.type() == "INTEGER") {
            element = std::to_string(json_obj->getDouble(param.value()));
        } else if (param.type() == "JSON_OBJECT") {
            element = json_obj->getObject(param.value())->asJsonString();
        } else {
            std::cout << "invalid envoy config." << std::endl;
        }
        json_elements[param.name()] = element;
    }
    // TODO: Error handling

    return create_json_string(json_elements);
}

/**
 * Returns string representation. Equivalent to "json.dumps(original.update(updater))" in python.
 * Prerequisite: original->isObject() == true && updater->isObject() == true
 */
std::string merge_jsons(const Json::ObjectSharedPtr original, const Json::ObjectSharedPtr updater) {
    std::map<std::string, std::string> json_elements;
    original->iterate([&json_elements] (const std::string& key, const Json::Object& val) -> bool {
        json_elements[key] = get_object_as_string(&val);
        return true;
    });
    updater->iterate([&json_elements] (const std::string& key, const Json::Object& val) -> bool {
        json_elements[key] = get_object_as_string(&val);
        return true;
    });

    return create_json_string(json_elements);
}

} // namespace Http
} // namespace Envoy