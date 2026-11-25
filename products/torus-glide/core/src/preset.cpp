// SPDX-License-Identifier: AGPL-3.0-or-later
#include "tg/preset.h"

#include <hba/core/preset_json.h>

#include <algorithm>

namespace tg {

namespace {
hba::core::Json writeMatrixJsonValue(const SceneMatrix& m) {
    hba::core::Json scenes = hba::core::Json::array();
    for (int s = 0; s < kSceneCount; ++s) {
        hba::core::Json row = hba::core::Json::array();
        for (int v = 0; v < kVoiceCount; ++v)
            row.push_back(m.offsets[static_cast<std::size_t>(s)][static_cast<std::size_t>(v)]);
        scenes.push_back(std::move(row));
    }
    return hba::core::Json{{"custom", m.isCustom}, {"scenes", std::move(scenes)}};
}

std::optional<SceneMatrix> readMatrixJsonValue(const hba::core::Json& value) {
    if (!value.is_object() || !value.contains("scenes") || !value["scenes"].is_array())
        return std::nullopt;
    const auto& scenes = value["scenes"];
    if (scenes.size() != static_cast<std::size_t>(kSceneCount))
        return std::nullopt;
    SceneMatrix m;
    m.isCustom = hba::core::jsonBoolOr(value, "custom", true);
    for (std::size_t s = 0; s < static_cast<std::size_t>(kSceneCount); ++s) {
        if (!scenes[s].is_array() || scenes[s].size() != static_cast<std::size_t>(kVoiceCount))
            return std::nullopt;
        for (std::size_t v = 0; v < static_cast<std::size_t>(kVoiceCount); ++v) {
            if (!scenes[s][v].is_number())
                return std::nullopt;
            m.offsets[s][v] = std::clamp(scenes[s][v].get<float>(), -24.0f, 48.0f);
        }
    }
    return m;
}
} // namespace

std::optional<PresetData> parsePresetJson(std::string_view json) {
    const auto root = hba::core::Json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object())
        return std::nullopt;

    PresetData data;
    data.name = hba::core::jsonStringOr(root, "name", {});
    data.category = hba::core::jsonStringOr(root, "category", "General");

    const auto& paramsObj = root.contains("params") && root["params"].is_object() ? root["params"] : root;
    for (const auto& [key, val] : paramsObj.items()) {
        if (const auto* spec = findParam(key)) {
            if (const auto parsed = hba::core::readParamJson(*spec, val, clampValue))
                data.params.set(spec->id, *parsed);
        } else if (const int lane = hba::core::laneOfCurveKey(key, kLaneCount); lane >= 0) {
            if (const auto curve = hba::core::readCurveJson(val))
                data.curves[static_cast<std::size_t>(lane)] = *curve;
        }
    }
    // Also read top-level curve keys if params were nested.
    for (int lane = 0; lane < kLaneCount; ++lane) {
        const auto ck = hba::core::curveKey(lane);
        if (root.contains(ck)) {
            if (const auto curve = hba::core::readCurveJson(root[ck]))
                data.curves[static_cast<std::size_t>(lane)] = *curve;
        }
    }

    if (root.contains("matrix")) {
        if (const auto parsed = readMatrixJsonValue(root["matrix"]))
            data.matrix = *parsed;
        else
            data.matrix = matrixForPack(data.params.choice(ParamId::voicingPack));
    } else {
        data.matrix = matrixForPack(data.params.choice(ParamId::voicingPack));
    }

    return data;
}

std::string serialisePresetJson(const PresetData& preset) {
    hba::core::Json root = hba::core::Json::object();
    root["format"] = "torus-glide-preset";
    root["version"] = 1;
    root["name"] = preset.name;
    root["category"] = preset.category;

    hba::core::Json params = hba::core::Json::object();
    for (const auto& spec : paramSpecs()) {
        if (spec.id == ParamId::quality || spec.id == ParamId::bypass)
            continue;
        params[std::string(spec.key)] = hba::core::writeParamJson(spec, preset.params.real(spec.id), clampValue);
    }
    root["params"] = std::move(params);
    for (int lane = 0; lane < kLaneCount; ++lane)
        root[hba::core::curveKey(lane)] = hba::core::writeCurveJson(preset.curves[static_cast<std::size_t>(lane)]);
    if (preset.matrix.isCustom)
        root["matrix"] = writeMatrixJsonValue(preset.matrix);

    return root.dump(2) + "\n";
}

std::optional<SceneMatrix> parseMatrixJson(std::string_view json) {
    const auto root = hba::core::Json::parse(json, nullptr, false);
    if (root.is_discarded())
        return std::nullopt;
    return readMatrixJsonValue(root);
}

std::string serialiseMatrixJson(const SceneMatrix& matrix) { return writeMatrixJsonValue(matrix).dump(); }

} // namespace tg
