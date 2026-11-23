// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/engine.h"

#include "audition.h"

#include <hba/dsp/denormals.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cl {

namespace {

inline constexpr int kMaxActiveNotes = 32;

int toMidi7Bit(double bipolar) noexcept CL_NONBLOCKING {
    return std::clamp(static_cast<int>(std::lround((bipolar * 0.5 + 0.5) * 127.0)), 0, 127);
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

} // namespace

struct Engine::Impl {
    struct ActiveNote {
        bool active = false;
        std::uint8_t channel = 1;
        std::int8_t note = 0;
        double absStartBeat = 0.0;
        double absEndBeat = 0.0;
        float bendSemitones = 0.0f;
        int lastBend14 = 8192;
    };

    double sampleRate = 48000.0;
    LaneCurves curves = defaultLaneCurves();
    PhraseOverrides overrides{};
    HeldNotes physicalHeld{};
    HeldNotes held{};
    HeldNotes latched{};
    HeldNotes lastEffectiveHeld{};
    bool sustainDown = false;
    bool wasLatch = false;

    PhrasePlan plan{};
    std::array<ActiveNote, kMaxActiveNotes> active{};

    bool wasPlaying = false;
    double expectedNextPpq = 0.0;
    OutputMode lastOutputMode = OutputMode::mono;
    InputMode lastInputMode = InputMode::seed;
    bool mpeRpnSent = false;
    int lastMonoBendRangeSent = -1;
    std::uint8_t nextMpeChannel = 2;

    // Deduplication state for continuous controllers (7-bit CC / 14-bit PB / 7-bit AT).
    std::array<int, 16> lastCc74{};
    std::array<int, 16> lastCc1{};
    std::array<int, 16> lastCc11{};
    std::array<int, 16> lastCc2{};
    std::array<int, 16> lastCc71{};
    std::array<int, 16> lastPressure{};
    std::array<int, 16> lastBend{};

    // Telemetry for Visage UI
    double playheadBeats = 0.0;
    double lastLanePpq = 0.0;
    std::int64_t lastPhraseIndex = 0;
    std::array<float, kLaneCount> phases{};
    std::array<float, kLaneCount> values{};
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
        lastCc74.fill(-1);
        lastCc1.fill(-1);
        lastCc11.fill(-1);
        lastCc2.fill(-1);
        lastCc71.fill(-1);
        lastPressure.fill(-1);
        lastBend.fill(-1);
    }

    void reset() noexcept CL_NONBLOCKING {
        for (auto& slot : active)
            slot.active = false;
        physicalHeld.clear();
        held.clear();
        latched.clear();
        lastEffectiveHeld.clear();
        sustainDown = false;
        wasLatch = false;
        wasPlaying = false;
        expectedNextPpq = 0.0;
        mpeRpnSent = false;
        lastMonoBendRangeSent = -1;
        nextMpeChannel = 2;
        playheadBeats = 0.0;
        lastLanePpq = 0.0;
        lastPhraseIndex = 0;
        phases.fill(0.0f);
        values.fill(0.0f);
        lastEventCount = 0;
        activityMask = 0;
        playingState = false;
        clearDedupe();
        audition.reset();
        plan.phraseIndex = -1;
        plan.signature = 0;
    }

    const HeldNotes& effectiveHeld(const Params& params) const noexcept CL_NONBLOCKING {
        return params.on(ParamId::latch) ? latched : held;
    }

    void flushAllActive(MidiEventBuffer& outMidi, int sampleOffset) noexcept CL_NONBLOCKING {
        for (auto& slot : active) {
            if (!slot.active)
                continue;
            outMidi.pushCritical(MidiEvent::noteOff(sampleOffset, slot.channel, slot.note));
            if (slot.lastBend14 != 8192) {
                outMidi.pushCritical(MidiEvent::pitchBend(sampleOffset, slot.channel, 8192, 2));
                lastBend[static_cast<std::size_t>(slot.channel - 1)] = 8192;
            }
            audition.noteOff(slot.channel, slot.note);
            slot.active = false;
        }
        audition.allNotesOff();
    }

