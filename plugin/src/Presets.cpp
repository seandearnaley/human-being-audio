#include "Presets.h"

#include <BinaryData.h>

#include <algorithm>

namespace tf {

namespace {
const juce::Identifier kPresetName { "presetName" };
constexpr const char* kUserCategory = "User";
// The factory preset that matches the parameter defaults.
constexpr const char* kDefaultPreset = "Coupled Reese";
}

juce::File Presets::userDirectory() {
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("Tract Form").getChildFile("Presets");
}

Presets::Presets(juce::AudioProcessorValueTreeState& state) : state_(state) { scan(); }

void Presets::scan() {
    presets_.clear();

    for (int i = 0; i < BinaryData::namedResourceListSize; ++i) {
        int size = 0;
        const char* data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size);
        if (auto preset = parsePreset(std::string_view(data, static_cast<std::size_t>(size))))
            presets_.push_back(std::move(*preset));
        else
            jassertfalse;  // a factory preset failed to parse
    }
    // Factory presets keep their category grouping; order within a category is by name.
    std::stable_sort(presets_.begin(), presets_.end(), [](const Preset& a, const Preset& b) {
        return std::tie(a.category, a.name) < std::tie(b.category, b.name);
    });

    std::vector<Preset> user;
    for (const auto& file : userDirectory().findChildFiles(juce::File::findFiles, false, "*.json")) {
        if (auto preset = parsePreset(file.loadFileAsString().toStdString())) {
            if (preset->name.empty()) preset->name = file.getFileNameWithoutExtension().toStdString();
            preset->category = kUserCategory;
            user.push_back(std::move(*preset));
        }
    }
    std::sort(user.begin(), user.end(), [](const Preset& a, const Preset& b) { return a.name < b.name; });
    presets_.insert(presets_.end(), std::make_move_iterator(user.begin()), std::make_move_iterator(user.end()));
}

std::vector<std::string> Presets::names() const {
    std::vector<std::string> result;
    result.reserve(presets_.size());
    for (const auto& preset : presets_) result.push_back(preset.name);
    return result;
}

std::string Presets::currentName() const {
    return state_.state.getProperty(kPresetName, kDefaultPreset).toString().toStdString();
}

void Presets::setCurrentName(const std::string& name) {
    state_.state.setProperty(kPresetName, toJuce(name), nullptr);
}

int Presets::currentIndex() const {
    const auto name = currentName();
    const auto found = std::find_if(presets_.begin(), presets_.end(), [&](const Preset& p) { return p.name == name; });
    return found == presets_.end() ? -1 : static_cast<int>(found - presets_.begin());
}

void Presets::load(int index) {
    if (index < 0 || index >= static_cast<int>(presets_.size())) return;
    const auto& preset = presets_[static_cast<std::size_t>(index)];
    for (std::size_t i = 0; i < kParams.size(); ++i) {
        // Quality is a machine setting, not part of a sound.
        if (static_cast<ParamId>(i) == ParamId::quality) continue;
        if (auto* parameter = state_.getParameter(toJuce(kParams[i].key)))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(preset.params.values[i]));
    }
    setCurrentName(preset.name);
}

void Presets::step(int delta) {
    if (presets_.empty()) return;
    const int count = static_cast<int>(presets_.size());
    const int current = currentIndex();
    load(current < 0 ? 0 : ((current + delta) % count + count) % count);
}

bool Presets::save(const juce::String& name) {
    const auto trimmed = name.trim();
    if (trimmed.isEmpty()) return false;

    Preset preset;
    preset.name = trimmed.toStdString();
    preset.category = kUserCategory;
    for (std::size_t i = 0; i < kParams.size(); ++i)
        if (auto* parameter = state_.getParameter(toJuce(kParams[i].key)))
            preset.params.values[i] = parameter->convertFrom0to1(parameter->getValue());

    const auto directory = userDirectory();
    if (!directory.createDirectory()) return false;
    const auto file = directory.getChildFile(juce::File::createLegalFileName(trimmed) + ".json");
    if (!file.replaceWithText(toJuce(serialisePreset(preset)))) return false;

    scan();
    setCurrentName(preset.name);
    return true;
}

}
