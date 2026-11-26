// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/engine.h"

#include "audition.h"
#include "styles.h"

#include <hba/dsp/denormals.h>
#include <hba/dsp/lane_math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cl {

namespace {

inline constexpr int kMaxActiveNotes = 32;
inline constexpr int kMaxCapturedEvents = 128;

int toMidi7Bit(double bipolar) noexcept CL_NONBLOCKING {
    return std::clamp(static_cast<int>(std::lround((bipolar * 0.5 + 0.5) * 127.0)), 0, 127);
}

int toMidi7BitUni(double uni01) noexcept CL_NONBLOCKING {
    return std::clamp(static_cast<int>(std::lround(std::clamp(uni01, 0.0, 1.0) * 127.0)), 0, 127);
}

int toPitchBend14(double semitones, double rangeSemitones) noexcept CL_NONBLOCKING {
    const double norm = std::clamp(semitones / std::max(1.0, rangeSemitones), -1.0, 1.0);
    return std::clamp(static_cast<int>(std::lround(8192.0 + norm * 8191.0)), 0, 16383);
}

int sampleOffsetOf(double eventBeat, double windowStart, double beatsPerSample,
                   int numSamples) noexcept CL_NONBLOCKING {
    const int raw = static_cast<int>(std::floor((eventBeat - windowStart) / beatsPerSample + 1.0e-7));
    return std::clamp(raw, 0, numSamples - 1);
}

double quantizeStepBeats(Quantize q) noexcept CL_NONBLOCKING {
    switch (q) {
    case Quantize::instant:
        return 0.0;
    case Quantize::sixteenth:
        return 0.25;
    case Quantize::quarter:
        return 1.0;
    case Quantize::oneBar:
        return 4.0;
    }
    return 0.25;
}

double rollRateBeatsForX(float x) noexcept CL_NONBLOCKING {
    if (x < 0.20f)
        return 0.50; // 1/8
    if (x < 0.42f)
        return 0.25; // 1/16
    if (x < 0.64f)
        return 1.0 / 6.0; // 1/16T (triplets!)
    if (x < 0.84f)
        return 0.125; // 1/32
    return 0.0625;    // 1/64 razor drill!
}

double stutterWindowBeatsForX(float x) noexcept CL_NONBLOCKING {
    if (x < 0.18f)
        return 1.0; // 1 beat
    if (x < 0.42f)
        return 0.5; // 1/2 beat
    if (x < 0.68f)
        return 0.25; // 1/4 beat
    if (x < 0.88f)
        return 0.125; // 1/8 beat
    return 0.0625;    // 1/16 beat micro-stutter
}

// Compute live bouncing-ball geometric step for Bounce X/Y program
double bounceStepBeats(int stepIdx, float x) noexcept CL_NONBLOCKING {
    // 7-step cycle: x < 0.45 decelerates (fast -> slow), x ~ 0.5 even triplets, x > 0.55 accelerates (slow -> fast!)
    constexpr int kCycle = 7;
    const int k = stepIdx % kCycle;
    const double u = static_cast<double>(k) / static_cast<double>(kCycle - 1); // 0..1
    const double curve = (static_cast<double>(x) - 0.5) * 2.0;               // -1..+1
    // Interpolate exponentially between 0.375 beats and 0.0625 beats
    const double t = curve >= 0.0 ? (0.5 * (1.0 - curve) + u * curve) : (0.5 * (1.0 + curve) + (1.0 - u) * (-curve));
    return 0.375 * std::pow(0.0625 / 0.375, std::clamp(t, 0.0, 1.0));
}

} // namespace

struct Engine::Impl {
    struct ActiveNote {
        bool active = false;
        std::uint8_t channel = 1;
        std::int8_t note = 0;
        double absEndBeat = 0.0;
    };

    struct CapturedEvent {
        double barBeat = 0.0;
        double durBeats = 0.25;
        std::int8_t note = 60;
        std::uint8_t vel = 100;
        std::int64_t barIndex = -1;
        bool active = false;
    };

    double sampleRate = 48000.0;
    LaneCurves curves = defaultLaneCurves();
    PatternBank bank{};
    PatternBank polyWorkBank{};

    // Live MIDI input state
    HeldNotes physicalHeld{};
    HeldNotes held{};
    HeldNotes latched{};
    HeldNotes lastEffectiveHeld{};
    bool sustainDown = false;
    bool wasSlicerLatch = false;
    std::uint8_t midiPadHeld = 0;
    int lastMidiPadProg = -1;

    // Rolling 1-bar clip capture buffer for SlicerStyle::clipSlice
    std::array<CapturedEvent, kMaxCapturedEvents> captured{};
    int capturedCount = 0;
    int capturedWrite = 0;
    std::int64_t lastCaptureBar = -1;

    // Compiled plans
    PhrasePlan plan{};
    PhrasePlan liveFillPlan{};
    std::int64_t liveFillBar = -1;
    int liveFillQuantX = -1;
    std::array<ActiveNote, kMaxActiveNotes> active{};

    // Quantized Scene & Playable XY Pad state
    int currentScene = 0;
    int pendingSceneIdx = -1;
    double sceneSwitchBeat = 0.0;

    int activeProg = 4; // Default: POLY
    int pendingProg = -1;
    bool engaged = false;
    bool pendingEngage = false;
    double padSwitchBeat = 0.0;
    double chopAnchorBeat = 0.0;
    double chopStartAbsBeat = 0.0;

    // Roll & Bounce cursor tracking
    double rollNextBeat = -1.0;
    int rollStepIndex = 0;
    double bounceNextBeat = -1.0;
    int bounceStepIndex = 0;

    // Transport tracking
    bool wasPlaying = false;
    double expectedNextPpq = 0.0;
    double lastTransportPpq = 0.0;
    EngineMode lastEngineMode = EngineMode::drum;
    SlicerStyle lastSlicerStyle = SlicerStyle::stabChop;

    // CC deduplication
    int lastCc74 = -1;
    int lastCc1 = -1;
    int lastCc11 = -1;
    int lastCc71 = -1;
    int lastPressure = -1;
    int lastBend = 8192;

    // Telemetry for 60 Hz UI
    double playheadBeats = 0.0;
    double lastLanePpq = 0.0;
    std::int64_t lastPhraseIndex = 0;
    std::array<float, kLaneCount> phases{};
    std::array<float, kLaneCount> values{};
    std::array<float, kDrumLaneCount> laneFlashes{};
    int lastEventCount = 0;
    std::uint8_t activityMask = 0;
    bool playingState = false;

    AuditionSynth audition{};

    void prepare(double sr) noexcept CL_NONBLOCKING {
        sampleRate = sr > 1000.0 ? sr : 48000.0;
        audition.prepare(sampleRate);
        reset();
    }

    void clearDedupe() noexcept CL_NONBLOCKING {
        lastCc74 = lastCc1 = lastCc11 = lastCc71 = lastPressure = -1;
        lastBend = 8192;
    }

