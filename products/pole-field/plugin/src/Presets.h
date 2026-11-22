#pragma once

#include "CurveStore.h"
#include "ImprintStore.h"
#include "pf/preset.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <string>
#include <vector>

namespace pf {

class Presets {
public:
    Presets(juce::AudioProcessorValueTreeState& state, CurveStore& curves, ImprintStore& imprint);

    void scan();
    const std::vector<Preset>& all() const noexcept { return presets_; }
    std::string currentName() const;
    int currentIndex() const;

    void load(int index);
    void step(int delta);
    bool save(const juce::String& name);

    static juce::File userDirectory();

private:
    void setCurrentName(const std::string& name);

    juce::AudioProcessorValueTreeState& state_;
    CurveStore& curves_;
    ImprintStore& imprint_;
    std::vector<Preset> presets_;
};

}
