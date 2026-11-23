#include "tf/preset.h"

#include <hba/core/preset_json.h>

#include <utility>

namespace tf {

namespace {
using hba::core::Json;

void fail(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
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
        const auto real = hba::core::readParamJson(spec(*id), value, clampToRange);
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
        root[std::string(kParams[i].key)] =
            hba::core::writeParamJson(kParams[i], preset.params.values[i], clampToRange);
    // A curve is only part of the sound while its lane plays it.
    for (int lane = 0; lane < kLaneCount; ++lane)
        if (preset.params.choice(laneParam(lane, LaneField::shape)) == static_cast<int>(LaneShape::custom))
            root[hba::core::curveKey(lane)] = hba::core::writeCurveJson(preset.curves[static_cast<std::size_t>(lane)]);
    return root.dump(2) + "\n";
}

std::optional<Curve> parseCurve(std::string_view json) { return hba::core::parseCurveJson(json); }

std::string serialiseCurve(const Curve& curve) { return hba::core::serialiseCurveJson(curve); }
}
