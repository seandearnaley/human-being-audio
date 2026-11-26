// SPDX-License-Identifier: AGPL-3.0-or-later
#include "engine_impl.h"

namespace tf {

using namespace detail;

void Engine::Impl::prepare(double sampleRate, int oversampling) {
    fs = sampleRate;
    factor = effectiveOversampling(sampleRate, oversampling);
    fo = fs * factor;
    controlOs = kControlPeriodOs;
    controlBase = std::max(1, kControlPeriodOs / factor);
    controlOs = controlBase * factor;
    tickCoef = onePoleCoef(kSmoothSeconds, controlBase / fs);
    delayTickCoef = onePoleCoef(kDelaySmoothSeconds, controlBase / fs);
    foldMixStep = 1.0 / (kFoldCrossfadeSeconds * fo);

    decSub.prepare(factor);
    decL.prepare(factor);
    decR.prepare(factor);
    const int foldTargetRate = effectiveOversampling(sampleRate, 16);
    localSub.prepare(factor, foldTargetRate);
    localA.prepare(factor, foldTargetRate);
    localB.prepare(factor, foldTargetRate);
    clip2xL.prepare();
    clip2xR.prepare();
    clip2xSub.prepare();
    clip2xMaster.prepare();
    decimatorDelayExact = measureDecimatorDelay(factor, foldTargetRate) + measureClipTripDelay();
    decimatorDelay = static_cast<int>(std::lround(decimatorDelayExact));

    feedback.prepare(static_cast<int>(std::ceil(kMaxFeedbackDelaySeconds * fo)));
    feedbackDc.prepare(5.0, fo);
    vowelComp = computeVowelCompDb(fo);
    foldA.prepare(fo * localA.factor());
    foldB.prepare(fo * localB.factor());
    for (auto& ch : sculptA)
        ch.prepare(fo);
    for (auto& ch : sculptB)
        ch.prepare(fo);
    squash.prepare(fs);
    limiter.prepare(fs, kGrowlLimiterReleaseSeconds);
    subLimiter.prepare(fs);
    subPeakHold.prepare(static_cast<int>(std::lround(kSubHoldSeconds * fs)));
    subEnvelopeRelease = onePoleCoef(kSubReleaseSeconds, 1.0 / fs);
    gate.prepare(fs);
    punch.prepare(fs);
    gateDelay.prepare(decimatorDelay);
    reset();
}

// Group delay at DC of the 4x trip + pass-0 ADAA1 linear delay around the growl clipper, base samples.
double Engine::Impl::measureClipTripDelay() {
    ClipTrip t;
    t.prepare();
    double halfDelay = 0.0;
    double moment = 0.0, sum = 0.0;
    for (int n = 0; n < 4096; ++n) {
        float buf[ClipTrip::kFactor];
        t.up(n == 0 ? 1.0f : 0.0f, buf);
        for (std::size_t k = 0; k < ClipTrip::kFactor; ++k) {
            const double v = buf[k];
            buf[k] = static_cast<float>(0.5 * (v + halfDelay));
            halfDelay = v;
        }
        const double y = t.down(buf);
        moment += n * y;
        sum += y;
    }
    return sum != 0.0 ? moment / sum : 0.0;
}

// Group delay at DC of the sub's trip (local oversampler, then decimator),
// which the growl shares by construction, in base-rate samples.
double Engine::Impl::measureDecimatorDelay(int factor, int foldTargetRate) {
    Decimator d;
    d.prepare(factor);
    LocalOversampler local;
    local.prepare(factor, foldTargetRate);
    std::array<float, kMaxOversampling> buf{}, scratch{};
    double moment = 0.0, sum = 0.0;
    float delay1 = 0.0f;
    for (int n = 0; n < 4096; ++n) {
        for (int j = 0; j < factor; ++j) {
            local.up(n == 0 && j == 0 ? 1.0f : 0.0f, scratch.data());
            for (int k = 0; k < local.factor(); ++k) {
                const float prev = delay1;
                delay1 = scratch[static_cast<std::size_t>(k)];
                scratch[static_cast<std::size_t>(k)] = prev;
            }
            buf[static_cast<std::size_t>(j)] = local.down(scratch.data());
        }
        const double y = d.process(buf.data());
        moment += n * y;
        sum += y;
    }
    return sum != 0.0 ? moment / sum : 0.0;
}

void Engine::Impl::resetDsp() noexcept TF_NONBLOCKING {
    decSub.reset();
    decL.reset();
    decR.reset();
    localSub.reset();
    localA.reset();
    localB.reset();
    clip2xL.reset();
    clip2xR.reset();
    clip2xSub.reset();
    clip2xMaster.reset();
    growlClipAdaa.reset();
    subClipHalfDelay = 0.0;
    masterClipHalfDelay = 0.0;
    feedback.clear();
    feedbackDc.reset();
    feedbackLp.reset();
    tractA.reset();
    tractB.reset();
    tractPostA.reset();
    tractPostB.reset();
    foldA.reset();
    foldB.reset();
    subFoldDelay = 0.0f;
    reachA.reset();
    reachB.reset();
    weightA.reset();
    weightB.reset();
    for (auto& ch : sculptA)
        ch.reset();
    for (auto& ch : sculptB)
        ch.reset();
    sculptSlot = 0;
    sculptXfadeMix = 1.0;
    growlHpL.reset();
    growlHpR.reset();
    sideHp.reset();
    sideHpPost.reset();
    for (auto& pass : postClipHp)
        for (auto& f : pass)
            f.reset();
    midAp.reset();
    midApPost.reset();
    subLp.reset();
    squash.reset();
    limiter.reset();
    subLimiter.reset();
    subPeakHold.reset();
    subEnvelope = 0.0;
    gateDelay.clear();
    foldMix = 1.0;
}

void Engine::Impl::reset() noexcept TF_NONBLOCKING {
    resetDsp();
    gate.reset();
    punch.reset();
    notes.clear();
    physicalNotes.clear();
    sustainDown = false;
    pendingNote = -1;
    pendingVelocity = 1.0;
    noteVelocity = 1.0;
    phaseMaster = phaseA = phaseB = phaseLockA = phaseLockB = 0.0;
    for (auto& s : smooth)
        s.reset();
    for (auto& s : laneSmooth)
        s.reset();
    laneMove.fill(0.0);
    for (auto& s : formantSmooth)
        s.reset();
    cycleSmooth.reset();
    for (auto& g : sculptGainSmooth)
        g.reset();
    bendTarget = 0.0;
    bendRange = kBendRange;
    modWheelTarget = 0.0;
    timbreOffsetTarget = 0.0;
    aftertouchTarget = 0.0;
    snapNext = true;
    countdown = 0;
    idle = true;
    silentCount = 0;
    wasPlaying = false;
    sinceAnchor = sinceNoteStart = freeSamples = 0;
    uiFreeSec = uiNoteSec = 0.0;
    grDb = 0.0f;
    effective = EffectiveState{};
}

// ---- notes -------------------------------------------------------------

void Engine::Impl::wake() noexcept TF_NONBLOCKING {
    if (!idle)
        return;
    idle = false;
    silentCount = 0;
    countdown = 0;
    snapNext = true;
}

// A note that starts from silence or after the stack emptied.
void Engine::Impl::startNote(int note, double velocity) noexcept TF_NONBLOCKING {
    if (pendingNote >= 0) {
        pendingNote = note;
        pendingVelocity = velocity;
        return;
    }
    if (phaseResetOn && !idle && gate.value() > 0.0) {
        gate.fade();
        pendingNote = note;
        pendingVelocity = velocity;
        return;
    }
    wake();
    beginNote(note, velocity);
}

void Engine::Impl::beginNote(int note, double velocity) noexcept TF_NONBLOCKING {
    if (phaseResetOn)
        phaseMaster = phaseA = phaseB = phaseLockA = phaseLockB = 0.0;
    noteVelocity = std::clamp(velocity, 0.01, 1.0);
    punch.trigger(0.4 + 0.6 * noteVelocity);
    glide.jump(note);
    sinceNoteStart = 0;
    if (!snapNext)
        retargetPitchNow();
    gate.open();
}

void Engine::Impl::noteOn(int note, float velocity) noexcept TF_NONBLOCKING {
    note = std::clamp(note, 0, 127);
    const double vel = std::clamp(static_cast<double>(velocity), 0.01, 1.0);
    physicalNotes.push(note);
    const bool legato = !notes.empty();
    notes.push(note);
    if (legato && pendingNote < 0) {
        noteVelocity = vel;
        glide.glideTo(note, glideMs * 1e-3 * fs);
    } else {
        startNote(note, vel);
    }
}

void Engine::Impl::noteOff(int note) noexcept TF_NONBLOCKING {
    note = std::clamp(note, 0, 127);
    physicalNotes.remove(note);
    if (sustainDown)
        return;
    const bool wasTop = notes.top() == note;
    if (!notes.remove(note))
        return;
    if (notes.empty()) {
        if (pendingNote >= 0)
            pendingNote = -1; // the fade runs out, nothing follows
        else
            gate.close();
    } else if (wasTop) {
        if (pendingNote >= 0)
            pendingNote = notes.top();
        else
            glide.glideTo(notes.top(), glideMs * 1e-3 * fs);
    }
}

void Engine::Impl::sustain(bool down) noexcept TF_NONBLOCKING {
    if (down == sustainDown)
        return;
    sustainDown = down;
    if (sustainDown)
        return;
    const int prevTop = notes.top();
    notes = physicalNotes;
    if (notes.empty()) {
        if (pendingNote >= 0)
            pendingNote = -1;
        else
            gate.close();
    } else if (notes.top() != prevTop) {
        if (pendingNote >= 0)
            pendingNote = notes.top();
        else
            glide.glideTo(notes.top(), glideMs * 1e-3 * fs);
    }
}

void Engine::Impl::allNotesOff() noexcept TF_NONBLOCKING {
    notes.clear();
    physicalNotes.clear();
    sustainDown = false;
    pendingNote = -1;
    if (gate.stage() != Gate::Stage::fade)
        gate.close();
}

// ---- control -----------------------------------------------------------

// Cycles per oversampled sample. The voices sit at f0 x ratio, detuned in
// cents, or with Beat lock offset by a constant number of Hz, so every
// partial of a voice (the modulator stays at exactly f0) moves by the same
// amount and the whole spectrum beats at one rate.
void Engine::Impl::pitchIncrements(double& master, double& a, double& b) const noexcept TF_NONBLOCKING {
    const double punchSt = sm(punchAmt_) * punch.value();
    const double f0 = noteToHz(glide.current() + sm(bend) + punchSt);
    const double beatMod = moved(LaneTarget::beat, sm(beat), 0.0, 1.0);
    master = f0 / fo;
    if (beatLockOn) {
        const double delta = beatLockHz(anchorBpmOrCurrent(), beatMod);
        const auto share = beatLockShares(sm(aDetune), sm(bDetune));
        const double minHz = std::max(8.0, 0.25 * f0);
        const double aHz = std::max(minHz, f0 * sm(aRatio) + delta * share.a);
        const double bHz = std::max(minHz, f0 * sm(bRatio) + delta * share.b);
        a = aHz / fo;
        b = bHz / fo;
        return;
    }
    const auto detA = feel::detuneCents(static_cast<float>(sm(aDetune)), static_cast<float>(beatMod));
    const auto detB = feel::detuneCents(static_cast<float>(sm(bDetune)), static_cast<float>(beatMod));
    a = master * sm(aRatio) * std::exp2(detA / 1200.0);
    b = master * sm(bRatio) * std::exp2(detB / 1200.0);
}

// A new note starts at pitch: jump the pitch ramps instead of waiting for
// the next control point.
void Engine::Impl::retargetPitchNow() noexcept TF_NONBLOCKING {
    double m, a, b;
    pitchIncrements(m, a, b);
    os[incMaster].snap(m);
    os[incA].snap(a);
    os[incB].snap(b);
    os[incLockA].snap(m * sm(aRatio));
    os[incLockB].snap(m * sm(bRatio));
}

// ---- lanes -------------------------------------------------------------

// The lane clock at `samplesAhead` base samples from now: the host
// position while playing, else the time since the note started.
double Engine::Impl::lanePpqAhead(long long samplesAhead) const noexcept TF_NONBLOCKING {
    return playing ? anchorPpq + static_cast<double>(sinceAnchor + samplesAhead) * qps
                   : static_cast<double>(sinceNoteStart + samplesAhead) * qps;
}

// One control period of the lanes, at host position `ppq`: each lane's send
// (tf::laneSend, the function the interface draws with) through its
// smoother, then summed by target. The smoothing covers any step in a send
// (a curve swapped by setLaneCurves, a retarget, a lane switched on), and
// the same code runs while the DSP sleeps, so the interface sees the same
// values either way.
double Engine::Impl::notePpqAhead(long long samplesAhead) const noexcept TF_NONBLOCKING {
    return static_cast<double>(sinceNoteStart + samplesAhead) * qps;
}

double Engine::Impl::freeSecAhead(long long samplesAhead) const noexcept TF_NONBLOCKING {
    return static_cast<double>(freeSamples + samplesAhead) / fs;
}

double Engine::Impl::noteSecAhead(long long samplesAhead) const noexcept TF_NONBLOCKING {
    return static_cast<double>(sinceNoteStart + samplesAhead) / fs;
}

void Engine::Impl::laneTick(const Params& p, double ppq, double nPpq, double fSec, double nSec,
                            double c) noexcept TF_NONBLOCKING {
    uiPpq = ppq;
    uiNotePpq = nPpq;
    uiFreeSec = fSec;
    uiNoteSec = nSec;
    laneMove.fill(0.0);
    for (int i = 0; i < kLaneCount; ++i) {
        const auto lane = static_cast<std::size_t>(i);
        const double v = laneSmooth[lane].step(laneSend(p, laneCurves, i, ppq, nPpq, fSec, nSec), c);
        uiPhase[lane] = static_cast<float>(lanePhaseFor(p, i, ppq, nPpq, fSec, nSec));
        uiValue[lane] = static_cast<float>(v);
        laneMove[static_cast<std::size_t>(laneTargetOf(p, i))] += v;
    }
}

// A target's value after movement, by kLaneSpans (as tf::laneApply, in
// double): additive in the parameter's own units, or by octaves for Reach
// and Sculpt, clamped to [lo, hi]. Phase and Fit wrap instead and are
// applied where they are used.
double Engine::Impl::moved(LaneTarget target, double value, double lo, double hi) const noexcept TF_NONBLOCKING {
    const auto t = static_cast<std::size_t>(target);
    const LaneSpan span = kLaneSpans[t];
    const double amount = span.amount;
    const double v = span.octaves ? value * std::exp2(amount * laneMove[t]) : value + amount * laneMove[t];
    return std::clamp(v, lo, hi);
}
// A wrapping target's movement in cycles (its span is in degrees).
double Engine::Impl::movedCycles(LaneTarget target) const noexcept TF_NONBLOCKING {
    const auto t = static_cast<std::size_t>(target);
    return kLaneSpans[t].amount / 360.0 * laneMove[t];
}

void Engine::Impl::smoothParameters(const Params& p, double c, bool snap) noexcept TF_NONBLOCKING {
    const auto step = [&](Sm id, double target) { return smooth[id].step(target, c); };
    step(coupling, p[ParamId::coupling]);
    step(beat, p[ParamId::beat]);
    step(oscShapeSm, p[ParamId::oscShape]);
    step(punchAmt_, p[ParamId::punchAmt]);
    step(aDetune, p[ParamId::aDetune]);
    step(bDetune, p[ParamId::bDetune]);
    step(aRatio, p[ParamId::aRatio]);
    step(bRatio, p[ParamId::bRatio]);
    step(aPhase, p[ParamId::aPhase]);
    step(bPhase, p[ParamId::bPhase]);
    step(aPm, p[ParamId::aPm]);
    step(bPm, p[ParamId::bPm]);
    step(fbAmount, p[ParamId::fbAmount]);
    step(width_, p[ParamId::width]);
    step(tract_, p[ParamId::tract]);
    step(tractShift_, p[ParamId::tractShift]);
    step(tractQ_, p[ParamId::tractQ]);
    step(tractPos_, p.choice(ParamId::tractPos) == static_cast<int>(TractPos::post) ? 1.0 : 0.0);
    step(drive_, p[ParamId::drive]);
    step(asym_, p[ParamId::asym]);
    step(warp_, p[ParamId::warp]);
    step(midGainDb, p[ParamId::midGain]);
    step(sideGainDb, p[ParamId::sideGain]);
    step(xoverLog, std::log(std::max(1.0f, p[ParamId::xover])));
    step(velSens_, p[ParamId::velSens]);
    step(subSplitLog,
         std::log(std::clamp(p[ParamId::subSplit], spec(ParamId::subSplit).min, spec(ParamId::subSplit).max)));
    step(subWarmthSm, p[ParamId::subWarmth]);
    step(subLevel_, p[ParamId::subLevel]);
    step(growlLevel_, p[ParamId::growlLevel]);
    step(squash_, p[ParamId::squash]);
    step(ceilingDb, p[ParamId::ceiling]);
    step(limitOn, p.on(ParamId::limit) ? 1.0 : 0.0);
    step(bend, bendTarget);
    step(reachLog, std::log(std::clamp(p[ParamId::reach], spec(ParamId::reach).min, spec(ParamId::reach).max)));
    step(weight_, p[ParamId::weight]);
    step(sculptLog, std::log(std::clamp(p[ParamId::sculpt], spec(ParamId::sculpt).min, spec(ParamId::sculpt).max)));
    step(spreadLog, std::log(std::clamp(p[ParamId::sculptSpread], spec(ParamId::sculptSpread).min,
                                        spec(ParamId::sculptSpread).max)));
    step(sculptRes_, p[ParamId::sculptRes]);
    step(sculptDepth_, p[ParamId::sculptDepth]);
    step(modWheel_, modWheelTarget);
    step(timbreOffset_, timbreOffsetTarget);
    step(aftertouch_, aftertouchTarget);
    {
        const double target = static_cast<double>(p[ParamId::fit]) / 360.0;
        const double current = smooth[fitCycles].value();
        step(fitCycles, snap ? target : current + wrapHalf(target - current));
    }
}

void Engine::Impl::updateSculptTargets(const Params& p, double f0, double widthVal, double c,
                                       bool snap) noexcept TF_NONBLOCKING {
    // Stereo Sculpt: Bell / Phaser / Comb with subtle stereo A/B detune scaled by Width.
    {
        const auto sMode = static_cast<SculptMode>(std::clamp(p.choice(ParamId::sculptMode), 0, 2));
        if (snap) {
            sculptModeCur = sculptModePrev = sMode;
            sculptXfadeMix = 1.0;
        } else if (sMode != sculptModeCur) {
            sculptModePrev = sculptModeCur;
            sculptModeCur = sMode;
            sculptSlot ^= 1;
            sculptA[static_cast<std::size_t>(sculptSlot)].reset();
            sculptB[static_cast<std::size_t>(sculptSlot)].reset();
            sculptXfadeMix = 0.0;
        }

        const int count = std::clamp(p.choice(ParamId::sculptCount), 0, kSculptMaxBands - 1) + 1;
        const double depthEff = std::clamp(sm(sculptDepth_), -1.0, 1.0);
        const double gainDb = feel::sculptGainDb(static_cast<float>(depthEff));
        const double sculptResEff = std::clamp(sm(sculptRes_) + 0.30 * sm(aftertouch_), 0.0, 1.0);
        const double k = 1.0 / feel::sculptQ(static_cast<float>(sculptResEff));
        double first =
            moved(LaneTarget::sculpt, std::exp(sm(sculptLog)), spec(ParamId::sculpt).min, spec(ParamId::sculpt).max);
        if (sm(aftertouch_) > 0.0)
            first = std::clamp(first * std::exp2(sm(aftertouch_)), static_cast<double>(spec(ParamId::sculpt).min),
                               static_cast<double>(spec(ParamId::sculpt).max));
        const double spread = std::exp(sm(spreadLog));
        // Bell tracks exact harmonics; Phaser and Comb apply subtle opposing stereo offset when Width > 0.
        const double stereoRatio = sMode == SculptMode::bell ? 1.0 : std::exp2(0.035 * widthVal);

        const double combHzA = std::clamp(first * f0 * stereoRatio, 25.0, 0.40 * fo);
        const double combHzB = std::clamp(first * f0 / stereoRatio, 25.0, 0.40 * fo);
        sculptCoefsA.setSpecial(count, depthEff, sculptResEff, fo / combHzA, snap ? 0 : controlOs);
        sculptCoefsB.setSpecial(count, depthEff, sculptResEff, fo / combHzB, snap ? 0 : controlOs);

        double harmonic = first;
        for (int i = 0; i < kSculptMaxBands; ++i, harmonic *= spread) {
            const double sgn = (i & 1) == 0 ? 1.0 : -1.0;
            const double hzBand = harmonic * f0;
            const double hzA = hzBand * (sgn > 0.0 ? stereoRatio : 1.0 / stereoRatio);
            const double hzB = hzBand * (sgn > 0.0 ? 1.0 / stereoRatio : stereoRatio);
            const bool on = i < count && hzBand < kMaxFilterRatio * fo;
            auto& gs = sculptGainSmooth[static_cast<std::size_t>(i)];
            const double target = on ? gainDb : 0.0;
            double db = gs.step(target, c);
            if (std::abs(db - target) < 1e-3) {
                gs.snap(target);
                db = target;
            }
            const double m = db == 0.0 ? 0.0 : (dbToGain(db) - 1.0) * k;
            const double gA = SvfCoefs::prewarp(std::min(hzA, kMaxFilterRatio * fo), fo);
            const double gB = SvfCoefs::prewarp(std::min(hzB, kMaxFilterRatio * fo), fo);
            sculptCoefsA.setTarget(i, gA, k, m, snap ? 0 : controlOs);
            sculptCoefsB.setTarget(i, gB, k, m, snap ? 0 : controlOs);
        }
    }
}

void Engine::Impl::updateEffectiveState(double couplingMod, double reachHarmonic, double morphMod, double shiftMod,
                                        double driveMod, double widthVal) noexcept TF_NONBLOCKING {
    effective.coupling = static_cast<float>(couplingMod);
    effective.beat = static_cast<float>(moved(LaneTarget::beat, sm(beat), 0.0, 1.0));
    effective.reach = static_cast<float>(reachHarmonic);
    effective.morph = static_cast<float>(morphMod);
    effective.shift = static_cast<float>(shiftMod);
    effective.drive = static_cast<float>(driveMod);
    {
        double sc =
            moved(LaneTarget::sculpt, std::exp(sm(sculptLog)), spec(ParamId::sculpt).min, spec(ParamId::sculpt).max);
        if (sm(aftertouch_) > 0.0)
            sc = std::clamp(sc * std::exp2(sm(aftertouch_)), static_cast<double>(spec(ParamId::sculpt).min),
                            static_cast<double>(spec(ParamId::sculpt).max));
        effective.sculpt = static_cast<float>(sc);
    }
    effective.width = static_cast<float>(widthVal);
    effective.active = true;
}

void Engine::Impl::controlTick(const Params& p) noexcept TF_NONBLOCKING {
    const bool snap = snapNext;
    snapNext = false;
    const double c = snap ? 1.0 : tickCoef;
    // Lanes, evaluated at the end of this control period.
    laneTick(p, lanePpqAhead(controlBase), notePpqAhead(controlBase), freeSecAhead(controlBase),
             noteSecAhead(controlBase), c);

    smoothParameters(p, c, snap);

    // Pitch + phase-locked Pitch Punch transient.
    glide.advance(controlBase);
    punch.advance(controlBase, static_cast<double>(p[ParamId::punchMs]));
    double incM, incAv, incBv;
    pitchIncrements(incM, incAv, incBv);
    hz = static_cast<float>(incM * fo);

    // Musical velocity offsets (zero delta at velocity == 1.0).
    const double velSens = std::clamp(sm(velSens_), 0.0, 1.0);
    const double velDelta = noteVelocity - 1.0; // in [-0.99, 0.0]
    const double driveVelOffset = 0.25 * velSens * velDelta;
    const double reachVelOct = 1.5 * velSens * velDelta;

    // Oversampled targets.
    std::array<double, osCount> t{};
    t[incMaster] = incM;
    t[incA] = incAv;
    t[incB] = incBv;
    t[incLockA] = incM * sm(aRatio);
    t[incLockB] = incM * sm(bRatio);
    // Phase offsets in cycles, not wrapped (the oscillator wraps), so a ramp
    // never sweeps the long way round.
    t[offA] = sm(aPhase) / 360.0 + movedCycles(LaneTarget::phase);
    t[offB] = sm(bPhase) / 360.0 - movedCycles(LaneTarget::phase);
    const double couplingMod =
        std::clamp(moved(LaneTarget::coupling, sm(coupling), 0.0, 1.0) + 0.35 * sm(aftertouch_), 0.0, 1.0);
    t[pmA] = feel::pmIndex(static_cast<float>(sm(aPm)), static_cast<float>(couplingMod));
    t[pmB] = feel::pmIndex(static_cast<float>(sm(bPm)), static_cast<float>(couplingMod));
    t[fbGain] = feel::feedbackRadians(static_cast<float>(sm(fbAmount)));

    // Reach: 24 dB/oct at reach x f0, below 0.45 of the oversampled rate;
    // the loop's at min(reach, kLoopMaxHarmonic) x f0.
    const double f0 = incM * fo;
    const double reachHarmonic = std::clamp(
        moved(LaneTarget::reach, std::exp(sm(reachLog)), spec(ParamId::reach).min, spec(ParamId::reach).max) *
            std::exp2(reachVelOct),
        static_cast<double>(spec(ParamId::reach).min), static_cast<double>(spec(ParamId::reach).max));
    t[reachG] = SvfCoefs::prewarp(std::min(reachHarmonic * f0, kMaxFilterRatio * fo), fo);
    const double loopHarmonic = std::min(reachHarmonic, kLoopMaxHarmonic);
    t[loopG] = SvfCoefs::prewarp(std::min(loopHarmonic * f0, kMaxFilterRatio * fo), fo);

    // Feedback delay in cycles of the note (two-tap comb at 1x and 2x cycles).
    const auto fbChoice =
        static_cast<std::size_t>(std::clamp(p.choice(ParamId::fbTime), 0, static_cast<int>(kFbCycles.size()) - 1));
    const double cycles = cycleSmooth.step(kFbCycles[fbChoice], snap ? 1.0 : delayTickCoef);
    const double periodOs = std::clamp(cycles / std::max(incM, 1e-9), 2.0, 0.5 * feedback.maxDelay());
    t[fbPeriod] = periodOs;
    t[fbDelay] = periodOs;

    t[width] = moved(LaneTarget::width, sm(width_), 0.0, 1.0);
    t[tractAmt] = std::clamp(sm(tract_), 0.0, 1.0);
    const double driveMod =
        std::clamp(moved(LaneTarget::drive, sm(drive_), 0.0, 1.0) + 0.35 * sm(modWheel_) + driveVelOffset, 0.0, 1.0);
    t[driveGain] = feel::driveGain(static_cast<float>(driveMod));
    t[asymOffset] = 0.5 * sm(asym_);
    t[warpAmt] = std::clamp(sm(warp_), 0.0, 1.0);
    t[subWarmth_] = std::clamp(sm(subWarmthSm), 0.0, 1.0);
    t[oscShape_] = std::clamp(sm(oscShapeSm), 0.0, 1.0);
    t[tractPost_] = std::clamp(sm(tractPos_), 0.0, 1.0);

    // Fit: the sub runs ahead of the master by the phase its LR4 low-pass
    // takes away at f0, plus fit, so at fit 0 the sub at the output is in
    // phase with the master.
    const double splitHz = subSplitHz(std::exp(sm(subSplitLog)), f0);
    const double rotation = lr4LowpassLagCycles(f0, splitHz, fs) + sm(fitCycles) + movedCycles(LaneTarget::fit);
    t[subRot] = snap ? rotation : os[subRot].value + wrapHalf(rotation - os[subRot].value);

    // Weight: dry -> one one-pole -> two at the pivot, level-matched over
    // the octave above it.
    const auto blend =
        weightBlend(feel::weightSlopeDb(static_cast<float>(moved(LaneTarget::weight, sm(weight_), 0.0, 1.0))),
                    weightPivotHz(f0, splitHz), fo);
    t[weightG] = blend.gain;
    t[weightDry] = blend.dry;
    t[weightOne] = blend.one;
    t[weightTwo] = blend.two;
    for (std::size_t i = 0; i < osCount; ++i) {
        if (snap)
            os[i].snap(t[i]);
        else
            os[i].to(t[i], controlOs);
    }

    updateSculptTargets(p, f0, t[width], c, snap);

    // Tract: vowels + morph + formant shift + Q focus.
    const double morphMod = std::clamp(
        moved(LaneTarget::morph, static_cast<double>(p[ParamId::morph]), 0.0, 1.0) + sm(timbreOffset_), 0.0, 1.0);
    const double shiftMod = moved(LaneTarget::shift, sm(tractShift_), -24.0, 24.0);
    const auto vowels =
        mixVowels(p.choice(ParamId::vowelA), p.choice(ParamId::vowelB), morphMod, shiftMod, sm(tractQ_), vowelComp);
    FormantTargets f;
    for (std::size_t i = 0; i < kFormantCount; ++i) {
        f.logHz[i] = formantSmooth[i].step(vowels.logHz[i], c);
        f.logBandwidth[i] = formantSmooth[kFormantCount + i].step(vowels.logBandwidth[i], c);
        f.gainDb[i] = formantSmooth[2 * kFormantCount + i].step(vowels.gainDb[i], c);
    }
    f.compDb = formantSmooth[3 * kFormantCount].step(vowels.compDb, c);
    if (snap)
        tractCoefs.snap(f, fo);
    else
        tractCoefs.setTarget(f, fo, controlOs);

    // Fold type: crossfade on change.
    const auto type = static_cast<FoldType>(
        std::clamp(p.choice(ParamId::foldType), 0, static_cast<int>(choices::foldType.size()) - 1));
    if (snap) {
        foldFrom = foldTo = type;
        foldMix = 1.0;
    } else if (type != foldTo) {
        foldFrom = foldMix >= 0.5 ? foldTo : foldFrom;
        foldTo = type;
        foldMix = 0.0;
    }

    // Base rate.
    cSub = butterworth(splitHz, fs);
    // Two post-clip BW4 HPs at 1.05x splitHz form an LR8 (-48 dB/oct, -64 dB at f0).
    cSubClip = Bw4Coefs::make(kPostClipSplitRatio * splitHz, fs * ClipTrip::kFactor);
    const double xoverHz = std::exp(sm(xoverLog));
    cXover = butterworth(xoverHz, fs);
    cXoverClip = butterworth(xoverHz, fs * ClipTrip::kFactor);
    std::array<double, baseCount> b{};
    b[subLevel] = std::clamp(sm(subLevel_), 0.0, 1.0);
    b[growlLevel] = std::clamp(sm(growlLevel_), 0.0, 1.0);
    b[midGain] = dbToGain(sm(midGainDb));
    b[sideGain] = dbToGain(sm(sideGainDb));
    b[squashAmt] = std::clamp(sm(squash_), 0.0, 1.0);
    b[ceilingGain] = dbToGain(std::min(0.0, sm(ceilingDb)));
    b[limitAmt] = std::clamp(sm(limitOn), 0.0, 1.0);
    b[ampVel] = std::clamp(1.0 + 0.60 * velSens * velDelta, 0.15, 1.0);
    const double subPeak = b[subLevel] * lr4LowpassMagnitude(f0, splitHz, fs);
    b[growlCeiling] = std::max(b[ceilingGain] * kGrowlCeilingMargin - subPeak, kGrowlCeilingFloor);
    for (std::size_t i = 0; i < baseCount; ++i) {
        if (snap)
            base[i].snap(b[i]);
        else
            base[i].to(b[i], controlBase);
    }

    updateEffectiveState(couplingMod, reachHarmonic, morphMod, shiftMod, driveMod, t[width]);
}

} // namespace tf
