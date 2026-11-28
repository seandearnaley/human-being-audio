// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <span>

namespace hba::host {

// Process each short MIDI event once. Sample offsets always refer to the original
// callback, even when a mono downmix or an auxiliary bus needs bounded workspace.
template <typename Render, typename Handle>
void renderMidiBlock(juce::AudioBuffer<float>& bus, const juce::MidiBuffer& midi, std::span<float> monoScratch,
                     int maxFrames, Render render, Handle handle) {
    const int frames = bus.getNumSamples();
    const int channels = bus.getNumChannels();
    if (frames <= 0 || channels <= 0)
        return;
    const bool mono = channels == 1;
    const int capacity = mono ? std::min(maxFrames, static_cast<int>(monoScratch.size())) : maxFrames;
    jassert(capacity > 0);
    if (capacity <= 0)
        return;
    int offset = 0;
    const auto renderUntil = [&](int end) {
        while (offset < end) {
            const int count = std::min(end - offset, capacity);
            auto* left = bus.getWritePointer(0, offset);
            auto* right = mono ? monoScratch.data() : bus.getWritePointer(1, offset);
            render(offset, count, left, right);
            if (mono) {
                for (int i = 0; i < count; ++i)
                    left[i] = 0.5f * (left[i] + right[i]);
            }
            offset += count;
        }
    };
    for (const auto event : midi) {
        if (event.numBytes > 3)
            continue;
        renderUntil(std::clamp(event.samplePosition, offset, frames));
        handle(event.getMessage());
    }
    renderUntil(frames);
}
} // namespace hba::host
