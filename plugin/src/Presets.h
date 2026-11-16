// Factory presets (embedded) and user presets (~/Music/Tract Form/Presets).
#pragma once

#include "Parameters.h"
#include "tf/preset.h"

#include <string>
#include <vector>

namespace tf {

class Presets {
public:
    explicit Presets(juce::AudioProcessorValueTreeState& state);

    const std::vector<Preset>& all() const { return presets_; }
    std::string currentName() const;

    void load(int index);
    void step(int delta);
    // Writes the current parameters as a user preset and selects it.
    bool save(const juce::String& name);

    static juce::File userDirectory();

private:
    void scan();
    int currentIndex() const;
    void setCurrentName(const std::string& name);

    juce::AudioProcessorValueTreeState& state_;
    std::vector<Preset> presets_;
};

}
