// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ln/params.h"

#include <hba/host/parameters.h>

namespace ln {

juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout();

class ParameterValues {
  public:
    explicit ParameterValues(const juce::AudioProcessorValueTreeState& state);
    Params snapshot() const noexcept;

  private:
    hba::host::ParameterSnapshotter<Params, std::array<ParamSpec, kParamCount>> snap_;
};

} // namespace ln
