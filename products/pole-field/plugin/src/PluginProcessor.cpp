#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <algorithm>

namespace pf {

namespace {
const juce::String kQualityId { "quality" };
}

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      state(*this, nullptr, "PoleField", makeParameterLayout()), curves(state), imprint(state),
      presets(state, curves, imprint), values_(state) {
    state.addParameterListener(kQualityId, this);
}

PluginProcessor::~PluginProcessor() {
    state.removeParameterListener(kQualityId, this);
    cancelPendingUpdate();
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    sampleRate_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
    blockSize_ = std::max(1, samplesPerBlock);
    const auto scratchSize = static_cast<std::size_t>(std::max(4096, blockSize_));
    monoScratchR_.assign(scratchSize, 0.0f);
    preInMono_.assign(scratchSize, 0.0f);
    analysis.setSampleRate(sampleRate_);
    prepareEngine();
}

void PluginProcessor::prepareEngine() {
    const auto quality = static_cast<std::size_t>(
        std::clamp(values_.snapshot().choice(ParamId::quality), 0, static_cast<int>(kQualityFactors.size()) - 1));
    engine_.prepare(sampleRate_, blockSize_, kQualityFactors[quality]);
    setLatencySamples(engine_.latencySamples());
}

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
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    if (in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono())
        return false;
    if (layouts.inputBuses.size() > 1) {
        const auto& sc = layouts.getChannelSet(true, 1);
        if (!sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
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
        void onNoteOn(int note, float) noexcept { engine.noteOn(note); }
        void onNoteOff(int note) noexcept { engine.noteOff(note); }
        void onPitchBend(float semitones) noexcept { engine.pitchBend(semitones); }
        void onPitchBendRange(float) noexcept {}
        void onModWheel(float norm01) noexcept { engine.setModWheel(norm01); }
        void onSustain(bool) noexcept {}
        void onTimbre(float norm01) noexcept { engine.setTimbre(norm01); }
        void onChannelPressure(float norm01) noexcept { engine.setAftertouch(norm01); }
        void onPolyAftertouch(int, float norm01) noexcept { engine.setAftertouch(norm01); }
        void onResetControllers() noexcept {
            engine.setModWheel(0.0f);
            engine.setTimbre(0.5f);
            engine.setAftertouch(0.0f);
            engine.pitchBend(0.0f);
        }
        void onAllNotesOff() noexcept { engine.allNotesOff(); }
    };
    midiParser_.process(message, Sink{engine_});
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int totalChannels = buffer.getNumChannels();
    if (numSamples <= 0 || totalChannels < 1)
        return;

    const int bus0Count = std::max(1, getChannelCountOfBus(true, 0));
    const int mainCh = std::min(totalChannels, bus0Count);
    const bool isMono = mainCh == 1;

    const bool scEnabled = getBusCount(true) > 1 && getBus(true, 1)->isEnabled();
    const int scCount = scEnabled ? getChannelCountOfBus(true, 1) : 0;
    const int scIdx0 = scCount > 0 ? getChannelIndexInProcessBlockBuffer(true, 1, 0) : -1;
    const int scIdx1 = scCount > 1 ? getChannelIndexInProcessBlockBuffer(true, 1, 1) : scIdx0;
    const bool hasSidechain = scIdx0 >= 0 && scIdx0 < totalChannels && scIdx1 >= 0 && scIdx1 < totalChannels;

    const Params params = values_.snapshot();
    if (curves.pull(audioCurves_)) engine_.setLaneCurves(audioCurves_);
    if (imprint.pull(audioImprint_)) engine_.setImprint(audioImprint_);

    const Transport blockStart = readTransport();
    const double quartersPerSample = blockStart.bpm / 60.0 / sampleRate_;
    const int maxChunk = static_cast<int>(std::min(monoScratchR_.size(), preInMono_.size()));
    if (maxChunk <= 0) {
        buffer.clear();
        midi.clear();
        return;
    }

    const auto render = [&](int from, int to) {
        if (to <= from)
            return;
        int pos = from;
        while (pos < to) {
            const int chunk = std::min(maxChunk, to - pos);
            float* left = buffer.getWritePointer(0) + pos;
            float* right = isMono ? monoScratchR_.data() : (buffer.getWritePointer(1) + pos);
            if (isMono) {
                std::copy_n(left, chunk, right);
                std::copy_n(left, chunk, preInMono_.data());
            } else {
                for (int i = 0; i < chunk; ++i)
                    preInMono_[static_cast<std::size_t>(i)] = 0.5f * (left[i] + right[i]);
            }
            const float* scL = hasSidechain ? (buffer.getReadPointer(scIdx0) + pos) : nullptr;
            const float* scR = hasSidechain ? (buffer.getReadPointer(scIdx1) + pos) : nullptr;
            Transport transport = blockStart;
            transport.ppq += quartersPerSample * pos;
            engine_.process(params, transport, left, right, chunk, scL, scR);
            if (isMono) {
                for (int i = 0; i < chunk; ++i)
                    left[i] = 0.5f * (left[i] + right[i]);
            }
            analysis.push(preInMono_.data(), left, isMono ? left : right, chunk);
            pos += chunk;
        }
    };

    int cursor = 0;
    for (const auto metadata : midi) {
        if (metadata.numBytes > 3) continue;
        const int position = juce::jlimit(0, numSamples, metadata.samplePosition);
        render(cursor, position);
        cursor = std::max(cursor, position);
        handleMidi(metadata.getMessage());
    }
    render(cursor, numSamples);
    midi.clear();

    Analysis::Status status;
    status.gainReductionDb = engine_.gainReductionDb();
    status.ppq = engine_.lanePpq();
    status.osFactor = engine_.oversamplingFactor();
    status.lanePhase = engine_.lanePhases();
    status.laneValue = engine_.laneValues();
    status.effective = engine_.effectiveState();
    analysis.setStatus(status);
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
            imprint.reloadFromState();
        }
}

}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new pf::PluginProcessor(); }
