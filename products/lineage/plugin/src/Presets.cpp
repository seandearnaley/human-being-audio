// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Presets.h"

#include "LineagePresetsData.h"

#include <hba/host/preset_bank.h>

#include <algorithm>
#include <array>
#include <string_view>

namespace ln {

namespace {
const juce::Identifier kPresetProp{"LineagePresetName"};

int categoryOrder(const std::string& cat) {
    static constexpr std::array<std::string_view, 6> kOrder{"Reese", "Strata", "Sync", "Neuro", "Halftime", "Sub"};
    return static_cast<int>(std::find(kOrder.begin(), kOrder.end(), cat) - kOrder.begin());
}

std::optional<Preset> parsePresetCompat(std::string_view json) { return parsePreset(json); }
} // namespace

juce::File Presets::userDirectory() { return hba::host::studioUserDirectory("Lineage", "Presets"); }

Presets::Presets(juce::AudioProcessorValueTreeState& state, CurveStore& curves, AutoPrintFn onAutoPrint)
    : state_(state), curves_(curves), onAutoPrint_(std::move(onAutoPrint)) {
    scanPresets();
    if (!presets_.empty())
        applyPreset(presets_[0], 0, false);
}

Presets::~Presets() { *alive_ = false; }

void Presets::scanPresets() {
    presets_ = hba::host::scanFactoryPresets<Preset>(
        LineagePresetsData::namedResourceList, LineagePresetsData::originalFilenames,
        LineagePresetsData::namedResourceListSize, LineagePresetsData::getNamedResource, parsePresetCompat,
        categoryOrder, true);
    hba::host::appendUserPresets(presets_, userDirectory(), parsePresetCompat, "User", false);
}

std::vector<ui::PresetInfo> Presets::entries() const {
    std::vector<ui::PresetInfo> out;
    out.reserve(presets_.size());
    for (const auto& p : presets_)
        out.push_back({p.name, p.category});
    return out;
}

void Presets::syncIndexFromName() {
    for (std::size_t i = 0; i < presets_.size(); ++i) {
        if (presets_[i].name == currentName_) {
            currentIndex_ = static_cast<int>(i);
            return;
        }
    }
}

void Presets::setCurrentName(std::string name) {
    currentName_ = std::move(name);
    syncIndexFromName();
    state_.state.setProperty(kPresetProp, juce::String(currentName_), nullptr);
}

void Presets::applyPreset(const Preset& data, int index, bool notifyHost) {
    for (std::size_t i = 0; i < kParams.size(); ++i) {
        const auto id = static_cast<ParamId>(i);
        if (id == ParamId::quality || id == ParamId::padTouch || id == ParamId::padHold)
            continue;
        if (auto* p = state_.getParameter(std::string(kParams[i].key))) {
            const float norm = p->convertTo0to1(data.params[id]);
            if (notifyHost)
                p->setValueNotifyingHost(norm);
            else
                p->setValue(norm);
        }
    }
    curves_.setAll(data.curves);
    currentIndex_ = index;
    setCurrentName(data.name);

    if (onAutoPrint_)
        onAutoPrint_(data.params, data.curves, data.autoPrint || data.params[ParamId::genMix] > 0.05f);
}

void Presets::load(int index) {
    if (presets_.empty())
        return;
    const int clamped = std::clamp(index, 0, static_cast<int>(presets_.size()) - 1);
    applyPreset(presets_[static_cast<std::size_t>(clamped)], clamped);
}

void Presets::step(int delta) {
    const int next = hba::host::wrappedStepIndex(currentIndex_, delta, static_cast<int>(presets_.size()));
    if (next >= 0)
        load(next);
}

void Presets::saveUserPresetAsync() {
    const auto dir = userDirectory();
    dir.createDirectory();
    chooser_ = std::make_unique<juce::FileChooser>(
        "Save Lineage Preset",
        dir.getChildFile(juce::File::createLegalFileName(currentName_.empty() ? "Custom Lineage" : currentName_) +
                         ".json"),
        "*.json");
    const auto alive = alive_;
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, alive](const juce::FileChooser& fc) {
                              if (!*alive)
                                  return;
                              const auto file = fc.getResult();
                              if (file == juce::File{})
                                  return;
                              Preset data;
                              data.name = file.getFileNameWithoutExtension().toStdString();
                              data.category = "User";
                              for (std::size_t i = 0; i < kParams.size(); ++i) {
                                  if (auto* raw = state_.getRawParameterValue(std::string(kParams[i].key)))
                                      data.params[static_cast<ParamId>(i)] = raw->load(std::memory_order_relaxed);
                              }
                              data.curves = curves_.all();
                              data.autoPrint = data.params[ParamId::genMix] > 0.05f;
                              file.replaceWithText(serialisePreset(data));
                              scanPresets();
                              setCurrentName(data.name);
                          });
}

void Presets::reloadFromState() {
    if (state_.state.hasProperty(kPresetProp))
        currentName_ = state_.state.getProperty(kPresetProp).toString().toStdString();
    else
        currentName_.clear();
    syncIndexFromName();
}

} // namespace ln
