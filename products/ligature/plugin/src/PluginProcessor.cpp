// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "lg/palette.h"

#include <algorithm>

namespace lg {

namespace {
const juce::Identifier kFormatProp{"format"};
const juce::Identifier kSongProp{"song"};
const juce::Identifier kPresetNameProp{"presetName"};
constexpr int kStateFormat = 2;
constexpr const char* kDefaultPreset = "Neon Pivot";

// Incoming notes C-1..B-1 perform: C-1..D#-1 launch sections A-D, E-1..G-1 tap bridges 1-4, G#-1..B-1 hold them.
constexpr int kLaunchBase = 0, kFireBase = 4, kHoldBase = 8, kControlTop = 11;
constexpr int kKeyChordLow = 60, kKeyChordHigh = 83;
} // namespace

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "Ligature", makeParameterLayout()), values_(state) {
    bypassMidi_.ensureSize(4096);
    const int index = presets.indexOf(kDefaultPreset);
    document_.load(index >= 0 ? presets.all()[static_cast<std::size_t>(index)] : defaultSong(), false);
    state.state.setProperty(kPresetNameProp, toJuce(document_.song().name), nullptr);
    songChanged();
    startTimerHz(20);
}

PluginProcessor::~PluginProcessor() {
    stopTimer();
    LinkHub::instance().releaseLeader(this);
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    sampleRate_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
#if LG_HAS_STUDIO_VOICES
    engine_.setVoiceRenderer(nullptr);
    voices_.prepare(sampleRate_, samplesPerBlock);
    engine_.setVoiceRenderer(&voices_);
#endif
    engine_.prepare(sampleRate_, samplesPerBlock);
    lastScene_ = values_.snapshot().choice(ParamId::scene);
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    if (!layouts.getMainInputChannelSet().isDisabled())
        return false;
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono() || out.isDisabled();
}

