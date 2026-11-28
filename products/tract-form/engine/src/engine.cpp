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
    if (filterMix < 1.0)
        filterMix = std::min(1.0, filterMix + foldMixStep);
    if (stackA.mix < 1.0)
        stackA.mix = std::min(1.0, stackA.mix + foldMixStep);
    if (stackB.mix < 1.0)
        stackB.mix = std::min(1.0, stackB.mix + foldMixStep);

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
    // Gentle at low settings for pure sub headroom; blooms warmly into rich 2nd/3rd harmonics
    // at higher settings for effortless small-speaker translation.
    const double master = std::sin(kTwoPi * phaseMaster);
    const double pSub = kTwoPi * frac(phaseMaster + os[subRot].value);
    const double subFund = std::sin(pSub);
    const double sw = os[subWarmth_].value;
    const double sw2 = 0.24 * sw + 0.14 * sw * sw;
    const double sw3 = 0.14 * sw + 0.08 * sw * sw;
    const float subSine = static_cast<float>(
        sw <= 1.0e-5 ? subFund : (subFund + sw2 * std::sin(2.0 * pSub) + sw3 * std::sin(3.0 * pSub)) / (1.0 + sw2));

    const double fbTap2 = std::min(os[fbDelay].value + os[fbPeriod].value, feedback.maxDelay());
    const double fbRaw = 0.5 * (feedback.read(os[fbDelay].value) + feedback.read(fbTap2));
    const double fb = os[fbGain].value * std::tanh(feedbackDc.process(fbRaw));

    const double phiA = kTwoPi * frac(phaseA + os[offA].value);
    const double phiB = kTwoPi * frac(phaseB + os[offB].value);
    const double phiLockA = kTwoPi * frac(phaseLockA + os[offA].value);
    const double phiLockB = kTwoPi * frac(phaseLockB + os[offB].value);
    const double modA = os[pmA].value * master + fb;
    const double modB = os[pmB].value * master + fb;
    double a = renderVoice(stackA, os[shapeA].value, phiA, phiLockA, modA, os[incA].value, os[offA].value);
    double b = renderVoice(stackB, os[shapeB].value, phiB, phiLockB, modB, os[incB].value, os[offB].value);
    const double lockA = std::sin(phiLockA + modA);
    const double lockB = std::sin(phiLockB + modB);

    feedback.write(static_cast<float>(feedbackLp.process(ReachCoefs::make(os[loopG].value), 0.5 * (lockA + lockB))));

    // The filter in the Pre position, before Tract and Fold (crossfaded with the Post position).
    const FilterControls fc{os[reachG].value, os[filterResO].value, os[filterDriveG].value};
    const double filterPost = os[filterPost_].value;
    if (filterPost < 1.0 - 1.0e-5) {
        const double pa = filterPreA.process(filterFrom, filterTo, filterMix, fc, a);
        const double pb = filterPreB.process(filterFrom, filterTo, filterMix, fc, b);
        a = (1.0 - filterPost) * pa + filterPost * a;
        b = (1.0 - filterPost) * pb + filterPost * b;
    } else {
        filterPreA.reset();
        filterPreB.reset();
    }

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
    // The filter in the Post position, after Fold. In the Pre position a Clean
    // low-pass two octaves above Reach keeps the fold's harmonics bounded.
    double filtA = ya, filtB = yb;
    if (filterPost > 1.0e-5) {
        const double pa = filterPostA.process(filterFrom, filterTo, filterMix, fc, ya);
        const double pb = filterPostB.process(filterFrom, filterTo, filterMix, fc, yb);
        if (filterPost < 1.0 - 1.0e-5) {
            const ReachCoefs top = ReachCoefs::make(os[topG].value);
            filtA = filterPost * pa + (1.0 - filterPost) * topA.process(top, ya);
            filtB = filterPost * pb + (1.0 - filterPost) * topB.process(top, yb);
        } else {
            filtA = pa;
            filtB = pb;
            topA.reset();
            topB.reset();
        }
    } else {
        filterPostA.reset();
        filterPostB.reset();
        const ReachCoefs top = ReachCoefs::make(os[topG].value);
        filtA = topA.process(top, ya);
        filtB = topB.process(top, yb);
    }
    const double preScA = weightA.process(mix, filtA);
    const double preScB = weightB.process(mix, filtB);
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

