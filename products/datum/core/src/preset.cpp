// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dt/preset.h"

#include <hba/core/preset_json.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <exception>

namespace dt {

namespace {
nlohmann::json encodeEnv(const hba::dsp::EnvelopeCurve& c) {
    nlohmann::json arr = nlohmann::json::array();
    const int n = std::clamp(c.count, 0, hba::dsp::kMaxEnvNodes);
    for (int i = 0; i < n; ++i) {
        const auto& nd = c.nodes[static_cast<std::size_t>(i)];
        arr.push_back({{"t", nd.t}, {"y", nd.y}, {"tension", nd.tension}, {"seg", static_cast<int>(nd.seg)}});
    }
    return arr;
}

void decodeEnv(const nlohmann::json& j, const char* key, hba::dsp::EnvelopeCurve& out) {
    if (!j.contains(key) || !j[key].is_array() || j[key].size() < 2)
        return;
    hba::dsp::EnvelopeCurve c;
    c.count = 0;
    for (const auto& item : j[key]) {
        if (c.count >= hba::dsp::kMaxEnvNodes || !item.is_object())
            break;
        hba::dsp::EnvNode nd;
        nd.t = item.value("t", 0.0f);
        nd.y = item.value("y", 0.0f);
        nd.tension = item.value("tension", 0.0f);
        const int seg = std::clamp(item.value("seg", 0), 0, 3);
        nd.seg = static_cast<hba::dsp::EnvSegment>(seg);
        c.nodes[static_cast<std::size_t>(c.count++)] = nd;
    }
    hba::dsp::sanitizeEnvelope(c);
    out = c;
}
} // namespace

std::string serializePreset(const Preset& preset) {
    nlohmann::json j = nlohmann::json::object();
    j["name"] = preset.name;
    j["category"] = preset.category;
    for (const auto& s : kParamSpecs) {
        j[std::string(s.key)] = hba::core::writeParamJson(s, preset.params[s.id], clampParam);
    }
    j["pitchCurve"] = encodeEnv(preset.pitchCurve);
    j["ampCurve"] = encodeEnv(preset.ampCurve);

    nlohmann::json slots = nlohmann::json::array();
    for (const auto& sl : preset.pattern.slots) {
        slots.push_back({{"tune", sl.tuneOffsetSt},
                         {"len", sl.lengthScale},
                         {"click", sl.clickScale},
                         {"drive", sl.driveScale},
                         {"level", sl.levelScale}});
    }
    j["slots"] = std::move(slots);

    nlohmann::json steps = nlohmann::json::array();
    for (int i = 0; i < kMaxSeqSteps; ++i) {
        const auto& st = preset.pattern.steps[static_cast<std::size_t>(i)];
        if (st.velocity > 0)
            steps.push_back({{"i", i}, {"v", st.velocity}, {"s", st.slot}, {"r", st.ratchet}});
    }
    j["steps"] = std::move(steps);
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
        for (const auto& s : kParamSpecs) {
            if (j.contains(std::string(s.key))) {
                if (const auto parsed = hba::core::readParamJson(s, j[std::string(s.key)], clampParam))
                    out.params[s.id] = *parsed;
            }
        }
        decodeEnv(j, "pitchCurve", out.pitchCurve);
        decodeEnv(j, "ampCurve", out.ampCurve);

        if (j.contains("slots") && j["slots"].is_array()) {
            for (std::size_t i = 0; i < std::min<std::size_t>(kKitSlots, j["slots"].size()); ++i) {
                const auto& item = j["slots"][i];
                if (!item.is_object())
                    continue;
                out.pattern.slots[i].tuneOffsetSt = item.value("tune", out.pattern.slots[i].tuneOffsetSt);
                out.pattern.slots[i].lengthScale = item.value("len", out.pattern.slots[i].lengthScale);
                out.pattern.slots[i].clickScale = item.value("click", out.pattern.slots[i].clickScale);
                out.pattern.slots[i].driveScale = item.value("drive", out.pattern.slots[i].driveScale);
                out.pattern.slots[i].levelScale = item.value("level", out.pattern.slots[i].levelScale);
            }
        }
        if (j.contains("steps") && j["steps"].is_array()) {
            out.pattern.steps.fill({});
            for (const auto& item : j["steps"]) {
                if (!item.is_object())
                    continue;
                const int idx = item.value("i", -1);
                if (idx < 0 || idx >= kMaxSeqSteps)
                    continue;
                auto& st = out.pattern.steps[static_cast<std::size_t>(idx)];
                st.velocity = static_cast<std::uint8_t>(std::clamp(item.value("v", 0), 0, 127));
                st.slot = static_cast<std::uint8_t>(std::clamp(item.value("s", 0), 0, kKitSlots - 1));
                st.ratchet = static_cast<std::uint8_t>(std::clamp(item.value("r", 1), 1, 3));
            }
        }
        sanitizePattern(out.pattern);
        return out;
    } catch (const std::exception& e) {
        if (errorOut)
            *errorOut = e.what();
        return std::nullopt;
    }
}

} // namespace dt
