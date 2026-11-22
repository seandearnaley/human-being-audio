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
    HeldNotes held{};

    PhrasePlan plan{};
    std::array<ActiveNote, kMaxActiveNotes> active{};

    bool wasPlaying = false;
    double expectedNextPpq = 0.0;
    OutputMode lastOutputMode = OutputMode::mono;
    InputMode lastInputMode = InputMode::seed;
    bool mpeRpnSent = false;

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
    std::array<float, kLaneCount> phases{};
    std::array<float, kLaneCount> values{};
    int lastEventCount = 0;
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
        held.clear();
        wasPlaying = false;
        expectedNextPpq = 0.0;
        mpeRpnSent = false;
        playheadBeats = 0.0;
        lastLanePpq = 0.0;
        phases.fill(0.0f);
        values.fill(0.0f);
        lastEventCount = 0;
        playingState = false;
        clearDedupe();
        audition.reset();
        plan.phraseIndex = -1;
        plan.signature = 0;
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
            audition.noteOff(slot.note);
            slot.active = false;
        }
        audition.allNotesOff();
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
        const std::uint32_t sig = phraseSignature(params, phraseIndex, held);
        if (plan.phraseIndex != phraseIndex || plan.signature != sig)
            compilePhrase(params, phraseIndex, held, plan);
    }

    void triggerNote(const CompiledNote& note, double absStartBeat, double absEndBeat, int offset,
                     MidiEventBuffer& outMidi, bool isDrum, int velBias) noexcept CL_NONBLOCKING {
        // Never double-trigger the same (channel, note) without closing the prior note first.
        for (auto& slot : active) {
            if (slot.active && slot.channel == note.channel && slot.note == note.noteNumber) {
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

        // Reset channel pitch bend cleanly right before attack if it drifted.
        const auto chIdx = static_cast<std::size_t>(note.channel - 1);
        if (!isDrum && lastBend[chIdx] != 8192) {
            outMidi.push(MidiEvent::pitchBend(offset, note.channel, 8192, 1));
            lastBend[chIdx] = 8192;
        }

        const int velocity = std::clamp(static_cast<int>(note.velocity) + velBias, 1, 127);
        outMidi.pushCritical(MidiEvent::noteOn(offset, note.channel, note.noteNumber, velocity));

        auto& slot = active[static_cast<std::size_t>(chosen)];
        slot.active = true;
        slot.channel = note.channel;
        slot.note = note.noteNumber;
        slot.absStartBeat = absStartBeat;
        slot.absEndBeat = absEndBeat;
        slot.bendSemitones = note.bendSemitones;
        slot.lastBend14 = 8192;
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
                double noteBendSt = 0.0;
                const ActiveNote* newest = nullptr;
                for (const auto& slot : active) {
                    if (slot.active && (newest == nullptr || slot.absStartBeat >= newest->absStartBeat))
                        newest = &slot;
                }
                if (newest != nullptr) {
                    const double dur = std::max(1.0e-4, newest->absEndBeat - newest->absStartBeat);
                    const double frac = std::clamp((tickBeat - newest->absStartBeat) / dur, 0.0, 1.0);
                    noteBendSt = static_cast<double>(newest->bendSemitones) * (frac * frac);
                }
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
        const auto inputMode = static_cast<InputMode>(std::clamp(params.choice(ParamId::inputMode), 0, 1));
        const auto pbIdx = static_cast<std::size_t>(std::clamp(params.choice(ParamId::bendRange), 0, 2));
        const double pbRangeSt = mode == OutputMode::mpe ? 48.0 : static_cast<double>(kBendRangeSemitones[pbIdx]);

        // Always keep current UI phrase plan up to date even while stopped.
        const double rawPpq = std::max(0.0, transport.ppq);
        const std::int64_t previewPhrase = static_cast<std::int64_t>(std::floor(rawPpq / phraseBeats));
        ensurePlan(params, previewPhrase);
        playheadBeats = rawPpq - static_cast<double>(previewPhrase) * phraseBeats;
        lastLanePpq = rawPpq;
        playingState = transport.playing;

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
            lastOutputMode = mode;
            lastInputMode = inputMode;
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
        const double windowStart = std::max(0.0, transport.ppq - shiftBeats);
        const double windowEnd = windowStart + static_cast<double>(numSamples) * beatsPerSample;

        if (!wasPlaying || transport.ppq < expectedNextPpq - 1.0e-4 ||
            std::abs(transport.ppq - expectedNextPpq) > 0.125) {
            flushAllActive(outMidi, 0);
            clearDedupe();
        }
        wasPlaying = true;
        expectedNextPpq = transport.ppq + static_cast<double>(numSamples) * beatsPerSample;

        if (mode == OutputMode::mpe && !mpeRpnSent)
            emitMpeInit(outMidi, 0);

        // Process note-offs, note-ons, and continuous CC/PB ticks in strict chronological
        // beat order so output is bit-identical across every host block size (1..1024).
        const auto thinIdx = static_cast<std::size_t>(std::clamp(params.choice(ParamId::ccThinMs), 0, 3));
        const double tickBeats = kCcThinMsValues[thinIdx] * 1.0e-3 * (bpm / 60.0);
        constexpr double kEps = 1.0e-9;

        std::int64_t nextTickIdx = static_cast<std::int64_t>(std::ceil((windowStart - kEps) / tickBeats));
        double noteCursor = windowStart;

        for (int iter = 0; iter < 512; ++iter) {
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
                        const double absStart = pOrigin + plan.notes[static_cast<std::size_t>(i)].warpedBeats;
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
                const int offset = sampleOffsetOf(slot.absEndBeat, windowStart, beatsPerSample, numSamples);
                outMidi.pushCritical(MidiEvent::noteOff(offset, slot.channel, slot.note));
                slot.active = false;
            } else if (nextNoteIdx >= 0 && nextNoteBeat <= nextTickBeat + kEps) {
                const CompiledNote note = plan.notes[static_cast<std::size_t>(nextNoteIdx)];
                const double absEnd =
                    std::min(nextNotePEnd - 1.0e-4, nextNoteBeat + std::max(0.03, note.durationBeats));
                const int offset = sampleOffsetOf(nextNoteBeat, windowStart, beatsPerSample, numSamples);

                std::array<float, kLaneCount> velSends{};
                for (int l = 0; l < kLaneCount; ++l)
                    velSends[static_cast<std::size_t>(l)] =
                        static_cast<float>(laneSend(params, curves, l, nextNoteBeat));
                const int velBias =
                    static_cast<int>(std::lround(laneSum(params, velSends, LaneTarget::velocity) * 28.0f));

                triggerNote(note, nextNoteBeat, absEnd, offset, outMidi, mode == OutputMode::drum, velBias);

                // Advance past simultaneous notes at the exact same beat (e.g., MPE chords) cleanly.
                // Check if another note in `plan` starts at the exact same beat right after `nextNoteIdx`.
                const std::int64_t pIdx = static_cast<std::int64_t>(std::floor(nextNoteBeat / phraseBeats));
                const double pOrigin = static_cast<double>(pIdx) * phraseBeats;
                for (int k = nextNoteIdx + 1; k < plan.noteCount; ++k) {
                    const CompiledNote& extra = plan.notes[static_cast<std::size_t>(k)];
                    const double extraStart = pOrigin + extra.warpedBeats;
                    if (std::abs(extraStart - nextNoteBeat) > kEps)
                        break;
                    const double extraEnd =
                        std::min(nextNotePEnd - 1.0e-4, extraStart + std::max(0.03, extra.durationBeats));
                    triggerNote(extra, extraStart, extraEnd, offset, outMidi, mode == OutputMode::drum, velBias);
                }
                noteCursor = nextNoteBeat + 2.0e-8;
            } else if (hasTick) {
                if (nextTickBeat >= windowStart - kEps) {
                    const int offset = sampleOffsetOf(nextTickBeat, windowStart, beatsPerSample, numSamples);
                    emitContinuousAt(params, nextTickBeat, offset, outMidi, mode, pbRangeSt);
                }
                ++nextTickIdx;
            }
        }

        // Restore UI preview plan for current phrase head
        ensurePlan(params, previewPhrase);
        outMidi.sort();
        lastEventCount = outMidi.count;

        // 4. Optional lightweight built-in audition voice on stereo bus.
        renderAuditionAudio(params, outMidi, left, right, numSamples, mode == OutputMode::drum, pbRangeSt);
    }

    void renderAuditionAudio(const Params& params, const MidiEventBuffer& outMidi, float* left, float* right,
                             int numSamples, bool isDrum, double pbRangeSt) noexcept CL_NONBLOCKING {
        if (left == nullptr || right == nullptr || numSamples <= 0)
            return;

        const bool enabled = params.on(ParamId::audition);
        const float gainDb = params[ParamId::auditionGain];

        int evIdx = 0;
        for (int i = 0; i < numSamples; ++i) {
            while (evIdx < outMidi.count && outMidi.events[static_cast<std::size_t>(evIdx)].sampleOffset <= i) {
                const MidiEvent& ev = outMidi.events[static_cast<std::size_t>(evIdx++)];
                const std::uint8_t status = ev.data[0] & 0xF0u;
                if (ev.isNoteOn()) {
                    audition.noteOn(ev.noteNumber(), ev.data[2], isDrum);
                } else if (ev.isNoteOff()) {
                    audition.noteOff(ev.noteNumber());
                } else if (status == 0xE0u && ev.size == 3) {
                    const int v14 = static_cast<int>(ev.data[1]) | (static_cast<int>(ev.data[2]) << 7);
                    audition.setBendSemitones((static_cast<double>(v14 - 8192) / 8191.0) * pbRangeSt);
                } else if (status == 0xB0u && ev.size == 3) {
                    if (ev.data[1] == 74)
                        audition.setBrightness(static_cast<double>(ev.data[2]) / 127.0);
                    else if (ev.data[1] == 1)
                        audition.setDrive(static_cast<double>(ev.data[2]) / 127.0);
                }
            }
            audition.renderSample(gainDb, enabled, left[i], right[i]);
        }
    }
};

