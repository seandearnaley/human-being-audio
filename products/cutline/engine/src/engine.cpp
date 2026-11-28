// SPDX-License-Identifier: AGPL-3.0-or-later
#include "engine_impl.h"
#include "cl/pad_programs.h"

#include <hba/dsp/denormals.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

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

// Transport continuity: how far the host may land from where the last block said it would and still be the same
// run. Behind it by more than kJumpBackBeats is a loop or a jump back; off by more than kJumpBeats is a jump.
constexpr double kJumpBackBeats = 1.0 / 64.0;
constexpr double kJumpBeats = 0.125;

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
    sentCount = 0;
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
    prevScene = 0;
    sceneFromBeat = std::numeric_limits<double>::lowest();
    activeProg = 0;
    pendingProg = -1;
    engaged = false;
    pendingEngage = false;
    padSwitchBeat = 0.0;
    repeatAnchor = 0.0;
    repeatBar = 0;
    repeatFill = false;
    fillLatch = {};
    slicerKey = {};
    slicerPlanData.noteCount = 0;
    wasPlaying = false;
    expectedNextPpq = 0.0;
    nextWindowStart = 0.0;
    lastBlockRate = 0.0;
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

// Returns whether the note-on was sent.
bool Engine::Impl::triggerNote(std::int8_t noteNum, int velocity, int laneHint, double absEndBeat, int offset,
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

    // A note-on respects the buffer's note-off reserve. When it does not fit, the note is not started at all, so
    // every note that did start still has room for its note-off.
    const int clampedVel = std::clamp(velocity, 1, 127);
    if (!outMidi.push(MidiEvent::noteOn(offset, ch, noteNum, clampedVel)))
        return false;

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
    return true;
}

