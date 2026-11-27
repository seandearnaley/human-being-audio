// SPDX-License-Identifier: AGPL-3.0-or-later
#include "engine_impl.h"
#include "pad_programs.h"

#include <hba/dsp/denormals.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cl {

namespace {

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

} // namespace

void Engine::Impl::prepare(double sr) noexcept CL_NONBLOCKING {
    sampleRate = sr > 1000.0 ? sr : 48000.0;
    audition.prepare(sampleRate);
    reset();
}

void Engine::Impl::clearDedupe() noexcept CL_NONBLOCKING {
    lastCc74 = lastCc1 = lastCc11 = lastCc71 = lastPressure = -1;
    lastBend = 8192;
}

void Engine::Impl::reset() noexcept CL_NONBLOCKING {
    for (auto& slot : active)
        slot.active = false;
    blockHitCount = 0;
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
    activeProg = 0;
    pendingProg = -1;
    engaged = false;
    pendingEngage = false;
    padSwitchBeat = 0.0;
    repeatAnchor = 0.0;
    repeatBar = 0;
    repeatFill = false;
    fillLatch = {};
    wasPlaying = false;
    expectedNextPpq = 0.0;
    lastTransportPpq = 0.0;
    lastEngineMode = EngineMode::drum;
    lastSlicerStyle = SlicerStyle::stabChop;
    pos = {};
    lastLanePpq = 0.0;
    phases.fill(0.0f);
    values.fill(0.0f);
    laneFlashes.fill(0.0f);
    lastEventCount = 0;
    activityMask = 0;
    playingState = false;
    clearDedupe();
    audition.reset();
}

const HeldNotes& Engine::Impl::effectiveHeld(const Params& params) const noexcept CL_NONBLOCKING {
    return params.on(ParamId::slicerLatch) ? latched : held;
}

void Engine::Impl::flushAllActive(MidiEventBuffer& outMidi, int sampleOffset) noexcept CL_NONBLOCKING {
    for (auto& slot : active) {
        if (!slot.active)
            continue;
        outMidi.pushCritical(MidiEvent::noteOff(sampleOffset, slot.channel, slot.note));
        audition.noteOff(slot.note);
        slot.active = false;
    }
    audition.allNotesOff();
}

void Engine::Impl::triggerNote(std::int8_t noteNum, int velocity, int laneHint, double absEndBeat, int offset,
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
            if (active[static_cast<std::size_t>(i)].absEndBeat < active[static_cast<std::size_t>(chosen)].absEndBeat)
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
        laneFlashes[static_cast<std::size_t>(laneHint)] = std::max(laneFlashes[static_cast<std::size_t>(laneHint)], f);
    }
    if (isDrum)
        audition.triggerDrumLane(laneHint >= 0 ? laneHint : 0, clampedVel);
    else
        audition.noteOn(noteNum, clampedVel, false, laneHint);
}