    void reset() noexcept CL_NONBLOCKING {
        for (auto& slot : active)
            slot.active = false;
        physicalHeld.clear();
        held.clear();
        latched.clear();
        lastEffectiveHeld.clear();
        sustainDown = false;
        wasSlicerLatch = false;
        midiPadHeld = 0;
        lastMidiPadProg = -1;
        for (auto& c : captured)
            c.active = false;
        capturedCount = 0;
        capturedWrite = 0;
        lastCaptureBar = -1;
        currentScene = 0;
        pendingSceneIdx = -1;
        sceneSwitchBeat = 0.0;
        activeProg = 4;
        pendingProg = -1;
        engaged = false;
        pendingEngage = false;
        padSwitchBeat = 0.0;
        chopAnchorBeat = 0.0;
        chopStartAbsBeat = 0.0;
        rollNextBeat = -1.0;
        rollStepIndex = 0;
        bounceNextBeat = -1.0;
        bounceStepIndex = 0;
        wasPlaying = false;
        expectedNextPpq = 0.0;
        lastTransportPpq = 0.0;
        playheadBeats = 0.0;
        lastLanePpq = 0.0;
        lastPhraseIndex = 0;
        phases.fill(0.0f);
        values.fill(0.0f);
        laneFlashes.fill(0.0f);
        lastEventCount = 0;
        activityMask = 0;
        playingState = false;
        clearDedupe();
        audition.reset();
        plan.phraseIndex = -1;
        plan.signature = 0;
        liveFillBar = -1;
        liveFillQuantX = -1;
    }

    const HeldNotes& effectiveHeld(const Params& params) const noexcept CL_NONBLOCKING {
        return params.on(ParamId::slicerLatch) ? latched : held;
    }

    void flushAllActive(MidiEventBuffer& outMidi, int sampleOffset) noexcept CL_NONBLOCKING {
        for (auto& slot : active) {
            if (!slot.active)
                continue;
            outMidi.pushCritical(MidiEvent::noteOff(sampleOffset, slot.channel, slot.note));
            audition.noteOff(slot.note);
            slot.active = false;
        }
        audition.allNotesOff();
    }

    void triggerNote(std::int8_t noteNum, int velocity, int laneHint, double absEndBeat, int offset,
                     MidiEventBuffer& outMidi, bool isDrum) noexcept CL_NONBLOCKING {
        const std::uint8_t ch = 1;
        for (auto& slot : active) {
            if (slot.active && slot.channel == ch && slot.note == noteNum) {
                outMidi.pushCritical(MidiEvent::noteOff(offset, ch, slot.note));
                slot.active = false;
            }
        }
        int chosen = -1;
        for (int i = 0; i < kMaxActiveNotes; ++i) {
            if (!active[static_cast<std::size_t>(i)].active) {
                chosen = i;
                break;
            }
        }
        if (chosen < 0) {
            chosen = 0;
            for (int i = 1; i < kMaxActiveNotes; ++i) {
                if (active[static_cast<std::size_t>(i)].absEndBeat <
                    active[static_cast<std::size_t>(chosen)].absEndBeat)
                    chosen = i;
            }
            auto& victim = active[static_cast<std::size_t>(chosen)];
            outMidi.pushCritical(MidiEvent::noteOff(offset, victim.channel, victim.note));
            victim.active = false;
        }

        const int clampedVel = std::clamp(velocity, 1, 127);
        outMidi.pushCritical(MidiEvent::noteOn(offset, ch, noteNum, clampedVel));

        auto& slot = active[static_cast<std::size_t>(chosen)];
        slot.active = true;
        slot.channel = ch;
        slot.note = noteNum;
        slot.absEndBeat = absEndBeat;

        if (laneHint >= 0 && laneHint < kDrumLaneCount) {
            const float f = static_cast<float>(clampedVel) / 127.0f;
            laneFlashes[static_cast<std::size_t>(laneHint)] =
                std::max(laneFlashes[static_cast<std::size_t>(laneHint)], f);
        }
        if (isDrum)
            audition.triggerDrumLane(laneHint >= 0 ? laneHint : 0, clampedVel);
        else
            audition.noteOn(noteNum, clampedVel, false, laneHint);
    }

    // Build effective params & pattern bank modulated cleanly by continuous XY pads (Slice / Swing / Poly)!
    void buildEffectiveCompilationInputs(const Params& baseParams, bool padOn, PadProgram prog, float padX, float padY,
                                         Params& outParams, const PatternBank*& outBank) noexcept CL_NONBLOCKING {
        outParams = baseParams;
        outBank = &bank;
        if (!padOn)
            return;

        if (prog == PadProgram::slice) {
            outParams[ParamId::chop] = std::clamp(0.06f + 0.94f * padX, 0.0f, 1.0f);
            outParams[ParamId::complexity] = std::clamp(0.10f + 0.90f * padY, 0.0f, 1.0f);
            outParams[ParamId::ratchet] = std::max(baseParams[ParamId::ratchet], padX * 0.62f);
        } else if (prog == PadProgram::swing) {
            outParams[ParamId::gravity] = std::clamp(padX * 2.0f - 1.0f, -1.0f, 1.0f);
            outParams[ParamId::ghosts] = std::clamp(padY, 0.0f, 1.0f);
            outParams[ParamId::dynamics] = std::clamp(0.38f + 0.62f * padY, 0.0f, 1.0f);
        } else if (prog == PadProgram::poly) {
            polyWorkBank = bank;
            auto& sc = polyWorkBank.scenes[static_cast<std::size_t>(currentScene)];
            if (!sc.customized)
                sc = effectiveSceneData(bank, currentScene);
            sc.customized = true;

            // Continuous polymetric twist with X: locked 16 -> mild 15/12/9/7 -> rich 11/12/7/5 -> wild prime polyrhythm!
            std::array<std::uint8_t, kDrumLaneCount> lens{16, 16, 16, 16, 16, 16, 16, 16};
            if (padX >= 0.80f)
                lens = {13, 15, 11, 9, 7, 11, 5, 3};
            else if (padX >= 0.52f)
                lens = {16, 16, 11, 12, 12, 15, 7, 5};
            else if (padX >= 0.24f)
                lens = {16, 16, 15, 12, 14, 16, 9, 7};

            const int rotShift = static_cast<int>(std::lround(padY * 11.0f));
            for (int l = 0; l < kDrumLaneCount; ++l) {
                auto& lane = sc.lanes[static_cast<std::size_t>(l)];
                if (lane.locked)
                    continue;
                lane.stepLength = lens[static_cast<std::size_t>(l)];
                if (l >= 2 && padX >= 0.24f) {
                    const int len = static_cast<int>(lane.stepLength);
                    const int hits = std::clamp(
                        static_cast<int>(std::lround((0.28f + 0.45f * padY) * static_cast<float>(len))), 2,
                                                std::max(2, len - 1));
                    lane.euclidHits = static_cast<std::uint8_t>(hits);
                    lane.euclidRot = static_cast<std::uint8_t>((rotShift + l * 2) % len);
                }
            }
            // Also polymetrically twist Slicer loop length in MIDI Slicer mode!
            polyWorkBank.slicerLength = static_cast<std::uint8_t>(
                padX < 0.25f ? 16 : (padX < 0.55f ? 12 : (padX < 0.80f ? 9 : 7)));
            outParams[ParamId::variation] =
                static_cast<float>(std::clamp(1 + static_cast<int>(std::lround(padY * 15.0f)), 1, 64));
            outBank = &polyWorkBank;
        }
    }

