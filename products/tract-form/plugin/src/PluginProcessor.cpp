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
    engine_.setPitchBendRange(pitchBendRangeSt_);
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
    if (message.isNoteOn()) {
        engine_.noteOn(message.getNoteNumber());
        lastNote_.store(message.getNoteNumber(), std::memory_order_relaxed);
    } else if (message.isNoteOff()) {
        engine_.noteOff(message.getNoteNumber());
    } else if (message.isPitchWheel()) {
        engine_.pitchBend(pitchBendRangeSt_ * static_cast<float>(message.getPitchWheelValue() - 8192) / 8192.0f);
    } else if (message.isController()) {
        const int ch = message.getChannel();
        const int cc = message.getControllerNumber();
        const int val = message.getControllerValue();
        if (ch >= 1 && ch <= 16) {
            if (const auto rpn = rpnDetector_.tryParse(ch, cc, val)) {
                if (!rpn->isNRPN && rpn->parameterNumber == 0) {
                    const float semitones = rpn->is14BitValue ? static_cast<float>(rpn->value >> 7) +
                                                                    static_cast<float>(rpn->value & 0x7f) / 100.0f
                                                              : static_cast<float>(rpn->value);
                    pitchBendRangeSt_ = std::clamp(semitones, 1.0f, 48.0f);
                    engine_.setPitchBendRange(pitchBendRangeSt_);
                }
            }
        }
        if (cc == 1) {
            engine_.setModWheel(static_cast<float>(val) / 127.0f);
        } else if (cc == 74) {
            engine_.setTimbre(val == 64 ? 0.5f : static_cast<float>(val) / 127.0f);
        } else if (cc == 121) {
            engine_.setModWheel(0.0f);
            engine_.setTimbre(0.5f);
            engine_.setAftertouch(0.0f);
            engine_.pitchBend(0.0f);
        } else if (message.isAllNotesOff() || message.isAllSoundOff()) {
            engine_.allNotesOff();
        }
    } else if (message.isChannelPressure()) {
        engine_.setAftertouch(static_cast<float>(message.getChannelPressureValue()) / 127.0f);
    } else if (message.isAftertouch()) {
        engine_.setAftertouch(static_cast<float>(message.getAfterTouchValue()) / 127.0f);
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
    if (curves.pull(audioCurves_)) engine_.setLaneCurves(audioCurves_);
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
        if (xml->hasTagName(state.state.getType())) {
            state.replaceState(juce::ValueTree::fromXml(*xml));
            curves.reloadFromState();
        }
}

}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new tf::PluginProcessor(); }