Engine::Engine() : impl_(std::make_unique<Impl>()) { impl_->prepare(48000.0); }
Engine::~Engine() = default;

void Engine::prepare(double sampleRate, int /*maxBlockSize*/) { impl_->prepare(sampleRate); }
void Engine::reset() noexcept CL_NONBLOCKING { impl_->reset(); }
void Engine::setLaneCurves(const LaneCurves& curves) noexcept CL_NONBLOCKING { impl_->curves = curves; }
void Engine::inputNoteOn(int midiNote, int /*velocity*/) noexcept CL_NONBLOCKING { impl_->held.push(midiNote); }
void Engine::inputNoteOff(int midiNote) noexcept CL_NONBLOCKING { impl_->held.remove(midiNote); }
void Engine::inputAllNotesOff() noexcept CL_NONBLOCKING { impl_->held.clear(); }

void Engine::process(const Params& params, const Transport& transport, MidiEventBuffer& outMidi, float* left,
                     float* right, int numSamples) noexcept CL_NONBLOCKING {
    impl_->process(params, transport, outMidi, left, right, numSamples);
}

const PhrasePlan& Engine::currentPlan() const noexcept CL_NONBLOCKING { return impl_->plan; }
double Engine::playheadPhraseBeats() const noexcept CL_NONBLOCKING { return impl_->playheadBeats; }
double Engine::lanePpq() const noexcept CL_NONBLOCKING { return impl_->lastLanePpq; }
std::array<float, kLaneCount> Engine::lanePhases() const noexcept CL_NONBLOCKING { return impl_->phases; }
std::array<float, kLaneCount> Engine::laneValues() const noexcept CL_NONBLOCKING { return impl_->values; }
int Engine::lastBlockEventCount() const noexcept CL_NONBLOCKING { return impl_->lastEventCount; }
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
