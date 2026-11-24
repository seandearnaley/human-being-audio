// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "tg/params.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

namespace tg {

juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout();

class ParameterValues {
  public:
    explicit ParameterValues(const juce::AudioProcessorValueTreeState& state);
    Params snapshot() const noexcept;

  private:
    std::array<std::atomic<float>*, kParamCount> raw_{};
};

} // namespace tg
