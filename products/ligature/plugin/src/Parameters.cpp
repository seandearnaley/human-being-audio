// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Parameters.h"

#include <cmath>
#include <string>

namespace lg {

juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout() {
    return hba::host::makeParameterLayout(
        kParams, [](const ParamSpec& spec) { return juce::NormalisableRange<float>(spec.min, spec.max); },
        [](const ParamSpec& spec, float real) -> std::string {
            if (spec.key == "level")
                return std::to_string(static_cast<int>(std::lround(real * 100.0f))) + "%";
            return std::to_string(real);
        });
}

} // namespace lg
