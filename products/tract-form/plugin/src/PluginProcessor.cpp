#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace tf {

namespace {
const juce::String kQualityId { "quality" };

int noteForHz(float hz) {
    return hz > 0.0f ? static_cast<int>(std::lround(69.0 + 12.0 * std::log2(static_cast<double>(hz) / 440.0))) : -1;
}
}

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "TractForm", makeParameterLayout()),
      curves(state),
      presets(state, curves),
      values_(state) {
    state.addParameterListener(kQualityId, this);
}

PluginProcessor::~PluginProcessor() {
    state.removeParameterListener(kQualityId, this);
    cancelPendingUpdate();
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    blockSize_ = std::max(1, samplesPerBlock);
    const auto tapSize = static_cast<std::size_t>(std::max(4096, blockSize_));
    masterTap_.assign(tapSize, 0.0f);
    growlTap_.assign(tapSize, 0.0f);
    analysis.setSampleRate(sampleRate_);
    prepareEngine();
}

void PluginProcessor::prepareEngine() {
    const auto quality = static_cast<std::size_t>(
        std::clamp(values_.snapshot().choice(ParamId::quality), 0, static_cast<int>(kQualityFactors.size()) - 1));
    engine_.prepare(sampleRate_, blockSize_, kQualityFactors[quality]);
    engine_.setPitchBendRange(midiParser_.pitchBendRangeSt());
    setLatencySamples(engine_.latencySamples());
}

// Quality changes the oversampling factor, which reallocates. It is not
// automatable, so this arrives from the interface or a restored session, and a
// session may be restored on any thread. The update is cancelled with the
// processor, so one still queued when the host unloads it never runs.
void PluginProcessor::parameterChanged(const juce::String&, float) {
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        handleAsyncUpdate();
    else
        triggerAsyncUpdate();
}

void PluginProcessor::handleAsyncUpdate() {
    suspendProcessing(true);
    prepareEngine();
    suspendProcessing(false);
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

Transport PluginProcessor::readTransport() const {
    Transport transport;
    if (const auto* playHead = getPlayHead()) {
        if (const auto position = playHead->getPosition()) {
            transport.bpm = position->getBpm().orFallback(transport.bpm);
            transport.ppq = position->getPpqPosition().orFallback(0.0);
            transport.playing = position->getIsPlaying();
        }
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
        void onPolyAftertouch(int note, float norm01) noexcept {
            const int active = engine.activeNote();
            if (active < 0 || note == active)
                engine.setAftertouch(norm01);
        }
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
    if (buffer.getNumChannels() < 2 || numSamples <= 0) {
        buffer.clear();
        return;
    }
    float* left = buffer.getWritePointer(0);
    float* right = buffer.getWritePointer(1);

    const Params params = values_.snapshot();
    if (curves.pull(audioCurves_)) engine_.setLaneCurves(audioCurves_);
    const bool wantAudition = auditionWanted_.load(std::memory_order_relaxed);
    if (wantAudition != auditionPlaying_) {
        auditionPlaying_ = wantAudition;
        if (auditionPlaying_) {
            engine_.noteOn(29, 0.95f); // F1
            lastNote_.store(29, std::memory_order_relaxed);
        } else {
            engine_.noteOff(29);
        }
    }
    const Transport blockStart = readTransport();
    lastBpm_.store(blockStart.bpm, std::memory_order_relaxed);
    const double safeSr = sampleRate_ > 0.0 ? sampleRate_ : 48000.0;
    const double quartersPerSample = blockStart.bpm / 60.0 / safeSr;
    const int tapCap = static_cast<int>(masterTap_.size());

    // Render up to each MIDI event, then apply it, so notes land on their sample.
    // Chunk through the preallocated tap buffers so oversized host blocks never
    // drop audio or MIDI events.
    const auto render = [&](int from, int to) {
        while (from < to) {
            const int chunk = tapCap > 0 ? std::min(to - from, tapCap) : (to - from);
            Transport transport = blockStart;
            transport.ppq += quartersPerSample * from;
            Taps taps{};
            if (tapCap > 0) {
                taps.master = masterTap_.data();
                taps.growl = growlTap_.data();
            }
            engine_.process(params, transport, left + from, right + from, chunk, taps);
            if (tapCap > 0)
                analysis.push(masterTap_.data(), growlTap_.data(), left + from, right + from, chunk);
            from += chunk;
        }
    };
    int cursor = 0;
    for (const auto metadata : midi) {
        // Everything this synth plays fits in three bytes. A longer message
        // (SysEx) would allocate when copied, so it is skipped unread.
        if (metadata.numBytes > 3) continue;
        const int position = juce::jlimit(0, numSamples, metadata.samplePosition);
        render(cursor, position);
        cursor = std::max(cursor, position);
        handleMidi(metadata.getMessage());
    }
    render(cursor, numSamples);
    midi.clear();

    Analysis::Status status;
    status.gate = engine_.gateOpen();
    status.hz = engine_.currentHz();
    status.note = status.gate ? noteForHz(status.hz) : -1;
    status.gainReductionDb = engine_.gainReductionDb();
    status.ppq = engine_.lanePpq();
    status.notePpq = engine_.notePpq();
    status.lanePhase = engine_.lanePhases();
    status.laneValue = engine_.laneValues();
    status.effective = engine_.effectiveState();
    analysis.setStatus(status);
}

PluginProcessor::Performance PluginProcessor::lastPerformance() const noexcept {
    return { lastNote_.load(std::memory_order_relaxed), lastBpm_.load(std::memory_order_relaxed) };
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() { return new PluginEditor(*this); }

void PluginProcessor::getStateInformation(juce::MemoryBlock& destination) {
    if (const auto xml = state.copyState().createXml()) copyXmlToBinary(*xml, destination);
}

void PluginProcessor::setStateInformation(const void* data, int size) {
    if (const auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(state.state.getType())) {
            state.replaceState(juce::ValueTree::fromXml(*xml));
            curves.reloadFromState();
        }
}

}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tf::PluginProcessor(); }
