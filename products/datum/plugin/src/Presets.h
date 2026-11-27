// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "StateStore.h"
#include "dt/preset.h"
#include "dt/ui/model.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <string>
#include <vector>

namespace dt {

class Presets {
  public:
    Presets(juce::AudioProcessorValueTreeState& state, StateStore& stateStore);
    ~Presets();

    std::vector<ui::PresetInfo> entries() const;
    std::string currentName() const { return currentName_; }
    void setCurrentName(std::string name);
    int currentIndex() const { return currentIndex_; }
    int count() const { return static_cast<int>(presets_.size()); }

    void load(int index);
    void step(int delta);
    void saveUserPresetAsync();
    void reloadFromState();

    static juce::File userDirectory();

  private:
    void scanPresets();
    void applyPreset(const Preset& data, int index, bool notifyHost = true);
    void syncIndexFromName();

    juce::AudioProcessorValueTreeState& state_;
    StateStore& stateStore_;
    std::vector<Preset> presets_;
    int currentIndex_ = 0;
    std::string currentName_;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};
};

} // namespace dt
