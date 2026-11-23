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
    sampleRate_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
    blockSize_ = std::max(1, samplesPerBlock);
    monoScratchR_.assign(static_cast<std::size_t>(std::max(4096, blockSize_)), 0.0f);
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
        const int centered = message.getPitchWheelValue() - 8192;
        const float norm =
            centered >= 0 ? static_cast<float>(centered) / 8191.0f : static_cast<float>(centered) / 8192.0f;
        engine_.pitchBend(pitchBendRangeSt_ * norm);
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
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels < 1) return;

    const bool isMono = numChannels == 1;
    const Params params = values_.snapshot();
    if (curves.pull(audioCurves_)) engine_.setLaneCurves(audioCurves_);
    if (imprint.pull(audioImprint_)) engine_.setImprint(audioImprint_);

    const Transport blockStart = readTransport();
    const double quartersPerSample = blockStart.bpm / 60.0 / sampleRate_;

    const auto render = [&](int from, int to) {
        if (to <= from) return;
        const int maxChunk = isMono ? static_cast<int>(monoScratchR_.size()) : (to - from);
        int pos = from;
        while (pos < to) {
            const int chunk = std::min(maxChunk, to - pos);
            float* left = buffer.getWritePointer(0) + pos;
            float* right = isMono ? monoScratchR_.data() : (buffer.getWritePointer(1) + pos);
            if (isMono)
                std::copy_n(left, chunk, right);
            Transport transport = blockStart;
            transport.ppq += quartersPerSample * pos;
            engine_.process(params, transport, left, right, chunk);
            if (isMono) {
                for (int i = 0; i < chunk; ++i)
                    left[i] = 0.5f * (left[i] + right[i]);
            }
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

    if (isMono) {
        const float* left = buffer.getReadPointer(0);
        analysis.push(left, left, numSamples);
    } else {
        analysis.push(buffer.getReadPointer(0), buffer.getReadPointer(1), numSamples);
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