    void emitMonoBendRpn(MidiEventBuffer& outMidi, int sampleOffset, int semitones) noexcept CL_NONBLOCKING {
        outMidi.push(MidiEvent::controlChange(sampleOffset, 1, 101, 0, 0));
        outMidi.push(MidiEvent::controlChange(sampleOffset, 1, 100, 0, 0));
        outMidi.push(MidiEvent::controlChange(sampleOffset, 1, 6, std::clamp(semitones, 1, 48), 0));
        outMidi.push(MidiEvent::controlChange(sampleOffset, 1, 101, 127, 0));
        outMidi.push(MidiEvent::controlChange(sampleOffset, 1, 100, 127, 0));
        lastMonoBendRangeSent = semitones;
    }

    void emitMpeInit(MidiEventBuffer& outMidi, int sampleOffset) noexcept CL_NONBLOCKING {
        // MPE Specification Lower Zone: Master Ch 1, 14 member channels (Ch 2..15), ±48 st member PB.
        outMidi.push(MidiEvent::controlChange(sampleOffset, 1, 101, 0, 0));
        outMidi.push(MidiEvent::controlChange(sampleOffset, 1, 100, 6, 0));
        outMidi.push(MidiEvent::controlChange(sampleOffset, 1, 6, 14, 0));
        outMidi.push(MidiEvent::controlChange(sampleOffset, 1, 101, 127, 0));
        outMidi.push(MidiEvent::controlChange(sampleOffset, 1, 100, 127, 0));
        // Member Pitch Bend Sensitivity RPN 00 00 = 48 semitones on Ch 2 (applies to Lower Zone).
        outMidi.push(MidiEvent::controlChange(sampleOffset, 2, 101, 0, 0));
        outMidi.push(MidiEvent::controlChange(sampleOffset, 2, 100, 0, 0));
        outMidi.push(MidiEvent::controlChange(sampleOffset, 2, 6, 48, 0));
        outMidi.push(MidiEvent::controlChange(sampleOffset, 2, 101, 127, 0));
        outMidi.push(MidiEvent::controlChange(sampleOffset, 2, 100, 127, 0));
        mpeRpnSent = true;
    }

    void ensurePlan(const Params& params, std::int64_t phraseIndex) noexcept CL_NONBLOCKING {
        const auto& h = effectiveHeld(params);
        lastEffectiveHeld = h;
        const std::uint32_t sig = phraseSignature(params, phraseIndex, h, overrides);
        if (plan.phraseIndex != phraseIndex || plan.signature != sig)
            compilePhrase(params, phraseIndex, h, plan, overrides);
    }

    std::uint8_t allocFreeMpeChannel(MidiEventBuffer& outMidi, int offset) noexcept CL_NONBLOCKING {
        for (int tries = 0; tries < 14; ++tries) {
            const std::uint8_t cand = static_cast<std::uint8_t>(2 + ((nextMpeChannel - 2 + tries) % 14));
            bool busy = false;
            for (const auto& slot : active) {
                if (slot.active && slot.channel == cand) {
                    busy = true;
                    break;
                }
            }
            if (!busy) {
                nextMpeChannel = static_cast<std::uint8_t>(2 + ((cand - 2 + 1) % 14));
                return cand;
            }
        }
        // All 14 member channels are busy: cleanly close the earliest-ending active MPE note and reuse its channel.
        int victimIdx = -1;
        for (int i = 0; i < kMaxActiveNotes; ++i) {
            const auto& slot = active[static_cast<std::size_t>(i)];
            if (slot.active && slot.channel >= 2 &&
                (victimIdx < 0 || slot.absEndBeat < active[static_cast<std::size_t>(victimIdx)].absEndBeat)) {
                victimIdx = i;
            }
        }
        if (victimIdx >= 0) {
            auto& victim = active[static_cast<std::size_t>(victimIdx)];
            const std::uint8_t ch = victim.channel;
            outMidi.pushCritical(MidiEvent::noteOff(offset, victim.channel, victim.note));
            victim.active = false;
            nextMpeChannel = static_cast<std::uint8_t>(2 + ((ch - 2 + 1) % 14));
            return ch;
        }
        return 2;
    }

