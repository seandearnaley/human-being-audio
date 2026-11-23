// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Presets.h"

#include "Parameters.h"
#include <CutlinePresetsData.h>

#include <algorithm>
#include <array>
#include <string_view>
#include <tuple>

namespace cl {

namespace {
const juce::Identifier kPresetName{"presetName"};
constexpr const char* kUserCategory = "User";
constexpr const char* kDefaultPreset = "Neuro Whip";
} // namespace

juce::File Presets::userDirectory() {
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory)
        .getChildFile("Cutline")
        .getChildFile("Presets");
}

Presets::Presets(juce::AudioProcessorValueTreeState& state, CurveStore& curves, OverrideStore& overrides)
    : state_(state), curves_(curves), overrides_(overrides) {
    scan();
}

void Presets::scan() {
    presets_.clear();

    for (int i = 0; i < CutlinePresetsData::namedResourceListSize; ++i) {
        int size = 0;
        const char* data = CutlinePresetsData::getNamedResource(CutlinePresetsData::namedResourceList[i], size);
        if (auto preset = parsePreset(std::string_view(data, static_cast<std::size_t>(size))))
            presets_.push_back(std::move(*preset));
        else
            jassertfalse;
    }

    const auto rank = [](const std::string& category) {
        static const std::array<std::string_view, 5> order{"Neuro", "Breaks", "Ratchet", "Halftime", "MPE"};
        return std::find(order.begin(), order.end(), category) - order.begin();
    };
    std::stable_sort(presets_.begin(), presets_.end(), [&](const Preset& a, const Preset& b) {
        return std::make_tuple(rank(a.category), std::string_view(a.name)) <
               std::make_tuple(rank(b.category), std::string_view(b.name));
    });

    std::vector<Preset> user;
    for (const auto& file : userDirectory().findChildFiles(juce::File::findFiles, false, "*.json")) {
        if (auto preset = parsePreset(file.loadFileAsString().toStdString())) {
            if (preset->name.empty())
                preset->name = file.getFileNameWithoutExtension().toStdString();
            preset->category = kUserCategory;
            user.push_back(std::move(*preset));
        }
    }
    std::sort(user.begin(), user.end(), [](const Preset& a, const Preset& b) { return a.name < b.name; });
    presets_.insert(presets_.end(), std::make_move_iterator(user.begin()), std::make_move_iterator(user.end()));
}

std::string Presets::currentName() const {
    return state_.state.getProperty(kPresetName, kDefaultPreset).toString().toStdString();
}

void Presets::setCurrentName(const std::string& name) { state_.state.setProperty(kPresetName, toJuce(name), nullptr); }

int Presets::currentIndex() const {
    const auto name = currentName();
    const auto found = std::find_if(presets_.begin(), presets_.end(), [&](const Preset& p) { return p.name == name; });
    return found == presets_.end() ? -1 : static_cast<int>(found - presets_.begin());
}

void Presets::load(int index) {
    if (index < 0 || index >= static_cast<int>(presets_.size()))
        return;
    const auto& preset = presets_[static_cast<std::size_t>(index)];
    for (std::size_t i = 0; i < kParams.size(); ++i) {
        if (auto* parameter = state_.getParameter(toJuce(kParams[i].key)))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(preset.params.values[i]));
    }
    curves_.setAll(preset.curves);
    overrides_.set(preset.overrides);
    setCurrentName(preset.name);
}

void Presets::step(int delta) {
    if (presets_.empty())
        return;
    const int count = static_cast<int>(presets_.size());
    const int current = currentIndex();
    load(current < 0 ? 0 : ((current + delta) % count + count) % count);
}

bool Presets::save(const juce::String& name) {
    const auto trimmed = name.trim();
    if (trimmed.isEmpty())
        return false;

    Preset preset;
    preset.name = trimmed.toStdString();
    preset.category = kUserCategory;
    for (std::size_t i = 0; i < kParams.size(); ++i) {
        if (auto* parameter = state_.getParameter(toJuce(kParams[i].key)))
            preset.params.values[i] = parameter->convertFrom0to1(parameter->getValue());
    }
    preset.curves = curves_.all();
    preset.overrides = overrides_.get();

    const auto directory = userDirectory();
    if (!directory.createDirectory())
        return false;
    const auto file = directory.getChildFile(juce::File::createLegalFileName(trimmed) + ".json");
    if (!file.replaceWithText(toJuce(serialisePreset(preset))))
        return false;

    scan();
    setCurrentName(preset.name);
    return true;
}

} // namespace cl
