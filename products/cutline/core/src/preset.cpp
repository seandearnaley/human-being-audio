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
        // v0.1.0 compatibility alias: "Live MIDI" -> "Chord Carve".
        if (spec.key == "inputMode" && label == "Live MIDI")
            return static_cast<float>(InputMode::chordCarve);
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

std::optional<PhraseOverrides> readOverridesJson(const Json& value) {
    if (!value.is_object())
        return std::nullopt;
    PhraseOverrides out{};
    if (value.contains("fillStart") && value["fillStart"].is_number())
        out.fillStartFrac = std::clamp(value["fillStart"].get<float>(), 0.50f, 0.90f);
    if (value.contains("turnStart") && value["turnStart"].is_number())
        out.turnStartFrac = std::clamp(value["turnStart"].get<float>(), 0.75f, 0.96f);
    if (value.contains("spans") && value["spans"].is_array()) {
        const auto& arr = value["spans"];
        for (std::size_t i = 0; i < arr.size() && i < static_cast<std::size_t>(PhraseOverrides::kMaxSpanOverrides);
             ++i) {
            const auto& item = arr[i];
            if (!item.is_object())
                continue;
            if (item.contains("locked") && item["locked"].is_boolean())
                out.spans[i].locked = item["locked"].get<bool>();
            if (item.contains("seed") && item["seed"].is_number_integer())
                out.spans[i].lockedSeed = static_cast<std::uint16_t>(std::clamp(item["seed"].get<int>(), 0, 999));
            if (item.contains("role") && item["role"].is_number_integer())
                out.spans[i].roleOverride = static_cast<std::int8_t>(std::clamp(item["role"].get<int>(), -1, 5));
        }
    }
    if (value.contains("notes") && value["notes"].is_array()) {
        int dst = 0;
        for (const auto& item : value["notes"]) {
            if (dst >= PhraseOverrides::kMaxNoteOverrides || !item.is_object())
                break;
            if (!item.contains("slot") || !item["slot"].is_number_integer())
                continue;
            NoteOverride n{};
            n.slotIndex = static_cast<std::int16_t>(std::clamp(item["slot"].get<int>(), 0, 255));
            if (item.contains("pitch") && item["pitch"].is_number_integer())
                n.pitchDelta = static_cast<std::int8_t>(std::clamp(item["pitch"].get<int>(), -24, 24));
            if (item.contains("vel") && item["vel"].is_number_integer())
                n.velocityDelta = static_cast<std::int8_t>(std::clamp(item["vel"].get<int>(), -64, 64));
            if (item.contains("muted") && item["muted"].is_boolean())
                n.muted = item["muted"].get<bool>();
            if (item.contains("flipBend") && item["flipBend"].is_boolean())
                n.flipBend = item["flipBend"].get<bool>();
            if (n.active())
                out.notes[static_cast<std::size_t>(dst++)] = n;
        }
    }
    return out;
}

Json writeOverridesJson(const PhraseOverrides& overrides) {
    Json obj = Json::object();
    if (overrides.fillStartFrac >= 0.0f)
        obj["fillStart"] = overrides.fillStartFrac;
    if (overrides.turnStartFrac >= 0.0f)
        obj["turnStart"] = overrides.turnStartFrac;
    bool anySpan = false;
    for (const auto& s : overrides.spans)
        anySpan = anySpan || s.active();
    if (anySpan) {
        Json spans = Json::array();
        for (const auto& s : overrides.spans) {
            Json so = Json::object();
            if (s.locked) {
                so["locked"] = true;
                so["seed"] = s.lockedSeed;
            }
            if (s.roleOverride >= 0)
                so["role"] = s.roleOverride;
            spans.push_back(std::move(so));
        }
        obj["spans"] = std::move(spans);
    }
    Json notes = Json::array();
    for (const auto& n : overrides.notes) {
        if (!n.active())
            continue;
        Json no = Json::object();
        no["slot"] = n.slotIndex;
        if (n.pitchDelta != 0)
            no["pitch"] = n.pitchDelta;
        if (n.velocityDelta != 0)
            no["vel"] = n.velocityDelta;
        if (n.muted)
            no["muted"] = true;
        if (n.flipBend)
            no["flipBend"] = true;
        notes.push_back(std::move(no));
    }
    if (!notes.empty())
        obj["notes"] = std::move(notes);
    return obj;
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
        if (key == "overrides") {
            const auto ov = readOverridesJson(value);
            if (!ov) {
                fail(error, "bad value for \"overrides\"");
                return std::nullopt;
            }
            preset.overrides = *ov;
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
    if (preset.overrides.hasAny())
        root["overrides"] = writeOverridesJson(preset.overrides);
    return root.dump(2) + "\n";
}

std::optional<Curve> parseCurve(std::string_view json) {
    const Json root = Json::parse(json, nullptr, false);
    return readCurve(root);
}

std::string serialiseCurve(const Curve& curve) { return writeCurve(curve).dump(); }

std::optional<PhraseOverrides> parseOverrides(std::string_view json) {
    const Json root = Json::parse(json, nullptr, false);
    return readOverridesJson(root);
}

std::string serialiseOverrides(const PhraseOverrides& overrides) { return writeOverridesJson(overrides).dump(); }

} // namespace cl