    void triggerNote(const CompiledNote& note, double absStartBeat, double absEndBeat, int offset,
                     MidiEventBuffer& outMidi, const Params& params, OutputMode mode, double pbRangeSt,
                     const std::array<float, kLaneCount>& sends, int velBias) noexcept CL_NONBLOCKING {
        const bool isDrum = (mode == OutputMode::drum);
        const std::uint8_t channel = (mode == OutputMode::mpe) ? allocFreeMpeChannel(outMidi, offset) : note.channel;

        // Never double-trigger the same (channel, note) without closing the prior note first.
        for (auto& slot : active) {
            if (slot.active && slot.channel == channel && slot.note == note.noteNumber) {
                outMidi.pushCritical(MidiEvent::noteOff(offset, slot.channel, slot.note));
                slot.active = false;
            }
        }

        // Allocate a slot; if full, cleanly close the earliest-ending note first.
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
                    active[static_cast<std::size_t>(chosen)].absEndBeat) {
                    chosen = i;
                }
            }
            auto& victim = active[static_cast<std::size_t>(chosen)];
            outMidi.pushCritical(MidiEvent::noteOff(offset, victim.channel, victim.note));
            victim.active = false;
        }

        // MPE v1.0 & clean Mono attack: initialize per-note PB, CC74, and Channel Pressure BEFORE NoteOn (order=1).
        const auto chIdx = static_cast<std::size_t>(channel - 1);
        int initPb14 = 8192;
        if (!isDrum) {
            const double laneBendSt =
                static_cast<double>(std::clamp(laneSum(params, sends, LaneTarget::pitchBend), -1.0f, 1.0f)) *
                std::min(pbRangeSt, 12.0);
            initPb14 = (mode == OutputMode::mpe) ? toPitchBend14(laneBendSt * 0.5, 48.0)
                                                 : toPitchBend14(laneBendSt, pbRangeSt);
            if (lastBend[chIdx] != initPb14) {
                outMidi.push(MidiEvent::pitchBend(offset, channel, initPb14, 1));
                lastBend[chIdx] = initPb14;
            }
            if (mode == OutputMode::mpe) {
                bool hasCc74 = false;
                bool hasAt = false;
                for (int l = 0; l < kLaneCount; ++l) {
                    if (!params.on(laneParam(l, LaneField::on)))
                        continue;
                    const auto t = laneTargetOf(params, l);
                    if (t == LaneTarget::cc74Slide)
                        hasCc74 = true;
                    else if (t == LaneTarget::pressure)
                        hasAt = true;
                }
                if (hasCc74) {
                    const int v74 = toMidi7Bit(
                        std::clamp(static_cast<double>(laneSum(params, sends, LaneTarget::cc74Slide)), -1.0, 1.0));
                    if (lastCc74[chIdx] != v74) {
                        if (outMidi.push(MidiEvent::controlChange(offset, channel, 74, v74, 1)))
                            lastCc74[chIdx] = v74;
                    }
                }
                if (hasAt) {
                    const int vAt = toMidi7Bit(
                        std::clamp(static_cast<double>(laneSum(params, sends, LaneTarget::pressure)), -1.0, 1.0));
                    if (lastPressure[chIdx] != vAt) {
                        if (outMidi.push(MidiEvent::channelPressure(offset, channel, vAt, 1)))
                            lastPressure[chIdx] = vAt;
                    }
                }
            }
        }

        const int velocity = std::clamp(static_cast<int>(note.velocity) + velBias, 1, 127);
        outMidi.pushCritical(MidiEvent::noteOn(offset, channel, note.noteNumber, velocity));

        auto& slot = active[static_cast<std::size_t>(chosen)];
        slot.active = true;
        slot.channel = channel;
        slot.note = note.noteNumber;
        slot.absStartBeat = absStartBeat;
        slot.absEndBeat = absEndBeat;
        slot.bendSemitones = note.bendSemitones;
        slot.lastBend14 = initPb14;
    }

    void emitContinuousAt(const Params& params, double tickBeat, int offset, MidiEventBuffer& outMidi, OutputMode mode,
                          double pbRangeSt) noexcept CL_NONBLOCKING {
        std::array<float, kLaneCount> sends{};
        for (int lane = 0; lane < kLaneCount; ++lane)
            sends[static_cast<std::size_t>(lane)] = static_cast<float>(laneSend(params, curves, lane, tickBeat));

        const auto emitCc = [&](LaneTarget target, int ccNum, std::array<int, 16>& cache) noexcept CL_NONBLOCKING {
            bool anyLane = false;
            for (int l = 0; l < kLaneCount; ++l) {
                if (params.on(laneParam(l, LaneField::on)) && laneTargetOf(params, l) == target) {
                    anyLane = true;
                    break;
                }
            }
            if (!anyLane)
                return;
            const int v = toMidi7Bit(std::clamp(static_cast<double>(laneSum(params, sends, target)), -1.0, 1.0));
            if (mode == OutputMode::mpe && target == LaneTarget::cc74Slide) {
                for (const auto& slot : active) {
                    if (!slot.active)
                        continue;
                    const auto idx = static_cast<std::size_t>(slot.channel - 1);
                    if (cache[idx] != v) {
                        if (outMidi.push(MidiEvent::controlChange(offset, slot.channel, ccNum, v)))
                            cache[idx] = v;
                    }
                }
            } else {
                if (cache[0] != v) {
                    if (outMidi.push(MidiEvent::controlChange(offset, 1, ccNum, v)))
                        cache[0] = v;
                }
            }
        };

        emitCc(LaneTarget::cc74Slide, 74, lastCc74);
        emitCc(LaneTarget::cc1Mod, 1, lastCc1);
        emitCc(LaneTarget::cc11Expr, 11, lastCc11);
        emitCc(LaneTarget::cc2Breath, 2, lastCc2);
        emitCc(LaneTarget::cc71Res, 71, lastCc71);

        // Channel Pressure / Aftertouch
        bool hasAt = false;
        for (int l = 0; l < kLaneCount; ++l) {
            if (params.on(laneParam(l, LaneField::on)) && laneTargetOf(params, l) == LaneTarget::pressure) {
                hasAt = true;
                break;
            }
        }
        if (hasAt) {
            const int atVal =
                toMidi7Bit(std::clamp(static_cast<double>(laneSum(params, sends, LaneTarget::pressure)), -1.0, 1.0));
            if (mode == OutputMode::mpe) {
                for (const auto& slot : active) {
                    if (!slot.active)
                        continue;
                    const auto idx = static_cast<std::size_t>(slot.channel - 1);
                    if (lastPressure[idx] != atVal) {
                        if (outMidi.push(MidiEvent::channelPressure(offset, slot.channel, atVal)))
                            lastPressure[idx] = atVal;
                    }
                }
            } else if (lastPressure[0] != atVal) {
                if (outMidi.push(MidiEvent::channelPressure(offset, 1, atVal)))
                    lastPressure[0] = atVal;
            }
        }

        // Pitch Bend: combine active note glide curve + LaneTarget::pitchBend overlay.
        if (mode != OutputMode::drum) {
            const double laneBendSt =
                static_cast<double>(std::clamp(laneSum(params, sends, LaneTarget::pitchBend), -1.0f, 1.0f)) *
                std::min(pbRangeSt, 12.0);

            if (mode == OutputMode::mpe) {
                for (auto& slot : active) {
                    if (!slot.active)
                        continue;
                    const double dur = std::max(1.0e-4, slot.absEndBeat - slot.absStartBeat);
                    const double frac = std::clamp((tickBeat - slot.absStartBeat) / dur, 0.0, 1.0);
                    const double totalSt = static_cast<double>(slot.bendSemitones) * (frac * frac) + laneBendSt * 0.5;
                    const int pb14 = toPitchBend14(totalSt, 48.0);
                    const auto idx = static_cast<std::size_t>(slot.channel - 1);
                    if (std::abs(pb14 - lastBend[idx]) >= 16) {
                        if (outMidi.push(MidiEvent::pitchBend(offset, slot.channel, pb14))) {
                            lastBend[idx] = pb14;
                            slot.lastBend14 = pb14;
                        }
                    }
                }
            } else {
                // Mono Ch 1: follow most recently triggered active note's pitch bend + lane bend.
                // When no note is held (release tail), hold the previous bend instead of snapping
                // abruptly back to center; triggerNote resets bend cleanly right before the next attack.
                const ActiveNote* newest = nullptr;
                for (const auto& slot : active) {
                    if (slot.active && (newest == nullptr || slot.absStartBeat >= newest->absStartBeat))
                        newest = &slot;
                }
                if (newest != nullptr) {
                    const double dur = std::max(1.0e-4, newest->absEndBeat - newest->absStartBeat);
                    const double frac = std::clamp((tickBeat - newest->absStartBeat) / dur, 0.0, 1.0);
                    const double noteBendSt = static_cast<double>(newest->bendSemitones) * (frac * frac);
                    const int pb14 = toPitchBend14(noteBendSt + laneBendSt, pbRangeSt);
                    if (std::abs(pb14 - lastBend[0]) >= 16) {
                        if (outMidi.push(MidiEvent::pitchBend(offset, 1, pb14))) {
                            lastBend[0] = pb14;
                            for (auto& slot : active) {
                                if (slot.active)
                                    slot.lastBend14 = pb14;
                            }
                        }
                    }
                }
            }
        }
    }

    void process(const Params& params, const Transport& transport, MidiEventBuffer& outMidi, float* left, float* right,
                 int numSamples) noexcept CL_NONBLOCKING {
        hba::dsp::ScopedFlushDenormals ftz;
        outMidi.clear();
        if (numSamples <= 0)
            return;

        const double bpm = std::clamp(transport.bpm, 20.0, 400.0);
        const double beatsPerSample = bpm / 60.0 / sampleRate;
        const auto barsIdx = static_cast<std::size_t>(std::clamp(params.choice(ParamId::phraseBars), 0, 2));
        const double phraseBeats = static_cast<double>(kPhraseBarCounts[barsIdx] * 4);
        const auto mode = static_cast<OutputMode>(std::clamp(params.choice(ParamId::outputMode), 0, 2));
        const auto inputMode = static_cast<InputMode>(std::clamp(params.choice(ParamId::inputMode), 0, 2));
        const auto pbIdx = static_cast<std::size_t>(std::clamp(params.choice(ParamId::bendRange), 0, 2));
        const double pbRangeSt = mode == OutputMode::mpe ? 48.0 : static_cast<double>(kBendRangeSemitones[pbIdx]);

        // Clear latched chord when Latch is toggled OFF so re-enabling later never resurrects a stale chord.
        const bool latchNow = params.on(ParamId::latch);
        if (wasLatch && !latchNow)
            latched = held;
        wasLatch = latchNow;

        // Always keep current UI phrase plan up to date even while stopped.
        const double rawPpq = std::max(0.0, transport.ppq);
        const std::int64_t previewPhrase = static_cast<std::int64_t>(std::floor(rawPpq / phraseBeats));
        ensurePlan(params, previewPhrase);
        lastPhraseIndex = previewPhrase;
        playheadBeats = rawPpq - static_cast<double>(previewPhrase) * phraseBeats;
        lastLanePpq = rawPpq;
        playingState = transport.playing;
        activityMask = 0;

        for (int lane = 0; lane < kLaneCount; ++lane) {
            const auto idx = static_cast<std::size_t>(lane);
            phases[idx] = static_cast<float>(
                lanePhase(rawPpq, laneLengthQuarters(params, lane), params[laneParam(lane, LaneField::phase)]));
            values[idx] = static_cast<float>(laneSend(params, curves, lane, rawPpq));
        }

        // Mode changes flush all active notes cleanly so no channel leaks occur.
        if (mode != lastOutputMode || inputMode != lastInputMode) {
            flushAllActive(outMidi, 0);
            clearDedupe();
            mpeRpnSent = false;
            lastMonoBendRangeSent = -1;
            nextMpeChannel = 2;
            lastOutputMode = mode;
            lastInputMode = inputMode;
        }

        // Releasing all keys in Chord Carve (Latch off) silences active notes cleanly.
        if (inputMode == InputMode::chordCarve && effectiveHeld(params).count == 0) {
            flushAllActive(outMidi, 0);
        }

        if (!transport.playing) {
            if (wasPlaying)
                flushAllActive(outMidi, 0);
            wasPlaying = false;
            lastEventCount = outMidi.count;
            renderAuditionAudio(params, outMidi, left, right, numSamples, mode == OutputMode::drum, pbRangeSt);
            return;
        }

        // Detect transport start, loop wrap, or discontinuous jump.
        const double shiftBeats = static_cast<double>(params[ParamId::shiftMs]) * 1.0e-3 * (bpm / 60.0);
        const double rawStart = transport.ppq - shiftBeats;
        const double rawEnd = rawStart + static_cast<double>(numSamples) * beatsPerSample;

        if (!wasPlaying || transport.ppq < expectedNextPpq - 1.0e-4 ||
            std::abs(transport.ppq - expectedNextPpq) > 0.125) {
            flushAllActive(outMidi, 0);
            clearDedupe();
        }
        wasPlaying = true;
        expectedNextPpq = transport.ppq + static_cast<double>(numSamples) * beatsPerSample;

        // During DAW count-in/pre-roll or positive shiftMs delay before beat 0 arrives,
        // wait cleanly instead of clamping windowStart to 0.0 and machine-gunning beat 0.
        if (rawEnd <= 0.0) {
            lastEventCount = outMidi.count;
            renderAuditionAudio(params, outMidi, left, right, numSamples, mode == OutputMode::drum, pbRangeSt);
            return;
        }

        const double windowStart = std::max(0.0, rawStart);
        const double windowEnd = rawEnd;

        if (mode == OutputMode::mpe && !mpeRpnSent)
            emitMpeInit(outMidi, 0);
        else if (mode == OutputMode::mono && lastMonoBendRangeSent != static_cast<int>(pbRangeSt))
            emitMonoBendRpn(outMidi, 0, static_cast<int>(pbRangeSt));

        // Process note-offs, note-ons, and continuous CC/PB ticks in strict chronological
        // beat order so output is bit-identical across every host block size (1..1024).
        const auto thinIdx = static_cast<std::size_t>(std::clamp(params.choice(ParamId::ccThinMs), 0, 3));
        const double tickBeats = kCcThinMsValues[thinIdx] * 1.0e-3 * (bpm / 60.0);
        constexpr double kEps = 1.0e-9;

        std::int64_t nextTickIdx = static_cast<std::int64_t>(std::ceil((windowStart - kEps) / tickBeats));
        double noteCursor = windowStart;

        for (int iter = 0; iter < 2048; ++iter) {
            // 1. Earliest pending Note Off strictly before windowEnd
            int earliestOffSlot = -1;
            double earliestOffBeat = windowEnd;
            for (int i = 0; i < kMaxActiveNotes; ++i) {
                const auto& slot = active[static_cast<std::size_t>(i)];
                if (slot.active && slot.absEndBeat < earliestOffBeat - kEps) {
                    earliestOffBeat = slot.absEndBeat;
                    earliestOffSlot = i;
                }
            }

            // 2. Earliest pending Note On in [noteCursor, windowEnd)
            int nextNoteIdx = -1;
            double nextNoteBeat = windowEnd;
            double nextNotePEnd = windowEnd;
            {
                double scan = noteCursor;
                for (int pg = 0; pg < 3 && scan < windowEnd - kEps; ++pg) {
                    const std::int64_t pIdx = static_cast<std::int64_t>(std::floor(scan / phraseBeats));
                    const double pOrigin = static_cast<double>(pIdx) * phraseBeats;
                    const double pEnd = pOrigin + phraseBeats;
                    const double subEnd = std::min(windowEnd, pEnd);
                    ensurePlan(params, pIdx);
                    for (int i = 0; i < plan.noteCount; ++i) {
                        const auto& cand = plan.notes[static_cast<std::size_t>(i)];
                        if (cand.muted)
                            continue;
                        const double absStart = pOrigin + cand.warpedBeats;
                        if (absStart >= scan - kEps && absStart < subEnd - kEps) {
                            nextNoteIdx = i;
                            nextNoteBeat = absStart;
                            nextNotePEnd = pEnd;
                            break;
                        }
                    }
                    if (nextNoteIdx >= 0)
                        break;
                    scan = subEnd;
                }
            }

            // 3. Earliest continuous control tick in [windowStart, windowEnd)
            double nextTickBeat = static_cast<double>(nextTickIdx) * tickBeats;
            const bool hasTick = (nextTickBeat < windowEnd - kEps);
            if (!hasTick)
                nextTickBeat = windowEnd;

            // Stop when nothing remains inside [windowStart, windowEnd)
            if (earliestOffSlot < 0 && nextNoteIdx < 0 && !hasTick)
                break;

            // Pick strictly earliest event; ties break: NoteOff -> NoteOn -> ControlTick
            if (earliestOffSlot >= 0 && earliestOffBeat <= nextNoteBeat + kEps &&
                earliestOffBeat <= nextTickBeat + kEps) {
                auto& slot = active[static_cast<std::size_t>(earliestOffSlot)];
                const int offset = sampleOffsetOf(slot.absEndBeat, rawStart, beatsPerSample, numSamples);
                outMidi.pushCritical(MidiEvent::noteOff(offset, slot.channel, slot.note));
                slot.active = false;
            } else if (nextNoteIdx >= 0 && nextNoteBeat <= nextTickBeat + kEps) {
                const CompiledNote note = plan.notes[static_cast<std::size_t>(nextNoteIdx)];
                const double absEnd =
                    std::min(nextNotePEnd - 1.0e-4, nextNoteBeat + std::max(0.03, note.durationBeats));
                const int offset = sampleOffsetOf(nextNoteBeat, rawStart, beatsPerSample, numSamples);

                std::array<float, kLaneCount> sends{};
                for (int l = 0; l < kLaneCount; ++l)
                    sends[static_cast<std::size_t>(l)] = static_cast<float>(laneSend(params, curves, l, nextNoteBeat));
                const int velBias = static_cast<int>(std::lround(laneSum(params, sends, LaneTarget::velocity) * 28.0f));

                triggerNote(note, nextNoteBeat, absEnd, offset, outMidi, params, mode, pbRangeSt, sends, velBias);

                // Advance past simultaneous notes at the exact same beat (e.g., MPE chords) cleanly.
                const std::int64_t pIdx = static_cast<std::int64_t>(std::floor(nextNoteBeat / phraseBeats));
                const double pOrigin = static_cast<double>(pIdx) * phraseBeats;
                for (int k = nextNoteIdx + 1; k < plan.noteCount; ++k) {
                    const CompiledNote& extra = plan.notes[static_cast<std::size_t>(k)];
                    if (extra.muted)
                        continue;
                    const double extraStart = pOrigin + extra.warpedBeats;
                    if (std::abs(extraStart - nextNoteBeat) > kEps)
                        break;
                    const double extraEnd =
                        std::min(nextNotePEnd - 1.0e-4, extraStart + std::max(0.03, extra.durationBeats));
                    triggerNote(extra, extraStart, extraEnd, offset, outMidi, params, mode, pbRangeSt, sends, velBias);
                }
                noteCursor = nextNoteBeat + 2.0e-8;
            } else if (hasTick) {
                if (nextTickBeat >= windowStart - kEps) {
                    const int offset = sampleOffsetOf(nextTickBeat, rawStart, beatsPerSample, numSamples);
                    emitContinuousAt(params, nextTickBeat, offset, outMidi, mode, pbRangeSt);
                }
                ++nextTickIdx;
            }
        }

        // Restore UI preview plan for current phrase head
        ensurePlan(params, previewPhrase);
        outMidi.sort();
        lastEventCount = outMidi.count;

        std::uint8_t act = 0;
        for (int i = 0; i < outMidi.count; ++i) {
            const auto& ev = outMidi.events[static_cast<std::size_t>(i)];
            const std::uint8_t hi = ev.data[0] & 0xF0u;
            if (ev.isNoteOn())
                act |= kActNote;
            else if (hi == 0xE0u)
                act |= kActBend;
            else if (hi == 0xB0u && ev.data[1] == 74)
                act |= kActCc74;
            else if (hi == 0xB0u && ev.data[1] == 1)
                act |= kActCc1;
            else if (hi == 0xD0u)
                act |= kActPressure;
        }
        activityMask = act;

        // 4. Optional lightweight 4-voice stereo audition synth on stereo bus.
        renderAuditionAudio(params, outMidi, left, right, numSamples, mode == OutputMode::drum, pbRangeSt);
    }

    void renderAuditionAudio(const Params& params, const MidiEventBuffer& outMidi, float* left, float* right,
                             int numSamples, bool isDrum, double pbRangeSt) noexcept CL_NONBLOCKING {
        if (left == nullptr || right == nullptr || numSamples <= 0)
            return;

        const bool enabled = params.on(ParamId::audition);
        const float gainDb = params[ParamId::auditionGain];
        const auto tone = static_cast<AuditionTone>(std::clamp(params.choice(ParamId::auditionTone), 0, 3));
        const bool useKit = isDrum || (tone == AuditionTone::kit);

        int evIdx = 0;
        for (int i = 0; i < numSamples; ++i) {
            while (evIdx < outMidi.count && outMidi.events[static_cast<std::size_t>(evIdx)].sampleOffset <= i) {
                const MidiEvent& ev = outMidi.events[static_cast<std::size_t>(evIdx++)];
                const std::uint8_t status = ev.data[0] & 0xF0u;
                if (ev.isNoteOn()) {
                    audition.noteOn(ev.channel(), ev.noteNumber(), ev.data[2], useKit, isDrum);
                } else if (ev.isNoteOff()) {
                    audition.noteOff(ev.channel(), ev.noteNumber());
                } else if (status == 0xE0u && ev.size == 3) {
                    const int v14 = static_cast<int>(ev.data[1]) | (static_cast<int>(ev.data[2]) << 7);
                    audition.setBendSemitones(ev.channel(), (static_cast<double>(v14 - 8192) / 8191.0) * pbRangeSt);
                } else if (status == 0xB0u && ev.size == 3) {
                    if (ev.data[1] == 74)
                        audition.setBrightness(static_cast<double>(ev.data[2]) / 127.0);
                    else if (ev.data[1] == 1)
                        audition.setDrive(static_cast<double>(ev.data[2]) / 127.0);
                } else if (status == 0xD0u && ev.size >= 2) {
                    audition.setPressure(static_cast<double>(ev.data[1]) / 127.0);
                }
            }
            audition.renderSample(gainDb, enabled, tone, left[i], right[i]);
        }
    }
};

