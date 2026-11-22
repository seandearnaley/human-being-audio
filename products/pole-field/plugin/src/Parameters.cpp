#include "Parameters.h"

namespace pf {

juce::String toJuce(std::string_view utf8) {
    return juce::String::fromUTF8(utf8.data(), static_cast<int>(utf8.size()));
}

namespace {
constexpr int kParameterVersion = 1;

juce::ParameterID idOf(const ParamSpec& spec) { return { toJuce(spec.key), kParameterVersion }; }

std::unique_ptr<juce::RangedAudioParameter> makeParameter(const ParamSpec& spec) {
    const auto name = toJuce(spec.name);

    if (spec.kind == ParamKind::toggle)
        return std::make_unique<juce::AudioParameterBool>(
            idOf(spec), name, spec.def >= 0.5f,
            juce::AudioParameterBoolAttributes().withAutomatable(spec.automatable));

    if (spec.kind == ParamKind::choice) {
        juce::StringArray labels;
        for (const auto label : spec.choices) labels.add(toJuce(label));
        return std::make_unique<juce::AudioParameterChoice>(
            idOf(spec), name, labels, static_cast<int>(spec.def),
            juce::AudioParameterChoiceAttributes().withAutomatable(spec.automatable));
    }

    juce::NormalisableRange<float> range(
        spec.min, spec.max,
        [&spec](float, float, float normalised) { return fromNormalised(spec, normalised); },
        [&spec](float, float, float real) { return toNormalised(spec, real); });

    return std::make_unique<juce::AudioParameterFloat>(
        idOf(spec), name, range, spec.def,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([&spec](float real, int) { return toJuce(formatValue(spec, real)); })
            .withAutomatable(spec.automatable));
}
}

juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& spec : kParams) layout.add(makeParameter(spec));
    return layout;
}

ParameterValues::ParameterValues(juce::AudioProcessorValueTreeState& state) {
    for (std::size_t i = 0; i < kParams.size(); ++i) {
        values_[i] = state.getRawParameterValue(toJuce(kParams[i].key));
        jassert(values_[i] != nullptr);
    }
}

Params ParameterValues::snapshot() const noexcept {
    Params params;
    for (std::size_t i = 0; i < kParams.size(); ++i)
        params.values[i] = values_[i]->load(std::memory_order_relaxed);
    return params;
}

}
