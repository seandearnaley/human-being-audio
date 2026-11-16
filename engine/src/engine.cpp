#include "tf/engine.h"

#include "control.h"
#include "crossover.h"
#include "decimator.h"
#include "denormals.h"
#include "feedback_delay.h"
#include "fold.h"
#include "lanes.h"
#include "limiter.h"
#include "squash.h"
#include "tract.h"
#include "voice.h"

#include "tf/feel.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace tf {

using namespace dsp;

namespace {

// Control values are recomputed every 16 oversampled samples and move linearly
// in between; one-pole smoothing at that rate removes steps from automation,
// choices and lanes.
constexpr int kControlPeriodOs = 16;
constexpr double kSmoothSeconds = 0.010;
constexpr double kDelaySmoothSeconds = 0.050;
constexpr double kFoldCrossfadeSeconds = 0.010;
constexpr double kMaxFeedbackDelaySeconds = 2.0;  // 1/4 at 30 bpm
constexpr double kBendRange = 2.0;
constexpr double kMinBpm = 20.0;
constexpr double kMaxBpm = 999.0;
constexpr double kPpqTolerance = 1e-5;  // quarters; larger jumps re-anchor the clock
// Gain staging. The growl leaves the fold at full scale; at growlLevel 1 it is
// trimmed so that, on top of a sub at subLevel 0.5 (-6 dBFS peak), the default
// patch peaks below the soft clip's knee and the limiter stays idle. Without
// it the output stage ducks the sub on every growl peak.
constexpr double kGrowlTrim = 0.4;

int effectiveOversampling(double sampleRate, int requested) {
    int factor = 1;
    while (factor < requested && factor < kMaxOversampling) factor *= 2;
    const int divisor = sampleRate >= 176400.0 ? 4 : sampleRate >= 88200.0 ? 2 : 1;
    const int minimum = requested >= 2 ? 2 : 1;
    return std::max(minimum, factor / divisor);
}

// Oversampled-domain control values, ramped per oversampled sample.
enum Os : std::size_t { incMaster, incA, incB, offA, offB, pmA, pmB, fbGain, fbDelay, width, tractAmt, driveGain, asymOffset, warpAmt, osCount };
// Base-rate control values, ramped per base sample.
enum Base : std::size_t { subLevel, growlLevel, midGain, sideGain, squashAmt, ceilingGain, limitAmt, baseCount };
// Smoothed raw parameters.
enum Sm : std::size_t {
    coupling, beat, aDetune, bDetune, aRatio, bRatio, aPhase, bPhase, aPm, bPm, fbAmount, width_, tract_, drive_, asym_,
    warp_, midGainDb, sideGainDb, xoverLog, subFreqLog, subLevel_, growlLevel_, squash_, ceilingDb, limitOn, bend,
    smCount
};

double noteToHz(double note) noexcept TF_NONBLOCKING { return 440.0 * std::exp2((note - 69.0) / 12.0); }

}

struct Engine::Impl {
    // Configuration, fixed by prepare().
    double fs = 48000.0;
    double fo = 192000.0;
    int factor = 4;
    int controlBase = 4;  // base samples per control period
    int controlOs = 16;   // oversampled samples per control period
    int decimatorDelay = 0;
    double decimatorDelayExact = 0.0;
    double tickCoef = 1.0, delayTickCoef = 1.0;
    std::array<double, kVowelCount> vowelComp {};

    // Oversampled domain.
    Decimator decSub, decL, decR;
    LocalOversampler localSub, localL, localR;  // around the fold; the sub gets the same delay
    std::array<float, kMaxOversampling> bufSub {}, bufL {}, bufR {}, bufLocal {};
    FeedbackDelay feedback;
    DcBlocker feedbackDc;
    TractCoefs tractCoefs;
    TractChannel tractL, tractR;
    Folder foldL, foldR;
    double phaseMaster = 0.0, phaseA = 0.0, phaseB = 0.0;
    FoldType foldFrom = FoldType::fold, foldTo = FoldType::fold;
    double foldMix = 1.0, foldMixStep = 1.0;
    std::array<Ramp, osCount> os {};

