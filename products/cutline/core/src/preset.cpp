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
    return hba::core::readParamJson(spec, value, clampToRange);
}

Json writeValue(const ParamSpec& spec, float real) { return hba::core::writeParamJson(spec, real, clampToRange); }

std::string curveKey(int lane) { return hba::core::curveKey(lane); }
int laneOfCurveKey(const std::string& key) { return hba::core::laneOfCurveKey(key, kLaneCount); }
std::optional<Curve> readCurve(const Json& value) { return hba::core::readCurveJson(value); }
Json writeCurve(const Curve& curve) { return hba::core::writeCurveJson(curve); }

StepEdit readStepJson(const Json& item) {
    StepEdit st{};
    if (!item.is_object())
        return st;
    if (item.contains("s") && item["s"].is_number_integer())
        st.state = static_cast<StepState>(std::clamp(item["s"].get<int>(), -1, 1));
    if (item.contains("v") && item["v"].is_number_integer())
        st.velOffset = static_cast<std::int8_t>(std::clamp(item["v"].get<int>(), -64, 64));
    if (item.contains("r") && item["r"].is_number_integer()) {
        const int r = item["r"].get<int>();
        if (r == 2 || r == 3 || r == 4)
            st.ratchet = static_cast<std::uint8_t>(r);
    }
    if (item.contains("n") && item["n"].is_number_integer())
        st.microNudge = static_cast<std::int8_t>(std::clamp(item["n"].get<int>(), -24, 24));
    if (item.contains("f") && item["f"].is_boolean())
        st.flam = item["f"].get<bool>();
    return st;
}

Json writeStepJson(int idx, const StepEdit& st) {
    Json o = Json::object();
    o["i"] = idx;
    if (st.state != StepState::autoGen)
        o["s"] = static_cast<int>(st.state);
    if (st.velOffset != 0)
        o["v"] = static_cast<int>(st.velOffset);
    if (st.ratchet != 0)
        o["r"] = static_cast<int>(st.ratchet);
    if (st.microNudge != 0)
        o["n"] = static_cast<int>(st.microNudge);
    if (st.flam)
        o["f"] = true;
    return o;
}

std::optional<PatternBank> readPatternBankJson(const Json& value) {
    if (!value.is_object())
        return std::nullopt;
    PatternBank out{};
    if (value.contains("slicerLength") && value["slicerLength"].is_number_integer())
        out.slicerLength = static_cast<std::uint8_t>(std::clamp(value["slicerLength"].get<int>(), 1, kMaxLaneSteps));
    if (value.contains("slicerSteps") && value["slicerSteps"].is_array()) {
        for (const auto& item : value["slicerSteps"]) {
            if (!item.is_object() || !item.contains("i") || !item["i"].is_number_integer())
                continue;
            const int idx = item["i"].get<int>();
            if (idx >= 0 && idx < kMaxLaneSteps)
                out.slicerSteps[static_cast<std::size_t>(idx)] = readStepJson(item);
        }
    }
    if (value.contains("scenes") && value["scenes"].is_array()) {
        const auto& arr = value["scenes"];
        for (std::size_t si = 0; si < arr.size() && si < static_cast<std::size_t>(kSceneCount); ++si) {
            const auto& so = arr[si];
            if (!so.is_object())
                continue;
            auto& sc = out.scenes[si];
            if (so.contains("custom") && so["custom"].is_boolean())
                sc.customized = so["custom"].get<bool>();
            if (so.contains("style") && so["style"].is_number_integer())
                sc.styleOverride =
                    static_cast<std::int8_t>(std::clamp(so["style"].get<int>(), -1, kDrumStyleCount - 1));
            if (so.contains("fill") && so["fill"].is_number_integer())
                sc.fillTypeOverride = static_cast<std::int8_t>(std::clamp(so["fill"].get<int>(), -1, 5));
            if (so.contains("varOff") && so["varOff"].is_number_integer())
                sc.variationOffset = static_cast<std::int8_t>(std::clamp(so["varOff"].get<int>(), -32, 32));
            if (so.contains("compBias") && so["compBias"].is_number())
                sc.complexityBias = std::clamp(so["compBias"].get<float>(), -1.0f, 1.0f);
            if (so.contains("chopBias") && so["chopBias"].is_number())
                sc.chopBias = std::clamp(so["chopBias"].get<float>(), -1.0f, 1.0f);
            if (so.contains("ghostBias") && so["ghostBias"].is_number())
                sc.ghostBias = std::clamp(so["ghostBias"].get<float>(), -1.0f, 1.0f);
            if (so.contains("lanes") && so["lanes"].is_array()) {
                const auto& larr = so["lanes"];
                for (std::size_t li = 0; li < larr.size() && li < static_cast<std::size_t>(kDrumLaneCount); ++li) {
                    const auto& lo = larr[li];
                    if (!lo.is_object())
                        continue;
                    auto& lane = sc.lanes[li];
                    if (lo.contains("len") && lo["len"].is_number_integer())
                        lane.stepLength = static_cast<std::uint8_t>(std::clamp(lo["len"].get<int>(), 0, kMaxLaneSteps));
                    if (lo.contains("note") && lo["note"].is_number_integer())
                        lane.midiNote = static_cast<std::uint8_t>(std::clamp(lo["note"].get<int>(), 0, 127));
                    if (lo.contains("mute") && lo["mute"].is_boolean())
                        lane.muted = lo["mute"].get<bool>();
                    if (lo.contains("solo") && lo["solo"].is_boolean())
                        lane.solo = lo["solo"].get<bool>();
                    if (lo.contains("lock") && lo["lock"].is_boolean())
                        lane.locked = lo["lock"].get<bool>();
                    if (lo.contains("euclid") && lo["euclid"].is_number_integer())
                        lane.euclidHits =
                            static_cast<std::uint8_t>(std::clamp(lo["euclid"].get<int>(), 0, kMaxLaneSteps));
                    if (lo.contains("rot") && lo["rot"].is_number_integer())
                        lane.euclidRot =
                            static_cast<std::uint8_t>(std::clamp(lo["rot"].get<int>(), 0, kMaxLaneSteps - 1));
                    if (lo.contains("lockVar") && lo["lockVar"].is_number_integer())
                        lane.lockedVar = static_cast<std::uint8_t>(std::clamp(lo["lockVar"].get<int>(), 0, 64));
                    if (lo.contains("steps") && lo["steps"].is_array()) {
                        for (const auto& item : lo["steps"]) {
                            if (!item.is_object() || !item.contains("i") || !item["i"].is_number_integer())
                                continue;
                            const int idx = item["i"].get<int>();
                            if (idx >= 0 && idx < kMaxLaneSteps)
                                lane.steps[static_cast<std::size_t>(idx)] = readStepJson(item);
                        }
                    }
                }
            }
        }
    }
    return out;
}

