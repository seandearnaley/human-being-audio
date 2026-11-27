// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "cl/groove_json.h"

#include <algorithm>

namespace cl {

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "Cutline", makeParameterLayout()), curves(state), patterns(state),
      presets(state, curves, patterns), values_(state) {}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    sampleRate_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
    monoScratchR_.assign(static_cast<std::size_t>(std::max(4096, samplesPerBlock)), 0.0f);
    bypassMidi_.ensureSize(4096);
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

// Ingests live incoming MIDI notes with a sample-accurate PPQ timestamp, for Pad MIDI and the MIDI Slicer.
void PluginProcessor::ingestMidi(const juce::MidiBuffer& midi, const Transport& transport) {
    const double beatsPerSample = std::clamp(transport.bpm, 20.0, 400.0) / 60.0 / sampleRate_;
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
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    const Transport transport = readTransport(numSamples);
    ingestMidi(midi, transport);
    // A note that passed through while bypassed ends at the instrument, not in Cutline.
    struct Release {
        std::array<juce::uint8, 3> bytes{};
        int sample = 0;
    };
    std::array<Release, 64> releases{};
    std::size_t releaseCount = 0;
    for (const auto metadata : midi) {
        if (releaseCount >= releases.size() || metadata.numBytes != 3)
            continue;
        const auto msg = metadata.getMessage();
        if (!msg.isNoteOff())
            continue;
        auto& held = passedThrough_[static_cast<std::size_t>(msg.getChannel() - 1)];
        const auto note = static_cast<std::size_t>(msg.getNoteNumber());
        if (!held.test(note))
            continue;
        held.reset(note);
        Release& r = releases[releaseCount++];
        std::copy_n(metadata.data, 3, r.bytes.begin());
        r.sample = metadata.samplePosition;
    }
    midi.clear();
    for (std::size_t i = 0; i < releaseCount; ++i)
        midi.addEvent(releases[i].bytes.data(), 3, releases[i].sample);

    const Params params = values_.snapshot();
    if (curves.pull(audioCurves_))
        engine_.setLaneCurves(audioCurves_);
    if (patterns.pull(audioGroove_))
        engine_.setGroove(audioGroove_);

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

    telemetry.publish(engine_, transport.bpm,
                      params.choice(ParamId::engineMode) == static_cast<int>(EngineMode::slicer));
}

// Bypassed: every note Cutline has sounding gets its note-off first, at the block's first sample, so a slicer or drum
// note never hangs; then the incoming MIDI passes through. The input still reaches the engine, so a chord
// released while bypassed is not held when Cutline comes back.
void PluginProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int numSamples = buffer.getNumSamples();
    ingestMidi(midi, readTransport(std::max(0, numSamples)));
    engine_.releaseAll(outEvents_);
    // The input, set aside so the note-offs go first. A note-off passes only for a note whose note-on passed through
    // while bypassed: Cutline consumed the others, so the instrument never had them.
    bypassMidi_.clear();
    for (const auto metadata : midi) {
        if (metadata.numBytes == 3) {
            const auto msg = metadata.getMessage();
            auto& held = passedThrough_[static_cast<std::size_t>(msg.getChannel() - 1)];
            const auto note = static_cast<std::size_t>(msg.getNoteNumber());
            if (msg.isNoteOn()) {
                held.set(note);
            } else if (msg.isNoteOff()) {
                if (!held.test(note))
                    continue;
                held.reset(note);
            }
        }
        bypassMidi_.addEvent(metadata.data, metadata.numBytes, metadata.samplePosition);
    }
    midi.clear();
    for (int i = 0; i < outEvents_.count; ++i) {
        const auto& ev = outEvents_.events[static_cast<std::size_t>(i)];
        if (ev.size > 0 && ev.size <= 3)
            midi.addEvent(ev.data, ev.size, 0);
    }
    midi.addEvents(bypassMidi_, 0, -1, 0);
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() { return new PluginEditor(*this); }

namespace {
const juce::Identifier kFormatProp{"format"};
} // namespace

// Saving does not change the plugin: hosts call this often. The format and the bank are stamped on a COPY of the
// state, so saving neither edits the live state nor republishes the bank to the audio thread. It is not a full
// guarantee for a call off the message thread: copyState() takes the value tree's lock, but the stores' setProperty
// calls on the message thread do not, so a save that races an edit can read the tree mid-change.
void PluginProcessor::getStateInformation(juce::MemoryBlock& destination) {
    juce::ValueTree copy = state.copyState();
    copy.setProperty(kFormatProp, kGrooveFormat, nullptr);
    copy.setProperty(kGrooveBankProp, toJuce(serialiseGrooveBank(patterns.bank())), nullptr);
    if (const auto xml = copy.createXml())
        copyXmlToBinary(*xml, destination);
}

// Cutline 3 state carries format="3". Anything else (a Cutline 2 session, or bytes that do not parse)
// loads the default state, as the spec says.
void PluginProcessor::setStateInformation(const void* data, int size) {
    const auto xml = getXmlFromBinary(data, size);
    const bool current = xml != nullptr && xml->hasTagName(state.state.getType()) &&
                         xml->getIntAttribute(kFormatProp.toString(), 0) >= kGrooveFormat;
    if (current) {
        state.replaceState(juce::ValueTree::fromXml(*xml));
        curves.reloadFromState();
        if (patterns.reloadFromState())
            return;
    }
    loadDefaultState();
}

void PluginProcessor::loadDefaultState() {
    for (const auto& spec : kParams) {
        if (auto* parameter = state.getParameter(toJuce(spec.key)))
            parameter->setValueNotifyingHost(parameter->getDefaultValue());
    }
    curves.setAll(defaultLaneCurves());
    patterns.clear();
    presets.clearCurrentName();
}

} // namespace cl

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new cl::PluginProcessor(); }
