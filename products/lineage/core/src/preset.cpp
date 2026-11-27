// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ln/preset.h"

#include <hba/core/preset_json.h>

#include <algorithm>
#include <string_view>

namespace ln {

namespace {
using Json = hba::core::Json;

std::optional<float> readValue(const ParamSpec& spec, const Json& value) {
    return hba::core::readParamJson(spec, value, clampToRange);
}

Json writeValue(const ParamSpec& spec, float real) { return hba::core::writeParamJson(spec, real, clampToRange); }

void fail(std::string* error, std::string message) {
    if (error != nullptr)
        *error = std::move(message);
}
} // namespace

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
        if (key == "autoPrint") {
            if (!value.is_boolean()) {
                fail(error, "\"autoPrint\" must be a boolean");
                return std::nullopt;
            }
            preset.autoPrint = value.get<bool>();
            continue;
        }
        if (const int lane = hba::core::laneOfCurveKey(key, kLaneCount); lane >= 0) {
            const auto curve = hba::core::readCurveJson(value);
            if (!curve) {
                fail(error, "bad value for \"" + key + "\"");
                return std::nullopt;
            }
            preset.curves[static_cast<std::size_t>(lane)] = *curve;
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
    if (preset.params[ParamId::genMix] > 0.01f)
        preset.autoPrint = true;
    return preset;
}

std::string serialisePreset(const Preset& preset) {
    Json root = Json::object();
    root["name"] = preset.name;
    root["category"] = preset.category;
    if (preset.autoPrint || preset.params[ParamId::genMix] > 0.01f)
        root["autoPrint"] = true;
    for (std::size_t i = 0; i < kParams.size(); ++i)
        root[std::string(kParams[i].key)] = writeValue(kParams[i], preset.params.values[i]);
    for (int lane = 0; lane < kLaneCount; ++lane) {
        if (preset.params.choice(laneParam(lane, LaneField::shape)) == static_cast<int>(LaneShape::custom))
            root[hba::core::curveKey(lane)] = hba::core::writeCurveJson(preset.curves[static_cast<std::size_t>(lane)]);
    }
    return root.dump(2) + "\n";
}

std::optional<Curve> parseCurve(std::string_view json) { return hba::core::parseCurveJson(json); }

std::string serialiseCurve(const Curve& curve) { return hba::core::serialiseCurveJson(curve); }

} // namespace ln
