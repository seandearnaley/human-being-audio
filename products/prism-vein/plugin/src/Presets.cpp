// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Presets.h"

#include "PrismVeinPresetsData.h"

#include <hba/host/preset_bank.h>

#include <algorithm>
#include <array>
#include <string_view>

namespace pv {

namespace {
const juce::Identifier kPresetProp{"PrismVeinPresetName"};

int categoryOrder(const std::string& cat) {
    static constexpr std::array<std::string_view, 5> kOrder{
        "Color Bass", "Neurofunk", "Future Bass", "Carrier & Freeze", "Hyperpop & Glitch",
    };
    return static_cast<int>(std::find(kOrder.begin(), kOrder.end(), cat) - kOrder.begin());
}

std::optional<Preset> parsePresetCompat(std::string_view json) { return parsePreset(json); }
} // namespace

juce::File Presets::userDirectory() { return hba::host::studioUserDirectory("Prism Vein", "Presets"); }

Presets::Presets(juce::AudioProcessorValueTreeState& state, StateStore& stateStore)
    : state_(state), stateStore_(stateStore) {
    scanPresets();
    if (!presets_.empty())
        applyPreset(presets_[0], 0, false);
}

Presets::~Presets() { *alive_ = false; }

void Presets::scanPresets() {
    presets_ = hba::host::scanFactoryPresets<Preset>(
        PrismVeinPresetsData::namedResourceList, PrismVeinPresetsData::originalFilenames,
        PrismVeinPresetsData::namedResourceListSize, PrismVeinPresetsData::getNamedResource, parsePresetCompat,
        categoryOrder, true);
    hba::host::appendUserPresets(presets_, userDirectory(), parsePresetCompat, "User", false);
}

std::vector<ui::PresetEntry> Presets::entries() const {
    std::vector<ui::PresetEntry> out;
    out.reserve(presets_.size());
    for (const auto& p : presets_)
        out.push_back({p.category, p.name});
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
    for (std::size_t i = 0; i < kParamSpecs.size(); ++i) {
        const auto id = static_cast<ParamId>(i);
        // Never disrupt user's live demo switch or global OS quality on preset browse.
        if (id == ParamId::quality || id == ParamId::demoAudition)
            continue;
        if (auto* p = state_.getParameter(std::string(kParamSpecs[i].key))) {
            const float norm = p->convertTo0to1(data.params[id]);
            if (notifyHost)
                p->setValueNotifyingHost(norm);
            else
                p->setValue(norm);
        }
    }
    stateStore_.setAll({data.chordMask, data.curves});
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
        "Save Prism Vein Preset",
        dir.getChildFile(juce::File::createLegalFileName(currentName_.empty() ? "Custom Prism" : currentName_) +
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
                              for (std::size_t i = 0; i < kParamSpecs.size(); ++i) {
                                  if (auto* raw = state_.getRawParameterValue(std::string(kParamSpecs[i].key)))
                                      data.params[static_cast<ParamId>(i)] = raw->load(std::memory_order_relaxed);
                              }
                              const auto extra = stateStore_.all();
                              data.chordMask = extra.chordMask;
                              data.curves = extra.curves;
                              file.replaceWithText(serializePreset(data));
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

} // namespace pv
