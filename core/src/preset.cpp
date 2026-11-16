#include "tf/preset.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace tf {

namespace {
using Json = nlohmann::ordered_json;

std::optional<float> readValue(const ParamSpec& spec, const Json& value) {
    if (value.is_boolean() && spec.kind == ParamKind::toggle) return value.get<bool>() ? 1.0f : 0.0f;
    if (value.is_string() && spec.kind != ParamKind::continuous) {
        const auto label = value.get<std::string>();
        const auto found = std::find(spec.choices.begin(), spec.choices.end(), label);
        if (found == spec.choices.end()) return std::nullopt;
        return static_cast<float>(found - spec.choices.begin());
    }
    if (value.is_number()) return clampToRange(spec, value.get<float>());
    return std::nullopt;
}

Json writeValue(const ParamSpec& spec, float real) {
    switch (spec.kind) {
        case ParamKind::toggle: return real >= 0.5f;
        case ParamKind::choice: return std::string(spec.choices[static_cast<std::size_t>(clampToRange(spec, real))]);
        case ParamKind::continuous: break;
    }
    return real;
}

void fail(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}
}

std::optional<Preset> parsePreset(std::string_view json, std::string* error) {
    const Json root = Json::parse(json, nullptr, false);
    if (!root.is_object()) {
        fail(error, "preset is not a JSON object");
        return std::nullopt;
    }

    Preset preset;
    for (const auto& [key, value] : root.items()) {
        if (key == "name" || key == "category") {
            if (!value.is_string()) {
                fail(error, "\"" + key + "\" must be a string");
                return std::nullopt;
            }
            (key == "name" ? preset.name : preset.category) = value.get<std::string>();
            continue;
        }
        const auto id = findParam(key);
        if (!id) {
            fail(error, "unknown parameter \"" + key + "\"");
            return std::nullopt;
        }
        const auto real = readValue(spec(*id), value);
        if (!real) {
            fail(error, "bad value for \"" + key + "\"");
            return std::nullopt;
        }
        preset.params[*id] = *real;
    }
    return preset;
}

std::string serialisePreset(const Preset& preset) {
    Json root = Json::object();
    root["name"] = preset.name;
    root["category"] = preset.category;
    for (std::size_t i = 0; i < kParams.size(); ++i)
        root[std::string(kParams[i].key)] = writeValue(kParams[i], preset.params.values[i]);
    return root.dump(2) + "\n";
}

}
