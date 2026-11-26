// Host parameters, generated from the core table.
#pragma once

#include "tf/params.h"

#include <hba/host/parameters.h>

#include <string_view>

namespace tf {

inline juce::String toJuce(std::string_view utf8) { return hba::host::toJuce(utf8); }

juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout();

// Raw parameter values in table order, for reading on the audio thread.
class ParameterValues {
  public:
    explicit ParameterValues(juce::AudioProcessorValueTreeState& state);
    Params snapshot() const noexcept;

  private:
    hba::host::ParameterSnapshotter<Params, decltype(kParams)> snap_;
};

} // namespace tf
