// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "CurveStore.h"
#include "MatrixStore.h"
#include "tg/preset.h"
#include "tg/ui/model.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <string>
#include <vector>

namespace tg {

class Presets {
  public:
    Presets(juce::AudioProcessorValueTreeState& state, CurveStore& curves, MatrixStore& matrix);
    ~Presets();

    std::vector<ui::PresetEntry> entries() const;
    std::string currentName() const { return currentName_; }
    void setCurrentName(std::string name);
    int currentIndex() const { return currentIndex_; }

    void load(int index);
    void step(int delta);
    void saveUserPresetAsync();
    void reloadFromState();

    static juce::File userDirectory();

  private:
    void scanPresets();
    void applyPreset(const PresetData& data, int index, bool notifyHost = true);
    void syncIndexFromName();

    juce::AudioProcessorValueTreeState& state_;
    CurveStore& curves_;
    MatrixStore& matrix_;
    std::vector<PresetData> presets_;
    int currentIndex_ = 0;
    std::string currentName_;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};
};

} // namespace tg