// MIDI input. `perform` is false while bypassed: held notes are still tracked for the played-chord name, but nothing
// reaches the engine. A follower takes no launches or new key chords (its leader decides), yet it always lets go.
void PluginProcessor::handleInput(const juce::MidiBuffer& midi, int numSamples, bool perform, bool leading) {
    const auto params = values_.snapshot();
    const bool keyChords = params.on(ParamId::keyChords);
    const bool act = perform && leading;
    bool chordChanged = false;
    for (const auto metadata : midi) {
        if (metadata.numBytes > 3)
            continue;
        const auto msg = metadata.getMessage();
        const int offset = std::clamp(metadata.samplePosition, 0, std::max(0, numSamples - 1));
        if (msg.isNoteOn()) {
            const int note = msg.getNoteNumber();
            if (note <= kControlTop) {
                if (!act)
                    continue;
                if (note < kFireBase)
                    engine_.command({CommandKind::launch, static_cast<std::int16_t>(note - kLaunchBase)});
                else if (note < kHoldBase)
                    engine_.command({CommandKind::fire, static_cast<std::int16_t>(note - kFireBase)});
                else
                    engine_.command({CommandKind::hold, static_cast<std::int16_t>(note - kHoldBase)});
            } else if (keyChords && note >= kKeyChordLow && note <= kKeyChordHigh) {
                if (act)
                    engine_.inputNote(note, msg.getVelocity(), true, offset);
            } else {
                heldInput_.set(static_cast<std::size_t>(note));
                chordChanged = true;
            }
        } else if (msg.isNoteOff()) {
            const int note = msg.getNoteNumber();
            if (note >= kHoldBase && note <= kControlTop) {
                if (act)
                    engine_.command({CommandKind::release, 0});
            } else {
                // Whatever One-key chords or the instance's role is now, a key chord it started must stop.
                if (perform && note >= kKeyChordLow && note <= kKeyChordHigh)
                    engine_.inputNote(note, 0, false, offset);
                heldInput_.reset(static_cast<std::size_t>(note));
            }
        } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
            if (perform)
                for (int n = kKeyChordLow; n <= kKeyChordHigh; ++n)
                    engine_.inputNote(n, 0, false, offset);
            heldInput_.reset();
        }
    }
    if (chordChanged && heldInput_.count() >= 3) {
        std::uint32_t mask = 0;
        int lowest = -1;
        for (int n = 0; n < 128; ++n)
            if (heldInput_.test(static_cast<std::size_t>(n))) {
                mask |= 1u << (n % 12);
                if (lowest < 0)
                    lowest = n;
            }
        const auto seq = ((heldChord_.load(std::memory_order_relaxed) >> 24u) + 1u) & 0xFFu;
        heldChord_.store(mask | (static_cast<std::uint32_t>(lowest % 12) << 12u) |
                             (static_cast<std::uint32_t>(heldInput_.count()) << 16u) | (seq << 24u),
                         std::memory_order_relaxed);
    }
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    const auto params = values_.snapshot();
    Transport transport;
    if (const auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition()) {
            transport.bpm = position->getBpm().orFallback(transport.bpm);
            transport.ppq = position->getPpqPosition().orFallback(0.0);
            transport.playing = position->getIsPlaying();
        }

    // A follower takes its performance from the leader only: its own launches would fight the leader's.
    const int role = params.choice(ParamId::role) - 1;
    const bool leading = role < 0;
    {
        const auto scope = commandFifo_.read(commandFifo_.getNumReady());
        scope.forEach([this, leading](int i) {
            if (leading)
                engine_.command(commands_[static_cast<std::size_t>(i)]);
        });
    }
    // Scene launches only while something plays: a host chasing automation on locate, or a session restore, must
    // not start the preview by itself.
    const int scene = params.choice(ParamId::scene);
    if (scene != lastScene_) {
        const bool running = transport.playing || engine_.telemetry().previewPlaying;
        if (scene > 0 && leading && running)
            engine_.command({CommandKind::launch, static_cast<std::int16_t>(scene - 1)});
        lastScene_ = scene;
    }
    if (!leading) {
        Performance p;
        if (LinkHub::instance().pullPerformance(p))
            engine_.follow(p);
    }
    handleInput(midi, numSamples, true, leading);
    midi.clear();
    // Back from bypass: notes that passed through to the instrument while bypassed are still on there, and their
    // note-offs will now be consumed here, so end them now.
    for (std::size_t ch = 0; ch < passedThrough_.size(); ++ch) {
        if (passedThrough_[ch].none())
            continue;
        for (int note = 0; note < 128; ++note)
            if (passedThrough_[ch].test(static_cast<std::size_t>(note)))
                midi.addEvent(juce::MidiMessage::noteOff(static_cast<int>(ch) + 1, note), 0);
        passedThrough_[ch].reset();
    }

    EngineParams ep;
    ep.landBars = landBars(params.choice(ParamId::land));
    ep.audition = params.on(ParamId::audition);
    ep.auditionGain = params[ParamId::level];
    ep.beat = params.on(ParamId::beat);
    ep.outputRole = role;
    ep.studioVoices = params.choice(ParamId::voices) == 1;

    const int numChannels = buffer.getNumChannels();
    const bool pureMidiHost = numChannels < 1 || wrapperType == wrapperType_AudioUnit;
    if (pureMidiHost)
        buffer.clear();
    float* left = pureMidiHost ? nullptr : buffer.getWritePointer(0);
    float* right = pureMidiHost ? nullptr : (numChannels > 1 ? buffer.getWritePointer(1) : buffer.getWritePointer(0));
    engine_.process(ep, transport, out_, left, right, numSamples);
    if (left != nullptr && left == right) // mono: both sides were added into the one channel, so halve it to (L+R)/2
        buffer.applyGain(0, 0, numSamples, 0.5f);
    for (int i = 0; i < out_.count; ++i) {
        const auto& ev = out_.events[static_cast<std::size_t>(i)];
        if (ev.size > 0 && ev.size <= 3)
            midi.addEvent(ev.data.data(), ev.size, std::clamp(ev.sampleOffset, 0, numSamples - 1));
    }

    if (leading)
        LinkHub::instance().publishPerformance(this, engine_.performance());
    const auto tele = engine_.telemetry();
    telemetry_.publish(tele);
    lastBpm_.store(tele.bpm, std::memory_order_relaxed);

    // Capture what was played, for the "Last performance" drag.
    const auto push = [this](const CaptureEvent& e) {
        const auto scope = captureFifo_.write(1);
        if (scope.blockSize1 > 0)
            captured_[static_cast<std::size_t>(scope.startIndex1)] = e;
    };
    // Stamped on the performance's own clock, which runs on through loop braces and jumps: a second pass of a loop
    // lands after the first instead of on top of it, and no note can end before it starts.
    const double beatsPerSample = tele.bpm / 60.0 / sampleRate_;
    if (tele.playing && !wasPlaying_) {
        captureBeat_ = tele.beat;
        push({captureBeat_, 0, 0, 0, true});
    }
    wasPlaying_ = tele.playing;
    for (int i = 0; i < out_.count; ++i) {
        const auto& ev = out_.events[static_cast<std::size_t>(i)];
        push({captureBeat_ + ev.sampleOffset * beatsPerSample, ev.data[0], ev.data[1], ev.data[2], false});
    }
    if (tele.playing)
        captureBeat_ += numSamples * beatsPerSample;
}

