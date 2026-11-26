// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Parameters.h"

#include <algorithm>
#include <cmath>

namespace ln {

juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout() {
    return hba::host::makeParameterLayout(
        kParams,
        [](const ParamSpec& s) {
            return juce::NormalisableRange<float>(
                s.min, s.max, [s](float, float, float norm) { return fromNormalised(s, std::clamp(norm, 0.0f, 1.0f)); },
                [s](float, float, float val) { return toNormalised(s, val); });
        },
        [](const ParamSpec& s, float v) { return formatValue(s, v); });
}

ParameterValues::ParameterValues(const juce::AudioProcessorValueTreeState& state) : snap_(state, kParams) {}

Params ParameterValues::snapshot() const noexcept { return snap_.snapshot(); }

} // namespace ln
