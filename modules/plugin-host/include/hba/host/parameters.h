// SPDX-License-Identifier: GPL-3.0-or-later
// Shared JUCE AudioProcessorValueTreeState parameter layout & lock-free snapshot helpers.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <string_view>

namespace hba::host {

inline juce::String toJuce(std::string_view utf8) {
    return juce::String::fromUTF8(utf8.data(), static_cast<int>(utf8.size()));
}

template <typename SpecRangeT, typename MakeRangeFn, typename FormatFn>
inline juce::AudioProcessorValueTreeState::ParameterLayout
makeParameterLayout(const SpecRangeT& specs, MakeRangeFn&& makeRange, FormatFn&& formatValue,
                    int parameterVersion = 1) {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& spec : specs) {
        using Kind = decltype(spec.kind);
        const juce::ParameterID pid{toJuce(spec.key), parameterVersion};
        const auto name = toJuce(spec.name);

        if (spec.kind == Kind::toggle) {
            layout.add(std::make_unique<juce::AudioParameterBool>(
                pid, name, spec.def >= 0.5f, juce::AudioParameterBoolAttributes().withAutomatable(spec.automatable)));
        } else if (spec.kind == Kind::choice) {
            juce::StringArray labels;
            for (const auto label : spec.choices)
                labels.add(toJuce(label));
            layout.add(std::make_unique<juce::AudioParameterChoice>(
                pid, name, labels, static_cast<int>(spec.def),
                juce::AudioParameterChoiceAttributes().withAutomatable(spec.automatable)));
        } else {
            const juce::NormalisableRange<float> range = makeRange(spec);
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                pid, name, range, spec.def,
                juce::AudioParameterFloatAttributes()
                    .withAutomatable(spec.automatable)
                    .withStringFromValueFunction(
                        [&spec, formatValue](float real, int) { return toJuce(formatValue(spec, real)); })));
        }
    }
    return layout;
}

template <typename ParamsT, typename SpecArrayT> class ParameterSnapshotter {
  public:
    static constexpr std::size_t kCount = std::tuple_size_v<SpecArrayT>;

    ParameterSnapshotter(const juce::AudioProcessorValueTreeState& state, const SpecArrayT& specs) : specs_(&specs) {
        for (std::size_t i = 0; i < kCount; ++i) {
            raw_[i] = state.getRawParameterValue(toJuce(specs[i].key));
            jassert(raw_[i] != nullptr);
        }
    }

    ParamsT snapshot() const noexcept {
        ParamsT out;
        for (std::size_t i = 0; i < kCount; ++i)
            out.values[i] = raw_[i] != nullptr ? raw_[i]->load(std::memory_order_relaxed) : (*specs_)[i].def;
        return out;
    }

  private:
    const SpecArrayT* specs_ = nullptr;
    std::array<std::atomic<float>*, kCount> raw_{};
};

} // namespace hba::host