// Bypassed: what Ligature was playing ends, the incoming MIDI passes through untouched (as JUCE's default bypass and
// Cutline do), and held notes are still tracked so the played-chord name is right when it comes back.
void PluginProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    buffer.clear();
    handleInput(midi, std::max(1, buffer.getNumSamples()), false, true);
    engine_.releaseAll(out_);
    // The input, set aside so the note-offs go first. A note-off passes only for a note whose note-on passed through
    // while bypassed: Ligature consumed the others, so the instrument never had them.
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
    for (int i = 0; i < out_.count; ++i) {
        const auto& ev = out_.events[static_cast<std::size_t>(i)];
        if (ev.size > 0 && ev.size <= 3)
            midi.addEvent(ev.data.data(), ev.size, 0);
    }
    midi.addEvents(bypassMidi_, 0, -1, 0);
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() { return new PluginEditor(*this); }

void PluginProcessor::publishSong() {
    compileSong(document_.song(), engine_.songBuffer().write(), document_.revision());
    engine_.songBuffer().publish();
    publishedRevision_ = document_.revision();
}

void PluginProcessor::songChanged() {
    publishSong();
    const auto json = serialiseSong(document_.song());
    state.state.setProperty(kSongProp, juce::String::fromUTF8(json.c_str()), nullptr);
    // Only an instance that already leads shares its song. Claiming happens on the timer, never here: a new instance
    // must not take the lead (and push the default song to followers) before the host restores its state.
    if (outputRole() < 0 && LinkHub::instance().isLeader(this))
        LinkHub::instance().publishSong(this, json, document_.revision());
}

void PluginProcessor::loadPreset(int index) {
    const auto& all = presets.all();
    if (index < 0 || index >= static_cast<int>(all.size()))
        return;
    document_.load(all[static_cast<std::size_t>(index)], true);
    state.state.setProperty(kPresetNameProp, toJuce(all[static_cast<std::size_t>(index)].name), nullptr);
    songChanged();
}

std::string PluginProcessor::presetName() const {
    return state.state.getProperty(kPresetNameProp, kDefaultPreset).toString().toStdString();
}

void PluginProcessor::pushCommand(const Command& c) {
    const auto scope = commandFifo_.write(1);
    if (scope.blockSize1 > 0)
        commands_[static_cast<std::size_t>(scope.startIndex1)] = c;
}

float PluginProcessor::paramValue(ParamId id) const {
    if (auto* p = state.getParameter(toJuce(kParams[static_cast<std::size_t>(id)].key)))
        return p->convertFrom0to1(p->getValue());
    return kParams[static_cast<std::size_t>(id)].def;
}

void PluginProcessor::setParamValue(ParamId id, float value) {
    if (auto* p = state.getParameter(toJuce(kParams[static_cast<std::size_t>(id)].key)))
        p->setValueNotifyingHost(p->convertTo0to1(value));
}

int PluginProcessor::outputRole() const { return static_cast<int>(paramValue(ParamId::role) + 0.5f) - 1; }

