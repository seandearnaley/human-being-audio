// SPDX-License-Identifier: AGPL-3.0-or-later
#include "engine_impl.h"

namespace tf {

using namespace detail;

void Engine::Impl::oversampledSample(int j) noexcept TF_NONBLOCKING {
    for (auto& r : os)
        r.next();
    tractCoefs.advance();
    sculptCoefsA.advance();
    sculptCoefsB.advance();
    if (foldMix < 1.0)
        foldMix = std::min(1.0, foldMix + foldMixStep);
    if (sculptXfadeMix < 1.0)
        sculptXfadeMix = std::min(1.0, sculptXfadeMix + foldMixStep);

    phaseMaster += os[incMaster].value;
    phaseA += os[incA].value;
    phaseB += os[incB].value;
    phaseLockA += os[incLockA].value;
    phaseLockB += os[incLockB].value;
    phaseMaster -= std::floor(phaseMaster);
    phaseA -= std::floor(phaseA);
    phaseB -= std::floor(phaseB);
    phaseLockA -= std::floor(phaseLockA);
    phaseLockB -= std::floor(phaseLockB);

    // Master sine + phase-locked Sub Warmth (2nd/3rd harmonics pre-subLp, zero DC/phase shift).
    const float master = std::sin(static_cast<float>(kTwoPi * phaseMaster));
    const double pSub = kTwoPi * frac(phaseMaster + os[subRot].value);
    const double subFund = std::sin(pSub);
    const double sw = os[subWarmth_].value;
    const float subSine =
        static_cast<float>(sw <= 1.0e-5 ? subFund
                                        : (subFund + sw * (0.24 * std::sin(2.0 * pSub) + 0.14 * std::sin(3.0 * pSub))) /
                                              (1.0 + 0.24 * sw));

    const double fbTap2 = std::min(os[fbDelay].value + os[fbPeriod].value, feedback.maxDelay());
    const double fbRaw = 0.5 * (feedback.read(os[fbDelay].value) + feedback.read(fbTap2));
    const double fb = os[fbGain].value * std::tanh(feedbackDc.process(fbRaw));

    const double shp = os[oscShape_].value;
    const auto shapeOsc = [shp](double rad) noexcept TF_NONBLOCKING -> double {
        const double s1 = std::sin(static_cast<float>(rad));
        if (shp <= 1.0e-5)
            return s1;
        const double s2 = std::sin(static_cast<float>(2.0 * rad));
        const double rich = std::sin(static_cast<float>(rad + 0.85 * shp * s1 + 0.35 * shp * s2));
        return (1.0 - shp) * s1 + shp * rich;
    };

    const double a = shapeOsc(kTwoPi * frac(phaseA + os[offA].value) + os[pmA].value * master + fb);
    const double b = shapeOsc(kTwoPi * frac(phaseB + os[offB].value) + os[pmB].value * master + fb);
    const double lockA =
        std::sin(static_cast<float>(kTwoPi * frac(phaseLockA + os[offA].value) + os[pmA].value * master + fb));
    const double lockB =
        std::sin(static_cast<float>(kTwoPi * frac(phaseLockB + os[offB].value) + os[pmB].value * master + fb));

    feedback.write(static_cast<float>(feedbackLp.process(ReachCoefs::make(os[loopG].value), 0.5 * (lockA + lockB))));
    const ReachCoefs reach = ReachCoefs::make(os[reachG].value);

    // Pre/Post-Fold Tract with click-free crossfade; fold runs at local 16x rate.
    const double tractAmount = os[tractAmt].value;
    const double postMix = os[tractPost_].value;
    const double g = os[driveGain].value, off = os[asymOffset].value, warp = os[warpAmt].value;
    const int local = localA.factor();
    float* buf = bufLocal.data();

    double vaIn = a, vbIn = b;
    if (postMix < 1.0 - 1.0e-5) {
        const double preA = tractA.process(tractCoefs, tractAmount, a);
        const double preB = tractB.process(tractCoefs, tractAmount, b);
        vaIn = (1.0 - postMix) * preA + postMix * a;
        vbIn = (1.0 - postMix) * preB + postMix * b;
    } else {
        tractA.reset();
        tractB.reset();
    }

    localA.up(static_cast<float>(vaIn), buf);
    for (int k = 0; k < local; ++k)
        buf[k] = static_cast<float>(foldA.process(buf[k], g, off, warp, foldFrom, foldTo, foldMix));
    double ya = localA.down(buf);
    localB.up(static_cast<float>(vbIn), buf);
    for (int k = 0; k < local; ++k)
        buf[k] = static_cast<float>(foldB.process(buf[k], g, off, warp, foldFrom, foldTo, foldMix));
    double yb = localB.down(buf);

    if (postMix > 1.0e-5) {
        const double postA = tractPostA.process(tractCoefs, tractAmount, ya);
        const double postB = tractPostB.process(tractCoefs, tractAmount, yb);
        ya = (1.0 - postMix) * ya + postMix * postA;
        yb = (1.0 - postMix) * yb + postMix * postB;
    } else {
        tractPostA.reset();
        tractPostB.reset();
    }

    localSub.up(subSine, buf);
    for (int k = 0; k < local; ++k) {
        const float prev = subFoldDelay;
        subFoldDelay = buf[k];
        buf[k] = prev;
    }
    const float sub = localSub.down(buf);

    // Reach, Weight, and Stereo Sculpt (with click-free mode crossfade).
    const WeightMix mix{os[weightG].value, os[weightDry].value, os[weightOne].value, os[weightTwo].value};
    const double preScA = weightA.process(mix, reachA.process(reach, ya));
    const double preScB = weightB.process(mix, reachB.process(reach, yb));
    const auto curSlot = static_cast<std::size_t>(sculptSlot);
    const auto prevSlot = static_cast<std::size_t>(sculptSlot ^ 1);
    ya = sculptA[curSlot].process(sculptCoefsA, preScA, sculptModeCur);
    yb = sculptB[curSlot].process(sculptCoefsB, preScB, sculptModeCur);
    if (sculptXfadeMix < 1.0) {
        const double oldA = sculptA[prevSlot].process(sculptCoefsA, preScA, sculptModePrev);
        const double oldB = sculptB[prevSlot].process(sculptCoefsB, preScB, sculptModePrev);
        const double ang = sculptXfadeMix * (kPi * 0.5);
        const double gNew = std::sin(ang);
        const double gOld = std::cos(ang);
        ya = gNew * ya + gOld * oldA;
        yb = gNew * yb + gOld * oldB;
    }

    // Width: exact mono at 0; retains 18% cross-bleed at 100% so stereo Reese beating stays alive in headphones!
    const double w = os[width].value;
    const double bleed = 1.0 - 0.82 * w;
    const double invNorm = 1.0 / (1.0 + bleed);
    const double l = (ya + bleed * yb) * invNorm;
    const double r = (yb + bleed * ya) * invNorm;

    const auto idx = static_cast<std::size_t>(j);
    bufSub[idx] = sub;
    bufL[idx] = static_cast<float>(l);
    bufR[idx] = static_cast<float>(r);
}

void Engine::Impl::goIdle() noexcept TF_NONBLOCKING {
    resetDsp();
    gate.reset();
    idle = true;
    silentCount = 0;
    snapNext = true;
}

void Engine::Impl::updateClock(const Transport& t) noexcept TF_NONBLOCKING {
    const double bpm = std::clamp(t.bpm, kMinBpm, kMaxBpm);
    qps = bpm / 60.0 / fs;
    playing = t.playing;
    if (playing) {
        const double expected = anchorPpq + static_cast<double>(sinceAnchor) * qps;
        if (!wasPlaying || bpm != anchorBpm || std::abs(t.ppq - expected) > kPpqTolerance) {
            anchorPpq = t.ppq;
            anchorBpm = bpm;
            sinceAnchor = 0;
        }
    }
    wasPlaying = playing;
}

void Engine::Impl::process(const Params& p, const Transport& transport, float* left, float* right, int numSamples,
                           Taps taps) noexcept TF_NONBLOCKING {
    ScopedFlushDenormals noDenormals;
    clipMaxDb = clipSumDb = 0.0;
    glideMs = p[ParamId::glide];
    phaseResetOn = p.on(ParamId::phaseReset);
    beatLockOn = p.on(ParamId::beatLock);
    gate.setEnvelope(p[ParamId::ampAttack], p[ParamId::ampDecay], p[ParamId::ampSustain], p[ParamId::ampRelease]);
    updateClock(transport);

    int n = 0;
    if (!idle) {
        double blockMinLimGain = 1.0;
        for (; n < numSamples; ++n) {
            if (countdown == 0) {
                controlTick(p);
                countdown = controlBase;
            }
            --countdown;

            if (gate.step() && pendingNote >= 0) {
                const int note = pendingNote;
                const double vel = pendingVelocity;
                pendingNote = -1;
                beginNote(note, vel);
            }
            const double env = gateDelay.process(gate.value());

            for (int j = 0; j < factor; ++j)
                oversampledSample(j);
            const double sub = decSub.process(bufSub.data());
            const double l = decL.process(bufL.data());
            const double r = decR.process(bufR.data());

            for (auto& ramp : base)
                ramp.next();

            // Growl: high-pass at the split, squash, the fixed gain into the
            // growl's ceiling stage. (growlLevel comes after that stage.)
            double gl = growlHpL.process(cSub, l);
            double gr = growlHpR.process(cSub, r);
            squash.process(gl, gr, base[squashAmt].value);
            gl *= kGrowlGain;
            gr *= kGrowlGain;

            // Mid/side: the side is high-passed at the crossover; the mid gets
            // the matching all-pass so both keep the same phase above it.
            double mid = 0.5 * (gl + gr), side = 0.5 * (gl - gr);
            mid = midAp.process(cXover, mid) * base[midGain].value;
            side = sideHp.process(cXover, side) * base[sideGain].value;

            // The growl clipper, linked (one gain from the larger channel) and
            // at 2x, saturating towards the growl's static ceiling G (what the
            // sub's peak leaves under the output ceiling). Any memoryless
            // nonlinearity on a harmonic growl regenerates f0 and 2 f0, so both
            // channels are high-passed at the sub frequency again after it. (A
            // second side high-pass at the crossover here measured 2 dB worse
            // for mono low end, not better, so there is none.) The sub and the
            // master tap take the same 2x trip.
            // Sub lock: the same 2x trip, nothing else.
            std::array<float, ClipTrip::kFactor> upSub{}, upMaster{};
            clip2xSub.up(static_cast<float>(subLp.process(cSub, sub) * base[subLevel].value), upSub.data());
            clip2xMaster.up(static_cast<float>(sub), upMaster.data());
            const double subOut = clip2xSub.down(upSub.data());
            const double masterTap = clip2xMaster.down(upMaster.data());

            // G is static: the exact steady-state sub peak per control period.
            // Only when the sub's own transients (a note start through the
            // LR4) rise above that does the held sub envelope lower it; the
            // envelope holds 60 ms and releases slowly, so in steady state it
            // equals the sub peak and never follows the waveform.
            const double held = -subPeakHold.process(-std::abs(subOut));
            subEnvelope = held > subEnvelope ? held : subEnvelope + subEnvelopeRelease * (held - subEnvelope);
            const double ceilingG = std::max(
                std::min(base[growlCeiling].value, base[ceilingGain].value * kGrowlCeilingMargin - subEnvelope),
                kGrowlCeilingFloor);
            const double lim = base[limitAmt].value;
            // Clip 1.4 dB below ceilingG so post-clip BW4 overshoot lands under the
            // look-ahead limiter's threshold, keeping the limiter transparent and ripple-free.
            const double clipCeilEff = lim * (0.85 * ceilingG) + (1.0 - lim) * 2.5;
            std::array<float, ClipTrip::kFactor> upL{}, upR{};
            const double preClip = mid; // (L + R) / 2
            clip2xL.up(static_cast<float>(mid + side), upL.data());
            clip2xR.up(static_cast<float>(mid - side), upR.data());
            double clipDbNow = 0.0;
            for (std::size_t k = 0; k < ClipTrip::kFactor; ++k) {
                double a = upL[k], c = upR[k];
                for (std::size_t pass = 0; pass < kClipPasses; ++pass) {
                    const double db = -gainToDb(growlClip(a, c, clipCeilEff));
                    if (pass == 0)
                        clipDbNow = std::max(clipDbNow, db);
                    a = postClipHp[pass][0].process(cSubClip, a);
                    c = postClipHp[pass][1].process(cSubClip, c);
                    if (pass == 0) {
                        const double pm = midApPost.process(cXoverClip, 0.5 * (a + c));
                        const double ps = sideHpPost.process(cXoverClip, 0.5 * (a - c));
                        a = pm + ps;
                        c = pm - ps;
                    }
                }
                upL[k] = static_cast<float>(a);
                upR[k] = static_cast<float>(c);
            }
            gl = clip2xL.down(upL.data());
            gr = clip2xR.down(upR.data());
            if (clipDbNow > 0.0) {
                clipMaxDb = std::max(clipMaxDb, clipDbNow);
                clipSumDb += clipDbNow;
            }

            // Gate + velocity scaling, then two look-ahead limiters with identical delay.
            const double velEnv = env * base[ampVel].value;
            const double subGated = subOut * velEnv;
            const double ceilingLin = base[ceilingGain].value;
            double outL = gl * velEnv, outR = gr * velEnv;
            std::array<double, 1> growlDelayed{0.5 * (gl + gr)};
            limiter.process(outL, outR, growlDelayed, ceilingG, lim);
            const double level = base[growlLevel].value;
            outL *= level;
            outR *= level;
            growlDelayed[0] *= level;
            double subL = subGated, subR = subGated;
            std::array<double, 1> subDelayed{masterTap};
            subLimiter.process(subL, subR, subDelayed, ceilingLin, lim);
            blockMinLimGain = std::min(blockMinLimGain, std::min(limiter.gain(), subLimiter.gain()));
            outL += subL;
            outR += subR;
            outputClip(outL, outR, ceilingLin, lim);

            left[n] = static_cast<float>(outL);
            right[n] = static_cast<float>(outR);
            if (taps.master != nullptr)
                taps.master[n] = static_cast<float>(subDelayed[0]);
            if (taps.growl != nullptr)
                taps.growl[n] = static_cast<float>(growlDelayed[0]);
            if (taps.growlPreClip != nullptr)
                taps.growlPreClip[n] = static_cast<float>(preClip);

            ++sinceAnchor;
            ++sinceNoteStart;

            if (gate.silent() && notes.empty() && pendingNote < 0) {
                if (++silentCount > gateDelay.delay() + limiter.latency() + 2) {
                    goIdle();
                    ++n;
                    break;
                }
            } else {
                silentCount = 0;
            }
        }
        grDb = static_cast<float>(-gainToDb(blockMinLimGain));
    }
    clipDb = static_cast<float>(clipMaxDb);
    clipMeanDb = static_cast<float>(clipSumDb / numSamples);

    // Idle: silence, and the clocks keep running.
    if (n < numSamples) {
        const auto rest = static_cast<std::size_t>(numSamples - n);
        std::fill_n(left + n, rest, 0.0f);
        std::fill_n(right + n, rest, 0.0f);
        if (taps.master != nullptr)
            std::fill_n(taps.master + n, rest, 0.0f);
        if (taps.growl != nullptr)
            std::fill_n(taps.growl + n, rest, 0.0f);
        if (taps.growlPreClip != nullptr)
            std::fill_n(taps.growlPreClip + n, rest, 0.0f);
        grDb = 0.0f;
        // The lanes keep their control periods while the DSP sleeps.
        for (; n < numSamples; ++n) {
            if (countdown == 0) {
                laneTick(p, lanePpqAhead(controlBase), notePpqAhead(controlBase), tickCoef);
                const double mw = smooth[modWheel_].step(modWheelTarget, tickCoef);
                const double ti = smooth[timbreOffset_].step(timbreOffsetTarget, tickCoef);
                const double at = smooth[aftertouch_].step(aftertouchTarget, tickCoef);
                effective.coupling = static_cast<float>(std::clamp(
                    moved(LaneTarget::coupling, static_cast<double>(p[ParamId::coupling]), 0.0, 1.0) + 0.35 * at, 0.0,
                    1.0));
                effective.beat =
                    static_cast<float>(moved(LaneTarget::beat, static_cast<double>(p[ParamId::beat]), 0.0, 1.0));
                effective.reach =
                    static_cast<float>(moved(LaneTarget::reach,
                                             static_cast<double>(std::clamp(p[ParamId::reach], spec(ParamId::reach).min,
                                                                            spec(ParamId::reach).max)),
                                             spec(ParamId::reach).min, spec(ParamId::reach).max));
                effective.morph = static_cast<float>(std::clamp(
                    moved(LaneTarget::morph, static_cast<double>(p[ParamId::morph]), 0.0, 1.0) + ti, 0.0, 1.0));
                effective.shift = static_cast<float>(
                    moved(LaneTarget::shift, static_cast<double>(p[ParamId::tractShift]), -24.0, 24.0));
                effective.drive = static_cast<float>(std::clamp(
                    moved(LaneTarget::drive, static_cast<double>(p[ParamId::drive]), 0.0, 1.0) + 0.35 * mw, 0.0, 1.0));
                double sc = moved(LaneTarget::sculpt,
                                  static_cast<double>(std::clamp(p[ParamId::sculpt], spec(ParamId::sculpt).min,
                                                                 spec(ParamId::sculpt).max)),
                                  spec(ParamId::sculpt).min, spec(ParamId::sculpt).max);
                if (at > 0.0)
                    sc = std::clamp(sc * std::exp2(at), static_cast<double>(spec(ParamId::sculpt).min),
                                    static_cast<double>(spec(ParamId::sculpt).max));
                effective.sculpt = static_cast<float>(sc);
                effective.width =
                    static_cast<float>(moved(LaneTarget::width, static_cast<double>(p[ParamId::width]), 0.0, 1.0));
                effective.active = true;
                countdown = controlBase;
            }
            --countdown;
            ++sinceAnchor;
            ++sinceNoteStart;
        }
    }
}

Engine::Engine() : impl_(std::make_unique<Impl>()) { impl_->prepare(48000.0, 4); }
Engine::~Engine() = default;

void Engine::prepare(double sampleRate, int /*maxBlockSize*/, int oversampling) {
    impl_->prepare(sampleRate > 0.0 ? sampleRate : 48000.0, std::max(1, oversampling));
}
void Engine::reset() noexcept TF_NONBLOCKING { impl_->reset(); }
int Engine::latencySamples() const noexcept TF_NONBLOCKING { return impl_->latency(); }
int Engine::oversamplingFactor() const noexcept TF_NONBLOCKING { return impl_->factor; }

void Engine::noteOn(int midiNote, float velocity) noexcept TF_NONBLOCKING { impl_->noteOn(midiNote, velocity); }
void Engine::noteOff(int midiNote) noexcept TF_NONBLOCKING { impl_->noteOff(midiNote); }
void Engine::sustain(bool down) noexcept TF_NONBLOCKING { impl_->sustain(down); }
void Engine::allNotesOff() noexcept TF_NONBLOCKING { impl_->allNotesOff(); }
void Engine::setLaneCurves(const LaneCurves& curves) noexcept TF_NONBLOCKING { impl_->laneCurves = curves; }

void Engine::pitchBend(float semitones) noexcept TF_NONBLOCKING {
    impl_->bendTarget = std::clamp(static_cast<double>(semitones), -impl_->bendRange, impl_->bendRange);
}
void Engine::setPitchBendRange(float semitones) noexcept TF_NONBLOCKING {
    impl_->bendRange = std::clamp(static_cast<double>(semitones), 1.0, 48.0);
    impl_->bendTarget = std::clamp(impl_->bendTarget, -impl_->bendRange, impl_->bendRange);
}
void Engine::setModWheel(float norm01) noexcept TF_NONBLOCKING {
    impl_->modWheelTarget = std::clamp(static_cast<double>(norm01), 0.0, 1.0);
}
void Engine::setTimbre(float norm01) noexcept TF_NONBLOCKING {
    impl_->timbreOffsetTarget = std::clamp(static_cast<double>(norm01), 0.0, 1.0) - 0.5;
}
void Engine::setAftertouch(float norm01) noexcept TF_NONBLOCKING {
    impl_->aftertouchTarget = std::clamp(static_cast<double>(norm01), 0.0, 1.0);
}

void Engine::process(const Params& params, const Transport& transport, float* left, float* right, int numSamples,
                     Taps taps) noexcept TF_NONBLOCKING {
    if (numSamples <= 0 || left == nullptr || right == nullptr) return;
    impl_->process(params, transport, left, right, numSamples, taps);
}

float Engine::currentHz() const noexcept TF_NONBLOCKING { return impl_->hz; }
bool Engine::gateOpen() const noexcept TF_NONBLOCKING { return !impl_->notes.empty(); }
int Engine::activeNote() const noexcept TF_NONBLOCKING { return impl_->notes.top(); }
float Engine::gainReductionDb() const noexcept TF_NONBLOCKING { return std::max(0.0f, impl_->grDb); }
double Engine::lanePpq() const noexcept TF_NONBLOCKING { return impl_->uiPpq; }
double Engine::notePpq() const noexcept TF_NONBLOCKING { return impl_->uiNotePpq; }
float Engine::growlClipDb() const noexcept TF_NONBLOCKING { return impl_->clipDb; }
float Engine::growlClipMeanDb() const noexcept TF_NONBLOCKING { return impl_->clipMeanDb; }
std::array<float, kLaneCount> Engine::lanePhases() const noexcept TF_NONBLOCKING { return impl_->uiPhase; }
std::array<float, kLaneCount> Engine::laneValues() const noexcept TF_NONBLOCKING { return impl_->uiValue; }
Engine::EffectiveState Engine::effectiveState() const noexcept TF_NONBLOCKING { return impl_->effective; }
}