    void ensurePlan(const Params& params, std::int64_t phraseIndex, double bpm, bool padOn, PadProgram prog, float padX,
                    float padY) noexcept CL_NONBLOCKING {
        const auto& h = effectiveHeld(params);
        lastEffectiveHeld = h;
        Params effParams;
        const PatternBank* effBank = &bank;
        buildEffectiveCompilationInputs(params, padOn, prog, padX, padY, effParams, effBank);
        const std::uint32_t sig = phraseSignature(effParams, currentScene, h, *effBank, bpm);
        if (plan.phraseIndex != phraseIndex || plan.signature != sig || plan.activeScene != currentScene)
            compilePhrase(effParams, currentScene, phraseIndex, h, plan, *effBank, bpm);
    }

    void recordClipNoteOn(int midiNote, int vel, double ppq) noexcept CL_NONBLOCKING {
        const double safePpq = ppq >= 0.0 ? ppq : lastTransportPpq;
        const std::int64_t bar = static_cast<std::int64_t>(std::floor(safePpq / 4.0));
        const double barBeat = safePpq - static_cast<double>(bar) * 4.0;
        if (bar != lastCaptureBar) {
            for (auto& c : captured) {
                if (c.active && (bar - c.barIndex) >= 2)
                    c.active = false;
            }
            lastCaptureBar = bar;
        }
        auto& slot = captured[static_cast<std::size_t>(capturedWrite)];
        slot.barBeat = std::clamp(barBeat, 0.0, 3.99);
        slot.durBeats = 0.25;
        slot.note = static_cast<std::int8_t>(std::clamp(midiNote, 0, 127));
        slot.vel = static_cast<std::uint8_t>(std::clamp(vel, 1, 127));
        slot.barIndex = bar;
        slot.active = true;
        capturedWrite = (capturedWrite + 1) % kMaxCapturedEvents;

        int cnt = 0;
        for (const auto& c : captured) {
            if (c.active)
                ++cnt;
        }
        capturedCount = cnt;
    }

    void recordClipNoteOff(int midiNote, double ppq) noexcept CL_NONBLOCKING {
        const double safePpq = ppq >= 0.0 ? ppq : lastTransportPpq;
        const double barBeat = safePpq - std::floor(safePpq / 4.0) * 4.0;
        for (auto& c : captured) {
            if (c.active && c.note == midiNote) {
                double d = barBeat - c.barBeat;
                if (d <= 0.01)
                    d += 4.0;
                if (d > 0.02 && d < 3.9)
                    c.durBeats = d;
            }
        }
    }

    void emitContinuousAt(const Params& params, double tickBeat, int offset, MidiEventBuffer& outMidi, bool padOn,
                          PadProgram prog, float padX, float padY) noexcept CL_NONBLOCKING {
        std::array<float, kLaneCount> sends{};
        for (int lane = 0; lane < kLaneCount; ++lane)
            sends[static_cast<std::size_t>(lane)] = static_cast<float>(laneSend(params, curves, lane, tickBeat));

        auto targetOn = [&](LaneTarget t) noexcept CL_NONBLOCKING {
            for (int l = 0; l < kLaneCount; ++l) {
                if (params.on(laneParam(l, LaneField::on)) && laneTargetOf(params, l) == t)
                    return true;
            }
            return false;
        };

        const auto xyCc = static_cast<PadCcMode>(std::clamp(params.choice(ParamId::padCcMode), 0, 3));
        const bool xyDrivesCc74 =
            padOn && (xyCc == PadCcMode::cc74AndCc1 || xyCc == PadCcMode::cc74AndVel || prog == PadProgram::drop);
        const bool xyDrivesCc1 =
            padOn && (xyCc == PadCcMode::cc74AndCc1 || xyCc == PadCcMode::cc1AndAt || prog == PadProgram::drop);
        const bool xyDrivesAt = padOn && (xyCc == PadCcMode::cc1AndAt);

        // CC74 Slide (Brightness)
        if (xyDrivesCc74 || targetOn(LaneTarget::cc74Slide)) {
            const double val01 =
                xyDrivesCc74 ? static_cast<double>(padY)
                             : (std::clamp(static_cast<double>(laneSum(params, sends, LaneTarget::cc74Slide)), -1.0, 1.0) *
                                    0.5 +
                                0.5);
            const int v = toMidi7BitUni(val01);
            if (lastCc74 != v) {
                if (outMidi.push(MidiEvent::controlChange(offset, 1, 74, v)))
                    lastCc74 = v;
            }
            audition.setBrightness(val01);
        }

        // CC1 Mod Wheel
        if (xyDrivesCc1 || targetOn(LaneTarget::cc1Mod)) {
            const double val01 =
                xyDrivesCc1
                    ? static_cast<double>(prog == PadProgram::drop ? padY : padX)
                    : (std::clamp(static_cast<double>(laneSum(params, sends, LaneTarget::cc1Mod)), -1.0, 1.0) * 0.5 +
                       0.5);
            const int v = toMidi7BitUni(val01);
            if (lastCc1 != v) {
                if (outMidi.push(MidiEvent::controlChange(offset, 1, 1, v)))
                    lastCc1 = v;
            }
            audition.setDrive(val01);
        }

        auto emitAuxCc = [&](LaneTarget target, int ccNum, int& cache) noexcept CL_NONBLOCKING {
            if (!targetOn(target))
                return;
            const double val = std::clamp(static_cast<double>(laneSum(params, sends, target)), -1.0, 1.0);
            const int v = toMidi7Bit(val);
            if (cache != v) {
                if (outMidi.push(MidiEvent::controlChange(offset, 1, ccNum, v)))
                    cache = v;
            }
        };
        emitAuxCc(LaneTarget::cc11Expr, 11, lastCc11);
        emitAuxCc(LaneTarget::cc71Res, 71, lastCc71);

        // Channel Pressure (Aftertouch)
        if (xyDrivesAt || targetOn(LaneTarget::pressure)) {
            const double val01 =
                xyDrivesAt ? static_cast<double>(padY)
                           : (std::clamp(static_cast<double>(laneSum(params, sends, LaneTarget::pressure)), -1.0, 1.0) *
                                  0.5 +
                              0.5);
            const int v = toMidi7BitUni(val01);
            if (lastPressure != v) {
                if (outMidi.push(MidiEvent::channelPressure(offset, 1, v)))
                    lastPressure = v;
            }
            audition.setPressure(val01);
        }

        // Pitch Scoop ONLY emits pitch bend if user explicitly enables Pitch Scoop lane!
        if (targetOn(LaneTarget::pitchScoop)) {
            const double st =
                static_cast<double>(std::clamp(laneSum(params, sends, LaneTarget::pitchScoop), -1.0f, 1.0f)) * 2.0;
            const int pb = toPitchBend14(st, 12.0);
            if (std::abs(pb - lastBend) >= 16) {
                if (outMidi.push(MidiEvent::pitchBend(offset, 1, pb)))
                    lastBend = pb;
            }
            audition.setPitchScoop(st);
        } else {
            audition.setPitchScoop(0.0);
        }
    }

