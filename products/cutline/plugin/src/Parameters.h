// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "cl/params.h"

#include <hba/host/parameters.h>

#include <string_view>

namespace cl {

inline juce::String toJuce(std::string_view utf8) { return hba::host::toJuce(utf8); }
juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout();

class ParameterValues {
  public:
    explicit ParameterValues(juce::AudioProcessorValueTreeState& state);
    Params snapshot() const noexcept;

  private:
    hba::host::ParameterSnapshotter<Params, decltype(kParams)> snap_;
};

} // namespace cl
