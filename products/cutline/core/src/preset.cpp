// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/preset.h"

#include "cl/groove_json.h"
#include "groove_json_detail.h"

#include <hba/core/preset_json.h>

#include <string>
#include <string_view>
#include <utility>

namespace cl {

namespace {
using Json = hba::core::Json;

std::optional<float> readValue(const ParamSpec& spec, const Json& value) {
    return hba::core::readParamJson(spec, value, clampToRange);
}

Json writeValue(const ParamSpec& spec, float real) { return hba::core::writeParamJson(spec, real, clampToRange); }

std::string curveKey(int lane) { return hba::core::curveKey(lane); }
int laneOfCurveKey(const std::string& key) { return hba::core::laneOfCurveKey(key, kLaneCount); }
std::optional<Curve> readCurve(const Json& value) { return hba::core::readCurveJson(value); }
Json writeCurve(const Curve& curve) { return hba::core::writeCurveJson(curve); }

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
    const auto format = root.find("format");
    if (format == root.end() || !format->is_number_integer() || format->get<int>() < kGrooveFormat) {
        fail(error, std::string(kOldFormatError));
        return std::nullopt;
    }

    Preset preset;
    for (const auto& [key, value] : root.items()) {
        if (key == "format")
            continue;
        if (key == "name" || key == "category") {
            if (!value.is_string()) {
                fail(error, "\"" + key + "\" must be a string");
                return std::nullopt;
            }
            (key == "name" ? preset.name : preset.category) = value.get<std::string>();
            continue;
        }
        if (key == "bank") {
            const auto bank = detail::readGrooveBank(value, error);
            if (!bank)
                return std::nullopt;
            preset.bank = *bank;
            continue;
        }
        if (const int lane = laneOfCurveKey(key); lane >= 0) {
            const auto curve = readCurve(value);
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
    return preset;
}

std::string serialisePreset(const Preset& preset) {
    Json root = Json::object();
    root["format"] = kGrooveFormat;
    if (!preset.name.empty())
        root["name"] = preset.name;
    if (!preset.category.empty())
        root["category"] = preset.category;
    const Params defaults;
    for (std::size_t i = 0; i < kParams.size(); ++i) {
        if (preset.params.values[i] != defaults.values[i])
            root[std::string(kParams[i].key)] = writeValue(kParams[i], preset.params.values[i]);
    }
    const LaneCurves defaultCurves = defaultLaneCurves();
    for (int lane = 0; lane < kLaneCount; ++lane) {
        const auto idx = static_cast<std::size_t>(lane);
        if (!(preset.curves[idx] == defaultCurves[idx]))
            root[curveKey(lane)] = writeCurve(preset.curves[idx]);
    }
    root["bank"] = detail::writeGrooveBank(preset.bank);
    return root.dump(2) + "\n";
}

std::optional<Curve> parseCurve(std::string_view json) {
    const Json root = Json::parse(json, nullptr, false);
    return readCurve(root);
}

std::string serialiseCurve(const Curve& curve) { return writeCurve(curve).dump(); }

} // namespace cl
