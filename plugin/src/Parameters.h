// Host parameters, generated from the core table.
#pragma once

#include "tf/params.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

namespace tf {

juce::String toJuce(std::string_view utf8);

juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout();

// Raw parameter values in table order, for reading on the audio thread.
class ParameterValues {
public:
    explicit ParameterValues(juce::AudioProcessorValueTreeState& state);
    Params snapshot() const noexcept;

private:
    std::array<std::atomic<float>*, kParamCount> values_ {};
};

}
