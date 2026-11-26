#include "Parameters.h"

namespace pf {

juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout() {
    return hba::host::makeParameterLayout(
        kParams,
        [](const ParamSpec& spec) {
            return juce::NormalisableRange<float>(
                spec.min, spec.max, [&spec](float, float, float norm) { return fromNormalised(spec, norm); },
                [&spec](float, float, float real) { return toNormalised(spec, real); });
        },
        [](const ParamSpec& spec, float real) { return formatValue(spec, real); });
}

ParameterValues::ParameterValues(juce::AudioProcessorValueTreeState& state) : snap_(state, kParams) {}

Params ParameterValues::snapshot() const noexcept { return snap_.snapshot(); }

} // namespace pf
