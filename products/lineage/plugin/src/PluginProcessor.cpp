// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace ln {

namespace {
const juce::String kQualityId{"quality"};
const juce::String kDroneNoteId{"droneNote"};
} // namespace

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "Lineage", makeParameterLayout()), values_(state), curves(state),
      printStore(
          state, [this](const PrintBuffer& buf) { engine_.publishPrint(buf); }, [this] { engine_.clearPrint(); }),
      presets(state, curves,
              [this](const Params& p, const LaneCurves& c, bool autoPrint) {
                  if (autoPrint) {
                      const auto perf = lastPerformance();
                      printStore.triggerPrint(p, c, sampleRate_, perf.bpm, perf.note, false);
                  }
              }) {
    for (const auto& s : kParams)
        state.addParameterListener(std::string(s.key), this);
}

PluginProcessor::~PluginProcessor() {
    for (const auto& s : kParams)
        state.removeParameterListener(std::string(s.key), this);
    cancelPendingUpdate();
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    sampleRate_ = sampleRate > 8000.0 ? sampleRate : 48000.0;
    blockSize_ = std::max(1, samplesPerBlock);
    monoScratchR_.assign(static_cast<std::size_t>(std::max(4096, blockSize_)), 0.0f);
    standalonePpq_ = 0.0;
    prepareEngine();
}

void PluginProcessor::prepareEngine() {
    const auto qualityIdx = static_cast<std::size_t>(
        std::clamp(values_.snapshot().choice(ParamId::quality), 0, static_cast<int>(kQualityFactors.size()) - 1));
    engine_.prepare(sampleRate_, blockSize_, kQualityFactors[qualityIdx]);
    engine_.setPitchBendRange(midiParser_.pitchBendRangeSt());
    engine_.setLaneCurves(curves.all());
    const auto existingPrint = printStore.copyBuffer();
    if (existingPrint.valid())
        engine_.publishPrint(existingPrint);
    setLatencySamples(engine_.latencySamples());
}

void PluginProcessor::parameterChanged(const juce::String& id, float value) {
    resampler.invalidate();
    if (id == kDroneNoteId) {
        const int note = 24 + std::clamp(static_cast<int>(std::lround(value)), 0, 11);
        lastNote_.store(note, std::memory_order_relaxed);
    } else if (id == kQualityId) {
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
    if (!layouts.getMainInputChannelSet().isDisabled())
        return false;
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
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
    struct Sink {
        Engine& engine;
        std::atomic<int>& lastNote;
        void onNoteOn(int note, float velocity) noexcept {
            engine.noteOn(note, velocity);
            lastNote.store(note, std::memory_order_relaxed);
        }
        void onNoteOff(int note) noexcept { engine.noteOff(note); }
        void onPitchBend(float semitones) noexcept { engine.pitchBend(semitones); }
        void onPitchBendRange(float semitones) noexcept { engine.setPitchBendRange(semitones); }
        void onModWheel(float norm01) noexcept { engine.setModWheel(norm01); }
        void onSustain(bool down) noexcept { engine.sustain(down); }
        void onTimbre(float norm01) noexcept { engine.setTimbre(norm01); }
        void onChannelPressure(float norm01) noexcept { engine.setAftertouch(norm01); }
        void onPolyAftertouch(int, float norm01) noexcept { engine.setAftertouch(norm01); }
        void onResetControllers() noexcept {
            engine.sustain(false);
            engine.setModWheel(0.0f);
            engine.setTimbre(0.5f);
            engine.setAftertouch(0.0f);
            engine.pitchBend(0.0f);
        }
        void onAllNotesOff() noexcept { engine.allNotesOff(); }
    };
    midiParser_.process(message, Sink{engine_, lastNote_});
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
    const int bendChoice = std::clamp(params.choice(ParamId::bendRange), 0, static_cast<int>(kBendSemitones.size()) - 1);
    if (bendChoice != lastBendChoice_) {
        lastBendChoice_ = bendChoice;
        const float st = static_cast<float>(kBendSemitones[static_cast<std::size_t>(bendChoice)]);
        midiParser_.setPitchBendRange(st);
        engine_.setPitchBendRange(st);
    }
    if (curves.pull(audioCurves_))
        engine_.setLaneCurves(audioCurves_);

    const bool isMono = (numChannels == 1);
    float* left = buffer.getWritePointer(0);
    float* right = isMono ? monoScratchR_.data() : buffer.getWritePointer(1);
    if (isMono && numSamples > static_cast<int>(monoScratchR_.size())) {
        buffer.clear();
        midi.clear();
        return;
    }

    const Transport transportStart = readTransport(numSamples);
    lastBpm_.store(transportStart.bpm, std::memory_order_relaxed);
    const double beatsPerSample = (transportStart.bpm / 60.0) / sampleRate_;

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
            engine_.process(params, seg, left + cursor, right + cursor, chunk);
            cursor = eventSample;
        }
        handleMidi(metadata.getMessage());
    }

    if (cursor < numSamples) {
        const int chunk = numSamples - cursor;
        Transport seg = transportStart;
        if (seg.playing)
            seg.ppq += static_cast<double>(cursor) * beatsPerSample;
        engine_.process(params, seg, left + cursor, right + cursor, chunk);
    }

    if (isMono) {
        for (int i = 0; i < numSamples; ++i)
            left[i] = 0.5f * (left[i] + right[i]);
    }

    midi.clear();
    telemetry.publish(engine_.telemetry());
}

PluginProcessor::Performance PluginProcessor::lastPerformance() const noexcept {
    return {lastNote_.load(std::memory_order_relaxed), lastBpm_.load(std::memory_order_relaxed)};
}

void PluginProcessor::triggerPrint(bool stackGeneration) {
    const auto perf = lastPerformance();
    printStore.triggerPrint(currentParams(), curves.all(), sampleRate_, perf.bpm, perf.note, stackGeneration);
}

void PluginProcessor::triggerPrintSync(bool stackGeneration) {
    const auto perf = lastPerformance();
    printStore.renderSync(currentParams(), curves.all(), sampleRate_, perf.bpm, perf.note, stackGeneration);
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
    curves.reloadFromState();
    presets.reloadFromState();
    printStore.reloadFromState(currentParams(), curves.all(), sampleRate_);
}

} // namespace ln

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ln::PluginProcessor(); }
