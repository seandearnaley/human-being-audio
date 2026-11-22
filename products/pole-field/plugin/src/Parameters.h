#pragma once

#include "pf/params.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <string_view>

namespace pf {

juce::String toJuce(std::string_view utf8);
juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout();

class ParameterValues {
public:
    explicit ParameterValues(juce::AudioProcessorValueTreeState& state);
    Params snapshot() const noexcept;

private:
    std::array<std::atomic<float>*, kParamCount> values_ {};
};

}
