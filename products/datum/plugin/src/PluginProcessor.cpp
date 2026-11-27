// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace dt {

namespace {
const juce::String kQualityId{"quality"};
constexpr std::array<int, 3> kOsFactors{4, 8, 16};
} // namespace

PluginProcessor::PluginProcessor()
    : AudioProcessor(hba::host::withSynthAndSidechainBuses(BusesProperties())),
      state(*this, nullptr, "Datum", makeParameterLayout()), values_(state), stateStore(state),
      clickStore([this](const ClickSample& s) { engine_.publishClickSample(s); },
                 [this] { engine_.clearClickSample(); }),
      presets(state, stateStore) {
    for (const auto& s : kParamSpecs)
        state.addParameterListener(std::string(s.key), this);
}

PluginProcessor::~PluginProcessor() {
    for (const auto& s : kParamSpecs)
        state.removeParameterListener(std::string(s.key), this);
    cancelPendingUpdate();
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    sampleRate_ = sampleRate > 8000.0 ? sampleRate : 48000.0;
    blockSize_ = std::max(1, samplesPerBlock);
    const std::size_t scratchN = static_cast<std::size_t>(std::max(4096, blockSize_));
    monoScratchR_.assign(scratchN, 0.0f);
    scScratchL_.assign(scratchN, 0.0f);
    scScratchR_.assign(scratchN, 0.0f);
    standalonePpq_ = 0.0;
    prepareEngine();
}

void PluginProcessor::prepareEngine() {
    const auto qIdx = static_cast<std::size_t>(
        std::clamp(values_.snapshot().choice(ParamId::quality), 0, static_cast<int>(kOsFactors.size()) - 1));
    engine_.prepare(sampleRate_, blockSize_, kOsFactors[qIdx]);
    audioBundle_ = stateStore.all();
    engine_.setCurves(audioBundle_.pitchCurve, audioBundle_.ampCurve);
    engine_.setPattern(audioBundle_.pattern);
    if (clickStore.hasSample()) {
        const auto s = clickStore.copySample();
        if (s.valid())
            engine_.publishClickSample(s);
    }
    setLatencySamples(engine_.latencySamples());
}

void PluginProcessor::parameterChanged(const juce::String& id, float) {
    resampler.invalidate();
    if (id == kQualityId) {
        pendingPrepare_.store(true, std::memory_order_release);
        if (auto* mm = juce::MessageManager::getInstanceWithoutCreating()) {
            if (mm->isThisTheMessageThread())
                handleAsyncUpdate();
            else
                triggerAsyncUpdate();
        } else {
            handleAsyncUpdate();
        }
    }
}

void PluginProcessor::handleAsyncUpdate() {
    if (pendingPrepare_.exchange(false, std::memory_order_acq_rel)) {
        suspendProcessing(true);
        prepareEngine();
        suspendProcessing(false);
    }
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return hba::host::isSynthWithSidechainLayoutSupported(layouts);
}

void PluginProcessor::requestAudition(int slotIndex, float velocity) noexcept {
    pendingAuditionVel_.store(std::clamp(velocity, 0.05f, 1.0f), std::memory_order_relaxed);
    pendingAuditionSlot_.store(std::clamp(slotIndex, -1, kKitSlots - 1), std::memory_order_release);
}

Transport PluginProcessor::readTransport(int numSamples) {
    Transport transport;
    if (const auto* playHead = getPlayHead()) {
        if (const auto position = playHead->getPosition()) {
            transport.bpm = position->getBpm().orFallback(transport.bpm);
            transport.ppq = position->getPpqPosition().orFallback(0.0);
            transport.playing = position->getIsPlaying();
            return transport;
        }
    }
    if (wrapperType == wrapperType_Standalone) {
        transport.bpm = 172.0;
        transport.ppq = standalonePpq_;
        transport.playing = true;
        standalonePpq_ += (transport.bpm / 60.0 / sampleRate_) * static_cast<double>(numSamples);
    }
    return transport;
}