    // Base rate.
    Lr4Highpass growlHpL, growlHpR, sideHp;
    Lr4Allpass midAp;
    Lr4Lowpass subLp;
    SvfCoefs cSub, cXover;
    Squash squash;
    Limiter limiter;
    std::array<Ramp, baseCount> base {};

    // Control.
    std::array<Smoother, smCount> smooth {};
    std::array<Smoother, kLaneCount> laneSmooth {};
    std::array<Smoother, kFormantCount * 3 + 1> formantSmooth {};
    Smoother delaySmooth;
    int countdown = 0;
    bool snapNext = true;

    // Notes and gate.
    NoteStack notes;
    Glide glide;
    Gate gate;
    DelayLine<double> gateDelay;
    int pendingNote = -1;
    double bendTarget = 0.0;
    double glideMs = spec(ParamId::glide).def;
    bool phaseResetOn = true;

    // Clock.
    bool wasPlaying = false;
    double anchorPpq = 0.0, anchorBpm = 0.0;
    long long sinceAnchor = 0, sinceNoteStart = 0;
    double qps = 0.0;  // quarters per base sample
    bool playing = false;

    // Idle.
    bool idle = true;
    int silentCount = 0;

    // Interface state.
    float hz = 0.0f;
    float grDb = 0.0f;
    std::array<float, kLaneCount> uiPhase {}, uiValue {};
    double uiPpq = 0.0;  // the lane clock, host position or free-running

    void prepare(double sampleRate, int oversampling) {
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
        localSub.prepare(factor);
        localL.prepare(factor);
        localR.prepare(factor);
        decimatorDelayExact = measureDecimatorDelay(factor);
        decimatorDelay = static_cast<int>(std::lround(decimatorDelayExact));

        feedback.prepare(static_cast<int>(std::ceil(kMaxFeedbackDelaySeconds * fo)));
        feedbackDc.prepare(5.0, fo);
        vowelComp = computeVowelCompDb(fo);
        foldL.prepare(fo * localL.factor());
        foldR.prepare(fo * localR.factor());
        squash.prepare(fs);
        limiter.prepare(fs);
        gate.prepare(fs);
        gateDelay.prepare(decimatorDelay);
        reset();
    }

    // Group delay at DC of the sub's trip (local oversampler, then decimator),
    // which the growl shares by construction, in base-rate samples.
    static double measureDecimatorDelay(int factor) {
        Decimator d;
        d.prepare(factor);
        LocalOversampler local;
        local.prepare(factor);
        std::array<float, kMaxOversampling> buf {}, scratch {};
        double moment = 0.0, sum = 0.0;
        for (int n = 0; n < 4096; ++n) {
            for (int j = 0; j < factor; ++j) {
                local.up(n == 0 && j == 0 ? 1.0f : 0.0f, scratch.data());
                buf[static_cast<std::size_t>(j)] = local.down(scratch.data());
            }
            const double y = d.process(buf.data());
            moment += n * y;
            sum += y;
        }
        return sum != 0.0 ? moment / sum : 0.0;
    }

    void resetDsp() noexcept TF_NONBLOCKING {
        decSub.reset();
        decL.reset();
        decR.reset();
        localSub.reset();
        localL.reset();
        localR.reset();
        feedback.clear();
        feedbackDc.reset();
        tractL.reset();
        tractR.reset();
        foldL.reset();
        foldR.reset();
        growlHpL.reset();
        growlHpR.reset();
        sideHp.reset();
        midAp.reset();
        subLp.reset();
        squash.reset();
        limiter.reset();
        gateDelay.clear();
        foldMix = 1.0;
    }

