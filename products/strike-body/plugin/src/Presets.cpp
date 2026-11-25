// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Presets.h"

#include "StrikeBodyPresetsData.h"

#include <algorithm>

namespace sb {

namespace {
const juce::Identifier kPresetProp{"StrikeBodyPresetName"};

int categoryOrder(const std::string& cat) {
    if (cat == "Snare")
        return 0;
    if (cat == "Kick")
        return 1;
    if (cat == "Clap & Slap")
        return 2;
    if (cat == "Metallic")
        return 3;
    if (cat == "Toms")
        return 4;
    if (cat == "FX & Hats")
        return 5;
    return 6;
}
} // namespace

juce::File Presets::userDirectory() {
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory)
        .getChildFile("Human Being Audio")
        .getChildFile("Strike Body")
        .getChildFile("Presets");
}

Presets::Presets(juce::AudioProcessorValueTreeState& state) : state_(state) {
    scanPresets();
    if (!presets_.empty())
        applyPreset(presets_[0], 0, false);
}

Presets::~Presets() { *alive_ = false; }

void Presets::scanPresets() {
    presets_.clear();
    struct NamedRaw {
        std::string origName;
        PresetData data;
    };
    std::vector<NamedRaw> factory;
    for (int i = 0; i < StrikeBodyPresetsData::namedResourceListSize; ++i) {
        int size = 0;
        const char* raw = StrikeBodyPresetsData::getNamedResource(StrikeBodyPresetsData::namedResourceList[i], size);
        if (raw == nullptr || size <= 0)
            continue;
        if (auto parsed = parsePresetJson(std::string_view(raw, static_cast<std::size_t>(size)))) {
            std::string orig = StrikeBodyPresetsData::originalFilenames[i];
            factory.push_back({std::move(orig), std::move(*parsed)});
        }
    }
    std::sort(factory.begin(), factory.end(), [](const NamedRaw& a, const NamedRaw& b) {
        const int ca = categoryOrder(a.data.category);
        const int cb = categoryOrder(b.data.category);
        return ca != cb ? ca < cb : a.origName < b.origName;
    });
    for (auto& item : factory)
        presets_.push_back(std::move(item.data));

    const auto dir = userDirectory();
    if (dir.isDirectory()) {
        const auto files = dir.findChildFiles(juce::File::findFiles, false, "*.json");
        for (const auto& f : files) {
            if (auto parsed = parsePresetJson(f.loadFileAsString().toStdString())) {
                parsed->category = "User";
                if (parsed->name.empty())
                    parsed->name = f.getFileNameWithoutExtension().toStdString();
                presets_.push_back(std::move(*parsed));
            }
        }
    }
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
    if (presets_.empty())
        return;
    const int count = static_cast<int>(presets_.size());
    const int next = ((currentIndex_ + delta) % count + count) % count;
    load(next);
}

void Presets::saveUserPresetAsync() {
    const auto dir = userDirectory();
    dir.createDirectory();
    chooser_ = std::make_unique<juce::FileChooser>(
        "Save Strike Body Preset",
        dir.getChildFile(juce::File::createLegalFileName(currentName_.empty() ? "Custom Strike" : currentName_) +
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

} // namespace sb
