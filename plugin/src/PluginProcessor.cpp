#include "PluginProcessor.h"

#include "PluginEditor.h"

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
      presets(state),
      values_(state) {
    state.addParameterListener(kQualityId, this);
}

PluginProcessor::~PluginProcessor() { state.removeParameterListener(kQualityId, this); }

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    sampleRate_ = sampleRate;
    blockSize_ = samplesPerBlock;
    masterTap_.assign(static_cast<std::size_t>(samplesPerBlock), 0.0f);
    growlTap_.assign(static_cast<std::size_t>(samplesPerBlock), 0.0f);
    analysis.setSampleRate(sampleRate);
    prepareEngine();
}

void PluginProcessor::prepareEngine() {
    const auto quality = static_cast<std::size_t>(values_.snapshot().choice(ParamId::quality));
    engine_.prepare(sampleRate_, blockSize_, kQualityFactors[quality]);
    setLatencySamples(engine_.latencySamples());
}

// Quality changes the oversampling factor, which reallocates. It is not
// automatable, so this arrives from the interface or a restored session.
void PluginProcessor::parameterChanged(const juce::String&, float) {
    const auto reprepare = [this] {
        suspendProcessing(true);
        prepareEngine();
        suspendProcessing(false);
    };
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        reprepare();
    else
        juce::MessageManager::callAsync(reprepare);
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
    if (message.isNoteOn()) {
        engine_.noteOn(message.getNoteNumber());
        lastNote_.store(message.getNoteNumber(), std::memory_order_relaxed);
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
    if (buffer.getNumChannels() < 2 || numSamples > static_cast<int>(masterTap_.size())) {
        buffer.clear();
        return;
    }
    float* left = buffer.getWritePointer(0);
    float* right = buffer.getWritePointer(1);

    const Params params = values_.snapshot();
    const Transport blockStart = readTransport();
    lastBpm_.store(blockStart.bpm, std::memory_order_relaxed);
    const double quartersPerSample = blockStart.bpm / 60.0 / sampleRate_;

    // Render up to each MIDI event, then apply it, so notes land on their sample.
    const auto render = [&](int from, int to) {
        if (to <= from) return;
        Transport transport = blockStart;
        transport.ppq += quartersPerSample * from;
        engine_.process(params, transport, left + from, right + from, to - from,
                        { masterTap_.data() + from, growlTap_.data() + from });
    };
    int cursor = 0;
    for (const auto metadata : midi) {
        const int position = juce::jlimit(0, numSamples, metadata.samplePosition);
        render(cursor, position);
        cursor = std::max(cursor, position);
        handleMidi(metadata.getMessage());
    }
    render(cursor, numSamples);
    midi.clear();

    analysis.push(masterTap_.data(), growlTap_.data(), left, right, numSamples);
    Analysis::Status status;
    status.gate = engine_.gateOpen();
    status.hz = engine_.currentHz();
    status.note = status.gate ? noteForHz(status.hz) : -1;
    status.gainReductionDb = engine_.gainReductionDb();
    status.ppq = engine_.lanePpq();
    status.lanePhase = engine_.lanePhases();
    status.laneValue = engine_.laneValues();
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
        if (xml->hasTagName(state.state.getType())) state.replaceState(juce::ValueTree::fromXml(*xml));
}

}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tf::PluginProcessor(); }
