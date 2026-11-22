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
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "PoleField", makeParameterLayout()),
      curves(state),
      imprint(state),
      presets(state, curves, imprint),
      values_(state) {
    state.addParameterListener(kQualityId, this);
}

PluginProcessor::~PluginProcessor() {
    state.removeParameterListener(kQualityId, this);
    cancelPendingUpdate();
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    sampleRate_ = sampleRate;
    blockSize_ = std::max(1, samplesPerBlock);
    monoScratchR_.assign(static_cast<std::size_t>(blockSize_), 0.0f);
    analysis.setSampleRate(sampleRate);
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
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
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
    if (message.isNoteOn()) {
        engine_.noteOn(message.getNoteNumber());
    } else if (message.isNoteOff()) {
        engine_.noteOff(message.getNoteNumber());
    } else if (message.isPitchWheel()) {
        engine_.pitchBend(2.0f * static_cast<float>(message.getPitchWheelValue() - 8192) / 8192.0f);
    } else if (message.isAllNotesOff() || message.isAllSoundOff()) {
        engine_.allNotesOff();
    }
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels < 1) return;

    const bool isMono = numChannels == 1;
    if (isMono && numSamples > static_cast<int>(monoScratchR_.size())) {
        buffer.clear();
        return;
    }

    float* left = buffer.getWritePointer(0);
    float* right = isMono ? monoScratchR_.data() : buffer.getWritePointer(1);
    if (isMono) std::copy_n(left, numSamples, right);

    const Params params = values_.snapshot();
    if (curves.pull(audioCurves_)) engine_.setLaneCurves(audioCurves_);
    if (imprint.pull(audioImprint_)) engine_.setImprint(audioImprint_);

    const Transport blockStart = readTransport();
    const double quartersPerSample = blockStart.bpm / 60.0 / sampleRate_;

    const auto render = [&](int from, int to) {
        if (to <= from) return;
        Transport transport = blockStart;
        transport.ppq += quartersPerSample * from;
        engine_.process(params, transport, left + from, right + from, to - from);
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

    if (isMono) {
        for (int i = 0; i < numSamples; ++i) left[i] = 0.5f * (left[i] + right[i]);
        analysis.push(left, left, numSamples);
    } else {
        analysis.push(left, right, numSamples);
    }

    Analysis::Status status;
    status.gainReductionDb = engine_.gainReductionDb();
    status.ppq = engine_.lanePpq();
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