// One reese voice: its model at the centre phase plus the stack's side voices,
// every one phase-modulated by the same `mod` (radians) and offset by the same
// `offset` (cycles). The centre keeps the engine's own phase, so a stack of one
// is the voice exactly as it was.
double Engine::Impl::renderVoice(VoiceStack& v, double shape, double phi, double phiLock, double mod, double inc,
                                 double offset) noexcept TF_NONBLOCKING {
    const double modCycles = mod / kTwoPi;
    const auto model = [&](VoiceModel m, double ph, double phLock, double t, double dt, double step,
                           bool locked) noexcept TF_NONBLOCKING -> double {
        return m == VoiceModel::sine ? warpedSine(shape, ph, phLock, mod, step, locked)
                                     : modelSample(m, v.shape, t, dt);
    };
    const auto sample = [&](std::size_t j, double ph, double phLock, double step,
                            bool locked) noexcept TF_NONBLOCKING -> double {
        const double t = frac(ph / kTwoPi + modCycles);
        const double dt = v.fresh ? 0.0 : wrapHalf(t - v.lastT[j]);
        v.lastT[j] = t;
        const double now = model(v.to, ph, phLock, t, dt, step, locked);
        if (v.mix >= 1.0 || v.from == v.to)
            return now;
        return v.mix * now + (1.0 - v.mix) * model(v.from, ph, phLock, t, dt, step, locked);
    };
    double out = os[stackGain0].value * sample(0, phi, phiLock, inc, beatLockOn);
    for (std::size_t j = 1; j < v.phase.size(); ++j) {
        const double step = inc * stackMul[j] + stackAdd[j];
        v.phase[j] += step;
        v.phase[j] -= std::floor(v.phase[j]);
        const double ph = kTwoPi * frac(v.phase[j] + offset);
        const double gain = os[stackGain0 + j].value;
        if (gain == 0.0) {
            v.lastT[j] = frac(ph / kTwoPi + modCycles);
            continue;
        }
        out += gain * sample(j, ph, ph, step, false);
    }
    v.fresh = false;
    return out;
}

// Side voices restart at the same spread-out phases every time (voice B's
// interleaved with A's), so a note sounds the same however many came before it.
void Engine::Impl::resetStacks() noexcept TF_NONBLOCKING {
    hba::dsp::unisonSpreadPhases(0.0, stackA.phase);
    hba::dsp::unisonSpreadPhases(0.5, stackB.phase);
    stackA.fresh = stackB.fresh = true;
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
    playModeCur = static_cast<PlayMode>(std::clamp(p.choice(ParamId::playMode), 0, 1));
    glideModeCur = static_cast<GlideMode>(std::clamp(p.choice(ParamId::glideMode), 0, 1));
    octaveSt = kOctaveSemitones[static_cast<std::size_t>(std::clamp(p.choice(ParamId::octave), 0, 3))];
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
            // Sub lock: the same 4x trip + matched pass-0 ADAA1 linear midpoint, nothing else.
            std::array<float, ClipTrip::kFactor> upSub{}, upMaster{};
            clip2xSub.up(static_cast<float>(subLp.process(cSub, sub) * base[subLevel].value), upSub.data());
            clip2xMaster.up(static_cast<float>(sub), upMaster.data());
            for (std::size_t k = 0; k < ClipTrip::kFactor; ++k) {
                const double vs = upSub[k];
                upSub[k] = static_cast<float>(0.5 * (vs + subClipHalfDelay));
                subClipHalfDelay = vs;
                const double vm = upMaster[k];
                upMaster[k] = static_cast<float>(0.5 * (vm + masterClipHalfDelay));
                masterClipHalfDelay = vm;
            }
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
            // Pass 0 clips at 0.85*ceilingG (ADAA1 at 4x); pass 1 trims post-BW4 overshoot
            // to 0.73*ceilingG (knee 0.78 -> linear up to 0.57*ceilingG) so the final BW4 HP
            // lands under the look-ahead limiter's threshold, free of 5-15 Hz side ripple.
            const double clipCeil0 = lim * (0.85 * ceilingG) + (1.0 - lim) * 2.5;
            const double clipCeil1 = lim * (0.73 * ceilingG) + (1.0 - lim) * 2.5;
            std::array<float, ClipTrip::kFactor> upL{}, upR{};
            const double preClip = mid; // (L + R) / 2
            clip2xL.up(static_cast<float>(mid + side), upL.data());
            clip2xR.up(static_cast<float>(mid - side), upR.data());
            double clipDbNow = 0.0;
            for (std::size_t k = 0; k < ClipTrip::kFactor; ++k) {
                double a = upL[k], c = upR[k];
                for (std::size_t pass = 0; pass < kClipPasses; ++pass) {
                    const double gain = pass == 0 ? growlClipAdaa.process(a, c, clipCeil0)
                                                  : softClipLinked(a, c, 0.78 * clipCeil1, clipCeil1);
                    if (pass == 0)
                        clipDbNow = std::max(clipDbNow, -gainToDb(gain));
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
            ++freeSamples;

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
                laneTick(p, lanePpqAhead(controlBase), notePpqAhead(controlBase), freeSecAhead(controlBase),
                         noteSecAhead(controlBase), tickCoef);
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
            ++freeSamples;
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
double Engine::freeSeconds() const noexcept TF_NONBLOCKING { return impl_->uiFreeSec; }
double Engine::noteSeconds() const noexcept TF_NONBLOCKING { return impl_->uiNoteSec; }
float Engine::growlClipDb() const noexcept TF_NONBLOCKING { return impl_->clipDb; }
float Engine::growlClipMeanDb() const noexcept TF_NONBLOCKING { return impl_->clipMeanDb; }
std::array<float, kLaneCount> Engine::lanePhases() const noexcept TF_NONBLOCKING { return impl_->uiPhase; }
std::array<float, kLaneCount> Engine::laneValues() const noexcept TF_NONBLOCKING { return impl_->uiValue; }
Engine::EffectiveState Engine::effectiveState() const noexcept TF_NONBLOCKING { return impl_->effective; }
}