    int evalVelBias(const Params& params, double beat, bool padOn, float padY) const noexcept CL_NONBLOCKING {
        std::array<float, kLaneCount> sends{};
        for (int l = 0; l < kLaneCount; ++l)
            sends[static_cast<std::size_t>(l)] = static_cast<float>(laneSend(params, curves, l, beat));
        int bias = static_cast<int>(std::lround(laneSum(params, sends, LaneTarget::velocity) * 26.0f));
        const auto xyCc = static_cast<PadCcMode>(std::clamp(params.choice(ParamId::padCcMode), 0, 3));
        if (padOn && xyCc == PadCcMode::cc74AndVel)
            bias += static_cast<int>(std::lround((padY - 0.5f) * 44.0f));
        return bias;
    }

    void process(const Params& params, const Transport& transport, MidiEventBuffer& outMidi, float* left, float* right,
                 int numSamples) noexcept CL_NONBLOCKING {
        hba::dsp::ScopedFlushDenormals ftz;
        outMidi.clear();
        if (numSamples <= 0)
            return;

        const float flashDecay =
            static_cast<float>(std::exp(-static_cast<double>(numSamples) / (0.115 * sampleRate)));
        for (auto& f : laneFlashes)
            f = (f * flashDecay < 0.005f) ? 0.0f : (f * flashDecay);

        const double bpm = std::clamp(transport.bpm, 20.0, 400.0);
        const double beatsPerSample = bpm / 60.0 / sampleRate;
        const auto barsIdx = static_cast<std::size_t>(std::clamp(params.choice(ParamId::phraseBars), 0, 2));
        const double phraseBeats = static_cast<double>(kPhraseBarCounts[barsIdx] * 4);
        const auto rateIdx = static_cast<std::size_t>(std::clamp(params.choice(ParamId::seqRate), 0, 2));
        const double baseRate = kSeqRateMultipliers[rateIdx];
        const auto mode = static_cast<EngineMode>(std::clamp(params.choice(ParamId::engineMode), 0, 1));
        const auto slicerStyle = static_cast<SlicerStyle>(std::clamp(params.choice(ParamId::slicerStyle), 0, 4));
        const auto quant = static_cast<Quantize>(std::clamp(params.choice(ParamId::sceneQuantize), 0, 3));
        const auto noteMap = static_cast<NoteMap>(std::clamp(params.choice(ParamId::noteMap), 0, kNoteMapCount - 1));
        const auto pocket = static_cast<Attractor>(std::clamp(params.choice(ParamId::attractor), 0, 4));
        const double groove = static_cast<double>(std::clamp(params[ParamId::gravity], -1.0f, 1.0f));
        const bool isDrum = (mode == EngineMode::drum);

        const bool latchNow = params.on(ParamId::slicerLatch);
        if (wasSlicerLatch && !latchNow)
            latched = held;
        wasSlicerLatch = latchNow;

        // Resolve requested XY Pad Program & Engagement (Touch + Hold + MIDI C3-G3)
        const bool midiPadActive = isDrum && params.on(ParamId::padMidi) && (midiPadHeld != 0u);
        const int reqProg = (midiPadActive && lastMidiPadProg >= 0)
                                ? std::clamp(lastMidiPadProg, 0, kPerfPadCount - 1)
                                : std::clamp(params.choice(ParamId::padProgram), 0, kPerfPadCount - 1);
        const bool reqEngaged = params.on(ParamId::padTouch) || params.on(ParamId::padHold) || midiPadActive;
        const float padX = std::clamp(params[ParamId::padX], 0.0f, 1.0f);
        const float padY = std::clamp(params[ParamId::padY], 0.0f, 1.0f);
        const int reqScene = std::clamp(params.choice(ParamId::scene), 0, kSceneCount - 1);

        const double rawPpq = std::max(0.0, transport.ppq);
        lastTransportPpq = rawPpq;

        // Quantized Scene & Pad Program/Engagement switching
        const double qStep = quantizeStepBeats(quant);
        if (!transport.playing || qStep <= 0.0) {
            currentScene = reqScene;
            pendingSceneIdx = -1;
            const bool newlyEngaged = reqEngaged && (!engaged || reqProg != activeProg);
            activeProg = reqProg;
            engaged = reqEngaged;
            pendingProg = -1;
            pendingEngage = false;
            if (newlyEngaged && static_cast<PadProgram>(activeProg) == PadProgram::stutter) {
                chopAnchorBeat = std::floor(std::fmod(rawPpq * baseRate, phraseBeats) * 4.0) * 0.25;
                chopStartAbsBeat = rawPpq;
            }
        } else {
            if (reqScene != currentScene && reqScene != pendingSceneIdx) {
                pendingSceneIdx = reqScene;
                sceneSwitchBeat = std::ceil((rawPpq - 1e-6) / qStep) * qStep;
            } else if (reqScene == currentScene) {
                pendingSceneIdx = -1;
            }

            if (!reqEngaged) {
                // Always release immediately so stopping a gesture on Beat 1 feels razor-tight!
                engaged = false;
                pendingEngage = false;
                pendingProg = -1;
                activeProg = reqProg;
            } else if (!engaged || reqProg != activeProg) {
                if (!pendingEngage || pendingProg != reqProg) {
                    pendingEngage = true;
                    pendingProg = reqProg;
                    padSwitchBeat = std::ceil((rawPpq - 1e-6) / qStep) * qStep;
                }
            } else {
                pendingEngage = false;
                pendingProg = -1;
            }
        }

        const auto effProg = static_cast<PadProgram>(std::clamp(activeProg, 0, kPerfPadCount - 1));
        const double morphPreviewMul =
            (engaged && effProg == PadProgram::morph) ? (padX < 0.33f ? 0.5 : (padX > 0.67f ? 2.0 : 1.0)) : 1.0;
        const double previewRate = std::clamp(baseRate * morphPreviewMul, 0.25, 4.0);
        const double scaledPpq = rawPpq * previewRate;
        const std::int64_t previewPhrase = static_cast<std::int64_t>(std::floor(scaledPpq / phraseBeats));
        ensurePlan(params, previewPhrase, bpm, engaged, effProg, padX, padY);
        lastPhraseIndex = previewPhrase;
        playheadBeats = scaledPpq - static_cast<double>(previewPhrase) * phraseBeats;
        lastLanePpq = rawPpq;
        playingState = transport.playing;
        activityMask = 0;

        for (int lane = 0; lane < kLaneCount; ++lane) {
            const auto idx = static_cast<std::size_t>(lane);
            phases[idx] = static_cast<float>(
                lanePhase(rawPpq, laneLengthQuarters(params, lane), params[laneParam(lane, LaneField::phase)]));
            values[idx] = static_cast<float>(laneSend(params, curves, lane, rawPpq));
        }

        if (mode != lastEngineMode || slicerStyle != lastSlicerStyle) {
            flushAllActive(outMidi, 0);
            clearDedupe();
            lastEngineMode = mode;
            lastSlicerStyle = slicerStyle;
        }

        if (!isDrum && slicerStyle != SlicerStyle::clipSlice && effectiveHeld(params).count == 0)
            flushAllActive(outMidi, 0);

        if (!transport.playing) {
            if (wasPlaying)
                flushAllActive(outMidi, 0);
            wasPlaying = false;
            lastEventCount = outMidi.count;
            renderAuditionAudio(params, left, right, numSamples);
            return;
        }

        const double shiftBeats = static_cast<double>(params[ParamId::shiftMs]) * 1.0e-3 * (bpm / 60.0);
        const double rawStart = transport.ppq - shiftBeats;
        const double rawEnd = rawStart + static_cast<double>(numSamples) * beatsPerSample;

        if (!wasPlaying || transport.ppq < expectedNextPpq - 1.0e-4 ||
            std::abs(transport.ppq - expectedNextPpq) > 0.125) {
            flushAllActive(outMidi, 0);
            clearDedupe();
            rollNextBeat = -1.0;
            bounceNextBeat = -1.0;
            liveFillBar = -1;
        }
        wasPlaying = true;
        expectedNextPpq = transport.ppq + static_cast<double>(numSamples) * beatsPerSample;

        if (rawEnd <= 0.0) {
            lastEventCount = outMidi.count;
            renderAuditionAudio(params, left, right, numSamples);
            return;
        }

        const double windowStart = std::max(0.0, rawStart);
        const double windowEnd = rawEnd;
        constexpr double kEps = 1.0e-9;

        // Commit quantized Scene & XY Pad transitions cleanly on beat boundary
        if (pendingSceneIdx >= 0 && windowEnd >= sceneSwitchBeat - kEps) {
            currentScene = pendingSceneIdx;
            pendingSceneIdx = -1;
            ensurePlan(params, previewPhrase, bpm, engaged, effProg, padX, padY);
        }
        if (pendingEngage && windowEnd >= padSwitchBeat - kEps) {
            activeProg = pendingProg >= 0 ? pendingProg : reqProg;
            engaged = true;
            pendingEngage = false;
            pendingProg = -1;
            const auto newProg = static_cast<PadProgram>(activeProg);
            if (newProg == PadProgram::stutter) {
                const double local = std::fmod(std::max(0.0, padSwitchBeat * baseRate), phraseBeats);
                chopAnchorBeat = std::floor(local * 4.0) * 0.25;
                chopStartAbsBeat = padSwitchBeat;
            } else if (newProg == PadProgram::roll) {
                rollNextBeat = std::max(windowStart, padSwitchBeat);
                rollStepIndex = 0;
            } else if (newProg == PadProgram::bounce) {
                bounceNextBeat = std::max(windowStart, padSwitchBeat);
                bounceStepIndex = 0;
            } else if (newProg == PadProgram::drop && padX < 0.30f) {
                flushAllActive(outMidi, sampleOffsetOf(std::max(windowStart, padSwitchBeat), rawStart, beatsPerSample,
                                                       numSamples));
            }
            ensurePlan(params, previewPhrase, bpm, engaged, newProg, padX, padY);
        }

        // 1. Fire pending Note Offs inside [windowStart, windowEnd)
        for (auto& slot : active) {
            if (slot.active && slot.absEndBeat < windowEnd - kEps) {
                const int offset = sampleOffsetOf(slot.absEndBeat, rawStart, beatsPerSample, numSamples);
                outMidi.pushCritical(MidiEvent::noteOff(offset, slot.channel, slot.note));
                audition.noteOff(slot.note);
                slot.active = false;
            }
        }

        const auto curProg = static_cast<PadProgram>(std::clamp(activeProg, 0, kPerfPadCount - 1));
        const SceneData sc = effectiveSceneData(bank, currentScene);
        const auto& h = effectiveHeld(params);

        // Helper for Roll / Bounce expressive multi-layer bursts
        auto fireExpressiveBurst = [&](double beat, double dur, float yNorm, int stepIdx,
                                       bool isBounce) noexcept CL_NONBLOCKING {
            const int offset = sampleOffsetOf(beat, rawStart, beatsPerSample, numSamples);
            const int vBias = evalVelBias(params, beat, engaged, padY);
            const int baseVel = 46 + static_cast<int>(80.0f * std::clamp(yNorm, 0.0f, 1.0f));
            if (isDrum) {
                int primaryLane = 1; // Snare
                if (isBounce) {
                    constexpr std::array<int, 4> kBounceLanes{7, 6, 1, 2};
                    primaryLane = kBounceLanes[static_cast<std::size_t>(stepIdx) % kBounceLanes.size()];
                } else {
                    if (yNorm < 0.32f)
                        primaryLane = 2; // Whisper ghost taps
                    else if (yNorm < 0.68f)
                        primaryLane = (stepIdx & 1) ? 2 : 1; // Alternating snare + ghost
                    else
                        primaryLane = (stepIdx % 3 == 2) ? 6 : 1; // Explosive snare + perc!
                }
                const std::int8_t noteNum =
                    effectiveLaneMidiNote(noteMap, primaryLane, sc.lanes[static_cast<std::size_t>(primaryLane)]);
                triggerNote(noteNum, baseVel + vBias, primaryLane, beat + dur, offset, outMidi, true);

                // Escalation layer: above Y = 0.72, stack crisp Clap/Rim accent on accented hits!
                if (yNorm > 0.72f && (stepIdx % 2 == 0)) {
                    const std::int8_t clapNote = effectiveLaneMidiNote(noteMap, 3, sc.lanes[3]);
                    triggerNote(clapNote, baseVel - 10 + vBias, 3, beat + dur, offset, outMidi, true);
                }
            } else if (h.count > 0) {
                if (isBounce) {
                    const int vi = stepIdx % h.count;
                    int noteNum = static_cast<int>(h.notes[static_cast<std::size_t>(vi)]);
                    if (yNorm > 0.78f && (stepIdx % 3 == 2) && noteNum <= 96)
                        noteNum += 12;
                    triggerNote(static_cast<std::int8_t>(noteNum), baseVel + vBias, std::min(vi, 7), beat + dur, offset,
                                outMidi, false);
                } else {
                    for (int i = 0; i < h.count; ++i) {
                        int noteNum = static_cast<int>(h.notes[static_cast<std::size_t>(i)]);
                        if (yNorm > 0.76f && i == h.count - 1 && (stepIdx & 1) && noteNum <= 96)
                            noteNum += 12;
                        triggerNote(static_cast<std::int8_t>(noteNum), baseVel + vBias, std::min(i, 7), beat + dur,
                                    offset, outMidi, false);
                    }
                }
            }
        };

        // 2. Execute Active X/Y Articulation Engine OR Standard Compiled Groove / Slicer!
        if (engaged && curProg == PadProgram::roll) {
            const double step = std::max(0.0625, rollRateBeatsForX(padX) / baseRate);
            if (rollNextBeat < windowStart - 0.5 || rollNextBeat > windowEnd + 0.5)
                rollNextBeat = std::ceil((windowStart - kEps) / step) * step;
            while (rollNextBeat < windowEnd - kEps) {
                const float microRamp = 0.85f + 0.15f * static_cast<float>(std::fmod(rollNextBeat, 1.0));
                fireExpressiveBurst(rollNextBeat, step * 0.80, std::clamp(padY * microRamp, 0.05f, 1.0f),
                                    rollStepIndex++, false);
                rollNextBeat += step;
            }
        } else if (engaged && curProg == PadProgram::bounce) {
            if (bounceNextBeat < windowStart - 0.5 || bounceNextBeat > windowEnd + 0.5) {
                bounceNextBeat = windowStart;
                bounceStepIndex = 0;
            }
            while (bounceNextBeat < windowEnd - kEps) {
                const double step = std::max(0.0625, bounceStepBeats(bounceStepIndex, padX) / baseRate);
                const float prog = static_cast<float>(bounceStepIndex % 7) / 6.0f;
                const float yVal = std::clamp(0.25f + 0.55f * padY + 0.25f * prog, 0.10f, 1.0f);
                fireExpressiveBurst(bounceNextBeat, step * 0.78, yVal, bounceStepIndex++, true);
                bounceNextBeat += step;
            }
        } else if (engaged && curProg == PadProgram::drop && isDrum) {
            const double effWinStart = windowStart * baseRate;
            const double effWinEnd = windowEnd * baseRate;
            if (padX < 0.30f) {
                // Clean Air-Cut drop silence + crisp pickup on final 16th of bar!
                const double barIdx = std::floor(effWinStart / 4.0);
                const double pickup = (barIdx * 4.0 + 3.75) / baseRate;
                if (pickup >= windowStart - kEps && pickup < windowEnd - kEps)
                    fireExpressiveBurst(pickup, 0.18 / baseRate, std::max(0.55f, padY), 0, false);
            } else {
                const std::int64_t bar = static_cast<std::int64_t>(std::floor(effWinStart / 4.0));
                const int qX = padX < 0.65f ? 1 : 2;
                if (liveFillBar != bar || liveFillQuantX != qX) {
                    liveFillPlan.noteCount = 0;
                    liveFillPlan.phraseBeats = 4.0;
                    const FillType ft = (qX == 1) ? FillType::tomRun : FillType::drillChop;
                    appendDrumFill(liveFillPlan, 0.0, 4.0, 4.0, ft, groove, pocket, std::max(0.55f, padY),
                                   std::max(0.50f, padX), params.integer(ParamId::variation), noteMap, sc,
                                   static_cast<std::uint32_t>(bar * 2654435761U));
                    liveFillBar = bar;
                    liveFillQuantX = qX;
                }
                const double barOrigin = static_cast<double>(bar) * 4.0;
                for (int i = 0; i < liveFillPlan.noteCount; ++i) {
                    const auto& n = liveFillPlan.notes[static_cast<std::size_t>(i)];
                    const double scaledStart = barOrigin + n.warpedBeats;
                    if (scaledStart >= effWinStart - kEps && scaledStart < effWinEnd - kEps) {
                        const double absStart = scaledStart / baseRate;
                        const int offset = sampleOffsetOf(absStart, rawStart, beatsPerSample, numSamples);
                        const int vBias = evalVelBias(params, absStart, engaged, padY);
                        const int scaledVel = static_cast<int>(
                            std::lround(static_cast<float>(n.velocity) * (0.55f + 0.55f * padY))) +
                                              vBias;
                        triggerNote(n.noteNumber, scaledVel, n.lane,
                                    absStart + std::max(0.03, n.durationBeats / baseRate), offset, outMidi, true);
                    }
                }
            }
        } else if (engaged && curProg == PadProgram::stutter && !(slicerStyle == SlicerStyle::clipSlice && !isDrum)) {
            // Live BBCut Freeze-Slice Beat Repeat with continuous X slice window, Y gate/pump & SeqRate scaling!
            const double sliceLen = stutterWindowBeatsForX(padX);
            const double sliceStart = std::clamp(chopAnchorBeat, 0.0, std::max(0.0, phraseBeats - sliceLen));
            const double elapsedStart = std::max(0.0, (windowStart - chopStartAbsBeat) * baseRate);
            const double elapsedEnd = std::max(0.0, (windowEnd - chopStartAbsBeat) * baseRate);
            const std::int64_t cycleA = static_cast<std::int64_t>(std::floor(elapsedStart / sliceLen));
            const std::int64_t cycleB = static_cast<std::int64_t>(std::floor(elapsedEnd / sliceLen));
            const double gateScale = 0.20 + 0.85 * static_cast<double>(padY);

            for (std::int64_t cyc = cycleA; cyc <= cycleB; ++cyc) {
                const double cycOrigin = chopStartAbsBeat + (static_cast<double>(cyc) * sliceLen) / baseRate;
                const float pump = (cyc % 4 == 3) ? (1.0f + 0.14f * padY) : ((cyc % 4 == 1) ? 0.88f : 0.98f);
                for (int i = 0; i < plan.noteCount; ++i) {
                    const auto& n = plan.notes[static_cast<std::size_t>(i)];
                    if (n.muted)
                        continue;
                    if (n.warpedBeats < sliceStart - kEps || n.warpedBeats >= sliceStart + sliceLen - kEps)
                        continue;
                    const double absStart = cycOrigin + (n.warpedBeats - sliceStart) / baseRate;
                    if (absStart >= windowStart - kEps && absStart < windowEnd - kEps) {
                        const int offset = sampleOffsetOf(absStart, rawStart, beatsPerSample, numSamples);
                        const int vBias = evalVelBias(params, absStart, engaged, padY);
                        const int vel = static_cast<int>(std::lround(static_cast<float>(n.velocity) * pump)) + vBias;
                        const double dur =
                            std::max(0.025, std::min(n.durationBeats, sliceLen * 0.85) * gateScale / baseRate);
                        triggerNote(n.noteNumber, vel, n.lane, absStart + dur, offset, outMidi, isDrum);
                    }
                }
            }
        } else if (!isDrum && slicerStyle == SlicerStyle::clipSlice) {
            // Rolling 1-bar incoming MIDI Clip Re-Slicer with live SeqRate + Morph/Stutter/Swing XY support!
            const double morphMul =
                (engaged && curProg == PadProgram::morph) ? (padX < 0.33f ? 0.5 : (padX > 0.67f ? 2.0 : 1.0)) : 1.0;
            const double speedMul = std::clamp(baseRate * morphMul, 0.25, 4.0);
            const double effStart = windowStart * speedMul;
            const double barOrigin = std::floor(effStart / 4.0) * 4.0;
            const double effGroove =
                (engaged && curProg == PadProgram::swing) ? static_cast<double>(padX * 2.0f - 1.0f) : groove;
            const float chopAmt = (engaged && curProg == PadProgram::slice) ? padX : params[ParamId::chop];
            const float ratAmt = std::clamp(params[ParamId::ratchet], 0.0f, 1.0f);

            for (const auto& ev : captured) {
                if (!ev.active)
                    continue;
                double targetBarBeat = ev.barBeat;
                if (engaged && curProg == PadProgram::stutter) {
                    const double win = stutterWindowBeatsForX(padX);
                    const double sliceStart = std::fmod(chopAnchorBeat, 4.0);
                    if (ev.barBeat < sliceStart || ev.barBeat >= sliceStart + win)
                        continue;
                    const double localOff = ev.barBeat - sliceStart;
                    const double curCell = std::floor((effStart - barOrigin) / win) * win;
                    targetBarBeat = curCell + localOff;
                } else if (chopAmt > 0.20f) {
                    const std::uint32_t hChop =
                        hba::dsp::laneHash(static_cast<std::uint32_t>(ev.note * 919 + static_cast<int>(ev.barBeat * 8)));
                    if (ev.barBeat < 2.0 && (static_cast<double>(hChop) / 4294967296.0) < static_cast<double>(chopAmt) * 0.4) {
                        const double repBeat = barOrigin + warpBeat(ev.barBeat + 2.0, effGroove, pocket);
                        const double absRep = repBeat / speedMul;
                        if (absRep >= windowStart - kEps && absRep < windowEnd - kEps) {
                            const int off = sampleOffsetOf(absRep, rawStart, beatsPerSample, numSamples);
                            triggerNote(ev.note, static_cast<int>(ev.vel) - 8, 0, absRep + ev.durBeats / speedMul, off,
                                        outMidi, false);
                        }
                    }
                }
                const double warpedBar = warpBeat(targetBarBeat, effGroove, pocket);
                const double absStart = (barOrigin + warpedBar) / speedMul;
                if (absStart >= windowStart - kEps && absStart < windowEnd - kEps) {
                    const int off = sampleOffsetOf(absStart, rawStart, beatsPerSample, numSamples);
                    const int vBias = evalVelBias(params, absStart, engaged, padY);
                    triggerNote(ev.note, static_cast<int>(ev.vel) + vBias, 0,
                                absStart + std::max(0.04, ev.durBeats / speedMul), off, outMidi, false);
                    if (ratAmt > 0.55f) {
                        const double subBeat = absStart + 0.125;
                        if (subBeat < windowEnd - kEps) {
                            const int subOff = sampleOffsetOf(subBeat, rawStart, beatsPerSample, numSamples);
                            triggerNote(ev.note, std::max(32, static_cast<int>(ev.vel) - 24), 0, subBeat + 0.09, subOff,
                                        outMidi, false);
                        }
                    }
                }
            }
        } else {
            // Standard playback + global SeqRate (0.5x/1x/2x) + Morph (0.5x/1x/2x + Sub <-> Tops filter) + Slice/Swing/Poly XY!
            const bool isMorph = engaged && (curProg == PadProgram::morph);
            const double morphMul = isMorph ? (padX < 0.33f ? 0.5 : (padX > 0.67f ? 2.0 : 1.0)) : 1.0;
            const double speedMul = std::clamp(baseRate * morphMul, 0.25, 4.0);
            const bool subCoreOnly = isMorph && (padY < 0.30f);
            const bool topBreakdown = isMorph && (padY > 0.70f);
            const bool isSlicerAirCut = engaged && (curProg == PadProgram::drop) && !isDrum && (padX < 0.30f);

            const double effStart = windowStart * speedMul;
            const double effEnd = windowEnd * speedMul;

            double scan = effStart;
            for (int pg = 0; pg < 3 && scan < effEnd - kEps && !isSlicerAirCut; ++pg) {
                const std::int64_t pIdx = static_cast<std::int64_t>(std::floor(scan / phraseBeats));
                const double pOrigin = static_cast<double>(pIdx) * phraseBeats;
                const double pEnd = pOrigin + phraseBeats;
                const double subEnd = std::min(effEnd, pEnd);
                ensurePlan(params, pIdx, bpm, engaged, curProg, padX, padY);

                for (int i = 0; i < plan.noteCount; ++i) {
                    const auto& n = plan.notes[static_cast<std::size_t>(i)];
                    if (n.muted)
                        continue;
                    if (subCoreOnly && n.lane > 1)
                        continue; // Sub/Kick + Snare core only!
                    if (topBreakdown && n.lane == 0)
                        continue; // Drop Kick for crisp top-end breakdown!

                    const double absScaled = pOrigin + n.warpedBeats;
                    if (absScaled >= scan - kEps && absScaled < subEnd - kEps) {
                        const double absReal = absScaled / speedMul;
                        const double absEndReal =
                            absReal + std::max(0.03, std::min(n.durationBeats / speedMul, (pEnd - absScaled) / speedMul));
                        const int offset = sampleOffsetOf(absReal, rawStart, beatsPerSample, numSamples);
                        const int vBias = evalVelBias(params, absReal, engaged, padY) + (topBreakdown ? 10 : 0);
                        triggerNote(n.noteNumber, static_cast<int>(n.velocity) + vBias, n.lane, absEndReal, offset,
                                    outMidi, isDrum);
                    }
                }
                scan = subEnd;
            }
        }

        // 3. Optional continuous CC/Velocity + XY->CC expression ticks
        const auto thinIdx = static_cast<std::size_t>(std::clamp(params.choice(ParamId::ccThinMs), 0, 3));
        const double tickBeats = kCcThinMsValues[thinIdx] * 1.0e-3 * (bpm / 60.0);
        std::int64_t tickIdx = static_cast<std::int64_t>(std::ceil((windowStart - kEps) / tickBeats));
        for (int iter = 0; iter < 128; ++iter) {
            const double tickBeat = static_cast<double>(tickIdx) * tickBeats;
            if (tickBeat >= windowEnd - kEps)
                break;
            if (tickBeat >= windowStart - kEps) {
                const int offset = sampleOffsetOf(tickBeat, rawStart, beatsPerSample, numSamples);
                emitContinuousAt(params, tickBeat, offset, outMidi, engaged, curProg, padX, padY);
            }
            ++tickIdx;
        }

        // Restore UI preview plan reflecting active XY modulation!
        ensurePlan(params, previewPhrase, bpm, engaged, curProg, padX, padY);
        outMidi.sort();
        lastEventCount = outMidi.count;

        std::uint8_t act = 0;
        for (int i = 0; i < outMidi.count; ++i) {
            const auto& ev = outMidi.events[static_cast<std::size_t>(i)];
            const std::uint8_t hi = ev.data[0] & 0xF0u;
            if (ev.isNoteOn())
                act |= kActNote;
            else if (hi == 0xB0u || hi == 0xD0u || hi == 0xE0u)
                act |= kActCc;
        }
        if (engaged)
            act |= kActPad;
        if (!isDrum && h.count > 0)
            act |= kActSlice;
        if (capturedCount > 0)
            act |= kActClip;
        activityMask = act;

        renderAuditionAudio(params, left, right, numSamples);
    }

