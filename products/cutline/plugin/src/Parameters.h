// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "cl/params.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <string_view>

namespace cl {

juce::String toJuce(std::string_view utf8);
juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout();

class ParameterValues {
  public:
    explicit ParameterValues(juce::AudioProcessorValueTreeState& state);
    Params snapshot() const noexcept;

  private:
    std::array<std::atomic<float>*, kParamCount> values_{};
};

} // namespace cl
