// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Presets.h"

#include "TorusGlidePresetsData.h"

#include <hba/host/preset_bank.h>

#include <algorithm>
#include <array>
#include <string_view>

namespace tg {

namespace {
const juce::Identifier kPresetProp{"TorusGlidePresetName"};

int categoryOrder(const std::string& cat) {
    static constexpr std::array<std::string_view, 3> kOrder{"Spiral", "Copedent", "Mobius"};
    return static_cast<int>(std::find(kOrder.begin(), kOrder.end(), cat) - kOrder.begin());
}
} // namespace

juce::File Presets::userDirectory() { return hba::host::studioUserDirectory("Torus Glide", "Presets"); }

Presets::Presets(juce::AudioProcessorValueTreeState& state, CurveStore& curves, MatrixStore& matrix)
    : state_(state), curves_(curves), matrix_(matrix) {
    scanPresets();
    if (!presets_.empty())
        applyPreset(presets_[0], 0, false);
}

Presets::~Presets() { *alive_ = false; }

void Presets::scanPresets() {
    presets_ = hba::host::scanFactoryPresets<PresetData>(
        TorusGlidePresetsData::namedResourceList, TorusGlidePresetsData::originalFilenames,
        TorusGlidePresetsData::namedResourceListSize, TorusGlidePresetsData::getNamedResource, parsePresetJson,
        categoryOrder, true);
    hba::host::appendUserPresets(presets_, userDirectory(), parsePresetJson, "User", false);
}

std::vector<ui::PresetEntry> Presets::entries() const {
    std::vector<ui::PresetEntry> out;
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

void Presets::applyPreset(const PresetData& data, int index, bool notifyHost) {
    for (const auto& spec : paramSpecs()) {
        if (spec.id == ParamId::quality || spec.id == ParamId::bypass)
            continue;
        if (auto* p = state_.getParameter(std::string(spec.key))) {
            const float norm = p->convertTo0to1(data.params.real(spec.id));
            if (notifyHost)
                p->setValueNotifyingHost(norm);
            else
                p->setValue(norm);
        }
    }
    curves_.setAll(data.curves);
    matrix_.set(data.matrix);
    currentIndex_ = index;
    setCurrentName(data.name);
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
        "Save Torus Glide Preset",
        dir.getChildFile(juce::File::createLegalFileName(currentName_.empty() ? "Custom Torus" : currentName_) +
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
                              PresetData data;
                              data.name = file.getFileNameWithoutExtension().toStdString();
                              data.category = "User";
                              for (const auto& spec : paramSpecs()) {
                                  if (auto* raw = state_.getRawParameterValue(std::string(spec.key)))
                                      data.params.set(spec.id, raw->load(std::memory_order_relaxed));
                              }
                              data.curves = curves_.all();
                              data.matrix = matrix_.get();
                              file.replaceWithText(serialisePresetJson(data));
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

} // namespace tg
