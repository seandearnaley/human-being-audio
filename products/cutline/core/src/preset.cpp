// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/preset.h"

#include <hba/core/preset_json.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

namespace cl {

namespace {
using Json = hba::core::Json;

std::optional<float> readValue(const ParamSpec& spec, const Json& value) {
    // v0.1.0 compatibility alias: "Live MIDI" -> "Chord Carve".
    if (value.is_string() && spec.key == "inputMode" && value.get<std::string>() == "Live MIDI")
        return static_cast<float>(InputMode::chordCarve);
    return hba::core::readParamJson(spec, value, clampToRange);
}

Json writeValue(const ParamSpec& spec, float real) { return hba::core::writeParamJson(spec, real, clampToRange); }

std::string curveKey(int lane) { return hba::core::curveKey(lane); }
int laneOfCurveKey(const std::string& key) { return hba::core::laneOfCurveKey(key, kLaneCount); }
std::optional<Curve> readCurve(const Json& value) { return hba::core::readCurveJson(value); }
Json writeCurve(const Curve& curve) { return hba::core::writeCurveJson(curve); }

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
            if (item.contains("oct") && item["oct"].is_number_integer())
                out.spans[i].octaveShift = static_cast<std::int8_t>(std::clamp(item["oct"].get<int>(), -2, 2));
            if (item.contains("invert") && item["invert"].is_boolean())
                out.spans[i].invert = item["invert"].get<bool>();
            if (item.contains("time") && item["time"].is_number_integer())
                out.spans[i].timeScale = static_cast<std::int8_t>(std::clamp(item["time"].get<int>(), -1, 1));
        }
    }
    if (value.contains("chords") && value["chords"].is_array()) {
        const auto& arr = value["chords"];
        for (std::size_t i = 0; i < arr.size() && i < static_cast<std::size_t>(PhraseOverrides::kMaxChordBars); ++i) {
            if (arr[i].is_number_integer())
                out.barChordOffsets[i] = static_cast<std::int8_t>(std::clamp(arr[i].get<int>(), -7, 7));
        }
    }
    if (value.contains("notes") && value["notes"].is_array()) {
        int dst = 0;
        for (const auto& item : value["notes"]) {
            if (dst >= PhraseOverrides::kMaxNoteOverrides || !item.is_object())
                break;
            NoteOverride n{};
            if (item.contains("span") && item["span"].is_number_integer() && item.contains("tick") &&
                item["tick"].is_number_integer()) {
                n.key.spanIdx = static_cast<std::int8_t>(std::clamp(item["span"].get<int>(), 0, 31));
                n.key.stepTick = static_cast<std::int16_t>(std::clamp(item["tick"].get<int>(), 0, 4096));
                if (item.contains("voice") && item["voice"].is_number_integer())
                    n.key.voiceIdx = static_cast<std::int8_t>(std::clamp(item["voice"].get<int>(), 0, 15));
            } else if (item.contains("slot") && item["slot"].is_number_integer()) {
                n.slotIndex = static_cast<std::int16_t>(std::clamp(item["slot"].get<int>(), 0, 511));
            } else {
                continue;
            }
            if (item.contains("pitch") && item["pitch"].is_number_integer())
                n.pitchDelta = static_cast<std::int8_t>(std::clamp(item["pitch"].get<int>(), -24, 24));
            if (item.contains("vel") && item["vel"].is_number_integer())
                n.velocityDelta = static_cast<std::int8_t>(std::clamp(item["vel"].get<int>(), -64, 64));
            if (item.contains("len") && item["len"].is_number_integer())
                n.lengthSixteenths = static_cast<std::int8_t>(std::clamp(item["len"].get<int>(), -16, 32));
            if (item.contains("nudge") && item["nudge"].is_number_integer())
                n.microNudgeTicks = static_cast<std::int8_t>(std::clamp(item["nudge"].get<int>(), -24, 24));
            if (item.contains("ratchet") && item["ratchet"].is_number_integer()) {
                const int rc = item["ratchet"].get<int>();
                if (rc == 2 || rc == 3 || rc == 4 || rc == 6)
                    n.ratchetCount = static_cast<std::uint8_t>(rc);
            }
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
            if (s.octaveShift != 0)
                so["oct"] = s.octaveShift;
            if (s.invert)
                so["invert"] = true;
            if (s.timeScale != 0)
                so["time"] = s.timeScale;
            spans.push_back(std::move(so));
        }
        obj["spans"] = std::move(spans);
    }
    if (overrides.hasChordProgression()) {
        Json chords = Json::array();
        for (std::int8_t c : overrides.barChordOffsets)
            chords.push_back(static_cast<int>(c));
        obj["chords"] = std::move(chords);
    }
    Json notes = Json::array();
    for (const auto& n : overrides.notes) {
        if (!n.active())
            continue;
        Json no = Json::object();
        if (n.key.valid()) {
            no["span"] = n.key.spanIdx;
            no["tick"] = n.key.stepTick;
            if (n.key.voiceIdx != 0)
                no["voice"] = n.key.voiceIdx;
        } else {
            no["slot"] = n.slotIndex;
        }
        if (n.pitchDelta != 0)
            no["pitch"] = n.pitchDelta;
        if (n.velocityDelta != 0)
            no["vel"] = n.velocityDelta;
        if (n.lengthSixteenths != 0)
            no["len"] = n.lengthSixteenths;
        if (n.microNudgeTicks != 0)
            no["nudge"] = n.microNudgeTicks;
        if (n.ratchetCount != 0)
            no["ratchet"] = static_cast<int>(n.ratchetCount);
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