void Engine::Impl::recordClipNoteOn(int midiNote, int vel, double ppq) noexcept CL_NONBLOCKING {
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

void Engine::Impl::recordClipNoteOff(int midiNote, double ppq) noexcept CL_NONBLOCKING {
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

void Engine::Impl::emitContinuousAt(const Params& params, double tickBeat, int offset, MidiEventBuffer& outMidi,
                                    bool padOn, float padX, float padY) noexcept CL_NONBLOCKING {
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
    const bool xyDrivesCc74 = padOn && (xyCc == PadCcMode::cc74AndCc1 || xyCc == PadCcMode::cc74AndVel);
    const bool xyDrivesCc1 = padOn && (xyCc == PadCcMode::cc74AndCc1 || xyCc == PadCcMode::cc1AndAt);
    const bool xyDrivesAt = padOn && (xyCc == PadCcMode::cc1AndAt);

    // CC74 Slide (Brightness)
    if (xyDrivesCc74 || targetOn(LaneTarget::cc74Slide)) {
        const double val01 =
            xyDrivesCc74
                ? static_cast<double>(padY)
                : (std::clamp(static_cast<double>(laneSum(params, sends, LaneTarget::cc74Slide)), -1.0, 1.0) * 0.5 +
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
                ? static_cast<double>(padX)
                : (std::clamp(static_cast<double>(laneSum(params, sends, LaneTarget::cc1Mod)), -1.0, 1.0) * 0.5 + 0.5);
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
            xyDrivesAt
                ? static_cast<double>(padY)
                : (std::clamp(static_cast<double>(laneSum(params, sends, LaneTarget::pressure)), -1.0, 1.0) * 0.5 +
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

int Engine::Impl::evalVelBias(const Params& params, double beat, bool padOn, float padY) const noexcept CL_NONBLOCKING {
    std::array<float, kLaneCount> sends{};
    for (int l = 0; l < kLaneCount; ++l)
        sends[static_cast<std::size_t>(l)] = static_cast<float>(laneSend(params, curves, l, beat));
    int bias = static_cast<int>(std::lround(laneSum(params, sends, LaneTarget::velocity) * 26.0f));
    const auto xyCc = static_cast<PadCcMode>(std::clamp(params.choice(ParamId::padCcMode), 0, 3));
    if (padOn && xyCc == PadCcMode::cc74AndVel)
        bias += static_cast<int>(std::lround((padY - 0.5f) * 44.0f));
    return bias;
}

const Pattern& Engine::Impl::pattern() const noexcept CL_NONBLOCKING {
    return groove.bank.scenes[static_cast<std::size_t>(std::clamp(currentScene, 0, kSceneCount - 1))];
}

const DensityMap& Engine::Impl::densityMap() const noexcept CL_NONBLOCKING {
    return groove.maps[static_cast<std::size_t>(std::clamp(currentScene, 0, kSceneCount - 1))];
}

// True when bar `absBar` plays its fill. The schedule is a pure function of the bar. fillNow is sampled
// once per bar, at the first block that reaches the earliest moment a fill-window hit could start
// (a full early nudge, lane offset and flam before the window). Before that moment no fill-window hit
// can sound, so the bar resolves to the same notes whichever way the latch will fall.
bool Engine::Impl::fillActiveFor(std::int64_t absBar, bool fillNow, int everyBars, double scaledEnd,
                                 double beatsPerMs) noexcept CL_NONBLOCKING {
    const bool scheduled = fillScheduled(absBar, everyBars);
    FillLatch& latch = fillLatch[static_cast<std::size_t>(absBar & 1)];
    if (latch.bar != absBar) {
        const double samplePoint = static_cast<double>(absBar) * 4.0 +
                                   static_cast<double>(fillWindowStart(pattern().fill)) * 0.25 -
                                   maxEarlyBeats(beatsPerMs);
        if (scaledEnd < samplePoint)
            return scheduled;
        latch = {absBar, fillNow};
    }
    return scheduled || latch.on;
}

void Engine::Impl::pushDrumHit(const Block& b, int lane, double hostBeat, double hostDur,
                               int velocity) noexcept CL_NONBLOCKING {
    if (blockHitCount >= kMaxBlockHits)
        return;
    BlockHit& hit = blockHits[static_cast<std::size_t>(blockHitCount++)];
    hit.beat = hostBeat;
    hit.durBeats = hostDur;
    hit.note = effectiveLaneMidiNote(b.noteMap, lane, pattern().lanes[static_cast<std::size_t>(lane)].midiNote);
    hit.lane = static_cast<std::int8_t>(lane);
    hit.velocity = static_cast<std::uint8_t>(std::clamp(velocity, 1, 127));
    hit.drum = true;
}

// The pattern itself: resolve every bar that can reach into this block, and keep the hits that start in it.
void Engine::Impl::playPattern(const Block& b, bool fillNow) noexcept CL_NONBLOCKING {
    const Params& params = *b.params;
    const Pattern& pat = pattern();
    const int bars = patternBars(pat);
    const int everyBars = kFillEveryBars[static_cast<std::size_t>(std::clamp(params.choice(ParamId::fillEvery), 0, 4))];
    const HitTiming timing{static_cast<double>(params[ParamId::swing]), static_cast<double>(params[ParamId::gateLen]),
                           b.beatsPerMs};
    const double scaledStart = b.windowStart * b.rate;
    const double scaledEnd = b.windowEnd * b.rate;
    const auto firstBar = static_cast<std::int64_t>(std::floor((scaledStart - maxLateBeats(b.beatsPerMs)) / 4.0));
    const auto lastBar = static_cast<std::int64_t>(std::floor((scaledEnd + maxEarlyBeats(b.beatsPerMs)) / 4.0));
    const bool filterKit = engaged && b.program == PadProgram::rate;

    for (std::int64_t bar = firstBar; bar <= lastBar && bar < firstBar + 4; ++bar) {
        const bool fill = fillActiveFor(bar, fillNow, everyBars, scaledEnd, b.beatsPerMs);
        ResolvedBar resolved;
        resolveBar(pat, densityMap(), floorMod(bar, bars), {params[ParamId::density], params[ParamId::dynamics], fill},
                   resolved);
        forEachHit(pat, resolved, static_cast<double>(bar) * 4.0, timing, [&](const Hit& h) noexcept CL_NONBLOCKING {
            if (h.beat < scaledStart - kBeatEps || h.beat >= scaledEnd - kBeatEps)
                return;
            if (filterKit && !pads::rateKeepsLane(b.padY, h.lane))
                return;
            pushDrumHit(b, h.lane, h.beat / b.rate, h.durBeats / b.rate, h.velocity);
        });
    }
}

// Sorts this block's hits by time (stable, so equal times keep lane order) and starts them.
void Engine::Impl::emitBlockHits(const Block& b, MidiEventBuffer& outMidi) noexcept CL_NONBLOCKING {
    for (int i = 1; i < blockHitCount; ++i) {
        const BlockHit key = blockHits[static_cast<std::size_t>(i)];
        int j = i - 1;
        while (j >= 0 && blockHits[static_cast<std::size_t>(j)].beat > key.beat) {
            blockHits[static_cast<std::size_t>(j + 1)] = blockHits[static_cast<std::size_t>(j)];
            --j;
        }
        blockHits[static_cast<std::size_t>(j + 1)] = key;
    }
    for (int i = 0; i < blockHitCount; ++i) {
        const BlockHit& hit = blockHits[static_cast<std::size_t>(i)];
        const int offset = sampleOffsetOf(hit.beat, b.rawStart, b.beatsPerSample, b.numSamples);
        const int velocity = static_cast<int>(hit.velocity) + evalVelBias(*b.params, hit.beat, engaged, b.padY);
        triggerNote(hit.note, velocity, hit.lane, hit.beat + std::max(0.03, hit.durBeats), offset, outMidi, hit.drum);
    }
    blockHitCount = 0;
}

void Engine::Impl::process(const Params& params, const Transport& transport, MidiEventBuffer& outMidi, float* left,
                           float* right, int numSamples) noexcept CL_NONBLOCKING {
    hba::dsp::ScopedFlushDenormals ftz;
    outMidi.clear();
    if (numSamples <= 0)
        return;

    const float flashDecay = static_cast<float>(std::exp(-static_cast<double>(numSamples) / (0.115 * sampleRate)));
    for (auto& f : laneFlashes)
        f = (f * flashDecay < 0.005f) ? 0.0f : (f * flashDecay);

    const double bpm = std::clamp(transport.bpm, 20.0, 400.0);
    const double beatsPerSample = bpm / 60.0 / sampleRate;
    const auto rateIdx = static_cast<std::size_t>(std::clamp(params.choice(ParamId::seqRate), 0, 2));
    const double baseRate = kSeqRateMultipliers[rateIdx];
    const auto mode = static_cast<EngineMode>(std::clamp(params.choice(ParamId::engineMode), 0, 1));
    const auto slicerStyle = static_cast<SlicerStyle>(std::clamp(params.choice(ParamId::slicerStyle), 0, 4));
    const auto quant = static_cast<Quantize>(std::clamp(params.choice(ParamId::sceneQuantize), 0, 3));
    const bool isDrum = (mode == EngineMode::drum);
    const int everyBars = kFillEveryBars[static_cast<std::size_t>(std::clamp(params.choice(ParamId::fillEvery), 0, 4))];

    const bool latchNow = params.on(ParamId::slicerLatch);
    if (wasSlicerLatch && !latchNow)
        latched = held;
    wasSlicerLatch = latchNow;
    lastEffectiveHeld = effectiveHeld(params);

    // What the pad surface is asked to do: touch, hold, or C3-D#3 when Pad MIDI is on.
    const bool midiPadActive = isDrum && params.on(ParamId::padMidi) && (midiPadHeld != 0u);
    const int reqProg = (midiPadActive && lastMidiPadProg >= 0)
                            ? std::clamp(lastMidiPadProg, 0, kPerfPadCount - 1)
                            : std::clamp(params.choice(ParamId::padProgram), 0, kPerfPadCount - 1);
    const bool reqEngaged = params.on(ParamId::padTouch) || params.on(ParamId::padHold) || midiPadActive;
    const float padX = std::clamp(params[ParamId::padX], 0.0f, 1.0f);
    const float padY = std::clamp(params[ParamId::padY], 0.0f, 1.0f);
    const int reqScene = std::clamp(params.choice(ParamId::scene), 0, kSceneCount - 1);

    const double rawPpq = transport.ppq; // may be negative during host pre-roll
    lastTransportPpq = rawPpq;
    lastLanePpq = rawPpq;
    playingState = transport.playing;
    activityMask = 0;

    // A pad that starts sounding at `hostBeat`.
    const auto beginPad = [&](int program, double hostBeat, int offset) noexcept CL_NONBLOCKING {
        if (static_cast<PadProgram>(program) == PadProgram::repeat)
            beginRepeat(hostBeat, baseRate, params.on(ParamId::fillNow), everyBars);
        else if (static_cast<PadProgram>(program) == PadProgram::drop && pads::dropIsSilence(padX))
            flushAllActive(outMidi, offset);
    };

    // Quantized scene and pad switching.
    const double qStep = quantizeStepBeats(quant);
    if (!transport.playing || qStep <= 0.0) {
        currentScene = reqScene;
        pendingSceneIdx = -1;
        const bool newlyEngaged = reqEngaged && (!engaged || reqProg != activeProg);
        activeProg = reqProg;
        engaged = reqEngaged;
        pendingProg = -1;
        pendingEngage = false;
        if (newlyEngaged)
            beginPad(activeProg, rawPpq, 0);
    } else {
        if (reqScene != currentScene && reqScene != pendingSceneIdx) {
            pendingSceneIdx = reqScene;
            sceneSwitchBeat = std::ceil((rawPpq - 1e-6) / qStep) * qStep;
        } else if (reqScene == currentScene) {
            pendingSceneIdx = -1;
        }
        if (!reqEngaged) {
            engaged = false; // release at once, so letting go on beat 1 is tight
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
    if (!isDrum && slicerStyle != SlicerStyle::clipSlice && lastEffectiveHeld.count == 0)
        flushAllActive(outMidi, 0);

    // Position telemetry, from the unshifted transport.
    const auto updatePosition = [&](double rate, bool fillLive) noexcept CL_NONBLOCKING {
        const double scaled = rawPpq * rate;
        const auto absBar = static_cast<std::int64_t>(std::floor(scaled / 4.0));
        pos.absBar = absBar;
        pos.barInPattern = floorMod(absBar, patternBars(pattern()));
        pos.stepInBar = std::clamp(static_cast<int>(std::floor((scaled - static_cast<double>(absBar) * 4.0) * 4.0)), 0,
                                   kStepsPerBar - 1);
        pos.fillLive = fillLive;
        pos.repeatStartStep = -1;
        pos.repeatBeats = 0.0;
    };

    if (!transport.playing) {
        if (wasPlaying)
            flushAllActive(outMidi, 0);
        wasPlaying = false;
        updatePosition(baseRate, false);
        lastEventCount = outMidi.count;
        renderAuditionAudio(params, left, right, numSamples);
        return;
    }

    const double shiftBeats = static_cast<double>(params[ParamId::shiftMs]) * 1.0e-3 * (bpm / 60.0);
    const double rawStart = transport.ppq - shiftBeats;
    const double rawEnd = rawStart + static_cast<double>(numSamples) * beatsPerSample;

    if (!wasPlaying || transport.ppq < expectedNextPpq - 1.0e-4 || std::abs(transport.ppq - expectedNextPpq) > 0.125) {
        flushAllActive(outMidi, 0);
        clearDedupe();
        fillLatch = {};
    }
    wasPlaying = true;
    expectedNextPpq = transport.ppq + static_cast<double>(numSamples) * beatsPerSample;

    // Hit times are not clamped: the window follows the transport below beat 0 too.
    const double windowStart = rawStart;
    const double windowEnd = rawEnd;

    // Commit quantized scene and pad switches on their boundary.
    if (pendingSceneIdx >= 0 && windowEnd >= sceneSwitchBeat - kBeatEps) {
        currentScene = pendingSceneIdx;
        pendingSceneIdx = -1;
    }
    if (pendingEngage && windowEnd >= padSwitchBeat - kBeatEps) {
        activeProg = pendingProg >= 0 ? pendingProg : reqProg;
        engaged = true;
        pendingEngage = false;
        pendingProg = -1;
        const double at = std::max(windowStart, padSwitchBeat);
        beginPad(activeProg, padSwitchBeat, sampleOffsetOf(at, rawStart, beatsPerSample, numSamples));
    }

    Block b;
    b.params = &params;
    b.rawStart = rawStart;
    b.windowStart = windowStart;
    b.windowEnd = windowEnd;
    b.beatsPerSample = beatsPerSample;
    b.numSamples = numSamples;
    b.program = static_cast<PadProgram>(std::clamp(activeProg, 0, kPerfPadCount - 1));
    b.rate = baseRate * ((engaged && b.program == PadProgram::rate) ? pads::rateMultiplier(padX) : 1.0);
    b.beatsPerMs = bpm / 60000.0 * b.rate;
    b.noteMap = static_cast<NoteMap>(std::clamp(params.choice(ParamId::noteMap), 0, kNoteMapCount - 1));
    b.isDrum = isDrum;
    b.padX = padX;
    b.padY = padY;

    // Drop's right half is fillNow.
    const bool dropFill = engaged && b.program == PadProgram::drop && !pads::dropIsSilence(padX);
    const bool fillNow = params.on(ParamId::fillNow) || dropFill;
    {
        const auto absBar = static_cast<std::int64_t>(std::floor(rawPpq * b.rate / 4.0));
        updatePosition(b.rate, isDrum && fillActiveFor(absBar, fillNow, everyBars, windowEnd * b.rate, b.beatsPerMs));
    }

    // 1. Note-offs that fall inside this block.
    for (auto& slot : active) {
        if (slot.active && slot.absEndBeat < windowEnd - kBeatEps) {
            const int offset = sampleOffsetOf(slot.absEndBeat, rawStart, beatsPerSample, numSamples);
            outMidi.pushCritical(MidiEvent::noteOff(offset, slot.channel, slot.note));
            audition.noteOff(slot.note);
            slot.active = false;
        }
    }

    // 2. This block's notes: a pad program, or the pattern.
    blockHitCount = 0;
    if (isDrum) {
        if (engaged && b.program == PadProgram::roll)
            playRoll(b);
        else if (engaged && b.program == PadProgram::repeat)
            playRepeat(b);
        else if (!(engaged && b.program == PadProgram::drop && pads::dropIsSilence(padX)))
            playPattern(b, fillNow);
    }
    // MIDI Slicer playback is added in Task 4.
    emitBlockHits(b, outMidi);

    // 3. Continuous CC, expression lanes and XY -> CC.
    const auto thinIdx = static_cast<std::size_t>(std::clamp(params.choice(ParamId::ccThinMs), 0, 3));
    const double tickBeats = kCcThinMsValues[thinIdx] * 1.0e-3 * (bpm / 60.0);
    std::int64_t tickIdx = static_cast<std::int64_t>(std::ceil((windowStart - kBeatEps) / tickBeats));
    for (int iter = 0; iter < 128; ++iter) {
        const double tickBeat = static_cast<double>(tickIdx) * tickBeats;
        if (tickBeat >= windowEnd - kBeatEps)
            break;
        if (tickBeat >= windowStart - kBeatEps) {
            const int offset = sampleOffsetOf(tickBeat, rawStart, beatsPerSample, numSamples);
            emitContinuousAt(params, tickBeat, offset, outMidi, engaged, padX, padY);
        }
        ++tickIdx;
    }

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
    if (!isDrum && lastEffectiveHeld.count > 0)
        act |= kActSlice;
    if (capturedCount > 0)
        act |= kActClip;
    activityMask = act;

    renderAuditionAudio(params, left, right, numSamples);
}

void Engine::Impl::renderAuditionAudio(const Params& params, float* left, float* right,
                                       int numSamples) noexcept CL_NONBLOCKING {
    if (left == nullptr || right == nullptr || numSamples <= 0)
        return;
    const bool enabled = params.on(ParamId::audition);
    const float gainDb = params[ParamId::auditionGain];
    for (int i = 0; i < numSamples; ++i)
        audition.renderSample(gainDb, enabled, left[i], right[i]);
}

Engine::Engine() : impl_(std::make_unique<Impl>()) { impl_->prepare(48000.0); }
Engine::~Engine() = default;

void Engine::prepare(double sampleRate, int /*maxBlockSize*/) { impl_->prepare(sampleRate); }
void Engine::reset() noexcept CL_NONBLOCKING { impl_->reset(); }
void Engine::setLaneCurves(const LaneCurves& curves) noexcept CL_NONBLOCKING { impl_->curves = curves; }
void Engine::setGroove(const GrooveState& groove) noexcept CL_NONBLOCKING {
    impl_->groove = groove;
    ++impl_->grooveRevision;
}

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

PlayPosition Engine::position() const noexcept CL_NONBLOCKING { return impl_->pos; }
HeldNotes Engine::activeHeldNotes() const noexcept CL_NONBLOCKING { return impl_->lastEffectiveHeld; }
int Engine::activeScene() const noexcept CL_NONBLOCKING { return impl_->currentScene; }
int Engine::pendingScene() const noexcept CL_NONBLOCKING { return impl_->pendingSceneIdx; }
int Engine::activePadProgram() const noexcept CL_NONBLOCKING { return impl_->activeProg; }
int Engine::pendingPadProgram() const noexcept CL_NONBLOCKING { return impl_->pendingProg; }
bool Engine::padEngaged() const noexcept CL_NONBLOCKING { return impl_->engaged; }
std::uint8_t Engine::midiPadMask() const noexcept CL_NONBLOCKING { return impl_->midiPadHeld; }
int Engine::capturedClipCount() const noexcept CL_NONBLOCKING { return impl_->capturedCount; }
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
