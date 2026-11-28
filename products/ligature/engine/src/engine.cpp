// SPDX-License-Identifier: AGPL-3.0-or-later
#include "lg/engine.h"

#include "sketch_synth.h"

#include <algorithm>
#include <cmath>

namespace lg {

namespace {

constexpr double kEps = 1e-7;
constexpr int kMaxSounding = 256;
constexpr int kMaxPending = 32;
constexpr int kMaxSynthEvents = 1024;

struct Sounding {
    std::uint8_t channel = 0;
    std::uint8_t pitch = 0;
    std::uint8_t role = 0;
    double end = 0.0;
};

struct SynthEvent {
    int offset = 0;
    std::uint8_t kind = 0; // 0 off, 1 on, 2 drum
    std::uint8_t role = 0;
    std::uint8_t pitch = 0;
    float velocity = 0.0f;
};

double positiveMod(double x, double m) noexcept LG_NONBLOCKING {
    const double r = std::fmod(x, m);
    return r < 0.0 ? r + m : r;
}

} // namespace

struct Engine::Impl {
    double sampleRate = 48000.0;
    SketchSynth synth;

    // Clock
    bool wasPlaying = false;
    bool flushSounding = false; // after reset(): end what was sounding at the next block, which has an output
    double expected = 0.0;      // where the next block should start if nothing jumped
    bool previewPlaying = false;
    double previewBeat = 0.0;
    double blockBeats = 0.0; // the current block's length in beats

    Performance perf;
    std::uint64_t lastFollowed = 0;

    std::array<Command, kMaxPending> pending{};
    int pendingCount = 0;

    std::array<Sounding, kMaxSounding> sounding{};
    int soundingCount = 0;

    std::array<SynthEvent, kMaxSynthEvents> synthEvents{};
    int synthCount = 0;

    int auditionRemaining = 0; // samples until an auditioned chord is released

    struct InputEvent {
        int offset = 0;
        std::uint8_t note = 0;
        std::uint8_t velocity = 0;
        bool on = false;
    };
    std::array<InputEvent, 64> inputs{};
    int inputCount = 0;
    std::array<std::array<std::uint8_t, 6>, 128> keyNotes{}; // the notes each held key started
    std::array<std::uint8_t, 128> keyNoteCount{};

    // Per-block context
    const CompiledSong* song = nullptr;
    EngineParams params;
    MidiOutBuffer* out = nullptr;
    double beat0 = 0.0;
    double beatsPerSample = 0.0;
    int numSamples = 0;
    double lastStepBeat = -1.0; // the last 16th the drums fired on

    EngineTelemetry tele;

    int offsetOf(double beat) const noexcept LG_NONBLOCKING {
        const double s = (beat - beat0) / beatsPerSample;
        return std::clamp(static_cast<int>(std::floor(s + 1e-6)), 0, std::max(0, numSamples - 1));
    }

    void touch() noexcept LG_NONBLOCKING { ++perf.generation; }

    bool hasSong() const noexcept LG_NONBLOCKING { return song != nullptr && song->sectionCount > 0; }

    bool validItem(ItemRef r) const noexcept LG_NONBLOCKING {
        if (!hasSong())
            return false;
        return r.isBridge() ? r.index >= 0 && r.index < song->bridgeCount
                            : r.index >= 0 && r.index < song->sectionCount;
    }

    int itemBars(ItemRef r, int fit) const noexcept LG_NONBLOCKING {
        if (r.isBridge())
            return fit > 0 ? std::min(fit, song->barsOf(r)) : song->barsOf(r); // the bridge may have been shortened
        return song->barsOf(r);
    }

    void synthEvent(int offset, std::uint8_t kind, int role, int pitch, float velocity) noexcept LG_NONBLOCKING {
        if (synthCount >= kMaxSynthEvents)
            return;
        synthEvents[static_cast<std::size_t>(synthCount++)] = {offset, kind, static_cast<std::uint8_t>(role),
                                                               static_cast<std::uint8_t>(pitch), velocity};
    }

    void noteOff(int index, int offset) noexcept LG_NONBLOCKING {
        const auto s = sounding[static_cast<std::size_t>(index)];
        out->push(offset, static_cast<std::uint8_t>(0x80u | s.channel), s.pitch, 0);
        synthEvent(offset, 0, s.role, s.pitch, 0.0f);
        sounding[static_cast<std::size_t>(index)] = sounding[static_cast<std::size_t>(--soundingCount)];
    }

    void endAll(double beat) noexcept LG_NONBLOCKING {
        const int offset = offsetOf(beat);
        while (soundingCount > 0)
            noteOff(soundingCount - 1, offset);
    }

    void endDue(double untilBeat) noexcept LG_NONBLOCKING {
        for (int i = soundingCount - 1; i >= 0; --i)
            if (sounding[static_cast<std::size_t>(i)].end <= untilBeat + kEps)
                noteOff(i, offsetOf(sounding[static_cast<std::size_t>(i)].end));
    }

