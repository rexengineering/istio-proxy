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
    return get_array_as_string(objects);
}

std::string get_array_as_string(const std::vector<Json::ObjectSharedPtr>& objects) {
    std::stringstream ss;
    ss << "[";
    for (size_t i = 0; i < objects.size(); i++) {
        ss << get_object_as_string(objects[i].get());
        if (i < objects.size() - 1) {
            ss << ",";
        }
    }
    ss << "]";
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

std::string get_array_or_obj_str_from_json(const Json::ObjectSharedPtr json_obj,
        const std::string& key, const std::string& type) {
    
    if (type != "JSON_OBJECT" && type != "JSON_ARRAY") {
        throw EnvoyException("Bad param type: " + type);
    }

    if (!json_obj->hasObject(key)) {
        throw EnvoyException("Could not find " + key + " in json.");
    }

    std::string result;
    if (type == "JSON_OBJECT") {
        Json::ObjectSharedPtr result_ptr = json_obj->getObject(key);
        if (result_ptr->type() != Json::Type::Object) {
            throw EnvoyException(
                "Asked to get an object out (" + key + ") but it wasn't an object."
            );
        }
        result = get_object_as_string(result_ptr.get());
    } else if (type == "JSON_ARRAY") {
        std::vector<Json::ObjectSharedPtr> arr = json_obj->getObjectArray(key);
        result = get_array_as_string(arr);
    }
    return result;
}

std::string build_json_from_params(const Json::ObjectSharedPtr json_obj,
        const std::vector<bavs::BAVSParameter>& input_params) {

    std::map<std::string, std::string> json_elements;

    /**
     * First, we gotta process the case wherein the user asks simply send
     * a portion of the context as input without renaming it as anything, or
     * when the user wishes to add the entire response as a single variable
     * without renaming it.
     * 
     * TODO: maybe put this out into a different function.
     * 
     * TODO: this paradigm will allow us to support non-JSON types; for example,
     * we could store any arbitrary data (like an image or pdf) by b64encoding
     * the payload and saving the content-type header. But this is yet unimplemented.
     */
    if (input_params.size() == 1 && input_params[0].name() == ".") {

        // Wants to pull a context variable and send just that variable directly as input

        const auto& param = input_params[0];
        if (!json_obj->hasObject(param.value())) {
            if (param.default_value() == "") {
                throw EnvoyException("Could not find param " + param.value());
            } else {
                return param.default_value();
            }
        }
        return get_array_or_obj_str_from_json(json_obj, param.value(), param.type());
    } else if (input_params.size() == 1 && input_params[0].value() == ".") {

        // Wants to stuff entire response as a context variable for next tasks

        const auto& param = input_params[0];
        json_elements[param.name()] = get_object_as_string(json_obj.get());
        return create_json_string(json_elements);

    }

    for (auto const& param : input_params) {
        if (!json_obj->hasObject(param.value())) {
            if (param.default_value() == "") {
                throw EnvoyException("Could not find param " + param.value());
            } else {
                json_elements[param.name()] = param.default_value();
            }
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
            element = std::to_string(json_obj->getInteger(param.value()));
        } else if (param.type() == "JSON_OBJECT") {
            element = json_obj->getObject(param.value())->asJsonString();
        } else if (param.type() == "JSON_ARRAY") {
            std::string cur_type = "JSON_ARRAY";
            element = get_array_or_obj_str_from_json(json_obj, param.value(), cur_type);
        } else {
            throw EnvoyException("Invalid Envoy Config");
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