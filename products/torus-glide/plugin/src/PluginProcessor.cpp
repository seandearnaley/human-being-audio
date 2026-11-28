// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <hba/host/render_midi_block.h>

#include <algorithm>
#include <cmath>

namespace tg {

namespace {
const juce::String kQualityId{"quality"};
const juce::String kVoicingPackId{"voicingPack"};
} // namespace

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "TorusGlide", makeParameterLayout()), curves(state), matrix(state),
      presets(state, curves, matrix), values_(state) {
    for (const auto& s : paramSpecs())
        state.addParameterListener(std::string(s.key), this);
}

PluginProcessor::~PluginProcessor() {
    for (const auto& s : paramSpecs())
        state.removeParameterListener(std::string(s.key), this);
    cancelPendingUpdate();
}

double PluginProcessor::getTailLengthSeconds() const {
    // A one-pole release from unity reaches the engine's silence threshold after
    // -log(threshold) time constants. Space can be modulated to its full drain time.
    return -std::log(Engine::kSilentEnvelope) * currentParams().real(ParamId::releaseMs) * 0.001 +
           Engine::spaceDrainSeconds(1.0) + pipelineTailSeconds_.load(std::memory_order_relaxed);
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    sampleRate_ = sampleRate > 8000.0 ? sampleRate : 48000.0;
    blockSize_ = std::max(1, samplesPerBlock);
    generatedMidi_.ensureSize(4096);
    standalonePpq_ = 0.0;
    prepareEngine();
}

void PluginProcessor::prepareEngine() {
    const auto qualityIdx = static_cast<std::size_t>(
        std::clamp(values_.snapshot().choice(ParamId::quality), 0, static_cast<int>(kQualityFactors.size()) - 1));
    engine_.prepare(sampleRate_, blockSize_, kQualityFactors[qualityIdx]);
    engine_.setPitchBendRange(midiParser_.pitchBendRangeSt());
    engine_.setLaneCurves(curves.all());
    engine_.setSceneMatrix(matrix.get());
    pipelineTailSeconds_.store((engine_.latencySamples() + 3.0) / sampleRate_, std::memory_order_relaxed);
    setLatencySamples(engine_.latencySamples());
}

void PluginProcessor::parameterChanged(const juce::String& id, float value) {
    resampler.invalidate();
    const auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    const bool onMessageThread = mm != nullptr && mm->isThisTheMessageThread();
    if (id == kQualityId) {
        pendingPrepare_.store(true, std::memory_order_release);
        if (onMessageThread)
            handleAsyncUpdate();
        else
            triggerAsyncUpdate();
    } else if (id == kVoicingPackId && !restoringState_.load(std::memory_order_relaxed)) {
        pendingPack_.store(static_cast<int>(std::lround(value)), std::memory_order_release);
        if (onMessageThread)
            handleAsyncUpdate();
        else
            triggerAsyncUpdate();
    }
}

void PluginProcessor::handleAsyncUpdate() {
    if (pendingPrepare_.exchange(false, std::memory_order_acq_rel)) {
        suspendProcessing(true);
        prepareEngine();
        suspendProcessing(false);
    }
    const int pack = pendingPack_.exchange(-1, std::memory_order_acq_rel);
    if (pack >= 0)
        matrix.loadPack(pack);
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

void PluginProcessor::handleMidi(const juce::MidiMessage& message, bool allowKeySwitch) noexcept {
    struct Sink {
        Engine& engine;
        std::atomic<int>& lastNote;
        bool allowKS;
        void onNoteOn(int note, float velocity) noexcept {
            engine.noteOn(note, velocity, allowKS);
            if (!(allowKS && note >= 12 && note <= 15))
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
    midiParser_.process(message, Sink{engine_, lastNote_, allowKeySwitch});
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
    if (curves.pull(audioCurves_))
        engine_.setLaneCurves(audioCurves_);
    if (matrix.pull(audioMatrix_))
        engine_.setSceneMatrix(audioMatrix_);
    if (clearKeySwitchRequested_.exchange(false, std::memory_order_acq_rel))
        engine_.clearKeySwitch();
    const bool allowKeySwitch = params.flag(ParamId::keySwitch) && params.choice(ParamId::mode) != 0;

    // Audition button latches a musical note matching the current Root Key!
    const bool wantAudition = auditionWanted_.load(std::memory_order_relaxed);
    const int desiredAudNote = 36 + std::clamp(params.choice(ParamId::rootKey), 0, 11); // Octave 2 (e.g., F2 = 41)
    if (wantAudition != auditionPlaying_) {
        auditionPlaying_ = wantAudition;
        if (auditionPlaying_) {
            auditionNote_ = desiredAudNote;
            engine_.noteOn(auditionNote_, 0.92f);
            lastNote_.store(auditionNote_, std::memory_order_relaxed);
        } else {
            engine_.noteOff(auditionNote_);
        }
    } else if (auditionPlaying_ && desiredAudNote != auditionNote_) {
        engine_.noteOff(auditionNote_);
        auditionNote_ = desiredAudNote;
        engine_.noteOn(auditionNote_, 0.92f);
        lastNote_.store(auditionNote_, std::memory_order_relaxed);
    }

    // Consume incoming MIDI events sample-accurately in sub-blocks if needed, then replace midi with MPE Out.

    const Transport transportStart = readTransport(numSamples);
    lastBpm_.store(transportStart.bpm, std::memory_order_relaxed);
    const double beatsPerSample = (transportStart.bpm / 60.0) / sampleRate_;

    generatedMidi_.clear();
    hba::host::renderMidiBlock(
        buffer, midi, monoScratchR_, numSamples,
        [&](int offset, int count, float* left, float* right) {
            Transport seg = transportStart;
            if (seg.playing)
                seg.ppq += static_cast<double>(offset) * beatsPerSample;
            engine_.process(params, seg, left, right, count, &outMidi_);
            for (int i = 0; i < outMidi_.count; ++i) {
                const auto& ev = outMidi_.events[static_cast<std::size_t>(i)];
                if (ev.size > 0 && ev.size <= 3)
                    generatedMidi_.addEvent(ev.data, ev.size, offset + std::clamp(ev.sampleOffset, 0, count - 1));
            }
        },
        [&](const juce::MidiMessage& message) { handleMidi(message, allowKeySwitch); });

    midi.clear();
    if (!generatedMidi_.isEmpty())
        midi.addEvents(generatedMidi_, 0, numSamples, 0);
    telemetry.publish(engine_.telemetry());
}

PluginProcessor::Performance PluginProcessor::lastPerformance() const noexcept {
    return {lastNote_.load(std::memory_order_relaxed), lastBpm_.load(std::memory_order_relaxed)};
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
    restoringState_.store(true, std::memory_order_relaxed);
    state.replaceState(juce::ValueTree::fromXml(*xml));
    curves.reloadFromState();
    matrix.reloadFromState();
    presets.reloadFromState();
    restoringState_.store(false, std::memory_order_relaxed);
}

} // namespace tg

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tg::PluginProcessor(); }