Json writePatternBankJson(const PatternBank& bank) {
    Json obj = Json::object();
    if (bank.slicerLength != 16)
        obj["slicerLength"] = static_cast<int>(bank.slicerLength);
    Json slSteps = Json::array();
    for (int i = 0; i < kMaxLaneSteps; ++i) {
        if (bank.slicerSteps[static_cast<std::size_t>(i)].hasAny())
            slSteps.push_back(writeStepJson(i, bank.slicerSteps[static_cast<std::size_t>(i)]));
    }
    if (!slSteps.empty())
        obj["slicerSteps"] = std::move(slSteps);

    bool anyScene = false;
    for (const auto& sc : bank.scenes)
        anyScene = anyScene || sc.hasAny();
    if (anyScene) {
        Json scenes = Json::array();
        for (const auto& sc : bank.scenes) {
            Json so = Json::object();
            if (sc.customized)
                so["custom"] = true;
            if (sc.styleOverride >= 0)
                so["style"] = static_cast<int>(sc.styleOverride);
            if (sc.fillTypeOverride >= 0)
                so["fill"] = static_cast<int>(sc.fillTypeOverride);
            if (sc.variationOffset != 0)
                so["varOff"] = static_cast<int>(sc.variationOffset);
            if (std::abs(sc.complexityBias) > 1e-4f)
                so["compBias"] = sc.complexityBias;
            if (std::abs(sc.chopBias) > 1e-4f)
                so["chopBias"] = sc.chopBias;
            if (std::abs(sc.ghostBias) > 1e-4f)
                so["ghostBias"] = sc.ghostBias;

            bool anyLane = false;
            for (const auto& l : sc.lanes)
                anyLane = anyLane || l.hasAny();
            if (anyLane) {
                Json lanes = Json::array();
                for (const auto& l : sc.lanes) {
                    Json lo = Json::object();
                    if (l.stepLength != 0)
                        lo["len"] = static_cast<int>(l.stepLength);
                    if (l.midiNote != 0)
                        lo["note"] = static_cast<int>(l.midiNote);
                    if (l.muted)
                        lo["mute"] = true;
                    if (l.solo)
                        lo["solo"] = true;
                    if (l.locked)
                        lo["lock"] = true;
                    if (l.euclidHits != 0)
                        lo["euclid"] = static_cast<int>(l.euclidHits);
                    if (l.euclidRot != 0)
                        lo["rot"] = static_cast<int>(l.euclidRot);
                    if (l.lockedVar != 0)
                        lo["lockVar"] = static_cast<int>(l.lockedVar);
                    Json steps = Json::array();
                    for (int i = 0; i < kMaxLaneSteps; ++i) {
                        if (l.steps[static_cast<std::size_t>(i)].hasAny())
                            steps.push_back(writeStepJson(i, l.steps[static_cast<std::size_t>(i)]));
                    }
                    if (!steps.empty())
                        lo["steps"] = std::move(steps);
                    lanes.push_back(std::move(lo));
                }
                so["lanes"] = std::move(lanes);
            }
            scenes.push_back(std::move(so));
        }
        obj["scenes"] = std::move(scenes);
    }
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
        if (key == "bank") {
            const auto bank = readPatternBankJson(value);
            if (!bank) {
                fail(error, "bad value for \"bank\"");
                return std::nullopt;
            }
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
    if (preset.bank.hasAny())
        root["bank"] = writePatternBankJson(preset.bank);
    return root.dump(2) + "\n";
}

std::optional<Curve> parseCurve(std::string_view json) {
    const Json root = Json::parse(json, nullptr, false);
    return readCurve(root);
}

std::string serialiseCurve(const Curve& curve) { return writeCurve(curve).dump(); }

std::optional<PatternBank> parsePatternBank(std::string_view json) {
    const Json root = Json::parse(json, nullptr, false);
    return readPatternBankJson(root);
}

std::string serialisePatternBank(const PatternBank& bank) { return writePatternBankJson(bank).dump(); }

} // namespace cl
