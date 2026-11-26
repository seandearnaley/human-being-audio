// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Parameters.h"

namespace sb {

juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout() {
    return hba::host::makeParameterLayout(
        paramSpecs(), [](const ParamSpec& s) { return juce::NormalisableRange<float>(s.low, s.high, 0.0f, s.skew); },
        [](const ParamSpec& s, float v) { return formatValue(s, v); });
}

ParameterValues::ParameterValues(const juce::AudioProcessorValueTreeState& state) : snap_(state, paramSpecs()) {}

Params ParameterValues::snapshot() const noexcept { return snap_.snapshot(); }

} // namespace sb
