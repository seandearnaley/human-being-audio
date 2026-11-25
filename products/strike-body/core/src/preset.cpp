// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sb/preset.h"

#include <hba/core/preset_json.h>

namespace sb {

std::optional<PresetData> parsePresetJson(std::string_view json) {
    const auto root = hba::core::Json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object())
        return std::nullopt;

    PresetData data;
    data.name = hba::core::jsonStringOr(root, "name", {});
    data.category = hba::core::jsonStringOr(root, "category", "Snare");

    const auto& paramsObj = root.contains("params") && root["params"].is_object() ? root["params"] : root;
    for (const auto& [key, val] : paramsObj.items()) {
        if (const auto* spec = findParam(key)) {
            if (const auto parsed = hba::core::readParamJson(*spec, val, clampValue))
                data.params.set(spec->id, *parsed);
        }
    }
    return data;
}

std::string serialisePresetJson(const PresetData& preset) {
    hba::core::Json root = hba::core::Json::object();
    root["format"] = "strike-body-preset";
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
    return root.dump(2) + "\n";
}

} // namespace sb
