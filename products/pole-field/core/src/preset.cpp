#include "pf/preset.h"

#include <hba/core/preset_json.h>

#include <algorithm>
#include <string_view>

namespace pf {

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

std::optional<ImprintData> readImprint(const Json& value) {
    if (!value.is_object()) return std::nullopt;
    ImprintData out;
    if (value.contains("active") && value["active"].is_boolean()) out.active = value["active"].get<bool>();
    if (value.contains("makeupDb") && value["makeupDb"].is_number())
        out.corner.makeupDb = std::clamp(value["makeupDb"].get<float>(), -24.0f, 12.0f);
    if (!value.contains("stages") || !value["stages"].is_array() || value["stages"].size() != kNumStages)
        return out.active ? std::nullopt : std::optional<ImprintData>(out);
    for (std::size_t i = 0; i < kNumStages; ++i) {
        const auto& s = value["stages"][i];
        if (!s.is_array() || s.size() != 4) return std::nullopt;
        for (std::size_t k = 0; k < 4; ++k) if (!s[k].is_number()) return std::nullopt;
        out.corner.stages[i] = {
            std::clamp(s[0].get<float>(), 30.0f, 18500.0f),
            std::clamp(s[1].get<float>(), 0.55f, 22.0f),
            std::clamp(s[2].get<float>(), 30.0f, 19500.0f),
            std::clamp(s[3].get<float>(), 0.55f, 18.0f),
        };
    }
    return out;
}

Json writeImprint(const ImprintData& imprint) {
    Json obj = Json::object();
    obj["active"] = imprint.active;
    obj["makeupDb"] = imprint.corner.makeupDb;
    Json stages = Json::array();
    for (const auto& s : imprint.corner.stages)
        stages.push_back(Json::array({ s.poleHz, s.poleQ, s.zeroHz, s.zeroQ }));
    obj["stages"] = std::move(stages);
    return obj;
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
        if (key == "imprint") {
            const auto imp = readImprint(value);
            if (!imp) {
                fail(error, "bad imprint object");
                return std::nullopt;
            }
            preset.imprint = *imp;
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
    root["name"] = preset.name;
    root["category"] = preset.category;
    for (std::size_t i = 0; i < kParams.size(); ++i)
        root[std::string(kParams[i].key)] = writeValue(kParams[i], preset.params.values[i]);
    for (int lane = 0; lane < kLaneCount; ++lane)
        if (preset.params.choice(laneParam(lane, LaneField::shape)) == static_cast<int>(LaneShape::custom))
            root[curveKey(lane)] = writeCurve(preset.curves[static_cast<std::size_t>(lane)]);
    if (preset.imprint.active) root["imprint"] = writeImprint(preset.imprint);
    return root.dump(2) + "\n";
}

std::optional<Curve> parseCurve(std::string_view json) {
    const Json value = Json::parse(json, nullptr, false);
    return value.is_discarded() ? std::nullopt : readCurve(value);
}

std::string serialiseCurve(const Curve& curve) { return writeCurve(curve).dump(); }

std::optional<ImprintData> parseImprint(std::string_view json) {
    const Json value = Json::parse(json, nullptr, false);
    return value.is_discarded() ? std::nullopt : readImprint(value);
}

std::string serialiseImprint(const ImprintData& imprint) { return writeImprint(imprint).dump(); }

}