    void reset() noexcept TF_NONBLOCKING {
        resetDsp();
        gate.reset();
        notes.clear();
        pendingNote = -1;
        phaseMaster = phaseA = phaseB = 0.0;
        for (auto& s : smooth) s.reset();
        for (auto& s : laneSmooth) s.reset();
        for (auto& s : formantSmooth) s.reset();
        delaySmooth.reset();
        snapNext = true;
        countdown = 0;
        idle = true;
        silentCount = 0;
        wasPlaying = false;
        sinceAnchor = sinceNoteStart = 0;
        grDb = 0.0f;
    }

    int latency() const noexcept TF_NONBLOCKING { return decimatorDelay + limiter.latency(); }

    // ---- notes -------------------------------------------------------------

    void wake() noexcept TF_NONBLOCKING {
        if (!idle) return;
        idle = false;
        silentCount = 0;
        countdown = 0;
        snapNext = true;
    }

    // A note that starts from silence or after the stack emptied.
    void startNote(int note) noexcept TF_NONBLOCKING {
        if (pendingNote >= 0) {
            pendingNote = note;
            return;
        }
        if (phaseResetOn && !idle && gate.value() > 0.0) {
            gate.fade();
            pendingNote = note;
            return;
        }
        wake();
        beginNote(note);
    }

    void beginNote(int note) noexcept TF_NONBLOCKING {
        if (phaseResetOn) phaseMaster = phaseA = phaseB = 0.0;
        glide.jump(note);
        sinceNoteStart = 0;
        if (!snapNext) retargetPitchNow();
        gate.open();
    }

    void noteOn(int note) noexcept TF_NONBLOCKING {
        note = std::clamp(note, 0, 127);
        const bool legato = !notes.empty();
        notes.push(note);
        if (legato && pendingNote < 0) {
            glide.glideTo(note, glideMs * 1e-3 * fs);
        } else {
            startNote(note);
        }
    }

    void noteOff(int note) noexcept TF_NONBLOCKING {
        const bool wasTop = notes.top() == note;
        if (!notes.remove(note)) return;
        if (notes.empty()) {
            if (pendingNote >= 0) pendingNote = -1;  // the fade runs out, nothing follows
            else gate.close();
        } else if (wasTop) {
            if (pendingNote >= 0) pendingNote = notes.top();
            else glide.glideTo(notes.top(), glideMs * 1e-3 * fs);
        }
    }

    void allNotesOff() noexcept TF_NONBLOCKING {
        notes.clear();
        pendingNote = -1;
        if (gate.stage() != Gate::Stage::fade) gate.close();
    }

    // ---- control -----------------------------------------------------------

    double sm(Sm id) const noexcept TF_NONBLOCKING { return smooth[id].value(); }

    void pitchIncrements(double& master, double& a, double& b) const noexcept TF_NONBLOCKING {
        const double f0 = noteToHz(glide.current() + sm(bend));
        const double beatMod = std::clamp(sm(beat) + 0.5 * laneSmooth[3].value(), 0.0, 1.0);
        const auto detA = feel::detuneCents(static_cast<float>(sm(aDetune)), static_cast<float>(beatMod));
        const auto detB = feel::detuneCents(static_cast<float>(sm(bDetune)), static_cast<float>(beatMod));
        master = f0 / fo;
        a = master * sm(aRatio) * std::exp2(detA / 1200.0);
        b = master * sm(bRatio) * std::exp2(detB / 1200.0);
    }

    // A new note starts at pitch: jump the pitch ramps instead of waiting for
    // the next control point.
    void retargetPitchNow() noexcept TF_NONBLOCKING {
        double m, a, b;
        pitchIncrements(m, a, b);
        os[incMaster].snap(m);
        os[incA].snap(a);
        os[incB].snap(b);
    }