    void renderAuditionAudio(const Params& params, float* left, float* right,
                             int numSamples) noexcept CL_NONBLOCKING {
        if (left == nullptr || right == nullptr || numSamples <= 0)
            return;
        const bool enabled = params.on(ParamId::audition);
        const float gainDb = params[ParamId::auditionGain];
        for (int i = 0; i < numSamples; ++i)
            audition.renderSample(gainDb, enabled, left[i], right[i]);
    }
};

Engine::Engine() : impl_(std::make_unique<Impl>()) { impl_->prepare(48000.0); }
Engine::~Engine() = default;

void Engine::prepare(double sampleRate, int /*maxBlockSize*/) { impl_->prepare(sampleRate); }
void Engine::reset() noexcept CL_NONBLOCKING { impl_->reset(); }
void Engine::setLaneCurves(const LaneCurves& curves) noexcept CL_NONBLOCKING { impl_->curves = curves; }
void Engine::setPatternBank(const PatternBank& bank) noexcept CL_NONBLOCKING { impl_->bank = bank; }

void Engine::inputNoteOn(int midiNote, int velocity, double ppq) noexcept CL_NONBLOCKING {
    if (midiNote >= 60 && midiNote < 60 + kPerfPadCount) {
        const int p = midiNote - 60;
        impl_->midiPadHeld |= static_cast<std::uint8_t>(1u << static_cast<unsigned>(p));
        impl_->lastMidiPadProg = p;
    }
    if (impl_->held.count == 0)
        impl_->latched.clear();
    impl_->physicalHeld.push(midiNote, velocity);
    impl_->held.push(midiNote, velocity);
    impl_->latched.push(midiNote, velocity);
    impl_->recordClipNoteOn(midiNote, velocity, ppq);
}

