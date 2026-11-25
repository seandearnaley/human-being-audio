// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Parameters.h"

namespace tg {

juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& s : paramSpecs()) {
        const juce::ParameterID pid{std::string(s.key), 1};
        const juce::String name{std::string(s.name)};

        if (s.kind == ParamKind::toggle) {
            auto attrs = juce::AudioParameterBoolAttributes().withAutomatable(s.automatable);
            layout.add(std::make_unique<juce::AudioParameterBool>(pid, name, s.def >= 0.5f, attrs));
        } else if (s.kind == ParamKind::choice) {
            juce::StringArray items;
            for (const auto& c : s.choices)
                items.add(std::string(c));
            auto attrs = juce::AudioParameterChoiceAttributes().withAutomatable(s.automatable);
            layout.add(std::make_unique<juce::AudioParameterChoice>(pid, name, items, static_cast<int>(s.def), attrs));
        } else {
            juce::NormalisableRange<float> range(s.low, s.high, 0.0f, s.skew);
            auto attrs = juce::AudioParameterFloatAttributes()
                             .withAutomatable(s.automatable)
                             .withStringFromValueFunction(
                                 [spec = s](float v, int) { return juce::String(formatValue(spec, v)); });
            layout.add(std::make_unique<juce::AudioParameterFloat>(pid, name, range, s.def, attrs));
        }
    }
    return layout;
}

ParameterValues::ParameterValues(const juce::AudioProcessorValueTreeState& state) {
    for (std::size_t i = 0; i < kParamCount; ++i) {
        const auto& s = paramSpecs()[i];
        raw_[i] = state.getRawParameterValue(std::string(s.key));
    }
}

Params ParameterValues::snapshot() const noexcept {
    Params p;
    for (std::size_t i = 0; i < kParamCount; ++i)
        p.values[i] = raw_[i] != nullptr ? raw_[i]->load(std::memory_order_relaxed) : paramSpecs()[i].def;
    return p;
}

} // namespace tg