    void controlTick(const Params& p) noexcept TF_NONBLOCKING {
        const bool snap = snapNext;
        snapNext = false;
        const double c = snap ? 1.0 : tickCoef;
        const auto step = [&](Sm id, double target) { return smooth[id].step(target, c); };

        // Lanes, evaluated at the end of this control period.
        const double ppqEnd = playing ? anchorPpq + static_cast<double>(sinceAnchor + controlBase) * qps
                                      : static_cast<double>(sinceNoteStart + controlBase) * qps;
        const double motion = p[ParamId::motion];
        uiPpq = ppqEnd;
        for (int i = 0; i < kLaneCount; ++i) {
            const double theta = lanePhase(ppqEnd, p.choice(laneParam(i, LaneField::rate)), p[laneParam(i, LaneField::phase)]);
            const double raw = p.on(laneParam(i, LaneField::on))
                                   ? laneShape(static_cast<LaneShape>(p.choice(laneParam(i, LaneField::shape))), theta, i) *
                                         p[laneParam(i, LaneField::depth)] * motion
                                   : 0.0;
            const double v = laneSmooth[static_cast<std::size_t>(i)].step(raw, c);
            uiPhase[static_cast<std::size_t>(i)] = static_cast<float>(theta);
            uiValue[static_cast<std::size_t>(i)] = static_cast<float>(v);
        }
        const double lanePhaseMod = laneSmooth[0].value();
        const double laneMorph = laneSmooth[1].value();
        const double laneDrive = laneSmooth[2].value();

        step(coupling, p[ParamId::coupling]);
        step(beat, p[ParamId::beat]);
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
        step(drive_, p[ParamId::drive]);
        step(asym_, p[ParamId::asym]);
        step(warp_, p[ParamId::warp]);
        step(midGainDb, p[ParamId::midGain]);
        step(sideGainDb, p[ParamId::sideGain]);
        step(xoverLog, std::log(std::max(1.0f, p[ParamId::xover])));
        step(subFreqLog, std::log(std::max(1.0f, p[ParamId::subFreq])));
        step(subLevel_, p[ParamId::subLevel]);
        step(growlLevel_, p[ParamId::growlLevel]);
        step(squash_, p[ParamId::squash]);
        step(ceilingDb, p[ParamId::ceiling]);
        step(limitOn, p.on(ParamId::limit) ? 1.0 : 0.0);
        step(bend, bendTarget);

        // Pitch.
        glide.advance(controlBase);
        double incM, incAv, incBv;
        pitchIncrements(incM, incAv, incBv);
        hz = static_cast<float>(incM * fo);

        // Oversampled targets.
        std::array<double, osCount> t {};
        t[incMaster] = incM;
        t[incA] = incAv;
        t[incB] = incBv;
        // Phase offsets in cycles, not wrapped (the oscillator wraps), so a ramp
        // never sweeps the long way round.
        t[offA] = sm(aPhase) / 360.0 + 0.25 * lanePhaseMod;
        t[offB] = sm(bPhase) / 360.0 - 0.25 * lanePhaseMod;
        t[pmA] = feel::pmIndex(static_cast<float>(sm(aPm)), static_cast<float>(sm(coupling)));
        t[pmB] = feel::pmIndex(static_cast<float>(sm(bPm)), static_cast<float>(sm(coupling)));
        t[fbGain] = feel::feedbackRadians(static_cast<float>(sm(fbAmount)));
        const double bpm = anchorBpmOrCurrent();
        const double delayTarget = kFbTimeQuarters[static_cast<std::size_t>(std::clamp(p.choice(ParamId::fbTime), 0, 6))] *
                                   60.0 / bpm * fo;
        t[fbDelay] = std::clamp(delaySmooth.step(delayTarget, snap ? 1.0 : delayTickCoef), 2.0, feedback.maxDelay());
        t[width] = std::clamp(sm(width_), 0.0, 1.0);
        t[tractAmt] = std::clamp(sm(tract_), 0.0, 1.0);
        const double driveMod = std::clamp(sm(drive_) + 0.5 * laneDrive, 0.0, 1.0);
        t[driveGain] = feel::driveGain(static_cast<float>(driveMod));
        t[asymOffset] = 0.5 * sm(asym_);
        t[warpAmt] = std::clamp(sm(warp_), 0.0, 1.0);
        for (std::size_t i = 0; i < osCount; ++i) {
            if (snap) os[i].snap(t[i]);
            else os[i].to(t[i], controlOs);
        }

        // Tract.
        const double morphMod = std::clamp(static_cast<double>(p[ParamId::morph]) + 0.5 * laneMorph, 0.0, 1.0);
        const auto vowels = mixVowels(p.choice(ParamId::vowelA), p.choice(ParamId::vowelB), morphMod, vowelComp);
        FormantTargets f;
        for (std::size_t i = 0; i < kFormantCount; ++i) {
            f.logHz[i] = formantSmooth[i].step(vowels.logHz[i], c);
            f.logBandwidth[i] = formantSmooth[kFormantCount + i].step(vowels.logBandwidth[i], c);
            f.gainDb[i] = formantSmooth[2 * kFormantCount + i].step(vowels.gainDb[i], c);
        }
        f.compDb = formantSmooth[3 * kFormantCount].step(vowels.compDb, c);
        if (snap) tractCoefs.snap(f, fo);
        else tractCoefs.setTarget(f, fo, controlOs);

        // Fold type: crossfade on change.
        const auto type = static_cast<FoldType>(std::clamp(p.choice(ParamId::foldType), 0, 2));
        if (snap) {
            foldFrom = foldTo = type;
            foldMix = 1.0;
        } else if (type != foldTo) {
            foldFrom = foldMix >= 0.5 ? foldTo : foldFrom;
            foldTo = type;
            foldMix = 0.0;
        }

        // Base rate.
        cSub = butterworth(std::exp(sm(subFreqLog)), fs);
        cXover = butterworth(std::exp(sm(xoverLog)), fs);
        std::array<double, baseCount> b {};
        b[subLevel] = std::clamp(sm(subLevel_), 0.0, 1.0);
        b[growlLevel] = std::clamp(sm(growlLevel_), 0.0, 1.0);
        b[midGain] = dbToGain(sm(midGainDb));
        b[sideGain] = dbToGain(sm(sideGainDb));
        b[squashAmt] = std::clamp(sm(squash_), 0.0, 1.0);
        b[ceilingGain] = dbToGain(std::min(0.0, sm(ceilingDb)));
        b[limitAmt] = std::clamp(sm(limitOn), 0.0, 1.0);
        for (std::size_t i = 0; i < baseCount; ++i) {
            if (snap) base[i].snap(b[i]);
            else base[i].to(b[i], controlBase);
        }
    }

