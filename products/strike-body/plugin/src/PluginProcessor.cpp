// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace sb {

namespace {
const juce::String kQualityId{"quality"};
} // namespace

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "StrikeBody", makeParameterLayout()), presets(state), values_(state) {
    for (const auto& s : paramSpecs())
        state.addParameterListener(std::string(s.key), this);
    seedPreviewTelemetry();
}

PluginProcessor::~PluginProcessor() {
    for (const auto& s : paramSpecs())
        state.removeParameterListener(std::string(s.key), this);
    cancelPendingUpdate();
}

void PluginProcessor::seedPreviewTelemetry() {
    const Params params = values_.snapshot();
    const double sr = sampleRate_ > 8000.0 ? sampleRate_ : 48000.0;
    Engine preview;
    preview.prepare(sr, 256, 2);
    std::array<float, 256> l{};
    std::array<float, 256> r{};
    preview.process(params, l.data(), r.data(), 0);
    preview.trigger(Articulation::main, 1.0f, 1.0f);
    // Render 45 ms silently off the audio thread so the 40 ms Transient Microscope is alive on open.
    const int blocks = std::max(1, static_cast<int>(std::ceil(0.045 * sr / 256.0)));
    for (int b = 0; b < blocks; ++b)
        preview.process(params, l.data(), r.data(), 256);
    telemetry.publish(preview.telemetry());
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    sampleRate_ = sampleRate > 8000.0 ? sampleRate : 48000.0;
    blockSize_ = std::max(1, samplesPerBlock);
    monoScratchR_.assign(static_cast<std::size_t>(std::max(4096, blockSize_)), 0.0f);
    prepareEngine();
}

void PluginProcessor::prepareEngine() {
    const auto qualityIdx = static_cast<std::size_t>(
        std::clamp(values_.snapshot().choice(ParamId::quality), 0, static_cast<int>(kQualityFactors.size()) - 1));
    engine_.prepare(sampleRate_, blockSize_, kQualityFactors[qualityIdx]);
    setLatencySamples(engine_.latencySamples());
}

void PluginProcessor::parameterChanged(const juce::String& id, float) {
    bouncer.invalidate();
    if (id == kQualityId) {
        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
            handleAsyncUpdate();
        else
            triggerAsyncUpdate();
    }
}

void PluginProcessor::handleAsyncUpdate() {
    suspendProcessing(true);
    prepareEngine();
    suspendProcessing(false);
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (!layouts.getMainInputChannelSet().isDisabled())
        return false;
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void PluginProcessor::triggerAudition(Articulation art, float velocity) noexcept {
    const std::uint32_t w = audWrite_.load(std::memory_order_relaxed);
    const std::uint32_t r = audRead_.load(std::memory_order_acquire);
    if (w - r >= static_cast<std::uint32_t>(kAuditionQueueSize))
        return;
    const auto velByte =
        static_cast<std::uint32_t>(std::clamp(static_cast<int>(std::lround(velocity * 255.0f)), 1, 255));
    const std::uint32_t packed = (static_cast<std::uint32_t>(art) & 0x3u) | (velByte << 8u);
    audQueue_[w & kAuditionQueueMask] = packed;
    audWrite_.store(w + 1u, std::memory_order_release);
}

void PluginProcessor::handleMidi(const juce::MidiMessage& message, const Params& params) noexcept {
    if (message.isNoteOn()) {
        engine_.noteOn(message.getNoteNumber(), message.getFloatVelocity(), params);
    } else if (message.isNoteOff()) {
        engine_.noteOff(message.getNoteNumber());
    } else if (message.isAllNotesOff() || message.isAllSoundOff()) {
        engine_.allNotesOff();
    }
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels < 1) {
        buffer.clear();
        midi.clear();
        return;
    }

    const Params params = values_.snapshot();
    const bool isMono = (numChannels == 1);
    float* left = buffer.getWritePointer(0);
    float* right = isMono ? monoScratchR_.data() : buffer.getWritePointer(1);
    if (isMono && numSamples > static_cast<int>(monoScratchR_.size())) {
        buffer.clear();
        midi.clear();
        return;
    }

    // Drain UI audition pad / Chladni tap triggers lock-free with up-to-date cached params.
    std::uint32_t r = audRead_.load(std::memory_order_relaxed);
    const std::uint32_t w = audWrite_.load(std::memory_order_acquire);
    if (r != w) {
        engine_.process(params, left, right, 0);
        while (r != w) {
            const std::uint32_t packed = audQueue_[r & kAuditionQueueMask];
            const auto art = static_cast<Articulation>(packed & 0x3u);
            const float vel = static_cast<float>((packed >> 8u) & 0xffu) / 255.0f;
            engine_.trigger(art, vel, 1.0f);
            ++r;
        }
        audRead_.store(r, std::memory_order_release);
    }

    // Sample-accurate incoming MIDI processing.
    int cursor = 0;
    for (const auto metadata : midi) {
        const int eventSample = std::clamp(metadata.samplePosition, cursor, numSamples);
        if (eventSample > cursor) {
            engine_.process(params, left + cursor, right + cursor, eventSample - cursor);
            cursor = eventSample;
        }
        handleMidi(metadata.getMessage(), params);
    }

    if (cursor < numSamples)
        engine_.process(params, left + cursor, right + cursor, numSamples - cursor);

    if (isMono) {
        for (int i = 0; i < numSamples; ++i)
            left[i] = 0.5f * (left[i] + right[i]);
    }

    midi.clear();
    if (engine_.telemetry().triggerCount > 0u)
        telemetry.publish(engine_.telemetry());
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() { return new PluginEditor(*this); }

void PluginProcessor::getStateInformation(juce::MemoryBlock& destination) {
    if (const auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destination);
}

void PluginProcessor::setStateInformation(const void* data, int size) {
    const auto xml = getXmlFromBinary(data, size);
    if (xml == nullptr || !xml->hasTagName(state.state.getType()))
        return;
    state.replaceState(juce::ValueTree::fromXml(*xml));
    presets.reloadFromState();
    seedPreviewTelemetry();
}

} // namespace sb

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new sb::PluginProcessor(); }