    void noteOn(int role, int pitch, int velocity, double onBeat, double offBeat) noexcept LG_NONBLOCKING {
        if (!song->audible[static_cast<std::size_t>(role)])
            return;
        if (params.outputRole >= 0 && role != params.outputRole)
            return;
        const auto channel = static_cast<std::uint8_t>(params.outputRole >= 0 ? 0 : role);
        const int offset = offsetOf(onBeat);
        for (int i = soundingCount - 1; i >= 0; --i) { // a repeated note ends the one still ringing first
            const auto& s = sounding[static_cast<std::size_t>(i)];
            if (s.channel == channel && s.pitch == pitch)
                noteOff(i, offset);
        }
        if (soundingCount >= kMaxSounding)
            noteOff(0, offset);
        out->push(offset, static_cast<std::uint8_t>(0x90u | channel), static_cast<std::uint8_t>(pitch),
                  static_cast<std::uint8_t>(std::clamp(velocity, 1, 127)));
        sounding[static_cast<std::size_t>(soundingCount++)] = {channel, static_cast<std::uint8_t>(pitch),
                                                               static_cast<std::uint8_t>(role), offBeat};
        synthEvent(offset, 1, role, pitch, static_cast<float>(velocity) / 127.0f);
    }

    // A note that lasts until it is ended explicitly (one-key chords, auditions).
    void heldNoteOn(int role, int pitch, int velocity, int offset) noexcept LG_NONBLOCKING {
        const auto channel = static_cast<std::uint8_t>(role);
        for (int i = soundingCount - 1; i >= 0; --i) {
            const auto& snd = sounding[static_cast<std::size_t>(i)];
            if (snd.channel == channel && snd.pitch == pitch)
                noteOff(i, offset);
        }
        if (soundingCount >= kMaxSounding)
            noteOff(0, offset);
        out->push(offset, static_cast<std::uint8_t>(0x90u | channel), static_cast<std::uint8_t>(pitch),
                  static_cast<std::uint8_t>(std::clamp(velocity, 1, 127)));
        sounding[static_cast<std::size_t>(soundingCount++)] = {channel, static_cast<std::uint8_t>(pitch),
                                                               static_cast<std::uint8_t>(role), 1e18};
        synthEvent(offset, 1, role, pitch, static_cast<float>(velocity) / 127.0f);
    }

    void heldNoteOff(int role, int pitch, int offset) noexcept LG_NONBLOCKING {
        for (int i = soundingCount - 1; i >= 0; --i) {
            const auto& snd = sounding[static_cast<std::size_t>(i)];
            if (snd.channel == role && snd.pitch == pitch && snd.end > 1e17) {
                noteOff(i, offset);
                return;
            }
        }
    }

    // Does a key other than `key` hold `pitch` in the same role (bass is a key's first note)?
    bool heldByOtherKey(int key, bool bass, std::uint8_t pitch) const noexcept LG_NONBLOCKING {
        for (int other = 0; other < 128; ++other) {
            if (other == key)
                continue;
            const int count = keyNoteCount[static_cast<std::size_t>(other)];
            for (int k = bass ? 0 : 1; k < (bass ? std::min(1, count) : count); ++k)
                if (keyNotes[static_cast<std::size_t>(other)][static_cast<std::size_t>(k)] == pitch)
                    return true;
        }
        return false;
    }

    int keySection() const noexcept LG_NONBLOCKING {
        if (!hasSong())
            return 0;
        if (perf.item.isBridge() && perf.item.index < song->bridgeCount)
            return song->bridges[static_cast<std::size_t>(perf.item.index)].to;
        return std::clamp(perf.item.index, 0, song->sectionCount - 1);
    }