    double anchorBpmOrCurrent() const noexcept TF_NONBLOCKING { return qps * 60.0 * fs; }

    // ---- audio -------------------------------------------------------------

    void oversampledSample(int j) noexcept TF_NONBLOCKING {
        for (auto& r : os) r.next();
        tractCoefs.advance();
        if (foldMix < 1.0) foldMix = std::min(1.0, foldMix + foldMixStep);

        phaseMaster += os[incMaster].value;
        phaseA += os[incA].value;
        phaseB += os[incB].value;
        phaseMaster -= std::floor(phaseMaster);
        phaseA -= std::floor(phaseA);
        phaseB -= std::floor(phaseB);

        const float master = std::sin(static_cast<float>(kTwoPi * phaseMaster));
        const double fb = os[fbGain].value * std::tanh(feedbackDc.process(feedback.read(os[fbDelay].value)));
        const double a = std::sin(static_cast<float>(kTwoPi * frac(phaseA + os[offA].value) + os[pmA].value * master + fb));
        const double b = std::sin(static_cast<float>(kTwoPi * frac(phaseB + os[offB].value) + os[pmB].value * master + fb));
        feedback.write(static_cast<float>(0.5 * (a + b)));

        const double w = os[width].value;
        double l = (a + (1.0 - w) * b) / (2.0 - w);
        double r = (b + (1.0 - w) * a) / (2.0 - w);

        const double tractAmount = os[tractAmt].value;
        l = tractL.process(tractCoefs, tractAmount, l);
        r = tractR.process(tractCoefs, tractAmount, r);

        // The fold runs at the local (16x) rate; the master sine for the sub
        // takes the same trip so both paths keep the same delay.
        const double g = os[driveGain].value, off = os[asymOffset].value, warp = os[warpAmt].value;
        const int local = localL.factor();
        float* buf = bufLocal.data();
        localL.up(static_cast<float>(l), buf);
        for (int k = 0; k < local; ++k) buf[k] = static_cast<float>(foldL.process(buf[k], g, off, warp, foldFrom, foldTo, foldMix));
        const float lf = localL.down(buf);
        localR.up(static_cast<float>(r), buf);
        for (int k = 0; k < local; ++k) buf[k] = static_cast<float>(foldR.process(buf[k], g, off, warp, foldFrom, foldTo, foldMix));
        const float rf = localR.down(buf);
        localSub.up(master, buf);
        const float sub = localSub.down(buf);

        const auto idx = static_cast<std::size_t>(j);
        bufSub[idx] = sub;
        bufL[idx] = lf;
        bufR[idx] = rf;
    }

