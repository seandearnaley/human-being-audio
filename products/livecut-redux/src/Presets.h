// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <string>
#include <vector>

namespace livecut {

struct PresetEntry {
    std::string name;
    std::string category;
    std::string json;
};

// Factory + user JSON preset bank for Livecut Redux 2.0.
// Embedded factory presets live in LivecutPresetsData; user presets live in
// ~/Music/Livecut Redux/Presets/*.json.
class Presets {
  public:
    explicit Presets(juce::AudioProcessorValueTreeState& state);

    void scan();

    const std::vector<PresetEntry>& all() const { return entries_; }
    std::string currentName() const;
    void setCurrentName(const std::string& name);
    int currentIndex() const;

    void load(int index);
    void step(int delta);
    bool save(const juce::String& name);
    bool saveToFile(const juce::File& file);

    static juce::File userDirectory();

  private:
    juce::AudioProcessorValueTreeState& state_;
    std::vector<PresetEntry> entries_;
};

} // namespace livecut
