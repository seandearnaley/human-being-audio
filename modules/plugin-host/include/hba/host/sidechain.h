// SPDX-License-Identifier: GPL-3.0-or-later
// Canonical JUCE 9 helper for synthesizer plugins exposing an optional stereo
// sidechain input bus (VST3 Vst::kAux + AU MusicDevice sidechain compatibility).
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>

namespace hba::host {

// JUCE 9's VST3 wrapper inspects VST3ClientExtensions::getPluginHasMainInput()
// to classify input bus 0 as Vst::kAux (sidechain) instead of Vst::kMain on synths.
struct AuxSidechainExtension final : juce::VST3ClientExtensions {
    bool getPluginHasMainInput() const override { return false; }
};

template <typename BusesPropsT> inline BusesPropsT withSynthAndSidechainBuses(BusesPropsT props) {
    return props.withOutput("Output", juce::AudioChannelSet::stereo(), true)
        .withInput("Sidechain", juce::AudioChannelSet::stereo(), false);
}

inline bool isSynthWithSidechainLayoutSupported(const juce::AudioProcessor::BusesLayout& layouts) {
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    if (layouts.inputBuses.size() > 0) {
        const auto& sc = layouts.getChannelSet(true, 0);
        if (!sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

// Audio-thread safe helper returning read pointers for the optional sidechain input bus (or nullptr if inactive).
inline bool getSidechainPointers(const juce::AudioProcessor& proc, const juce::AudioBuffer<float>& buffer,
                                 const float*& scL, const float*& scR) noexcept {
    scL = nullptr;
    scR = nullptr;
    if (proc.getBusCount(true) <= 0)
        return false;
    const auto* bus = proc.getBus(true, 0);
    if (bus == nullptr || !bus->isEnabled())
        return false;
    const int scCount = proc.getChannelCountOfBus(true, 0);
    if (scCount <= 0)
        return false;
    const int idx0 = proc.getChannelIndexInProcessBlockBuffer(true, 0, 0);
    const int idx1 = scCount > 1 ? proc.getChannelIndexInProcessBlockBuffer(true, 0, 1) : idx0;
    const int total = buffer.getNumChannels();
    if (idx0 < 0 || idx0 >= total || idx1 < 0 || idx1 >= total)
        return false;
    scL = buffer.getReadPointer(idx0);
    scR = buffer.getReadPointer(idx1);
    return true;
}

} // namespace hba::host
