// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "StateStore.h"
#include "pv/preset.h"
#include "pv/ui/model.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <string>
#include <vector>

namespace pv {

class Presets {
  public:
    Presets(juce::AudioProcessorValueTreeState& state, StateStore& stateStore);
    ~Presets();

    static juce::File userDirectory();

    std::vector<ui::PresetEntry> entries() const;
    int count() const noexcept { return static_cast<int>(presets_.size()); }
    int currentIndex() const noexcept { return currentIndex_; }
    const std::string& currentName() const noexcept { return currentName_; }

    void setCurrentName(std::string name);
    void load(int index);
    void step(int delta);
    void saveUserPresetAsync();
    void reloadFromState();

  private:
    void scanPresets();
    void applyPreset(const Preset& data, int index, bool notifyHost = true);
    void syncIndexFromName();

    juce::AudioProcessorValueTreeState& state_;
    StateStore& stateStore_;
    std::vector<Preset> presets_;
    int currentIndex_ = 0;
    std::string currentName_ = "Iridescent F Min9";
    std::unique_ptr<juce::FileChooser> chooser_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

} // namespace pv