// Lists a note-on that was sent, where it sounds on the pattern's clock, for the PLAY surface.
void Engine::Impl::recordSent(const Block& b, const BlockHit& hit, int offset, int velocity) noexcept CL_NONBLOCKING {
    if (sentCount >= static_cast<int>(sent.size()))
        return;
    const double patternBeat = hit.beat * hit.rate;
    const double inBar = patternBeat - std::floor(patternBeat / 4.0) * 4.0;
    SentNote& s = sent[static_cast<std::size_t>(sentCount++)];
    s.ppq = b.hostStart + static_cast<double>(offset) * b.beatsPerSample;
    s.beatInBar = static_cast<float>(std::clamp(inBar, 0.0, 4.0));
    s.rate = static_cast<float>(hit.rate);
    s.seq = 0;
    s.sampleOffset = offset;
    s.note = hit.note;
    s.lane = hit.drum ? hit.lane : static_cast<std::int8_t>(-1);
    s.velocity = static_cast<std::uint8_t>(std::clamp(velocity, 1, 127));
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

// True when bar `absBar` plays its fill. The whole decision, the schedule or fillNow, is latched once per bar, at
// the first block that reaches the earliest moment a hit in `pat`'s fill window could start (a full early nudge,
// lane offset and flam before the window). Before that moment only the schedule is reported: no fill-window hit can
// sound yet, so the bar resolves to the same notes whichever way the latch will fall. After it, changing Fill
// or the scene cannot half-apply the fill. A change of the effective rate moves the playhead to a different bar,
// so process() clears the latches then and the decision is made afresh.
bool Engine::Impl::fillActiveFor(const Pattern& pat, std::int64_t absBar, bool fillNow, int everyBars, double scaledEnd,
                                 double beatsPerMs) noexcept CL_NONBLOCKING {
    const bool scheduled = fillScheduled(absBar, everyBars);
    FillLatch& latch = fillLatch[static_cast<std::size_t>(floorMod(absBar, kMaxBarsPerBlock))];
    if (latch.bar != absBar) {
        const double samplePoint = static_cast<double>(absBar) * 4.0 +
                                   static_cast<double>(fillWindowStart(pat.fill)) * 0.25 - maxEarlyBeats(beatsPerMs);
        if (scaledEnd < samplePoint)
            return scheduled;
        latch = {absBar, scheduled || fillNow};
    }
    return latch.on;
}

// The scene that plays a step whose grid time (window clock, host beats) is `gridBeat`.
int Engine::Impl::sceneAtGrid(double gridBeat) const noexcept CL_NONBLOCKING {
    if (pendingSceneIdx >= 0 && gridBeat >= sceneSwitchBeat - kBeatEps)
        return pendingSceneIdx;
    if (gridBeat < sceneFromBeat - kBeatEps)
        return prevScene;
    return currentScene;
}

void Engine::Impl::pushDrumHit(const Block& b, const Pattern& pat, int lane, double hostBeat, double hostDur,
                               int velocity) noexcept CL_NONBLOCKING {
    if (blockHitCount >= kMaxBlockHits)
        return;
    BlockHit& hit = blockHits[static_cast<std::size_t>(blockHitCount++)];
    hit.beat = hostBeat;
    hit.durBeats = hostDur;
    hit.rate = b.rate;
    hit.note = effectiveLaneMidiNote(b.noteMap, lane, pat.lanes[static_cast<std::size_t>(lane)].midiNote);
    hit.lane = static_cast<std::int8_t>(lane);
    hit.velocity = static_cast<std::uint8_t>(std::clamp(velocity, 1, 127));
    hit.drum = true;
    hit.padOn = b.padOn;
}

// The pattern itself: resolve every bar that can reach into this block, and keep the hits that start in it. Near a
// scene switch, each scene involved is resolved, and a hit is kept only from the scene that owns its step's grid time.
void Engine::Impl::playPattern(const Block& b) noexcept CL_NONBLOCKING {
    const Params& params = *b.params;
    const int everyBars = kFillEveryBars[static_cast<std::size_t>(std::clamp(params.choice(ParamId::fillEvery), 0, 4))];
    const HitTiming timing{static_cast<double>(params[ParamId::swing]), static_cast<double>(params[ParamId::gateLen]),
                           b.beatsPerMs};
    const double scaledStart = b.windowStart * b.rate;
    const double scaledEnd = b.windowEnd * b.rate;
    const double early = maxEarlyBeats(b.beatsPerMs);
    const double late = maxLateBeats(b.beatsPerMs);
    const auto firstBar = static_cast<std::int64_t>(std::floor((scaledStart - late) / 4.0));
    const auto lastBar = static_cast<std::int64_t>(std::floor((scaledEnd + early) / 4.0));
    // The pads that thin the kit: Rate's Y, Drop's zones, and Roll, whose lane plays the roll instead.
    const auto keepsLane = [&](int lane) noexcept CL_NONBLOCKING {
        if (!b.padOn)
            return true;
        switch (b.program) {
        case PadProgram::rate:
            return pads::rateKeepsLane(b.padY, lane);
        case PadProgram::drop:
            return pads::dropKeepsLane(b.padX, lane);
        case PadProgram::roll:
            return lane != pads::rollLane(b.padY);
        default:
            return true;
        }
    };
    // Silent before beat 0: of the hits that start before it, only bar 0's own early hits play. A hit of an earlier
    // bar that lands at or after beat 0 (late Swing, nudge or offset on its last step) plays as it always has.
    const double silentBefore = kTimelineStart * b.rate;

    // The scenes that can own a step with a hit in this block. A step at grid time g starts its hits no earlier than
    // g - early and no later than g + one step + late.
    const double reachStart = (scaledStart - late - 0.25) / b.rate;
    const double reachEnd = (scaledEnd + early) / b.rate;
    std::array<int, 3> scenes{currentScene, -1, -1};
    int sceneCount = 1;
    const auto addScene = [&](int scene) noexcept CL_NONBLOCKING {
        for (int i = 0; i < sceneCount; ++i)
            if (scenes[static_cast<std::size_t>(i)] == scene)
                return;
        scenes[static_cast<std::size_t>(sceneCount++)] = scene;
    };
    if (sceneFromBeat > reachStart)
        addScene(prevScene);
    if (pendingSceneIdx >= 0 && sceneSwitchBeat < reachEnd)
        addScene(pendingSceneIdx);

    for (int si = 0; si < sceneCount; ++si) {
        const int scene = std::clamp(scenes[static_cast<std::size_t>(si)], 0, kSceneCount - 1);
        const Pattern& pat = groove.bank.scenes[static_cast<std::size_t>(scene)];
        const DensityMap& map = groove.maps[static_cast<std::size_t>(scene)];
        const int bars = patternBars(pat);
        for (std::int64_t bar = firstBar; bar <= lastBar && bar < firstBar + kMaxBarsPerBlock; ++bar) {
            const bool fill = fillActiveFor(pat, bar, b.fillNow, everyBars, scaledEnd, b.beatsPerMs);
            ResolvedBar resolved;
            resolveBar(pat, map, floorMod(bar, bars), {b.density, b.dynamics, fill}, resolved);
            forEachHit(
                pat, resolved, static_cast<double>(bar) * 4.0, timing, [&](const Hit& h) noexcept CL_NONBLOCKING {
                    if (h.beat < scaledStart - kBeatEps || h.beat >= scaledEnd - kBeatEps)
                        return;
                    if (bar < 0 && h.beat < silentBefore - kBeatEps)
                        return;
                    const double grid = (static_cast<double>(bar) * 4.0 + static_cast<double>(h.step) * 0.25) / b.rate;
                    if (sceneAtGrid(grid) != scene)
                        return;
                    if (!keepsLane(h.lane))
                        return;
                    pushDrumHit(b, pat, h.lane, h.beat / b.rate, h.durBeats / b.rate, h.velocity);
                });
        }
    }
}

void Engine::Impl::pushNoteHit(const Block& b, int note, int voice, double hostBeat, double hostDur,
                               int velocity) noexcept CL_NONBLOCKING {
    if (blockHitCount >= kMaxBlockHits)
        return;
    BlockHit& hit = blockHits[static_cast<std::size_t>(blockHitCount++)];
    hit.beat = hostBeat;
    hit.durBeats = hostDur;
    hit.rate = b.rate;
    hit.note = static_cast<std::int8_t>(std::clamp(note, 0, 127));
    hit.lane = static_cast<std::int8_t>(std::clamp(voice, 0, 7));
    hit.velocity = static_cast<std::uint8_t>(std::clamp(velocity, 1, 127));
    hit.drum = false;
    hit.padOn = b.padOn;
}

// Recompiles the slicer phrase only when something it depends on changed.
void Engine::Impl::ensureSlicerPlan(const Params& params, double bpm) noexcept CL_NONBLOCKING {
    const SlicerInputs now = slicerInputs(params, currentScene, lastEffectiveHeld, bpm, grooveRevision);
    if (now.sameAs(slicerKey))
        return;
    compileSlicer(params, groove, currentScene, lastEffectiveHeld, bpm, slicerPlanData);
    slicerKey = now;
}

// The compiled phrase, looping at the pattern's length.
void Engine::Impl::playSlicer(const Block& b) noexcept CL_NONBLOCKING {
    const double phraseBeats = std::max(1.0, slicerPlanData.phraseBeats);
    const double scaledEnd = b.windowEnd * b.rate;
    double scan = b.windowStart * b.rate;
    for (int pass = 0; pass < 3 && scan < scaledEnd - kBeatEps; ++pass) {
        const double origin = std::floor(scan / phraseBeats) * phraseBeats;
        const double phraseEnd = origin + phraseBeats;
        const double subEnd = std::min(scaledEnd, phraseEnd);
        for (int i = 0; i < slicerPlanData.noteCount; ++i) {
            const SlicerNote& n = slicerPlanData.notes[static_cast<std::size_t>(i)];
            const double at = origin + n.beat;
            if (at < scan - kBeatEps || at >= subEnd - kBeatEps)
                continue;
            pushNoteHit(b, n.note, n.voice, at / b.rate, std::min(n.durBeats, phraseEnd - at) / b.rate, n.velocity);
        }
        scan = subEnd;
    }
}

// Clip Slice: replays the notes captured from the MIDI input, bar by bar, with Swing.
// While Repeat is engaged it loops the captured notes inside the Repeat window instead, from the Repeat anchor, as
// the drum and slicer Repeat do. A block can straddle a bar line (or a Repeat cycle), so every bar and cycle the
// block touches is visited: working from the block's start alone would skip a note that sits exactly on the boundary.
void Engine::Impl::playClip(const Block& b) noexcept CL_NONBLOCKING {
    const double swing = static_cast<double>((*b.params)[ParamId::swing]);
    const double scaledStart = b.windowStart * b.rate;
    const double scaledEnd = b.windowEnd * b.rate;
    const auto emitAt = [&](const CapturedEvent& ev, double gridBeat) noexcept CL_NONBLOCKING {
        const double at = swingBeat(gridBeat, swing);
        if (at < scaledStart - kBeatEps || at >= scaledEnd - kBeatEps)
            return;
        pushNoteHit(b, ev.note, 0, at / b.rate, std::max(0.04, ev.durBeats / b.rate), static_cast<int>(ev.vel));
    };

    if (b.padOn && b.program == PadProgram::repeat) {
        const double window = pads::repeatWindowBeats(b.padX);
        const double anchorInBar = repeatAnchor - std::floor(repeatAnchor / 4.0) * 4.0;
        const double sliceStart = std::min(anchorInBar, 4.0 - window); // keep the slice inside its bar
        // Swing delays a position by at most kMaxSwingBeats, so a cycle can reach this block from that far before it.
        const auto first = std::max<std::int64_t>(
            0, static_cast<std::int64_t>(std::floor((scaledStart - kMaxSwingBeats - repeatAnchor) / window)));
        const auto last = static_cast<std::int64_t>(std::floor((scaledEnd - repeatAnchor) / window));
        for (std::int64_t cycle = first; cycle <= last && cycle < first + 64; ++cycle) {
            const double origin = repeatAnchor + static_cast<double>(cycle) * window;
            for (const auto& ev : captured) {
                if (ev.active && ev.barBeat >= sliceStart && ev.barBeat < sliceStart + window)
                    emitAt(ev, origin + (ev.barBeat - sliceStart));
            }
        }
        return;
    }

    const auto firstBar = static_cast<std::int64_t>(std::floor(scaledStart / 4.0));
    const auto lastBar = static_cast<std::int64_t>(std::floor((scaledEnd - kBeatEps) / 4.0));
    for (std::int64_t bar = firstBar; bar <= lastBar && bar < firstBar + kMaxBarsPerBlock; ++bar) {
        // swingBeat is periodic over half a beat, so swinging the absolute beat equals swinging the bar beat.
        const double barOrigin = static_cast<double>(bar) * 4.0;
        for (const auto& ev : captured) {
            if (ev.active)
                emitAt(ev, barOrigin + ev.barBeat);
        }
    }
}

// One part of a block: a pad program, the pattern, or in MIDI Slicer mode the phrase or the captured clip.
// Silent before beat 0: every source but the pattern plays only the part of the block from kTimelineStart on. The
// pattern sees the whole block, because bar 0's early hits start before beat 0; it drops the earlier bars' hits there.
// Roll plays over the pattern: its lane rolls and the other lanes keep playing. Density, Rate, Drop's lane zones and
// Fill change how the pattern plays (playPattern and makeBlock). In MIDI Slicer mode Density and Fill do nothing.
void Engine::Impl::playBlock(const Block& b) noexcept CL_NONBLOCKING {
    if (b.windowEnd <= b.windowStart)
        return;
    const bool silent = b.padOn && b.program == PadProgram::drop && pads::dropIsSilence(b.padX);
    Block from = b;
    from.windowStart = std::max(b.windowStart, kTimelineStart);
    const bool fromStart = from.windowEnd > from.windowStart; // some of the block is at or after beat 0
    if (!b.isDrum) {
        if (!fromStart)
            return;
        const auto style = static_cast<SlicerStyle>(std::clamp(b.params->choice(ParamId::slicerStyle), 0, 4));
        if (b.padOn && b.program == PadProgram::roll)
            playRoll(from);
        else if (style == SlicerStyle::clipSlice) {
            if (!silent)
                playClip(from); // loops inside the Repeat window itself while Repeat is engaged
        } else if (b.padOn && b.program == PadProgram::repeat)
            playRepeat(from);
        else if (!silent)
            playSlicer(from);
        return;
    }
    if (b.padOn && b.program == PadProgram::roll) {
        playPattern(b); // without the rolled lane
        if (fromStart)
            playRoll(from);
    } else if (b.padOn && b.program == PadProgram::repeat) {
        if (fromStart)
            playRepeat(from);
    } else if (!silent) {
        playPattern(b);
    }
}

// Releases every active note that ends at or before `beat` (strictly before it when `strict`).
void Engine::Impl::releaseEndingBy(const Block& b, double beat, bool strict,
                                   MidiEventBuffer& outMidi) noexcept CL_NONBLOCKING {
    for (auto& slot : active) {
        if (!slot.active || (strict ? slot.absEndBeat >= beat : slot.absEndBeat > beat))
            continue;
        const int offset = sampleOffsetOf(slot.absEndBeat, b.rawStart, b.beatsPerSample, b.numSamples);
        outMidi.pushCritical(MidiEvent::noteOff(offset, slot.channel, slot.note));
        audition.noteOff(slot.note);
        slot.active = false;
    }
}

// Sorts this block's hits by time (stable, so equal times keep lane order) and starts them. Note-offs are sent in
// the same time order: a note that ends after a hit starts is still active when the hit retriggers it, so its
// note-off comes before the new note-on rather than cutting it. `silenceAt` (Drop's left half committing in this
// block; the double maximum when none) releases everything still sounding at that beat, after the hits before it have
// started. A hit before the block's first sample (the window can start there, after tempo or Shift automation) is
// sent on that sample and keeps its full length from there.
void Engine::Impl::emitBlockHits(const Block& b, double silenceAt, MidiEventBuffer& outMidi) noexcept CL_NONBLOCKING {
    // A silence is requested whenever silenceAt is given, even when it lands on (or within kBeatEps past) the block's
    // end, where the pad commits too; the flush offset then clamps to the block's last sample.
    bool silencePending = silenceAt != std::numeric_limits<double>::max();
    const int silenceOffset =
        silencePending ? sampleOffsetOf(silenceAt, b.rawStart, b.beatsPerSample, b.numSamples) : b.numSamples;
    const auto silenceNow = [&]() noexcept CL_NONBLOCKING {
        releaseEndingBy(b, silenceAt, true, outMidi);
        flushAllActive(outMidi, silenceOffset);
        silencePending = false;
    };
    for (int i = 1; i < blockHitCount; ++i) {
        const BlockHit key = blockHits[static_cast<std::size_t>(i)];
        int j = i - 1;
        while (j >= 0 && blockHits[static_cast<std::size_t>(j)].beat > key.beat) {
            blockHits[static_cast<std::size_t>(j + 1)] = blockHits[static_cast<std::size_t>(j)];
            --j;
        }
        blockHits[static_cast<std::size_t>(j + 1)] = key;
    }
    // Two hits of one MIDI note at one sample (lanes can share a note) become one hit: the louder velocity and the
    // longer note. Otherwise the host would see note-on, note-off, note-on at the same sample.
    const auto offsetOf = [&](const BlockHit& h) noexcept CL_NONBLOCKING {
        return sampleOffsetOf(h.beat, b.rawStart, b.beatsPerSample, b.numSamples);
    };
    for (int i = 1; i < blockHitCount; ++i) {
        BlockHit& hit = blockHits[static_cast<std::size_t>(i)];
        const int offset = offsetOf(hit);
        for (int j = i - 1; j >= 0; --j) {
            BlockHit& kept = blockHits[static_cast<std::size_t>(j)];
            if (offsetOf(kept) != offset)
                break;
            if (kept.velocity == 0 || kept.note != hit.note)
                continue;
            kept.velocity = std::max(kept.velocity, hit.velocity);
            kept.durBeats = std::max(kept.beat + kept.durBeats, hit.beat + hit.durBeats) - kept.beat;
            hit.velocity = 0; // merged
            break;
        }
    }
    for (int i = 0; i < blockHitCount; ++i) {
        const BlockHit& hit = blockHits[static_cast<std::size_t>(i)];
        if (hit.velocity == 0)
            continue;
        const int offset = offsetOf(hit);
        if (silencePending && hit.beat >= silenceAt - kBeatEps)
            silenceNow();
        else if (silencePending && offset >= silenceOffset)
            continue; // it would start on the sample the silence lands on, and be cut there
        releaseEndingBy(b, hit.beat, false, outMidi);
        const int velocity = static_cast<int>(hit.velocity) + evalVelBias(*b.params, hit.beat, hit.padOn, b.padY);
        const double onBeat = std::max(hit.beat, b.rawStart);
        if (triggerNote(hit.note, velocity, hit.lane, onBeat + std::max(0.03, hit.durBeats), offset, outMidi, hit.drum))
            recordSent(b, hit, offset, velocity);
    }
    if (silencePending)
        silenceNow();
    releaseEndingBy(b, b.windowEnd - kBeatEps, true, outMidi);
    blockHitCount = 0;
}

void Engine::Impl::process(const Params& params, const Transport& transport, MidiEventBuffer& outMidi, float* left,
                           float* right, int numSamples) noexcept CL_NONBLOCKING {
    hba::dsp::ScopedFlushDenormals ftz;
    outMidi.clear();
    sentCount = 0;
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

    // What the pad surface is asked to do: touch, hold, or C3-F3 when Pad MIDI is on.
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

    // One clock for playback and switching: window time is the transport minus Shift, so the pattern's grid as it
    // sounds. The window follows the transport below beat 0 too, but nothing plays there except bar 0's own early
    // hits (kTimelineStart).
    const double shiftBeats = static_cast<double>(params[ParamId::shiftMs]) * 1.0e-3 * (bpm / 60.0);
    const double rawStart = rawPpq - shiftBeats;

    // A start, a loop or a jump breaks the transport's continuity; it is detected on the host position alone. A small
    // step back is tempo automation (the host integrates the tempo, while a block's length in beats uses the tempo at
    // its start), not a loop.
    const bool continuous = transport.playing && wasPlaying && transport.ppq >= expectedNextPpq - kJumpBackBeats &&
                            std::abs(transport.ppq - expectedNextPpq) <= kJumpBeats;
    // While the transport is continuous, a block's window starts where the last one ended, so under tempo or Shift
    // automation every pattern instant belongs to exactly one block. Sample offsets stay relative to the block's own
    // start (rawStart) and clamp into it.
    const double windowStart = continuous ? nextWindowStart : rawStart;
    const double windowEnd = std::max(windowStart, rawStart + static_cast<double>(numSamples) * beatsPerSample);

    // A pad that starts sounding at window beat `beat`. Returns true when it silences what is sounding (Drop's left
    // half); the caller does that in time order.
    const auto beginPad = [&](int program, double beat) noexcept CL_NONBLOCKING {
        if (static_cast<PadProgram>(program) == PadProgram::repeat)
            beginRepeat(beat, baseRate, params.on(ParamId::fillNow), everyBars);
        return static_cast<PadProgram>(program) == PadProgram::drop && pads::dropIsSilence(padX);
    };

    // Scene switching: at once when stopped or when Scene Quantize is Instant (the boundary is the block's start),
    // otherwise on the next Scene Quantize step.
    const double qStep = quantizeStepBeats(quant);
    if (!transport.playing || qStep <= 0.0) {
        if (reqScene != currentScene) {
            prevScene = currentScene;
            currentScene = reqScene;
            sceneFromBeat = windowStart;
        }
        pendingSceneIdx = -1;
    } else if (reqScene != currentScene && reqScene != pendingSceneIdx) {
        pendingSceneIdx = reqScene;
        sceneSwitchBeat = std::ceil((windowStart - 1e-6) / qStep) * qStep;
    } else if (reqScene == currentScene) {
        pendingSceneIdx = -1;
    }

    // Pad switching: at once when stopped; while playing, release at once and engage on the next 1/16.
    if (!transport.playing) {
        const bool newlyEngaged = reqEngaged && (!engaged || reqProg != activeProg);
        activeProg = reqProg;
        engaged = reqEngaged;
        pendingProg = -1;
        pendingEngage = false;
        if (newlyEngaged && beginPad(activeProg, windowStart))
            flushAllActive(outMidi, 0);
    } else {
        if (!reqEngaged) {
            engaged = false; // release at once, so letting go on beat 1 is tight
            pendingEngage = false;
            pendingProg = -1;
            activeProg = reqProg;
        } else if (!engaged || reqProg != activeProg) {
            if (!pendingEngage || pendingProg != reqProg) {
                pendingEngage = true;
                pendingProg = reqProg;
                padSwitchBeat = std::ceil((windowStart - 1e-6) / pads::kPadQuantizeBeats) * pads::kPadQuantizeBeats;
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

    const auto makeBlock = [&](bool padOn, int prog, double from, double to) noexcept CL_NONBLOCKING {
        Block part;
        part.params = &params;
        part.hostStart = rawPpq;
        part.rawStart = rawStart;
        part.windowStart = from;
        part.windowEnd = to;
        part.beatsPerSample = beatsPerSample;
        part.numSamples = numSamples;
        part.program = static_cast<PadProgram>(std::clamp(prog, 0, kPerfPadCount - 1));
        part.padOn = padOn;
        part.rate = baseRate * ((padOn && part.program == PadProgram::rate) ? pads::rateMultiplier(padX) : 1.0);
        part.beatsPerMs = bpm / 60000.0 * part.rate;
        part.noteMap = static_cast<NoteMap>(std::clamp(params.choice(ParamId::noteMap), 0, kNoteMapCount - 1));
        // The Fill pad is fillNow; the Density pad replaces Density and Dynamics. Neither applies to MIDI Slicer.
        const bool drumPad = padOn && isDrum;
        part.fillNow = params.on(ParamId::fillNow) || (drumPad && part.program == PadProgram::fill);
        const bool densityPad = drumPad && part.program == PadProgram::density;
        part.density = densityPad ? padX : std::clamp(params[ParamId::density], 0.0f, 1.0f);
        part.dynamics = densityPad ? padY : std::clamp(params[ParamId::dynamics], 0.0f, 1.0f);
        part.isDrum = isDrum;
        part.padX = padX;
        part.padY = padY;
        return part;
    };

    // Position telemetry, on the window clock: the step of the pattern that is sounding. Before beat 0 the playhead
    // waits on bar 0, step 0.
    const auto updatePosition = [&](const Block& part, bool fillLive) noexcept CL_NONBLOCKING {
        const double scaled = std::max(windowStart, kTimelineStart) * part.rate;
        const auto absBar = static_cast<std::int64_t>(std::floor(scaled / 4.0));
        const double inBar = std::clamp(scaled - static_cast<double>(absBar) * 4.0, 0.0, 4.0);
        pos.absBar = absBar;
        pos.barInPattern = floorMod(absBar, patternBars(pattern()));
        pos.stepInBar = std::clamp(static_cast<int>(std::floor(inBar * 4.0)), 0, kStepsPerBar - 1);
        pos.fillLive = fillLive;
        pos.repeatStartStep = -1;
        pos.repeatBeats = 0.0;
        pos.density = part.density;
        pos.dynamics = part.dynamics;
        pos.rate = static_cast<float>(part.rate);
        pos.beatInBar = static_cast<float>(inBar);
    };

    if (!transport.playing) {
        if (wasPlaying)
            flushAllActive(outMidi, 0);
        wasPlaying = false;
        updatePosition(makeBlock(engaged, activeProg, windowStart, windowEnd), false);
        lastEventCount = outMidi.count;
        renderAuditionAudio(params, left, right, numSamples);
        return;
    }

    // A start, a loop or a jump: nothing carries over from before it, and a held Repeat loops from here.
    if (!continuous) {
        flushAllActive(outMidi, 0);
        clearDedupe();
        fillLatch = {};
        prevScene = currentScene;
        sceneFromBeat = std::numeric_limits<double>::lowest();
        if (engaged && static_cast<PadProgram>(activeProg) == PadProgram::repeat)
            beginRepeat(windowStart, baseRate, params.on(ParamId::fillNow), everyBars);
        // Pending switches fall on the next step after the new position, not on the beat they were due before.
        if (pendingSceneIdx >= 0 && qStep > 0.0)
            sceneSwitchBeat = std::ceil((windowStart - 1e-6) / qStep) * qStep;
        if (pendingEngage)
            padSwitchBeat = std::ceil((windowStart - 1e-6) / pads::kPadQuantizeBeats) * pads::kPadQuantizeBeats;
    }
    wasPlaying = true;
    expectedNextPpq = transport.ppq + static_cast<double>(numSamples) * beatsPerSample;
    nextWindowStart = windowEnd;

    // Commit quantized scene and pad switches on their boundary.
    if (pendingSceneIdx >= 0 && windowEnd >= sceneSwitchBeat - kBeatEps) {
        prevScene = currentScene;
        currentScene = pendingSceneIdx;
        sceneFromBeat = sceneSwitchBeat;
        pendingSceneIdx = -1;
    }
    const bool engagedBefore = engaged;
    const int progBefore = activeProg;
    double padAt = windowStart; // where the pad takes over, when it commits in this block
    bool padCommitted = false;
    double silenceAt = std::numeric_limits<double>::max(); // where Drop silences what is sounding
    if (pendingEngage && windowEnd >= padSwitchBeat - kBeatEps) {
        activeProg = pendingProg >= 0 ? pendingProg : reqProg;
        engaged = true;
        pendingEngage = false;
        pendingProg = -1;
        padAt = std::max(windowStart, padSwitchBeat);
        padCommitted = true;
        if (beginPad(activeProg, padSwitchBeat))
            silenceAt = padAt;
    }

    const Block b = makeBlock(engaged, activeProg, windowStart, windowEnd);
    // The fill latches are keyed by bar in rate-scaled time, so a bar decided at one rate says nothing about the bar
    // with the same number at another.
    if (b.rate != lastBlockRate) {
        fillLatch = {};
        lastBlockRate = b.rate;
    }
    {
        const auto absBar = static_cast<std::int64_t>(std::floor(windowStart * b.rate / 4.0));
        updatePosition(b, isDrum && windowStart >= kTimelineStart &&
                              fillActiveFor(pattern(), absBar, b.fillNow, everyBars, windowEnd * b.rate, b.beatsPerMs));
    }

    // This block's notes, then every note-off in time order. When a pad commits inside the block, what played
    // before it (the pattern, or the previous program) keeps the part of the block before the switch point.
    if (!isDrum && slicerStyle != SlicerStyle::clipSlice)
        ensureSlicerPlan(params, bpm);
    blockHitCount = 0;
    if (padCommitted && padAt > windowStart) {
        playBlock(makeBlock(engagedBefore, progBefore, windowStart, padAt));
        playBlock(makeBlock(true, activeProg, padAt, windowEnd));
    } else {
        playBlock(b);
    }
    emitBlockHits(b, silenceAt, outMidi);

    // Continuous CC, expression lanes and XY -> CC, from beat 0 on.
    const auto thinIdx = static_cast<std::size_t>(std::clamp(params.choice(ParamId::ccThinMs), 0, 3));
    const double tickBeats = kCcThinMsValues[thinIdx] * 1.0e-3 * (bpm / 60.0);
    const double ccFrom = std::max(windowStart, kTimelineStart);
    std::int64_t tickIdx = static_cast<std::int64_t>(std::ceil((ccFrom - kBeatEps) / tickBeats));
    for (int iter = 0; iter < 128; ++iter) {
        const double tickBeat = static_cast<double>(tickIdx) * tickBeats;
        if (tickBeat >= windowEnd - kBeatEps)
            break;
        if (tickBeat >= ccFrom - kBeatEps) {
            const int offset = sampleOffsetOf(tickBeat, rawStart, beatsPerSample, numSamples);
            const bool tickPadOn = (padCommitted && tickBeat < padAt - kBeatEps) ? engagedBefore : engaged;
            emitContinuousAt(params, tickBeat, offset, outMidi, tickPadOn, padX, padY);
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

void Engine::Impl::releaseAll(MidiEventBuffer& outMidi) noexcept CL_NONBLOCKING {
    outMidi.clear();
    sentCount = 0;
    blockHitCount = 0;
    flushAllActive(outMidi, 0);
    wasPlaying = false; // the next block is a fresh start, not the continuation of the bypassed one
    lastEventCount = outMidi.count;
}

void Engine::Impl::renderAuditionAudio(const Params& params, float* left, float* right,
                                       int numSamples) noexcept CL_NONBLOCKING {
    if (left == nullptr || numSamples <= 0)
        return;
    const bool enabled = params.on(ParamId::audition);
    const double targetGain = enabled ? hba::dsp::dbToGain(static_cast<double>(params[ParamId::auditionGain])) : 0.0;
    for (int i = 0; i < numSamples; ++i) {
        float l = 0.0f, r = 0.0f;
        audition.renderSampleLinear(targetGain, l, r);
        if (right != nullptr) {
            left[i] = l;
            right[i] = r;
        } else {
            left[i] = 0.5f * (l + r);
        }
    }
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

void Engine::releaseAll(MidiEventBuffer& outMidi) noexcept CL_NONBLOCKING { impl_->releaseAll(outMidi); }

std::span<const SentNote> Engine::sentNotes() const noexcept CL_NONBLOCKING {
    return {impl_->sent.data(), static_cast<std::size_t>(impl_->sentCount)};
}

PlayPosition Engine::position() const noexcept CL_NONBLOCKING { return impl_->pos; }
const SlicerPlan& Engine::slicerPlan() const noexcept CL_NONBLOCKING { return impl_->slicerPlanData; }
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