void Engine::inputNoteOff(int midiNote, double ppq) noexcept CL_NONBLOCKING {
    if (midiNote >= 60 && midiNote < 60 + kPerfPadCount) {
        const int p = midiNote - 60;
        impl_->midiPadHeld = static_cast<std::uint8_t>(impl_->midiPadHeld & ~(1u << static_cast<unsigned>(p)));
        if (impl_->midiPadHeld == 0u) {
            impl_->lastMidiPadProg = -1;
        } else if (impl_->lastMidiPadProg == p) {
            for (int i = 0; i < kPerfPadCount; ++i) {
                if ((impl_->midiPadHeld & (1u << static_cast<unsigned>(i))) != 0u) {
                    impl_->lastMidiPadProg = i;
                    break;
                }
            }
        }
    }
    impl_->physicalHeld.remove(midiNote);
    if (!impl_->sustainDown)
        impl_->held.remove(midiNote);
    impl_->recordClipNoteOff(midiNote, ppq);
}

void Engine::inputSustain(bool down) noexcept CL_NONBLOCKING {
    impl_->sustainDown = down;
    if (!down)
        impl_->held = impl_->physicalHeld;
}

void Engine::inputAllNotesOff() noexcept CL_NONBLOCKING {
    impl_->physicalHeld.clear();
    impl_->held.clear();
    impl_->latched.clear();
    impl_->midiPadHeld = 0;
    impl_->lastMidiPadProg = -1;
    impl_->sustainDown = false;
}

