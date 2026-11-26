// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <algorithm>

namespace cl {

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "Cutline", makeParameterLayout()), curves(state), patterns(state),
      presets(state, curves, patterns), values_(state) {}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    sampleRate_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
    monoScratchR_.assign(static_cast<std::size_t>(std::max(4096, samplesPerBlock)), 0.0f);
    engine_.prepare(sampleRate_, samplesPerBlock);
    standalonePpq_ = 0.0;
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (!layouts.getMainInputChannelSet().isDisabled())
        return false;
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono() || out.isDisabled();
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

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    const Transport transport = readTransport(numSamples);
    const double beatsPerSample = std::clamp(transport.bpm, 20.0, 400.0) / 60.0 / sampleRate_;

    // Ingest live incoming MIDI notes with sample-accurate PPQ timestamp for Performance Pads & Clip Slicer.
    for (const auto metadata : midi) {
        if (metadata.numBytes > 3)
            continue;
        const auto msg = metadata.getMessage();
        const double evPpq =
            std::max(0.0, transport.ppq + static_cast<double>(metadata.samplePosition) * beatsPerSample);
        if (msg.isNoteOn())
            engine_.inputNoteOn(msg.getNoteNumber(), msg.getVelocity(), evPpq);
        else if (msg.isNoteOff())
            engine_.inputNoteOff(msg.getNoteNumber(), evPpq);
        else if (msg.isSustainPedalOn())
            engine_.inputSustain(true);
        else if (msg.isSustainPedalOff())
            engine_.inputSustain(false);
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            engine_.inputAllNotesOff();
    }
    midi.clear();

    const Params params = values_.snapshot();
    if (curves.pull(audioCurves_))
        engine_.setLaneCurves(audioCurves_);
    if (patterns.pull(audioBank_))
        engine_.setPatternBank(audioBank_);

    const int numChannels = buffer.getNumChannels();
    const bool pureMidiHost = (numChannels < 1) || (wrapperType == wrapperType_AudioUnit);

    float* left = nullptr;
    float* right = nullptr;
    const bool isMono = !pureMidiHost && (numChannels == 1);

    if (!pureMidiHost) {
        if (isMono && numSamples > static_cast<int>(monoScratchR_.size())) {
            buffer.clear();
        } else {
            left = buffer.getWritePointer(0);
            right = isMono ? monoScratchR_.data() : buffer.getWritePointer(1);
        }
    } else if (numChannels > 0) {
        buffer.clear();
    }

    engine_.process(params, transport, outEvents_, left, right, numSamples);

    if (isMono && left != nullptr && right != nullptr) {
        for (int i = 0; i < numSamples; ++i)
            left[i] = 0.5f * (left[i] + right[i]);
    }

    for (int i = 0; i < outEvents_.count; ++i) {
        const auto& ev = outEvents_.events[static_cast<std::size_t>(i)];
        if (ev.size > 0 && ev.size <= 3)
            midi.addEvent(ev.data, ev.size, juce::jlimit(0, numSamples - 1, ev.sampleOffset));
    }

    telemetry.publish(engine_, transport.bpm);
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() { return new PluginEditor(*this); }

void PluginProcessor::getStateInformation(juce::MemoryBlock& destination) {
    if (const auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destination);
}

void PluginProcessor::setStateInformation(const void* data, int size) {
    if (const auto xml = getXmlFromBinary(data, size)) {
        if (xml->hasTagName(state.state.getType())) {
            state.replaceState(juce::ValueTree::fromXml(*xml));
            curves.reloadFromState();
            patterns.reloadFromState();
        }
    }
}

} // namespace cl

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new cl::PluginProcessor(); }
