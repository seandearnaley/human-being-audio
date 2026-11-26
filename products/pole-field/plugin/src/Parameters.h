#pragma once

#include "pf/params.h"

#include <hba/host/parameters.h>

#include <string_view>

namespace pf {

inline juce::String toJuce(std::string_view utf8) { return hba::host::toJuce(utf8); }
juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout();

class ParameterValues {
  public:
    explicit ParameterValues(juce::AudioProcessorValueTreeState& state);
    Params snapshot() const noexcept;

  private:
    hba::host::ParameterSnapshotter<Params, decltype(kParams)> snap_;
};

} // namespace pf