void Engine::process(const Params& params, const Transport& transport, MidiEventBuffer& outMidi, float* left,
                     float* right, int numSamples) noexcept CL_NONBLOCKING {
    impl_->process(params, transport, outMidi, left, right, numSamples);
}

const PhrasePlan& Engine::currentPlan() const noexcept CL_NONBLOCKING { return impl_->plan; }
HeldNotes Engine::activeHeldNotes() const noexcept CL_NONBLOCKING { return impl_->lastEffectiveHeld; }
std::int64_t Engine::currentPhraseIndex() const noexcept CL_NONBLOCKING { return impl_->lastPhraseIndex; }
int Engine::activeScene() const noexcept CL_NONBLOCKING { return impl_->currentScene; }
int Engine::pendingScene() const noexcept CL_NONBLOCKING { return impl_->pendingSceneIdx; }
int Engine::activePadProgram() const noexcept CL_NONBLOCKING { return impl_->activeProg; }
int Engine::pendingPadProgram() const noexcept CL_NONBLOCKING { return impl_->pendingProg; }
bool Engine::padEngaged() const noexcept CL_NONBLOCKING { return impl_->engaged; }
std::uint8_t Engine::midiPadMask() const noexcept CL_NONBLOCKING { return impl_->midiPadHeld; }
int Engine::capturedClipCount() const noexcept CL_NONBLOCKING { return impl_->capturedCount; }
double Engine::playheadPhraseBeats() const noexcept CL_NONBLOCKING { return impl_->playheadBeats; }
double Engine::lanePpq() const noexcept CL_NONBLOCKING { return impl_->lastLanePpq; }
std::array<float, kLaneCount> Engine::lanePhases() const noexcept CL_NONBLOCKING { return impl_->phases; }
std::array<float, kLaneCount> Engine::laneValues() const noexcept CL_NONBLOCKING { return impl_->values; }
std::array<float, kDrumLaneCount> Engine::laneHitFlashes() const noexcept CL_NONBLOCKING { return impl_->laneFlashes; }
int Engine::lastBlockEventCount() const noexcept CL_NONBLOCKING { return impl_->lastEventCount; }
std::uint8_t Engine::lastActivityMask() const noexcept CL_NONBLOCKING { return impl_->activityMask; }
bool Engine::isPlaying() const noexcept CL_NONBLOCKING { return impl_->playingState; }

std::uint64_t Engine::activeNotesLowMask() const noexcept CL_NONBLOCKING {
    std::uint64_t mask = 0;
    for (const auto& s : impl_->active) {
        if (s.active && s.note >= 0 && s.note < 64)
            mask |= (std::uint64_t{1} << static_cast<unsigned>(s.note));
    }
    return mask;
}

std::uint64_t Engine::activeNotesHighMask() const noexcept CL_NONBLOCKING {
    std::uint64_t mask = 0;
    for (const auto& s : impl_->active) {
        if (s.active && s.note >= 64)
            mask |= (std::uint64_t{1} << static_cast<unsigned>(s.note - 64));
    }
    return mask;
}

} // namespace cl