void PluginProcessor::setOutputRole(int role) {
    setParamValue(ParamId::role, static_cast<float>(std::clamp(role, -1, kRoleCount - 1) + 1));
    if (role >= 0) {
        LinkHub::instance().releaseLeader(this);
        followerSongGeneration_ = 0; // take the leader's song at the next tick
    } else {
        songChanged();
    }
}

bool PluginProcessor::leaderFound() const { return outputRole() < 0 || LinkHub::instance().hasLeader(); }

std::optional<Chord> PluginProcessor::playedChord() const {
    const auto packed = heldChord_.load(std::memory_order_relaxed);
    if (((packed >> 16u) & 0xFFu) < 3u)
        return std::nullopt;
    return identifyChord(static_cast<std::uint16_t>(packed & 0xFFFu), static_cast<int>((packed >> 12u) & 0xFu));
}

void PluginProcessor::drainCapture() {
    const auto scope = captureFifo_.read(captureFifo_.getNumReady());
    const int role = outputRole();
    scope.forEach([&](int i) {
        const auto& e = captured_[static_cast<std::size_t>(i)];
        if (e.reset) {
            capture_.clear();
            captureOpen_.clear();
            return;
        }
        const int channel = e.status & 0x0F;
        const bool on = (e.status & 0xF0) == 0x90 && e.velocity > 0;
        const bool off = (e.status & 0xF0) == 0x80 || ((e.status & 0xF0) == 0x90 && e.velocity == 0);
        const auto key = std::make_pair(channel, static_cast<int>(e.pitch));
        if (on) {
            captureOpen_[key] = {e.beat, e.velocity};
        } else if (off) {
            const auto it = captureOpen_.find(key);
            if (it == captureOpen_.end())
                return;
            if (capture_.size() < 20000) {
                const Role r =
                    role >= 0 ? static_cast<Role>(role) : static_cast<Role>(std::clamp(channel, 0, kRoleCount - 1));
                capture_.push_back({it->second.first, e.beat, e.pitch, it->second.second, r});
            }
            captureOpen_.erase(it);
        }
    });
}

void PluginProcessor::timerCallback() { serviceMessageThread(); }

void PluginProcessor::serviceMessageThread() {
    drainCapture();
    auto& hub = LinkHub::instance();
    if (outputRole() >= 0) {
        hub.releaseLeader(this);
        if (const auto json = hub.songIfNewer(followerSongGeneration_))
            if (auto song = parseSong(*json)) {
                document_.mirror(*song);
                publishSong();
                state.state.setProperty(kSongProp, juce::String::fromUTF8(json->c_str()), nullptr);
            }
    } else if (!hub.isLeader(this) && hub.claimLeader(this)) {
        hub.publishSong(this, serialiseSong(document_.song()), document_.revision());
    }
}

// Saving never changes the plugin: the song is already in the state tree; the format is stamped on a copy.
void PluginProcessor::getStateInformation(juce::MemoryBlock& destination) {
    juce::ValueTree copy = state.copyState();
    copy.setProperty(kFormatProp, kStateFormat, nullptr);
    if (const auto xml = copy.createXml())
        copyXmlToBinary(*xml, destination);
}

// Ligature 2 state carries format="2". Anything else (a v1 session, or bytes that do not parse) loads the default
// song.
void PluginProcessor::setStateInformation(const void* data, int size) {
    const auto xml = getXmlFromBinary(data, size);
    if (xml != nullptr && xml->hasTagName(state.state.getType()) &&
        xml->getIntAttribute(kFormatProp.toString(), 0) >= kStateFormat) {
        state.replaceState(juce::ValueTree::fromXml(*xml));
        if (auto song = parseSong(state.state.getProperty(kSongProp).toString().toStdString())) {
            document_.load(*song, false);
            songChanged();
            return;
        }
    }
    const int index = presets.indexOf(kDefaultPreset);
    document_.load(index >= 0 ? presets.all()[static_cast<std::size_t>(index)] : defaultSong(), false);
    state.state.setProperty(kPresetNameProp, toJuce(document_.song().name), nullptr);
    songChanged();
}

} // namespace lg

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new lg::PluginProcessor(); }
