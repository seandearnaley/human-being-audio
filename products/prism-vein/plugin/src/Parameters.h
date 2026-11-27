// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "pv/params.h"

#include <hba/host/parameters.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace pv {

juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout();

class ParameterValues {
  public:
    explicit ParameterValues(const juce::AudioProcessorValueTreeState& state);
    Params snapshot() const noexcept;

  private:
    hba::host::ParameterSnapshotter<Params, decltype(kParamSpecs)> snap_;
};

} // namespace pv