    void goIdle() noexcept TF_NONBLOCKING {
        resetDsp();
        gate.reset();
        idle = true;
        silentCount = 0;
        snapNext = true;
    }

    void updateClock(const Transport& t) noexcept TF_NONBLOCKING {
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

    void process(const Params& p, const Transport& transport, float* left, float* right, int numSamples,
                 Taps taps) noexcept TF_NONBLOCKING {
        ScopedFlushDenormals noDenormals;
        glideMs = p[ParamId::glide];
        phaseResetOn = p.on(ParamId::phaseReset);
        updateClock(transport);

        int n = 0;
        if (!idle) {
            for (; n < numSamples; ++n) {
                if (countdown == 0) {
                    controlTick(p);
                    countdown = controlBase;
                }
                --countdown;

                if (gate.step() && pendingNote >= 0) {
                    const int note = pendingNote;
                    pendingNote = -1;
                    beginNote(note);
                }
                const double env = gateDelay.process(gate.value());

                for (int j = 0; j < factor; ++j) oversampledSample(j);
                const double sub = decSub.process(bufSub.data());
                const double l = decL.process(bufL.data());
                const double r = decR.process(bufR.data());

                for (auto& ramp : base) ramp.next();

                // Growl: high-pass at the sub frequency, squash, level.
                double gl = growlHpL.process(cSub, l);
                double gr = growlHpR.process(cSub, r);
                squash.process(gl, gr, base[squashAmt].value);
                gl *= base[growlLevel].value * kGrowlTrim;
                gr *= base[growlLevel].value * kGrowlTrim;

                // Mid/side: the side is high-passed at the crossover; the mid gets
                // the matching all-pass so both keep the same phase above it.
                double mid = 0.5 * (gl + gr), side = 0.5 * (gl - gr);
                mid = midAp.process(cXover, mid) * base[midGain].value;
                side = sideHp.process(cXover, side) * base[sideGain].value;

                // Sub lock.
                const double subOut = subLp.process(cSub, sub) * base[subLevel].value;

                double outL = (mid + side + subOut) * env;
                double outR = (mid - side + subOut) * env;
                softClipLinked(outL, outR);
                std::array<double, 2> tapValues { sub, mid };
                limiter.process(outL, outR, tapValues, base[ceilingGain].value, base[limitAmt].value);

                left[n] = static_cast<float>(outL);
                right[n] = static_cast<float>(outR);
                if (taps.master != nullptr) taps.master[n] = static_cast<float>(tapValues[0]);
                if (taps.growl != nullptr) taps.growl[n] = static_cast<float>(tapValues[1]);

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
            grDb = static_cast<float>(-gainToDb(limiter.gain()));
        }

        // Idle: silence, and the clocks keep running.
        if (n < numSamples) {
            const auto rest = static_cast<std::size_t>(numSamples - n);
            std::fill_n(left + n, rest, 0.0f);
            std::fill_n(right + n, rest, 0.0f);
            if (taps.master != nullptr) std::fill_n(taps.master + n, rest, 0.0f);
            if (taps.growl != nullptr) std::fill_n(taps.growl + n, rest, 0.0f);
            sinceAnchor += numSamples - n;
            sinceNoteStart += numSamples - n;
            grDb = 0.0f;
            idleLaneDisplay(p);
        }
    }

    // Lane phases and values for the interface while the DSP sleeps.
    void idleLaneDisplay(const Params& p) noexcept TF_NONBLOCKING {
        const double ppq = playing ? anchorPpq + static_cast<double>(sinceAnchor) * qps
                                   : static_cast<double>(sinceNoteStart) * qps;
        uiPpq = ppq;
        for (int i = 0; i < kLaneCount; ++i) {
            const double theta = lanePhase(ppq, p.choice(laneParam(i, LaneField::rate)), p[laneParam(i, LaneField::phase)]);
            const double v = p.on(laneParam(i, LaneField::on))
                                 ? laneShape(static_cast<LaneShape>(p.choice(laneParam(i, LaneField::shape))), theta, i) *
                                       p[laneParam(i, LaneField::depth)] * p[ParamId::motion]
                                 : 0.0;
            uiPhase[static_cast<std::size_t>(i)] = static_cast<float>(theta);
            uiValue[static_cast<std::size_t>(i)] = static_cast<float>(v);
        }
    }
};

Engine::Engine() : impl_(std::make_unique<Impl>()) { impl_->prepare(48000.0, 4); }
Engine::~Engine() = default;

void Engine::prepare(double sampleRate, int /*maxBlockSize*/, int oversampling) {
    impl_->prepare(sampleRate > 0.0 ? sampleRate : 48000.0, std::max(1, oversampling));
}
void Engine::reset() noexcept TF_NONBLOCKING { impl_->reset(); }
int Engine::latencySamples() const noexcept TF_NONBLOCKING { return impl_->latency(); }
int Engine::oversamplingFactor() const noexcept TF_NONBLOCKING { return impl_->factor; }

void Engine::noteOn(int midiNote) noexcept TF_NONBLOCKING { impl_->noteOn(midiNote); }
void Engine::noteOff(int midiNote) noexcept TF_NONBLOCKING { impl_->noteOff(midiNote); }
void Engine::allNotesOff() noexcept TF_NONBLOCKING { impl_->allNotesOff(); }
void Engine::pitchBend(float semitones) noexcept TF_NONBLOCKING {
    impl_->bendTarget = std::clamp(static_cast<double>(semitones), -kBendRange, kBendRange);
}

void Engine::process(const Params& params, const Transport& transport, float* left, float* right, int numSamples,
                     Taps taps) noexcept TF_NONBLOCKING {
    if (numSamples <= 0 || left == nullptr || right == nullptr) return;
    impl_->process(params, transport, left, right, numSamples, taps);
}

float Engine::currentHz() const noexcept TF_NONBLOCKING { return impl_->hz; }
bool Engine::gateOpen() const noexcept TF_NONBLOCKING { return !impl_->notes.empty(); }
float Engine::gainReductionDb() const noexcept TF_NONBLOCKING { return std::max(0.0f, impl_->grDb); }
double Engine::lanePpq() const noexcept TF_NONBLOCKING { return impl_->uiPpq; }
std::array<float, kLaneCount> Engine::lanePhases() const noexcept TF_NONBLOCKING { return impl_->uiPhase; }
std::array<float, kLaneCount> Engine::laneValues() const noexcept TF_NONBLOCKING { return impl_->uiValue; }

}
