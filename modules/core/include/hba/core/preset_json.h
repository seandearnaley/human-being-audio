// SPDX-License-Identifier: GPL-3.0-or-later
// Shared JSON codec helpers for HBA studio presets and modulation curves.
#pragma once

#include <hba/dsp/curve.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

namespace hba::core {

using Json = nlohmann::ordered_json;

inline constexpr std::array<std::string_view, 3> kSegmentNames{"line", "smooth", "hold"};

inline std::string jsonStringOr(const Json& obj, std::string_view key, std::string fallback = {}) {
    if (!obj.is_object())
        return fallback;
    const auto it = obj.find(std::string(key));
    if (it != obj.end() && it->is_string())
        return it->get<std::string>();
    return fallback;
}

inline bool jsonBoolOr(const Json& obj, std::string_view key, bool fallback = false) {
    if (!obj.is_object())
        return fallback;
    const auto it = obj.find(std::string(key));
    if (it != obj.end() && it->is_boolean())
        return it->get<bool>();
    return fallback;
}

inline std::string curveKey(int lane) { return "lane" + std::to_string(lane + 1) + "Curve"; }

inline int laneOfCurveKey(const std::string& key, int laneCount) {
    for (int lane = 0; lane < laneCount; ++lane) {
        if (key == curveKey(lane))
            return lane;
    }
    return -1;
}

inline std::optional<hba::dsp::Curve> readCurveJson(const Json& value) {
    if (!value.is_array() || value.empty() || value.size() > static_cast<std::size_t>(hba::dsp::kMaxCurvePoints))
        return std::nullopt;
    hba::dsp::Curve curve;
    for (const auto& item : value) {
        if (!item.is_array() || item.size() != 4 || !item[0].is_number() || !item[1].is_number() ||
            !item[2].is_number() || !item[3].is_string())
            return std::nullopt;
        const auto name = item[3].get<std::string>();
        const auto segment = std::find(kSegmentNames.begin(), kSegmentNames.end(), name);
        if (segment == kSegmentNames.end())
            return std::nullopt;
        hba::dsp::CurvePoint point;
        point.x = item[0].get<float>();
        point.y = item[1].get<float>();
        point.tension = std::clamp(item[2].get<float>(), -1.0f, 1.0f);
        point.segment = static_cast<hba::dsp::Segment>(segment - kSegmentNames.begin());
        hba::dsp::addPoint(curve, point);
    }
    return curve;
}

inline Json writeCurveJson(const hba::dsp::Curve& curve) {
    Json points = Json::array();
    const int n = std::clamp(curve.count, 0, hba::dsp::kMaxCurvePoints);
    for (int i = 0; i < n; ++i) {
        const auto& point = curve.points[static_cast<std::size_t>(i)];
        const auto segIdx = std::min<std::size_t>(static_cast<std::size_t>(point.segment), kSegmentNames.size() - 1u);
        points.push_back(Json::array({point.x, point.y, point.tension, std::string(kSegmentNames[segIdx])}));
    }
    return points;
}

inline std::optional<hba::dsp::Curve> parseCurveJson(std::string_view json) {
    const Json value = Json::parse(json, nullptr, false);
    return value.is_discarded() ? std::nullopt : readCurveJson(value);
}

inline std::string serialiseCurveJson(const hba::dsp::Curve& curve) { return writeCurveJson(curve).dump(); }

template <typename ParamSpecT, typename ClampFn>
inline std::optional<float> readParamJson(const ParamSpecT& spec, const Json& value, ClampFn&& clampFn) {
    using Kind = decltype(spec.kind);
    if (value.is_boolean() && spec.kind == Kind::toggle)
        return value.get<bool>() ? 1.0f : 0.0f;
    if (value.is_string() && spec.kind != Kind::continuous) {
        const auto label = value.get<std::string>();
        const auto found = std::find(spec.choices.begin(), spec.choices.end(), label);
        if (found == spec.choices.end())
            return std::nullopt;
        return static_cast<float>(found - spec.choices.begin());
    }
    if (value.is_number()) {
        const float num = value.get<float>();
        if (!std::isfinite(num))
            return std::nullopt;
        return clampFn(spec, num);
    }
    return std::nullopt;
}

template <typename ParamSpecT, typename ClampFn>
inline Json writeParamJson(const ParamSpecT& spec, float real, ClampFn&& clampFn) {
    using Kind = decltype(spec.kind);
    const float safe = std::isfinite(real) ? clampFn(spec, real) : spec.def;
    switch (spec.kind) {
    case Kind::toggle:
        return safe >= 0.5f;
    case Kind::choice: {
        if (spec.choices.empty())
            return std::string{};
        const int idx = std::clamp(static_cast<int>(std::lround(safe)), 0, static_cast<int>(spec.choices.size()) - 1);
        return std::string(spec.choices[static_cast<std::size_t>(idx)]);
    }
    case Kind::continuous:
        break;
    }
    return safe;
}

} // namespace hba::core
