// SPDX-License-Identifier: GPL-3.0-or-later
#include "Presets.h"

#include "Parameters.h"

#include <LivecutPresetsData.h>
#include <hba/core/preset_json.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <tuple>

namespace livecut {

namespace {
using Json = hba::core::Json;
const juce::Identifier kPresetName{"presetName"};
constexpr const char* kUserCategory = "User";
constexpr const char* kDefaultPreset = "Cut Breaks";

juce::String toJuce(std::string_view s) { return juce::String::fromUTF8(s.data(), static_cast<int>(s.size())); }

bool readPresetMeta(std::string_view text, std::string& name, std::string& category) {
    const auto parsed = Json::parse(text.begin(), text.end(), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object())
        return false;
    if (parsed.contains("name") && parsed["name"].is_string())
        name = parsed["name"].get<std::string>();
    if (parsed.contains("category") && parsed["category"].is_string())
        category = parsed["category"].get<std::string>();
    return !name.empty();
}

void applyPresetJson(juce::AudioProcessorValueTreeState& state, std::string_view text) {
    const auto parsed = Json::parse(text.begin(), text.end(), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object())
        return;

    // Reset all parameters to clean defaults first so omitted fields never carry stale state.
    for (auto* param : state.processor.getParameters()) {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
            ranged->setValueNotifyingHost(ranged->getDefaultValue());
    }

    for (auto it = parsed.begin(); it != parsed.end(); ++it) {
        const std::string key = it.key();
        if (key == "name" || key == "category")
            continue;
        auto* ranged = state.getParameter(toJuce(key));
        if (ranged == nullptr)
            continue;
        const auto& val = it.value();
        if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(ranged)) {
            if (val.is_string()) {
                const auto target = toJuce(val.get<std::string>());
                const int idx = choiceParam->choices.indexOf(target);
                if (idx >= 0)
                    choiceParam->setValueNotifyingHost(choiceParam->convertTo0to1(static_cast<float>(idx)));
            } else if (val.is_number()) {
                const float clamped =
                    std::clamp(val.get<float>(), 0.0f, static_cast<float>(choiceParam->choices.size() - 1));
                choiceParam->setValueNotifyingHost(choiceParam->convertTo0to1(clamped));
            }
        } else if (ranged->isBoolean()) {
            if (val.is_boolean())
                ranged->setValueNotifyingHost(val.get<bool>() ? 1.0f : 0.0f);
            else if (val.is_number())
                ranged->setValueNotifyingHost(val.get<float>() > 0.5f ? 1.0f : 0.0f);
        } else if (val.is_number()) {
            const float raw = ranged->getNormalisableRange().snapToLegalValue(val.get<float>());
            ranged->setValueNotifyingHost(ranged->convertTo0to1(raw));
        }
    }
}

std::string serialiseStateJson(juce::AudioProcessorValueTreeState& state, const std::string& name) {
    Json obj = Json::object();
    obj["name"] = name;
    obj["category"] = kUserCategory;
    for (auto* param : state.processor.getParameters()) {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
        if (ranged == nullptr)
            continue;
        const std::string key = ranged->getParameterID().toStdString();
        const float norm = ranged->getValue();
        const float defNorm = ranged->getDefaultValue();
        if (std::abs(norm - defNorm) < 1.0e-5f)
            continue;
        if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(ranged)) {
            const int idx = std::clamp(juce::roundToInt(choiceParam->convertFrom0to1(norm)), 0,
                                       std::max(0, choiceParam->choices.size() - 1));
            obj[key] = choiceParam->choices[idx].toStdString();
        } else if (ranged->isBoolean()) {
            obj[key] = norm > 0.5f;
        } else if (auto* intParam = dynamic_cast<juce::AudioParameterInt*>(ranged)) {
            obj[key] = juce::roundToInt(intParam->convertFrom0to1(norm));
        } else {
            obj[key] = ranged->convertFrom0to1(norm);
        }
    }
    return obj.dump(4) + "\n";
}
} // namespace

juce::File Presets::userDirectory() {
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory)
        .getChildFile("Livecut Redux")
        .getChildFile("Presets");
}

Presets::Presets(juce::AudioProcessorValueTreeState& state) : state_(state) { scan(); }

void Presets::scan() {
    entries_.clear();
    for (int i = 0; i < LivecutPresetsData::namedResourceListSize; ++i) {
        int size = 0;
        const char* data = LivecutPresetsData::getNamedResource(LivecutPresetsData::namedResourceList[i], size);
        std::string_view sv(data, static_cast<std::size_t>(size));
        std::string name, category;
        if (readPresetMeta(sv, name, category))
            entries_.push_back({std::move(name), std::move(category), std::string(sv)});
        else
            jassertfalse;
    }

    const auto rank = [](const std::string& category) {
        static const std::array<std::string_view, 5> order{"Pad", "Jungle", "Neuro", "Glitch", "Resonance"};
        return std::find(order.begin(), order.end(), category) - order.begin();
    };
    std::stable_sort(entries_.begin(), entries_.end(), [&](const PresetEntry& a, const PresetEntry& b) {
        return std::make_tuple(rank(a.category), std::string_view(a.name)) <
               std::make_tuple(rank(b.category), std::string_view(b.name));
    });

    std::vector<PresetEntry> user;
    for (const auto& file : userDirectory().findChildFiles(juce::File::findFiles, false, "*.json")) {
        const std::string text = file.loadFileAsString().toStdString();
        std::string name, category;
        if (readPresetMeta(text, name, category))
            user.push_back({std::move(name), kUserCategory, text});
    }
    std::sort(user.begin(), user.end(), [](const PresetEntry& a, const PresetEntry& b) { return a.name < b.name; });
    entries_.insert(entries_.end(), std::make_move_iterator(user.begin()), std::make_move_iterator(user.end()));
}

std::string Presets::currentName() const {
    return state_.state.getProperty(kPresetName, kDefaultPreset).toString().toStdString();
}

void Presets::setCurrentName(const std::string& name) { state_.state.setProperty(kPresetName, toJuce(name), nullptr); }

int Presets::currentIndex() const {
    const auto name = currentName();
    const auto it =
        std::find_if(entries_.begin(), entries_.end(), [&](const PresetEntry& e) { return e.name == name; });
    return it == entries_.end() ? -1 : static_cast<int>(it - entries_.begin());
}

void Presets::load(int index) {
    if (index < 0 || index >= static_cast<int>(entries_.size()))
        return;
    const auto& entry = entries_[static_cast<std::size_t>(index)];
    applyPresetJson(state_, entry.json);
    setCurrentName(entry.name);
}

void Presets::step(int delta) {
    if (entries_.empty())
        return;
    const int count = static_cast<int>(entries_.size());
    const int cur = currentIndex();
    load(cur < 0 ? 0 : ((cur + delta) % count + count) % count);
}

bool Presets::save(const juce::String& name) {
    const auto trimmed = name.trim();
    if (trimmed.isEmpty())
        return false;
    const auto dir = userDirectory();
    if (!dir.createDirectory())
        return false;
    const auto file = dir.getChildFile(juce::File::createLegalFileName(trimmed) + ".json");
    if (!file.replaceWithText(toJuce(serialiseStateJson(state_, trimmed.toStdString()))))
        return false;
    scan();
    setCurrentName(trimmed.toStdString());
    return true;
}

} // namespace livecut