    void handleInputs() noexcept LG_NONBLOCKING {
        constexpr std::array<int, 12> kDegree{0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
        for (int i = 0; i < inputCount; ++i) {
            const auto& e = inputs[static_cast<std::size_t>(i)];
            const int offset = std::clamp(e.offset, 0, std::max(0, numSamples - 1));
            auto& held = keyNotes[e.note];
            auto& heldCount = keyNoteCount[e.note];
            if (!e.on) {
                // Legato playing: a note the next chord shares was retriggered by it and stays.
                for (int k = 0; k < heldCount; ++k)
                    if (!heldByOtherKey(e.note, k == 0, held[static_cast<std::size_t>(k)]))
                        heldNoteOff(k == 0 ? static_cast<int>(Role::bass) : static_cast<int>(Role::chords),
                                    held[static_cast<std::size_t>(k)], offset);
                heldCount = 0;
                continue;
            }
            if (!hasSong() || params.outputRole >= 0 || e.note < 60 || e.note > 83)
                continue;
            const int degree = kDegree[static_cast<std::size_t>(e.note % 12)];
            if (degree < 0)
                continue;
            const auto& kc = song->keyChords[static_cast<std::size_t>(keySection())];
            const int count = kc.count[static_cast<std::size_t>(degree)];
            const int lift = e.note >= 72 ? 12 : 0;
            heldCount = 0;
            for (int k = 0; k < count && k < 6; ++k) {
                const int role = k == 0 ? static_cast<int>(Role::bass) : static_cast<int>(Role::chords);
                const int pitch = std::clamp(kc.notes[static_cast<std::size_t>(degree)][static_cast<std::size_t>(k)] +
                                                 (k == 0 ? 0 : lift),
                                             0, 127);
                heldNoteOn(role, pitch, e.velocity, offset);
                held[static_cast<std::size_t>(heldCount++)] = static_cast<std::uint8_t>(pitch);
            }
        }
        inputCount = 0;
    }

    void audition(const Command& c) noexcept LG_NONBLOCKING {
        const ItemRef item =
            c.index >= kPreviewBridgeBase ? ItemRef::bridge(c.index - kPreviewBridgeBase) : ItemRef::section(c.index);
        if (!validItem(item))
            return;
        endAll(beat0);
        const auto& pattern = song->pattern(item, 0);
        float first = -1.0f;
        for (int i = 0; i < pattern.count; ++i) {
            const auto& n = pattern.notes[static_cast<std::size_t>(i)];
            if (n.span == c.span && n.role == static_cast<std::uint8_t>(Role::chords)) {
                first = n.start;
                break;
            }
        }
        bool bassDone = false;
        for (int i = 0; i < pattern.count; ++i) {
            const auto& n = pattern.notes[static_cast<std::size_t>(i)];
            if (n.span != c.span)
                continue;
            if (n.role == static_cast<std::uint8_t>(Role::chords) && std::abs(n.start - first) < 1e-4f)
                heldNoteOn(n.role, n.pitch, 96, 0);
            else if (n.role == static_cast<std::uint8_t>(Role::bass) && !bassDone) {
                heldNoteOn(n.role, n.pitch, 100, 0);
                bassDone = true;
            }
        }
        auditionRemaining = static_cast<int>(sampleRate * 1.1);
    }

    // Plays the notes of `pattern` whose start falls in [p0, p1) beats of pattern time; `base` is the absolute beat
    // of pattern time 0.
    void scan(const CompiledPattern& pattern, double p0, double p1, double base) noexcept LG_NONBLOCKING {
        const float s0 = static_cast<float>(p0 * 4.0), s1 = static_cast<float>(p1 * 4.0);
        int lo = 0, hi = pattern.count;
        while (lo < hi) {
            const int mid = (lo + hi) / 2;
            if (pattern.notes[static_cast<std::size_t>(mid)].start < s0 - 1e-5f)
                lo = mid + 1;
            else
                hi = mid;
        }
        for (int i = lo; i < pattern.count; ++i) {
            const auto& n = pattern.notes[static_cast<std::size_t>(i)];
            if (n.start >= s1 - 1e-5f)
                break;
            const double onBeat = base + n.start / 4.0;
            endDue(onBeat); // a repeat inside one block must not end its predecessor late
            noteOn(n.role, n.pitch, n.velocity, onBeat, base + n.end / 4.0);
        }
    }

    void drumsFor(const CompiledPattern& pattern, double t0, double t1, double origin,
                  bool inBridge) noexcept LG_NONBLOCKING {
        if (!params.beat || !params.audition)
            return;
        const double L = pattern.bars * 16.0;
        for (double b = std::ceil(t0 * 4.0 - 1e-6) / 4.0; b < t1 - kEps; b += 0.25) {
            if (b <= lastStepBeat + kEps)
                continue;
            lastStepBeat = b;
            const double stepInItem = positiveMod((b - origin) * 4.0, L);
            const int s = static_cast<int>(std::lround(stepInItem)) % static_cast<int>(L);
            const int offset = offsetOf(b);
            if (inBridge && pattern.slamTail && s >= static_cast<int>(L) - 4)
                continue;
            const bool lastBar = inBridge && s >= static_cast<int>(L) - 16 && !perf.hold;
            if (s % 4 == 0)
                synthEvent(offset, 2, static_cast<int>(DrumKind::kick), 0, 1.0f);
            if (s % 4 == 2)
                synthEvent(offset, 2, static_cast<int>(DrumKind::hat), 0, 1.0f);
            else if (s % 2 == 1)
                synthEvent(offset, 2, static_cast<int>(DrumKind::hat), 0, 0.35f);
            if (lastBar && s >= static_cast<int>(L) - 8) {
                if (s % 2 == 0 || s >= static_cast<int>(L) - 4)
                    synthEvent(offset, 2, static_cast<int>(DrumKind::clap), 0,
                               0.45f + static_cast<float>(s - (static_cast<int>(L) - 8)) / 14.0f);
            } else if (inBridge && perf.hold) {
                if (s % 2 == 0)
                    synthEvent(offset, 2, static_cast<int>(DrumKind::clap), 0,
                               0.4f + static_cast<float>(s % 16) / 30.0f);
            } else if (s % 16 == 4 || s % 16 == 12) {
                synthEvent(offset, 2, static_cast<int>(DrumKind::clap), 0, 0.8f);
            }
        }
    }

    // Emits everything the playing item plays in [t0, t1).
    void play(double t0, double t1) noexcept LG_NONBLOCKING {
        if (!validItem(perf.item) || t1 <= t0)
            return;
        const auto& pattern = song->pattern(perf.item, perf.fit);
        const double len = pattern.bars * 4.0;
        if (len <= 0.0)
            return;
        if (perf.item.isBridge()) {
            scan(pattern, t0 - perf.anchor, t1 - perf.anchor, perf.anchor);
            drumsFor(pattern, t0, t1, perf.anchor, true);
            return;
        }
        double p0 = positiveMod(t0 - perf.anchor, len);
        double base = t0 - p0;
        double remaining = t1 - t0;
        while (remaining > kEps) {
            const double chunk = std::min(remaining, len - p0);
            scan(pattern, p0, p0 + chunk, base);
            remaining -= chunk;
            base += len;
            p0 = 0.0;
        }
        drumsFor(pattern, t0, t1, perf.anchor, false);
    }

    void enterSection(int section, double at) noexcept LG_NONBLOCKING {
        perf.item = ItemRef::section(std::clamp(section, 0, std::max(0, song->sectionCount - 1)));
        perf.anchor = at;
        perf.fit = 0;
        perf.target = -1;
        perf.hold = false;
        touch();
    }

    // The playing item by identity (Section::uid), read before a new song replaces the old one.
    struct Identity {
        std::uint32_t item = 0; // the playing section
        std::uint32_t from = 0; // the playing bridge's ends
        std::uint32_t to = 0;
        std::uint32_t target = 0; // where a bridge or plan lands
        std::uint32_t plan = 0;
    };

    std::uint32_t uidOf(int section) const noexcept LG_NONBLOCKING {
        return section >= 0 && section < song->sectionCount ? song->sectionUid[static_cast<std::size_t>(section)] : 0u;
    }

    int sectionWithUid(std::uint32_t uid) const noexcept LG_NONBLOCKING {
        for (int i = 0; i < song->sectionCount; ++i)
            if (uid != 0 && song->sectionUid[static_cast<std::size_t>(i)] == uid)
                return i;
        return -1;
    }

    Identity identity() const noexcept LG_NONBLOCKING {
        Identity id;
        if (!hasSong())
            return id;
        if (!perf.item.isBridge()) {
            id.item = uidOf(perf.item.index);
        } else if (perf.item.index >= 0 && perf.item.index < song->bridgeCount) {
            const auto& b = song->bridges[static_cast<std::size_t>(perf.item.index)];
            id.from = uidOf(b.from);
            id.to = uidOf(b.to);
        }
        id.target = uidOf(perf.target);
        id.plan = perf.planActive ? uidOf(perf.planTarget) : 0u;
        return id;
    }

    // After a new song arrives: a section added or removed elsewhere must not move playback onto another section.
    // A removed section gives way to the one now at its place, on the same bar grid; a bridge whose ends changed
    // lands. Followers take their positions from the leader instead.
    void reidentify(const Identity& was) noexcept LG_NONBLOCKING {
        if (!hasSong() || params.outputRole >= 0)
            return;
        bool moved = false;
        if (was.item != 0) {
            const int now = sectionWithUid(was.item);
            if (now < 0) {
                perf.item = ItemRef::section(std::clamp(perf.item.index, 0, song->sectionCount - 1));
                perf.fit = 0;
                moved = true;
            } else if (now != perf.item.index) {
                perf.item = ItemRef::section(now);
                moved = true;
            }
        } else if (was.from != 0) {
            const int from = sectionWithUid(was.from);
            const int to = sectionWithUid(was.to);
            const int bridge = from >= 0 && to >= 0 ? song->bridgeIndex(from, to) : -1;
            if (bridge < 0) { // the bridge no longer exists: land where it was going, or where it would have
                const int target = sectionWithUid(was.target);
                enterSection(target >= 0 ? target : (to >= 0 ? to : 0), std::floor(beat0 / 4.0) * 4.0);
                endAll(beat0);
                moved = true;
            } else if (bridge != perf.item.index) {
                perf.item = ItemRef::bridge(bridge);
                moved = true;
            }
        }
        if (perf.target >= 0 && was.target != 0 && perf.item.isBridge()) {
            const int target = sectionWithUid(was.target);
            if (target != perf.target) {
                perf.target = target; // -1: the bridge's own end
                moved = true;
            }
        }
        // A queued launch whose section, origin or bridge changed is dropped: the user launches it again.
        const bool planBridgeGone =
            perf.planBridge >= 0 && (perf.planBridge >= song->bridgeCount ||
                                     song->bridges[static_cast<std::size_t>(perf.planBridge)].to != perf.planTarget);
        if (perf.planActive && (moved || planBridgeGone || sectionWithUid(was.plan) != perf.planTarget)) {
            perf.planActive = false;
            moved = true;
        }
        if (moved)
            touch();
    }

    // The start of the section pass that contains `beat`.
    double passStart(double beat) const noexcept LG_NONBLOCKING {
        const double len = itemBars(perf.item, perf.fit) * 4.0;
        if (perf.item.isBridge())
            return perf.anchor;
        return beat - positiveMod(beat - perf.anchor, len);
    }

    // Times a bridge (or a direct launch) backwards from where `target` lands on the Land grid.
    void plan(int bridge, int target, int landBars, double beat) noexcept LG_NONBLOCKING {
        const double origin = passStart(beat);
        const int nextBar = static_cast<int>(std::floor((beat - origin) / 4.0 + 1e-9)) + 1;
        const int full = bridge >= 0 ? song->bridges[static_cast<std::size_t>(bridge)].bars : 0;
        int start = nextBar, fit = full;
        if (landBars > 1) {
            int landing = static_cast<int>(std::ceil(static_cast<double>(nextBar) / landBars)) * landBars;
            if (full > 0 && landing - nextBar < 1) // a bridge needs at least a bar before the landing
                landing += landBars;
            start = std::max(nextBar, landing - full);
            fit = landing - start;
        }
        perf.planActive = true;
        perf.planBridge = bridge >= 0 && fit > 0 ? bridge : -1;
        perf.planTarget = target;
        perf.planStart = origin + start * 4.0;
        perf.planFit = std::clamp(fit, 0, kMaxBridgeBars);
        touch();
    }

    void startPreview(ItemRef from) noexcept LG_NONBLOCKING {
        endAll(beat0); // a preview restarted while one plays: the old clock's notes end now
        previewPlaying = true;
        previewBeat = 0.0;
        perf.item = validItem(from) ? from : ItemRef::section(0);
        perf.anchor = 0.0;
        perf.fit = 0;
        perf.target = perf.item.isBridge() ? song->bridges[static_cast<std::size_t>(perf.item.index)].to : -1;
        perf.planActive = false;
        perf.started = true;
        wasPlaying = false; // the next block starts fresh at beat 0 without re-anchoring
        touch();
    }

    void apply(const Command& c, double beat, bool playing) noexcept LG_NONBLOCKING {
        if (!hasSong())
            return;
        switch (c.kind) {
        case CommandKind::previewPlay:
            startPreview(c.index >= kPreviewBridgeBase ? ItemRef::bridge(c.index - kPreviewBridgeBase)
                                                       : ItemRef::section(c.index));
            return;
        case CommandKind::previewStop:
            previewPlaying = false;
            return;
        case CommandKind::audition:
            if (!playing)
                audition(c);
            return;
        case CommandKind::release:
            if (perf.hold) {
                perf.hold = false;
                touch();
            }
            return;
        default:
            break;
        }

        if (!playing) { // launching while stopped starts the preview there
            if (c.kind == CommandKind::fire || c.kind == CommandKind::hold) {
                if (c.index >= 0 && c.index < song->bridgeCount) {
                    startPreview(ItemRef::bridge(c.index));
                    perf.hold = c.kind == CommandKind::hold;
                }
            } else if (c.index >= 0 && c.index < song->sectionCount) {
                startPreview(ItemRef::section(c.index));
            }
            return;
        }

        switch (c.kind) {
        case CommandKind::launch:
        case CommandKind::launchDirect: {
            const int target = c.index;
            if (target < 0 || target >= song->sectionCount)
                return;
            if (perf.item.isBridge()) { // a bridge in flight lands where it is told
                perf.target = target;
                touch();
                return;
            }
            if (perf.item.index == target) { // launching the playing section cancels a queued launch
                perf.planActive = false;
                touch();
                return;
            }
            const int bridge = c.kind == CommandKind::launch ? song->bridgeIndex(perf.item.index, target) : -1;
            plan(bridge, target, params.landBars, beat);
            return;
        }
        case CommandKind::fire:
        case CommandKind::hold: {
            const int bridge = c.index;
            if (bridge < 0 || bridge >= song->bridgeCount)
                return;
            const bool hold = c.kind == CommandKind::hold;
            if (perf.item == ItemRef::bridge(bridge)) {
                if (hold) {
                    perf.hold = true;
                    touch();
                }
                return;
            }
            if (perf.item.isBridge()) // another bridge is playing: let it land first
                return;
            plan(bridge, song->bridges[static_cast<std::size_t>(bridge)].to, hold ? 1 : params.landBars, beat);
            perf.hold = hold;
            return;
        }
        default:
            return;
        }
    }

    // Where the playing bridge ends: the end of its first bar while held (it loops there), else its last bar.
    double bridgeEdge(double t) const noexcept LG_NONBLOCKING {
        const double loopEnd = perf.anchor + 4.0;
        if (perf.hold && t < loopEnd + kEps)
            return loopEnd;
        return perf.anchor + itemBars(perf.item, perf.fit) * 4.0;
    }

    // Runs the timeline across [b0, b1): note-ons, note-offs and transitions at their exact beats.
    void run(double b0, double b1) noexcept LG_NONBLOCKING {
        double t = b0;
        for (int guard = 0; guard < 64; ++guard) {
            // A transition due at (or missed before) t happens first.
            if (perf.planActive && perf.planStart <= t + kEps) {
                endAll(t);
                perf.planActive = false;
                if (perf.planBridge >= 0) {
                    perf.item = ItemRef::bridge(perf.planBridge);
                    perf.fit = perf.planFit;
                    perf.target = perf.planTarget;
                    perf.anchor = t;
                    touch();
                } else {
                    enterSection(perf.planTarget, t);
                }
                continue;
            }
            if (perf.item.isBridge() && bridgeEdge(t) <= t + kEps) {
                endAll(t);
                if (perf.hold && t <= perf.anchor + 4.0 + kEps) {
                    perf.anchor = t; // keep building on the first bar
                    touch();
                } else {
                    const int target =
                        perf.target >= 0 ? perf.target : song->bridges[static_cast<std::size_t>(perf.item.index)].to;
                    enterSection(target, t);
                }
                continue;
            }
            if (t >= b1 - kEps)
                break;
            double boundary = b1;
            if (perf.planActive)
                boundary = std::min(boundary, perf.planStart);
            if (perf.item.isBridge())
                boundary = std::min(boundary, bridgeEdge(t));
            play(t, boundary);
            endDue(boundary);
            t = boundary;
        }
        endDue(b1);
    }

    void sortOutput() noexcept LG_NONBLOCKING {
        // By sample offset; at the same offset note-offs go first so a repeated note retriggers.
        auto& ev = out->events;
        const auto key = [](const MidiOutEvent& e) noexcept {
            return e.sampleOffset * 2 + ((e.data[0] & 0xF0u) == 0x90u ? 1 : 0);
        };
        for (int i = 1; i < out->count; ++i) {
            const auto cur = ev[static_cast<std::size_t>(i)];
            int j = i - 1;
            while (j >= 0 && key(ev[static_cast<std::size_t>(j)]) > key(cur)) {
                ev[static_cast<std::size_t>(j + 1)] = ev[static_cast<std::size_t>(j)];
                --j;
            }
            ev[static_cast<std::size_t>(j + 1)] = cur;
        }
        auto& se = synthEvents;
        for (int i = 1; i < synthCount; ++i) {
            const auto cur = se[static_cast<std::size_t>(i)];
            int j = i - 1;
            while (j >= 0 && se[static_cast<std::size_t>(j)].offset > cur.offset) {
                se[static_cast<std::size_t>(j + 1)] = se[static_cast<std::size_t>(j)];
                --j;
            }
            se[static_cast<std::size_t>(j + 1)] = cur;
        }
    }

    VoiceRenderer* voices = nullptr; // set per block when the studio voices are selected
    double renderBpm = 120.0;
    bool renderPlaying = false;

    void releaseSound() noexcept LG_NONBLOCKING {
        if (voices != nullptr)
            voices->releaseAll();
        synth.releaseAll();
    }

    void render(float* left, float* right, int n) noexcept LG_NONBLOCKING {
        if (left == nullptr || right == nullptr) {
            synthCount = 0;
            return;
        }
        std::fill(left, left + n, 0.0f);
        std::fill(right, right + n, 0.0f);
        if (!params.audition) { // the sound is off: whichever source was playing lets go of its notes
            synthCount = 0;
            releaseSound();
            return;
        }
        int at = 0;
        const float gain = std::clamp(params.auditionGain, 0.0f, 2.0f);
        for (int i = 0; i <= synthCount; ++i) {
            const int until = i < synthCount ? std::clamp(synthEvents[static_cast<std::size_t>(i)].offset, at, n) : n;
            if (until > at) {
                if (voices != nullptr)
                    voices->render(left + at, right + at, until - at, gain, renderBpm, beat0 + at * beatsPerSample,
                                   renderPlaying);
                else
                    synth.render(left + at, right + at, until - at, gain);
            }
            at = until;
            if (i == synthCount)
                break;
            const auto& e = synthEvents[static_cast<std::size_t>(i)];
            if (voices != nullptr) {
                if (e.kind == 0)
                    voices->noteOff(e.role, e.pitch);
                else if (e.kind == 1)
                    voices->noteOn(e.role, e.pitch, e.velocity);
                else
                    voices->drum(e.role, e.velocity);
            } else if (e.kind == 0) {
                synth.noteOff(e.role, e.pitch);
            } else if (e.kind == 1) {
                synth.noteOn(e.role, e.pitch, e.velocity);
            } else {
                synth.drum(static_cast<DrumKind>(e.role), e.velocity);
            }
        }
        synthCount = 0;
        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
            peak = std::max(peak, std::max(std::abs(left[i]), std::abs(right[i])));
        tele.peak = peak;
    }
};

Engine::Engine() : songs_(std::make_unique<TripleBuffer<CompiledSong>>()), impl_(std::make_unique<Impl>()) {}

Engine::~Engine() = default;

void Engine::prepare(double sampleRate, int maxBlockSize) {
    impl_->sampleRate = sampleRate > 1000.0 ? sampleRate : 48000.0;
    impl_->synth.prepare(impl_->sampleRate);
    (void)maxBlockSize;
    reset();
}

void Engine::reset() noexcept LG_NONBLOCKING {
    auto& m = *impl_;
    m.wasPlaying = false;
    m.previewPlaying = false;
    m.previewBeat = 0.0;
    m.flushSounding = m.soundingCount > 0; // a re-prepare mid-play: the instrument downstream still holds them
    m.synthCount = 0;
    m.pendingCount = 0;
    m.perf.planActive = false;
    m.perf.hold = false;
    m.lastStepBeat = -1.0;
    m.keyNoteCount = {};
    m.lastFollowed = 0;
    m.synth.reset();
    if (m.voices != nullptr)
        m.voices->releaseAll();
}

void Engine::command(const Command& c) noexcept LG_NONBLOCKING {
    auto& m = *impl_;
    if (m.pendingCount < kMaxPending)
        m.pending[static_cast<std::size_t>(m.pendingCount++)] = c;
}

void Engine::follow(const Performance& p) noexcept LG_NONBLOCKING {
    auto& m = *impl_;
    // The leader's preview: start and stop with it, on its clock. Re-synced only when it drifts by more than a
    // couple of blocks, since a re-sync reads as a host jump and releases every note.
    if (p.previewing != m.previewPlaying) {
        m.previewPlaying = p.previewing;
        m.previewBeat = p.previewBeat;
    } else if (p.previewing && std::abs(p.previewBeat - m.previewBeat) > 2.0 * m.blockBeats) {
        m.previewBeat = p.previewBeat;
    }
    if (p.generation == m.lastFollowed)
        return;
    m.lastFollowed = p.generation;
    const auto keepGeneration = m.perf.generation;
    m.perf = p;
    m.perf.generation = keepGeneration;
}

Performance Engine::performance() const noexcept LG_NONBLOCKING { return impl_->perf; }

void Engine::inputNote(int note, int velocity, bool on, int sampleOffset) noexcept LG_NONBLOCKING {
    auto& m = *impl_;
    if (note < 0 || note > 127 || m.inputCount >= static_cast<int>(m.inputs.size()))
        return;
    m.inputs[static_cast<std::size_t>(m.inputCount++)] = {sampleOffset, static_cast<std::uint8_t>(note),
                                                          static_cast<std::uint8_t>(std::clamp(velocity, 1, 127)), on};
}

void Engine::process(const EngineParams& params, const Transport& host, MidiOutBuffer& out, float* left, float* right,
                     int numSamples) noexcept LG_NONBLOCKING {
    auto& m = *impl_;
    out.clear();
    m.out = &out;
    m.params = params;
    m.params.landBars = params.landBars == 1 || params.landBars == 2 ? params.landBars : 4;
    const auto was = m.identity(); // the old song is only readable until acquire()
    const bool newSong = songs_->acquire();
    m.song = &songs_->read();
    if (newSong) {
        m.lastStepBeat = -1.0;
        m.reidentify(was);
    }
    m.numSamples = std::max(0, numSamples);
    VoiceRenderer* wanted = params.studioVoices ? voices_ : nullptr;
    if (wanted != m.voices) { // switching sources: let the old one go quiet
        if (m.voices != nullptr)
            m.voices->releaseAll();
        else
            m.synth.releaseAll();
        m.voices = wanted;
    }

    const bool hostPlaying = host.playing;
    if (hostPlaying)
        m.previewPlaying = false;
    // Preview commands are handled before the clock is read: they decide whether there is a clock.
    for (int i = 0; i < m.pendingCount; ++i) {
        const auto& c = m.pending[static_cast<std::size_t>(i)];
        if (c.kind == CommandKind::previewStop || (c.kind == CommandKind::previewPlay && !hostPlaying)) {
            m.apply(c, 0.0, false);
            m.pending[static_cast<std::size_t>(i)].kind = CommandKind::release; // consumed (a release is harmless)
        }
    }

    const bool playing = (hostPlaying || m.previewPlaying) && m.hasSong();
    const double bpm = std::clamp(hostPlaying ? host.bpm : static_cast<double>(m.song->bpm), 20.0, 400.0);
    m.synth.setTempo(bpm);
    m.renderBpm = bpm;
    m.renderPlaying = playing;
    m.beat0 = hostPlaying ? host.ppq : m.previewBeat;
    m.beatsPerSample = bpm / 60.0 / m.sampleRate;
    m.blockBeats = m.numSamples * m.beatsPerSample;
    const double beat1 = m.beat0 + m.blockBeats;
    if (m.flushSounding) {
        m.endAll(m.beat0);
        m.flushSounding = false;
    }

    if (!playing) {
        // Commands while stopped start the preview (handled by apply) or do nothing.
        for (int i = 0; i < m.pendingCount; ++i)
            m.apply(m.pending[static_cast<std::size_t>(i)], m.beat0, false);
        m.pendingCount = 0;
        if (m.wasPlaying)
            m.endAll(m.beat0);
        m.wasPlaying = false;
        if (m.auditionRemaining > 0) {
            m.auditionRemaining -= m.numSamples;
            if (m.auditionRemaining <= 0)
                m.endAll(m.beat0);
        }
        m.handleInputs();
        m.perf.previewing = m.previewPlaying;
        m.perf.previewBeat = m.previewBeat;
        if (m.previewPlaying) { // a preview just started: play it from the next block
            m.sortOutput();
            m.render(left, right, m.numSamples);
            return;
        }
        m.sortOutput();
        m.render(left, right, m.numSamples);
        m.tele.playing = false;
        m.tele.hostPlaying = false;
        m.tele.previewPlaying = false;
        m.tele.performance = m.perf;
        m.tele.songRevision = m.song->revision;
        m.tele.barsToStart = m.tele.barsToLand = -1;
        for (auto& s : m.tele.sounding)
            s = {0, 0};
        return;
    }

    if (!m.validItem(m.perf.item)) // on the first section's own grid, as a start would be
        m.enterSection(0, std::floor(m.beat0 / (m.song->barsOf(ItemRef::section(0)) * 4.0)) *
                              (m.song->barsOf(ItemRef::section(0)) * 4.0));

    const bool leading = params.outputRole < 0; // a follower takes its position from the leader, never decides one
    if (!m.wasPlaying) {
        // Starting: a section aligns to the host's bar grid so the same host position always plays the same notes.
        if (m.perf.item.isBridge() && hostPlaying && leading) {
            const int target =
                m.perf.target >= 0 ? m.perf.target : m.song->bridges[static_cast<std::size_t>(m.perf.item.index)].to;
            m.enterSection(target, 0.0);
        }
        if (!m.perf.item.isBridge() && hostPlaying && leading) {
            const double len = m.itemBars(m.perf.item, 0) * 4.0;
            m.perf.anchor = std::floor(m.beat0 / len) * len;
        }
        if (hostPlaying && leading)
            m.perf.planActive = false;
        m.lastStepBeat = -1.0;
        if (leading)
            m.touch();
    } else if (std::abs(m.beat0 - m.expected) > 1e-3) {
        // The host jumped (loop brace, scrub, locate): release everything, land any bridge, drop any plan.
        m.endAll(m.beat0);
        m.lastStepBeat = -1.0;
        if (leading)
            m.perf.planActive = false;
        if (m.perf.item.isBridge() && leading) {
            const int target =
                m.perf.target >= 0 ? m.perf.target : m.song->bridges[static_cast<std::size_t>(m.perf.item.index)].to;
            m.enterSection(target, std::floor(m.beat0 / 4.0) * 4.0);
        }
        m.touch();
    }

    for (int i = 0; i < m.pendingCount; ++i)
        m.apply(m.pending[static_cast<std::size_t>(i)], m.beat0, true);
    m.pendingCount = 0;

    if (m.auditionRemaining > 0) { // playback started while a chord auditioned: it must not ring on
        m.endAll(m.beat0);
        m.auditionRemaining = 0;
    }
    m.handleInputs();
    m.run(m.beat0, beat1);
    m.sortOutput();
    m.render(left, right, m.numSamples);

    m.wasPlaying = true;
    m.expected = beat1;
    if (!hostPlaying)
        m.previewBeat = beat1;
    m.perf.previewing = m.previewPlaying;
    m.perf.previewBeat = m.previewBeat;

    // Telemetry
    auto& t = m.tele;
    t.hostPlaying = hostPlaying;
    t.previewPlaying = m.previewPlaying;
    t.playing = true;
    t.beat = m.beat0;
    t.bpm = bpm;
    t.performance = m.perf;
    t.songRevision = m.song->revision;
    t.playingBars = m.itemBars(m.perf.item, m.perf.fit);
    const double len = t.playingBars * 4.0;
    const double pos = m.perf.item.isBridge() ? m.beat0 - m.perf.anchor : positiveMod(m.beat0 - m.perf.anchor, len);
    t.stepInItem = static_cast<float>(std::clamp(pos * 4.0, 0.0, len * 4.0));
    if (m.perf.planActive) {
        t.barsToStart = std::max(0, static_cast<int>(std::ceil((m.perf.planStart - m.beat0) / 4.0 - 1e-9)));
        t.barsToLand = t.barsToStart + (m.perf.planBridge >= 0 ? m.perf.planFit : 0);
    } else if (m.perf.item.isBridge()) {
        t.barsToStart = -1;
        t.barsToLand =
            m.perf.hold ? -1 : std::max(0, static_cast<int>(std::ceil((m.perf.anchor + len - m.beat0) / 4.0 - 1e-9)));
    } else {
        t.barsToStart = t.barsToLand = -1;
    }
    for (auto& s : t.sounding)
        s = {0, 0};
    for (int i = 0; i < m.soundingCount; ++i) {
        const auto& s = m.sounding[static_cast<std::size_t>(i)];
        if (s.role < kRoleCount)
            t.sounding[s.role][s.pitch >> 6u] |= (std::uint64_t{1} << (s.pitch & 63u));
    }
}

void Engine::releaseAll(MidiOutBuffer& out) noexcept LG_NONBLOCKING {
    auto& m = *impl_;
    out.clear();
    m.out = &out;
    m.numSamples = 1;
    m.beatsPerSample = 1.0;
    m.beat0 = 0.0;
    while (m.soundingCount > 0) {
        const auto s = m.sounding[static_cast<std::size_t>(--m.soundingCount)];
        out.push(0, static_cast<std::uint8_t>(0x80u | s.channel), s.pitch, 0);
    }
    m.releaseSound();
    m.synthCount = 0;
    m.wasPlaying = false;
    m.keyNoteCount = {};
    m.lastFollowed = 0; // a follower copies its leader's position afresh when it resumes
}

EngineTelemetry Engine::telemetry() const noexcept LG_NONBLOCKING { return impl_->tele; }

} // namespace lg
