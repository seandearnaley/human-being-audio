// Factory presets (embedded) and user presets (~/Music/Tract Form/Presets).
#pragma once

#include "CurveStore.h"
#include "Parameters.h"
#include "tf/preset.h"

#include <string>
#include <vector>

namespace tf {

class Presets {
public:
    Presets(juce::AudioProcessorValueTreeState& state, CurveStore& curves);

    const std::vector<Preset>& all() const { return presets_; }
    std::string currentName() const;
    void setCurrentName(const std::string& name);

    void load(int index);
    void step(int delta);
    // Writes the current parameters as a user preset and selects it.
    bool save(const juce::String& name);

    static juce::File userDirectory();

private:
    void scan();
    int currentIndex() const;

    juce::AudioProcessorValueTreeState& state_;
    CurveStore& curves_;
    std::vector<Preset> presets_;
};

}
