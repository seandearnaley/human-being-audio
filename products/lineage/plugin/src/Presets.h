// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "CurveStore.h"
#include "ln/preset.h"
#include "ln/ui/model.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ln {

class Presets {
  public:
    using AutoPrintFn = std::function<void(const Params&, const LaneCurves&, bool autoPrint)>;

    Presets(juce::AudioProcessorValueTreeState& state, CurveStore& curves, AutoPrintFn onAutoPrint = {});
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
    CurveStore& curves_;
    AutoPrintFn onAutoPrint_;
    std::vector<Preset> presets_;
    int currentIndex_ = 0;
    std::string currentName_;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};
};

} // namespace ln