Engine::Engine() : impl_(std::make_unique<Impl>()) { impl_->prepare(48000.0); }
Engine::~Engine() = default;

void Engine::prepare(double sampleRate, int /*maxBlockSize*/) { impl_->prepare(sampleRate); }
void Engine::reset() noexcept CL_NONBLOCKING { impl_->reset(); }
void Engine::setLaneCurves(const LaneCurves& curves) noexcept CL_NONBLOCKING { impl_->curves = curves; }
void Engine::setOverrides(const PhraseOverrides& overrides) noexcept CL_NONBLOCKING { impl_->overrides = overrides; }
void Engine::inputNoteOn(int midiNote, int /*velocity*/) noexcept CL_NONBLOCKING {
    if (impl_->held.count == 0)
        impl_->latched.clear();
    impl_->physicalHeld.push(midiNote);
    impl_->held.push(midiNote);
    impl_->latched.push(midiNote);
}
void Engine::inputNoteOff(int midiNote) noexcept CL_NONBLOCKING {
    impl_->physicalHeld.remove(midiNote);
    if (!impl_->sustainDown)
        impl_->held.remove(midiNote);
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
    impl_->sustainDown = false;
}

void Engine::process(const Params& params, const Transport& transport, MidiEventBuffer& outMidi, float* left,
                     float* right, int numSamples) noexcept CL_NONBLOCKING {
    impl_->process(params, transport, outMidi, left, right, numSamples);
}

const PhrasePlan& Engine::currentPlan() const noexcept CL_NONBLOCKING { return impl_->plan; }
HeldNotes Engine::activeHeldNotes() const noexcept CL_NONBLOCKING { return impl_->lastEffectiveHeld; }
std::int64_t Engine::currentPhraseIndex() const noexcept CL_NONBLOCKING { return impl_->lastPhraseIndex; }
double Engine::playheadPhraseBeats() const noexcept CL_NONBLOCKING { return impl_->playheadBeats; }
double Engine::lanePpq() const noexcept CL_NONBLOCKING { return impl_->lastLanePpq; }
std::array<float, kLaneCount> Engine::lanePhases() const noexcept CL_NONBLOCKING { return impl_->phases; }
std::array<float, kLaneCount> Engine::laneValues() const noexcept CL_NONBLOCKING { return impl_->values; }
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
