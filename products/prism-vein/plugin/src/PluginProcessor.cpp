// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <algorithm>
#include <cstring>

namespace pv {

namespace {
const juce::String kQualityId{"quality"};
const juce::String kLatencyId{"latencyMode"};
} // namespace

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      state(*this, nullptr, "PrismVein", makeParameterLayout()), values_(state), stateStore(state),
      presets(state, stateStore) {
    state.addParameterListener(kQualityId, this);
    state.addParameterListener(kLatencyId, this);
}

PluginProcessor::~PluginProcessor() {
    state.removeParameterListener(kQualityId, this);
    state.removeParameterListener(kLatencyId, this);
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
    midiParser_.reset(2.0f);
    prepareEngine();
}

void PluginProcessor::prepareEngine() {
    engine_.setParams(values_.snapshot());
    engine_.prepare(sampleRate_, blockSize_);
    const auto extra = stateStore.all();
    audioChordMask_ = extra.chordMask;
    audioCurves_ = extra.curves;
    engine_.setChordMask(audioChordMask_);
    engine_.setLaneCurves(audioCurves_);
    setLatencySamples(engine_.latencySamples());
}

void PluginProcessor::schedulePrepare() {
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

void PluginProcessor::parameterChanged(const juce::String& id, float) {
    if (id == kQualityId || id == kLatencyId)
        schedulePrepare();
}

void PluginProcessor::handleAsyncUpdate() {
    if (pendingPrepare_.exchange(false, std::memory_order_acq_rel)) {
        suspendProcessing(true);
        prepareEngine();
        suspendProcessing(false);
    }
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    if (in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono())
        return false;
    if (layouts.inputBuses.size() > 1) {
        const auto& sc = layouts.getChannelSet(true, 1);
        if (!sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
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
        void onNoteOn(int note, float vel) noexcept { engine.noteOn(note, vel); }
        void onNoteOff(int note) noexcept { engine.noteOff(note); }
        void onPitchBend(float semitones) noexcept { engine.setPitchBend(semitones); }
        void onPitchBendRange(float) noexcept {}
        void onModWheel(float norm01) noexcept { engine.setModWheel(norm01); }
        void onSustain(bool down) noexcept { engine.setSustainPedal(down); }
        void onTimbre(float) noexcept {}
        void onChannelPressure(float) noexcept {}
        void onPolyAftertouch(int, float) noexcept {}
        void onResetControllers() noexcept { engine.resetControllers(); }
        void onAllNotesOff() noexcept { engine.allNotesOff(); }
    };
    midiParser_.process(message, Sink{engine_});
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int totalChannels = buffer.getNumChannels();
    if (numSamples <= 0 || totalChannels < 1) {
        midi.clear();
        return;
    }

    const int bus0Count = std::max(1, getChannelCountOfBus(true, 0));
    const int mainCh = std::min(totalChannels, bus0Count);
    const bool isMono = mainCh == 1;

    // Optional Sidechain Carrier bus (input bus 1 on Audio FX plugins).
    const bool scEnabled = getBusCount(true) > 1 && getBus(true, 1)->isEnabled();
    const int scCount = scEnabled ? getChannelCountOfBus(true, 1) : 0;
    const int scIdx0 = scCount > 0 ? getChannelIndexInProcessBlockBuffer(true, 1, 0) : -1;
    const int scIdx1 = scCount > 1 ? getChannelIndexInProcessBlockBuffer(true, 1, 1) : scIdx0;
    const bool hasSc = scIdx0 >= 0 && scIdx0 < totalChannels && scIdx1 >= 0 && scIdx1 < totalChannels;

    const Params params = values_.snapshot();
    engine_.setParams(params);
    if (stateStore.pull(audioChordMask_, audioCurves_)) {
        engine_.setChordMask(audioChordMask_);
        engine_.setLaneCurves(audioCurves_);
    }

    const int maxChunk = static_cast<int>(std::min({monoScratchR_.size(), scScratchL_.size(), scScratchR_.size()}));
    if (maxChunk <= 0) {
        buffer.clear();
        midi.clear();
        return;
    }

    const Transport transportStart = readTransport(numSamples);
    const double beatsPerSample = (transportStart.bpm / 60.0) / sampleRate_;

    const auto render = [&](int from, int to) {
        if (to <= from)
            return;
        int pos = from;
        while (pos < to) {
            const int chunk = std::min(maxChunk, to - pos);
            float* left = buffer.getWritePointer(0) + pos;
            float* right = isMono ? monoScratchR_.data() : (buffer.getWritePointer(1) + pos);
            if (isMono)
                std::memcpy(right, left, static_cast<std::size_t>(chunk) * sizeof(float));

            const float* scL = nullptr;
            const float* scR = nullptr;
            if (hasSc) {
                std::memcpy(scScratchL_.data(), buffer.getReadPointer(scIdx0) + pos,
                            static_cast<std::size_t>(chunk) * sizeof(float));
                std::memcpy(scScratchR_.data(), buffer.getReadPointer(scIdx1) + pos,
                            static_cast<std::size_t>(chunk) * sizeof(float));
                scL = scScratchL_.data();
                scR = scScratchR_.data();
            }

            Transport seg = transportStart;
            if (seg.playing)
                seg.ppq += static_cast<double>(pos) * beatsPerSample;
            engine_.setTransport(seg);

            engine_.process(left, right, scL, scR, left, right, chunk);

            if (isMono) {
                for (int i = 0; i < chunk; ++i)
                    left[i] = 0.5f * (left[i] + right[i]);
            }
            pos += chunk;
        }
    };

    int cursor = 0;
    for (const auto metadata : midi) {
        if (metadata.numBytes > 3)
            continue;
        const int eventSample = std::clamp(metadata.samplePosition, cursor, numSamples);
        render(cursor, eventSample);
        cursor = eventSample;
        handleMidi(metadata.getMessage());
    }
    render(cursor, numSamples);

    const int outCh = getChannelCountOfBus(false, 0);
    for (int ch = mainCh; ch < std::min(outCh, totalChannels); ++ch)
        buffer.clear(ch, 0, numSamples);

    midi.clear();
    telemetry.publish(engine_.telemetry());
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() { return new PluginEditor(*this); }

void PluginProcessor::getStateInformation(juce::MemoryBlock& destination) {
    const auto session = state.copyState();
    if (const auto xml = session.createXml())
        copyXmlToBinary(*xml, destination);
}

void PluginProcessor::setStateInformation(const void* data, int size) {
    const auto xml = getXmlFromBinary(data, size);
    if (xml == nullptr || !xml->hasTagName(state.state.getType()))
        return;
    state.replaceState(juce::ValueTree::fromXml(*xml));
    stateStore.reloadFromState();
    presets.reloadFromState();
    // Ensure recalled latencyMode / OS quality updates engine latency immediately.
    schedulePrepare();
}

} // namespace pv

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new pv::PluginProcessor(); }
