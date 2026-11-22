// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "CurveStore.h"
#include "cl/preset.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <string>
#include <vector>

namespace cl {

class Presets {
  public:
    Presets(juce::AudioProcessorValueTreeState& state, CurveStore& curves);

    const std::vector<Preset>& all() const { return presets_; }
    std::string currentName() const;
    int currentIndex() const;

    void load(int index);
    void step(int delta);
    bool save(const juce::String& name);

  private:
    static juce::File userDirectory();
    void scan();
    void setCurrentName(const std::string& name);

    juce::AudioProcessorValueTreeState& state_;
    CurveStore& curves_;
    std::vector<Preset> presets_;
};

} // namespace cl
