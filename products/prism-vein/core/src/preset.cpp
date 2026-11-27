// SPDX-License-Identifier: AGPL-3.0-or-later
#include "pv/preset.h"

#include <hba/core/preset_json.h>
#include <nlohmann/json.hpp>

#include <exception>

namespace pv {

std::string serializePreset(const Preset& preset) {
    nlohmann::json j = nlohmann::json::object();
    j["name"] = preset.name;
    j["category"] = preset.category;
    j["chordMask"] = static_cast<int>(preset.chordMask & 0x0FFFu);
    for (const auto& s : kParamSpecs)
        j[std::string(s.key)] = hba::core::writeParamJson(s, preset.params[s.id], clampParam);

    nlohmann::json curves = nlohmann::json::array();
    for (int i = 0; i < kLaneCount; ++i)
        curves.push_back(hba::core::serialiseCurveJson(preset.curves[static_cast<std::size_t>(i)]));
    j["curves"] = std::move(curves);
    return j.dump(4) + "\n";
}

std::optional<Preset> parsePreset(std::string_view jsonText, std::string* errorOut) {
    try {
        const auto j = nlohmann::json::parse(jsonText);
        if (!j.is_object()) {
            if (errorOut)
                *errorOut = "preset root must be a JSON object";
            return std::nullopt;
        }
        Preset out;
        out.name = hba::core::jsonStringOr(j, "name", out.name);
        out.category = hba::core::jsonStringOr(j, "category", out.category);
        if (j.contains("chordMask") && j["chordMask"].is_number_integer()) {
            const int raw = j["chordMask"].get<int>();
            const auto masked = static_cast<std::uint16_t>(static_cast<unsigned>(raw) & 0x0FFFu);
            out.chordMask = masked != 0u ? masked : kDefaultChordMask;
        }
        for (const auto& s : kParamSpecs) {
            if (j.contains(std::string(s.key))) {
                if (const auto parsed = hba::core::readParamJson(s, j[std::string(s.key)], clampParam))
                    out.params[s.id] = *parsed;
            }
        }
        if (j.contains("curves") && j["curves"].is_array()) {
            for (std::size_t i = 0; i < std::min<std::size_t>(kLaneCount, j["curves"].size()); ++i) {
                if (!j["curves"][i].is_string())
                    continue;
                if (const auto parsed = hba::core::parseCurveJson(j["curves"][i].get<std::string>()))
                    out.curves[i] = *parsed;
            }
        }
        return out;
    } catch (const std::exception& e) {
        if (errorOut)
            *errorOut = e.what();
        return std::nullopt;
    }
}

} // namespace pv
