// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/preset.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <string_view>

namespace cl {

namespace {
using Json = nlohmann::ordered_json;

std::optional<float> readValue(const ParamSpec& spec, const Json& value) {
    if (value.is_boolean() && spec.kind == ParamKind::toggle)
        return value.get<bool>() ? 1.0f : 0.0f;
    if (value.is_string() && spec.kind != ParamKind::continuous) {
        const auto label = value.get<std::string>();
        const auto found = std::find(spec.choices.begin(), spec.choices.end(), label);
        if (found == spec.choices.end())
            return std::nullopt;
        return static_cast<float>(found - spec.choices.begin());
    }
    if (value.is_number())
        return clampToRange(spec, value.get<float>());
    return std::nullopt;
}

Json writeValue(const ParamSpec& spec, float real) {
    switch (spec.kind) {
    case ParamKind::toggle:
        return real >= 0.5f;
    case ParamKind::choice:
        return std::string(spec.choices[static_cast<std::size_t>(clampToRange(spec, real))]);
    case ParamKind::continuous:
        break;
    }
    return real;
}

constexpr std::array<std::string_view, 3> kSegmentNames{"line", "smooth", "hold"};

std::string curveKey(int lane) { return "lane" + std::to_string(lane + 1) + "Curve"; }

int laneOfCurveKey(const std::string& key) {
    for (int lane = 0; lane < kLaneCount; ++lane) {
        if (key == curveKey(lane))
            return lane;
    }
    return -1;
}

std::optional<Curve> readCurve(const Json& value) {
    if (!value.is_array() || value.empty() || value.size() > static_cast<std::size_t>(kMaxCurvePoints))
        return std::nullopt;
    Curve curve;
    for (const auto& item : value) {
        if (!item.is_array() || item.size() != 4 || !item[0].is_number() || !item[1].is_number() ||
            !item[2].is_number() || !item[3].is_string())
            return std::nullopt;
        const auto name = item[3].get<std::string>();
        const auto segment = std::find(kSegmentNames.begin(), kSegmentNames.end(), name);
        if (segment == kSegmentNames.end())
            return std::nullopt;
        CurvePoint point;
        point.x = item[0].get<float>();
        point.y = item[1].get<float>();
        point.tension = std::clamp(item[2].get<float>(), -1.0f, 1.0f);
        point.segment = static_cast<Segment>(segment - kSegmentNames.begin());
        addPoint(curve, point);
    }
    return curve;
}

Json writeCurve(const Curve& curve) {
    Json points = Json::array();
    for (int i = 0; i < curve.count; ++i) {
        const auto& point = curve.points[static_cast<std::size_t>(i)];
        points.push_back(Json::array(
            {point.x, point.y, point.tension, std::string(kSegmentNames[static_cast<std::size_t>(point.segment)])}));
    }
    return points;
}

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
    return root.dump(2) + "\n";
}

std::optional<Curve> parseCurve(std::string_view json) {
    const Json root = Json::parse(json, nullptr, false);
    return readCurve(root);
}

std::string serialiseCurve(const Curve& curve) { return writeCurve(curve).dump(); }

} // namespace cl
