#include "Presets.h"

#include <BinaryData.h>
#include <hba/host/preset_bank.h>

#include <algorithm>
#include <array>
#include <string_view>

namespace tf {

namespace {
const juce::Identifier kPresetName{"presetName"};
constexpr const char* kUserCategory = "User";
// The factory preset that matches the parameter defaults.
constexpr const char* kDefaultPreset = "Coupled Reese";

std::ptrdiff_t categoryRank(const std::string& category) {
    static constexpr std::array<std::string_view, 6> kOrder{"Reese", "Neuro", "Talk", "Sub", "Motion", "Pluck & Stab"};
    return std::find(kOrder.begin(), kOrder.end(), category) - kOrder.begin();
}
} // namespace

juce::File Presets::userDirectory() { return hba::host::studioUserDirectory("Tract Form", "Presets", false); }

Presets::Presets(juce::AudioProcessorValueTreeState& state, CurveStore& curves) : state_(state), curves_(curves) {
    scan();
}

void Presets::scan() {
    presets_ =
        hba::host::scanFactoryPresets<Preset>(BinaryData::namedResourceList, nullptr, BinaryData::namedResourceListSize,
                                              BinaryData::getNamedResource, parsePreset, categoryRank, false);
    hba::host::appendUserPresets(presets_, userDirectory(), parsePreset, kUserCategory, true);
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
        // Quality is a machine setting, not part of a sound.
        if (static_cast<ParamId>(i) == ParamId::quality)
            continue;
        if (auto* parameter = state_.getParameter(toJuce(kParams[i].key)))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(preset.params.values[i]));
    }
    curves_.setAll(preset.curves);
    setCurrentName(preset.name);
}

void Presets::step(int delta) {
    const int next = hba::host::wrappedStepIndex(currentIndex(), delta, static_cast<int>(presets_.size()));
    if (next >= 0)
        load(next);
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

} // namespace tf