void PluginProcessor::handleMidi(const juce::MidiMessage& message) noexcept {
    if (message.isNoteOn()) {
        engine_.triggerMidi(message.getNoteNumber(), message.getFloatVelocity());
    } else if (message.isNoteOff()) {
        engine_.noteOff(message.getNoteNumber());
    } else if (message.isAllNotesOff() || message.isAllSoundOff()) {
        engine_.allNotesOff();
    }
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    auto outBus = getBusBuffer(buffer, false, 0);
    const int outChannels = outBus.getNumChannels();
    if (numSamples <= 0 || outChannels < 1) {
        buffer.clear();
        midi.clear();
        return;
    }

    // Safely snapshot optional sidechain input BEFORE writing synth output
    // (JUCE may alias input & output bus channel pointers in-place).
    const float* scInL = nullptr;
    const float* scInR = nullptr;
    const bool hasSc = hba::host::getSidechainPointers(*this, buffer, scInL, scInR) &&
                       numSamples <= static_cast<int>(scScratchL_.size());
    if (hasSc) {
        std::memcpy(scScratchL_.data(), scInL, static_cast<std::size_t>(numSamples) * sizeof(float));
        std::memcpy(scScratchR_.data(), scInR, static_cast<std::size_t>(numSamples) * sizeof(float));
    }

    const Params params = values_.snapshot();
    if (stateStore.pull(audioBundle_)) {
        engine_.setCurves(audioBundle_.pitchCurve, audioBundle_.ampCurve);
        engine_.setPattern(audioBundle_.pattern);
    }

    const int audSlot = pendingAuditionSlot_.exchange(-2, std::memory_order_acq_rel);
    if (audSlot >= -1) {
        const int target = audSlot < 0 ? params.choice(ParamId::activeSlot) : audSlot;
        engine_.triggerSlot(target, pendingAuditionVel_.load(std::memory_order_relaxed));
    }

    const bool isMono = (outChannels == 1);
    float* left = outBus.getWritePointer(0);
    float* right = isMono ? monoScratchR_.data() : outBus.getWritePointer(1);
    if (isMono && numSamples > static_cast<int>(monoScratchR_.size())) {
        buffer.clear();
        midi.clear();
        return;
    }
    for (int ch = 2; ch < outChannels; ++ch)
        outBus.clear(ch, 0, numSamples);

    const Transport transportStart = readTransport(numSamples);
    lastBpm_.store(transportStart.bpm, std::memory_order_relaxed);
    const double beatsPerSample = (transportStart.bpm / 60.0) / sampleRate_;

    const float* scL = hasSc ? scScratchL_.data() : nullptr;
    const float* scR = hasSc ? scScratchR_.data() : nullptr;

    int cursor = 0;
    for (const auto metadata : midi) {
        if (metadata.numBytes > 3)
            continue;
        const int eventSample = std::clamp(metadata.samplePosition, cursor, numSamples);
        if (eventSample > cursor) {
            const int chunk = eventSample - cursor;
            Transport seg = transportStart;
            if (seg.playing)
                seg.ppq += static_cast<double>(cursor) * beatsPerSample;
            engine_.process(params, seg, left + cursor, right + cursor, chunk, scL ? scL + cursor : nullptr,
                            scR ? scR + cursor : nullptr);
            cursor = eventSample;
        }
        handleMidi(metadata.getMessage());
    }

    if (cursor < numSamples) {
        const int chunk = numSamples - cursor;
        Transport seg = transportStart;
        if (seg.playing)
            seg.ppq += static_cast<double>(cursor) * beatsPerSample;
        engine_.process(params, seg, left + cursor, right + cursor, chunk, scL ? scL + cursor : nullptr,
                        scR ? scR + cursor : nullptr);
    }

    if (isMono) {
        for (int i = 0; i < numSamples; ++i)
            left[i] = 0.5f * (left[i] + right[i]);
    }

    midi.clear();
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
    stateStore.reloadFromState();
    presets.reloadFromState();
}

} // namespace dt

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new dt::PluginProcessor(); }
