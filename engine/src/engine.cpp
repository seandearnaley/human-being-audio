#include "tf/engine.h"

#include "control.h"
#include "crossover.h"
#include "decimator.h"
#include "denormals.h"
#include "feedback_delay.h"
#include "fold.h"
#include "harmonic.h"
#include "lanes.h"
#include "limiter.h"
#include "squash.h"
#include "tract.h"
#include "voice.h"

#include "tf/feel.h"
#include "tf/voice_pitch.h"

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
constexpr double kMaxFeedbackDelaySeconds = 2.0;  // 1/4 at 30 bpm; 2 x 4 cycles of an 8 Hz note
// The voice path (spec, "Harmonic core"), per oversampled sample:
//   master -> voices A, B (phase-modulated by the master and the feedback)
//   -> each voice through its own tract, fold (at 16x), Reach and Weight
//   -> Width mixes them -> decimation. The sub is the master rotated by Fit.
// The feedback loop. It hears the voices locked to the master: the same
// oscillators at exactly f0 x ratio, without detune or Beat lock offset, with
// the same phase modulation and feedback. Their (A + B) / 2 is low-passed by
// Reach with the voices' own 24 dB/oct filter, delayed, DC-blocked, saturated
// and added to both voices' phases. So the feedback is periodic in f0 and each
// voice stays a carrier at its own frequency modulated only by things locked
// to the master; fed from the detuned voices themselves, the loop carried their
// beat into both phases and the fold multiplied it (default patch at F2, lanes
// off: -16.7 dB off-harmonic; -31.6 locked). Two poles of Reach on the loop
// left the hot patch chaotic (-1.7 dB without detune; -75 dB with four). In
// cycles of the note the loop reads n and 2n cycles back and averages them: a
// comb that passes the harmonics and has its zeros on the half-harmonics, so
// the period-doubling a one-cycle loop falls into above about 1.5 rad has
// nowhere to go (at 1 cyc, lanes off, F1: Chrome Rasp -3 dB off-harmonic with
// one tap, -25 with two; Shear Pin -2 and -26). The beat divisions keep the
// single tap.
constexpr double kFbSecondTap = 0.5;
constexpr double kBendRange = 2.0;
constexpr double kMinBpm = 20.0;
constexpr double kMaxBpm = 999.0;
constexpr double kPpqTolerance = 1e-5;  // quarters; larger jumps re-anchor the clock
// Gain staging. The sub is a sine whose peak after its low-pass is known
// exactly every control period, so the growl gets its own static ceiling
// G = ceiling x margin - sub peak (lowered further only while the sub's own
// transients rise above that peak). The growl path, in order:
//   Squash -> growlLevel -> mid/side
//   -> at 2x, kClipPasses times: linked soft clipper towards G, then LR4
//      high-pass at subFreq on both channels (the clipper regenerates f0 and
//      2 f0 from a harmonic growl; the high-pass keeps them out of the sub
//      band but lifts the clipped peaks, which the next pass trims)
//   -> look-ahead true-peak limiter on the growl alone, threshold G, slow
//      release, catching what the clipper and high-pass overshoot.
// The sub and the master tap take the same 2x trip and the limiter's delay
// and are never processed, so nothing the growl does modulates the sub band;
// a second limiter touches the sub only if the sub alone exceeds the ceiling.
// With limit on, sum peak <= G + sub peak <= ceiling. The final linked soft
// clip at 0 dBFS is the safety net (the only bound with limit off). Latency is
// unchanged. kGrowlGain sets where the clipper starts to bite. It was set in
// 0.1 to bite lightly on the default patch; since 0.2 Reach (16 x f0 by
// default) takes most of a dense growl's energy at F1, the default patch's
// growl sits about 10 dB lower there and the clipper idles at F1 (it still
// bites lightly at F2). Left as it was: the presets set Reach and the level.
constexpr double kGrowlGain = 0.85;
constexpr double kGrowlCeilingMargin = 0.96605;  // -0.3 dB, for inter-sample peaks
constexpr double kGrowlCeilingFloor = 1e-3;      // -60 dBFS: a sub at or above the ceiling leaves the growl this
constexpr double kGrowlLimiterReleaseSeconds = 0.12;  // > 3 periods of E1 (41 Hz): no ripple at the note rate
constexpr double kSubHoldSeconds = 0.06;              // > 2 periods of E1
constexpr double kSubReleaseSeconds = 0.25;
// The growl clipper's oversampling (the sub and master tap take the same
// trip). 4x measured the same (-51.8 dB inharmonic, tube fold, growl 1) and
// costs a sample of latency.
using ClipTrip = BaseRate2x;
// Clip, high-pass at subFreq, and again: the high-pass removes the f0 and 2 f0
// the clip regenerates but lifts the clipped peaks; the second clip trims that
// lift, regenerating far less, and its high-pass removes the rest. One pass left
// the limiter 3.5 dB of average work on the default patch.
constexpr std::size_t kClipPasses = 4;

int effectiveOversampling(double sampleRate, int requested) {
    int factor = 1;
    while (factor < requested && factor < kMaxOversampling) factor *= 2;
    const int divisor = sampleRate >= 176400.0 ? 4 : sampleRate >= 88200.0 ? 2 : 1;
    const int minimum = requested >= 2 ? 2 : 1;
    return std::max(minimum, factor / divisor);
}

// Oversampled-domain control values, ramped per oversampled sample.
enum Os : std::size_t {
    incMaster, incA, incB, offA, offB, pmA, pmB, fbGain, fbDelay, width, tractAmt, driveGain, asymOffset, warpAmt,
    incLockA, incLockB,  // the voices at exactly f0 x ratio: what the feedback loop hears
    fbSecond,   // weight of the feedback tap at twice the delay (cycles only)
    subRot,     // the sub's phase against the master, cycles, unwrapped
    reachG,     // Reach low-pass, pre-warped cutoff
    weightG,    // Weight one-poles, TPT gain G = g / (1 + g)
    weightDry, weightOne, weightTwo,  // Weight blend, level compensation included
    osCount
};
// Base-rate control values, ramped per base sample.
enum Base : std::size_t { subLevel, growlLevel, midGain, sideGain, squashAmt, ceilingGain, limitAmt, growlCeiling, baseCount };

// |H| of the engine's LR4 low-pass (two TPT Butterworth sections) at `hz`:
// 1 / (1 + W^4) with W the pre-warped frequency ratio. Zero at and above
// Nyquist, where the decimator has already removed the sub.
double lr4LowpassMagnitude(double hz, double cutoff, double sampleRate) noexcept TF_NONBLOCKING {
    if (hz >= 0.499 * sampleRate) return 0.0;
    const double w = std::tan(kPi * std::max(0.0, hz) / sampleRate) / SvfCoefs::prewarp(cutoff, sampleRate);
    const double w2 = w * w;
    return 1.0 / (1.0 + w2 * w2);
}
// Smoothed raw parameters.
enum Sm : std::size_t {
    coupling, beat, aDetune, bDetune, aRatio, bRatio, aPhase, bPhase, aPm, bPm, fbAmount, width_, tract_, drive_, asym_,
    warp_, midGainDb, sideGainDb, xoverLog, subFreqLog, subLevel_, growlLevel_, squash_, ceilingDb, limitOn, bend,
    reachLog, weight_, fitCycles,  // fitCycles is unwrapped: it moves the short way round
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

    // Oversampled domain. Voice A runs through channel A's tract, fold, Reach
    // and Weight, voice B through channel B's; Width mixes them after that.
    Decimator decSub, decL, decR;
    LocalOversampler localSub, localA, localB;  // around the fold; the sub gets the same delay
    // Around the growl clipper; the sub and the master tap take the same trip.
    ClipTrip clip2xL, clip2xR, clip2xSub, clip2xMaster;
    std::array<float, kMaxOversampling> bufSub {}, bufL {}, bufR {}, bufLocal {};
    FeedbackDelay feedback;
    DcBlocker feedbackDc;
    ReachFilter feedbackLp;  // Reach on the loop, the same 24 dB/oct as the voices
    TractCoefs tractCoefs;
    TractChannel tractA, tractB;
    Folder foldA, foldB;
    ReachFilter reachA, reachB;
    WeightFilter weightA, weightB;
    double phaseMaster = 0.0, phaseA = 0.0, phaseB = 0.0;
    double phaseLockA = 0.0, phaseLockB = 0.0;
    FoldType foldFrom = FoldType::fold, foldTo = FoldType::fold;
    double foldMix = 1.0, foldMixStep = 1.0;
    std::array<Ramp, osCount> os {};

    // Base rate.
    Lr4Highpass growlHpL, growlHpR, sideHp;
    // After each pass of the growl clipper, at the clipper's 2x rate, per channel.
    std::array<std::array<Lr4Highpass, 2>, kClipPasses> postClipHp {};
    Lr4Allpass midAp;
    Lr4Lowpass subLp;
    SvfCoefs cSub, cXover, cSubClip;
    Squash squash;
    Limiter limiter;     // on the growl
    Limiter subLimiter;  // on the sub, idle unless the sub alone exceeds the ceiling
    MovingMinimum subPeakHold;  // of -|sub|: a moving maximum
    double subEnvelope = 0.0, subEnvelopeRelease = 1.0;
    std::array<Ramp, baseCount> base {};

    // Control.
    std::array<Smoother, smCount> smooth {};
    std::array<Smoother, kLaneCount> laneSmooth {};
    std::array<Smoother, kFormantCount * 3 + 1> formantSmooth {};
    Smoother delaySmooth;  // feedback delay in samples, beat divisions
    Smoother cycleSmooth;  // feedback delay in cycles of the note
    bool delayInCycles = false;
    double fbDelayNow = 2.0;
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
    bool beatLockOn = false;

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
    float clipDb = 0.0f, clipMeanDb = 0.0f;  // growl clipper over the last block
    double clipMaxDb = 0.0, clipSumDb = 0.0;
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
        localA.prepare(factor);
        localB.prepare(factor);
        clip2xL.prepare();
        clip2xR.prepare();
        clip2xSub.prepare();
        clip2xMaster.prepare();
        decimatorDelayExact = measureDecimatorDelay(factor) + measureClipTripDelay();
        decimatorDelay = static_cast<int>(std::lround(decimatorDelayExact));

        feedback.prepare(static_cast<int>(std::ceil(kMaxFeedbackDelaySeconds * fo)));
        feedbackDc.prepare(5.0, fo);
        vowelComp = computeVowelCompDb(fo);
        foldA.prepare(fo * localA.factor());
        foldB.prepare(fo * localB.factor());
        squash.prepare(fs);
        limiter.prepare(fs, kGrowlLimiterReleaseSeconds);
        subLimiter.prepare(fs);
        subPeakHold.prepare(static_cast<int>(std::lround(kSubHoldSeconds * fs)));
        subEnvelopeRelease = onePoleCoef(kSubReleaseSeconds, 1.0 / fs);
        gate.prepare(fs);
        gateDelay.prepare(decimatorDelay);
        reset();
    }

    // Group delay at DC of the 2x trip around the growl clipper, base samples.
    static double measureClipTripDelay() {
        ClipTrip t;
        t.prepare();
        double moment = 0.0, sum = 0.0;
        for (int n = 0; n < 4096; ++n) {
            float buf[ClipTrip::kFactor];
            t.up(n == 0 ? 1.0f : 0.0f, buf);
            const double y = t.down(buf);
            moment += n * y;
            sum += y;
        }
        return sum != 0.0 ? moment / sum : 0.0;
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
        localA.reset();
        localB.reset();
        clip2xL.reset();
        clip2xR.reset();
        clip2xSub.reset();
        clip2xMaster.reset();
        feedback.clear();
        feedbackDc.reset();
        feedbackLp.reset();
        tractA.reset();
        tractB.reset();
        foldA.reset();
        foldB.reset();
        reachA.reset();
        reachB.reset();
        weightA.reset();
        weightB.reset();
        growlHpL.reset();
        growlHpR.reset();
        sideHp.reset();
        for (auto& pass : postClipHp)
            for (auto& f : pass) f.reset();
        midAp.reset();
        subLp.reset();
        squash.reset();
        limiter.reset();
        subLimiter.reset();
        subPeakHold.reset();
        subEnvelope = 0.0;
        gateDelay.clear();
        foldMix = 1.0;
    }

    void reset() noexcept TF_NONBLOCKING {
        resetDsp();
        gate.reset();
        notes.clear();
        pendingNote = -1;
        phaseMaster = phaseA = phaseB = phaseLockA = phaseLockB = 0.0;
        for (auto& s : smooth) s.reset();
        for (auto& s : laneSmooth) s.reset();
        for (auto& s : formantSmooth) s.reset();
        delaySmooth.reset();
        cycleSmooth.reset();
        delayInCycles = false;
        fbDelayNow = 2.0;
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
        if (phaseResetOn) phaseMaster = phaseA = phaseB = phaseLockA = phaseLockB = 0.0;
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

    // Cycles per oversampled sample. The voices sit at f0 x ratio, detuned in
    // cents, or with Beat lock offset by a constant number of Hz, so every
    // partial of a voice (the modulator stays at exactly f0) moves by the same
    // amount and the whole spectrum beats at one rate.
    void pitchIncrements(double& master, double& a, double& b) const noexcept TF_NONBLOCKING {
        const double f0 = noteToHz(glide.current() + sm(bend));
        const double beatMod = std::clamp(sm(beat) + 0.5 * laneSmooth[3].value(), 0.0, 1.0);
        master = f0 / fo;
        if (beatLockOn) {
            const double delta = beatLockHz(anchorBpmOrCurrent(), beatMod);
            const auto share = beatLockShares(sm(aDetune), sm(bDetune));
            a = (f0 * sm(aRatio) + delta * share.a) / fo;
            b = (f0 * sm(bRatio) + delta * share.b) / fo;
            return;
        }
        const auto detA = feel::detuneCents(static_cast<float>(sm(aDetune)), static_cast<float>(beatMod));
        const auto detB = feel::detuneCents(static_cast<float>(sm(bDetune)), static_cast<float>(beatMod));
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
        os[incLockA].snap(m * sm(aRatio));
        os[incLockB].snap(m * sm(bRatio));
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
        step(reachLog, std::log(std::clamp(p[ParamId::reach], spec(ParamId::reach).min, spec(ParamId::reach).max)));
        step(weight_, p[ParamId::weight]);
        // Fit is a phase: smooth towards the nearest copy of the target, so
        // 350 -> 10 degrees moves 20 degrees, not 340.
        {
            const double target = static_cast<double>(p[ParamId::fit]) / 360.0;
            const double current = smooth[fitCycles].value();
            step(fitCycles, snap ? target : current + wrapHalf(target - current));
        }

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
        t[incLockA] = incM * sm(aRatio);
        t[incLockB] = incM * sm(bRatio);
        // Phase offsets in cycles, not wrapped (the oscillator wraps), so a ramp
        // never sweeps the long way round.
        t[offA] = sm(aPhase) / 360.0 + 0.25 * lanePhaseMod;
        t[offB] = sm(bPhase) / 360.0 - 0.25 * lanePhaseMod;
        t[pmA] = feel::pmIndex(static_cast<float>(sm(aPm)), static_cast<float>(sm(coupling)));
        t[pmB] = feel::pmIndex(static_cast<float>(sm(bPm)), static_cast<float>(sm(coupling)));
        t[fbGain] = feel::feedbackRadians(static_cast<float>(sm(fbAmount)));
        const double bpm = anchorBpmOrCurrent();
        // In cycles of the note (incM is cycles per oversampled sample) or in
        // beats. In cycles the count is smoothed and the delay follows the
        // pitch exactly, so the comb stays on the harmonics through glides and
        // bends (a smoothed delay lags a glide and the loop bursts while it
        // catches up). In beats the delay itself is smoothed. Switching between
        // the two starts the new smoother from the current delay.
        const auto fbChoice = static_cast<std::size_t>(
            std::clamp(p.choice(ParamId::fbTime), 0, static_cast<int>(kFbTimeLengths.size()) - 1));
        const bool inCycles = fbChoice < static_cast<std::size_t>(kFbCycleChoices);
        const double delayCoef = snap ? 1.0 : delayTickCoef;
        const double safeInc = std::max(incM, 1e-9);
        double delay = 0.0;
        if (inCycles) {
            if (!snap && !delayInCycles) cycleSmooth.snap(fbDelayNow * safeInc);
            delay = cycleSmooth.step(kFbTimeLengths[fbChoice], delayCoef) / safeInc;
        } else {
            if (!snap && delayInCycles) delaySmooth.snap(fbDelayNow);
            delay = delaySmooth.step(kFbTimeLengths[fbChoice] * 60.0 / bpm * fo, delayCoef);
        }
        delayInCycles = inCycles;
        // (The second tap, at twice the delay, is clamped by the line itself;
        // in cycles it stays inside: 2 x 4 cycles of note 0 is under a second.)
        fbDelayNow = std::clamp(delay, 2.0, feedback.maxDelay());
        t[fbDelay] = fbDelayNow;
        t[fbSecond] = inCycles ? kFbSecondTap : 0.0;
        t[width] = std::clamp(sm(width_), 0.0, 1.0);
        t[tractAmt] = std::clamp(sm(tract_), 0.0, 1.0);
        const double driveMod = std::clamp(sm(drive_) + 0.5 * laneDrive, 0.0, 1.0);
        t[driveGain] = feel::driveGain(static_cast<float>(driveMod));
        t[asymOffset] = 0.5 * sm(asym_);
        t[warpAmt] = std::clamp(sm(warp_), 0.0, 1.0);

        // Fit: the sub runs ahead of the master by the phase its LR4 low-pass
        // takes away at f0, plus fit, so at fit 0 the sub at the output is in
        // phase with the master. The target is taken next to the current value
        // (whole cycles do not matter) so the ramp never sweeps the long way.
        const double f0 = incM * fo;
        const double rotation = lr4LowpassLagCycles(f0, std::exp(sm(subFreqLog)), fs) + sm(fitCycles);
        t[subRot] = snap ? rotation : os[subRot].value + wrapHalf(rotation - os[subRot].value);

        // Reach: 24 dB/oct at reach x f0, below 0.45 of the oversampled rate.
        t[reachG] = SvfCoefs::prewarp(std::min(std::exp(sm(reachLog)) * f0, kMaxFilterRatio * fo), fo);
        // Weight: dry -> one one-pole -> two at 3 f0, level-matched at 2 f0 and 3 f0.
        const auto blend = weightBlend(feel::weightSlopeDb(static_cast<float>(std::clamp(sm(weight_), 0.0, 1.0))), f0, fo);
        t[weightG] = blend.gain;
        t[weightDry] = blend.dry;
        t[weightOne] = blend.one;
        t[weightTwo] = blend.two;
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
        cSubClip = butterworth(std::exp(sm(subFreqLog)), fs * ClipTrip::kFactor);
        cXover = butterworth(std::exp(sm(xoverLog)), fs);
        std::array<double, baseCount> b {};
        b[subLevel] = std::clamp(sm(subLevel_), 0.0, 1.0);
        b[growlLevel] = kGrowlGain * std::clamp(sm(growlLevel_), 0.0, 1.0);
        b[midGain] = dbToGain(sm(midGainDb));
        b[sideGain] = dbToGain(sm(sideGainDb));
        b[squashAmt] = std::clamp(sm(squash_), 0.0, 1.0);
        b[ceilingGain] = dbToGain(std::min(0.0, sm(ceilingDb)));
        b[limitAmt] = std::clamp(sm(limitOn), 0.0, 1.0);
        // The sub is a sine, so its peak after the LR4 low-pass is exact:
        // level times the filter's magnitude at the current pitch. The growl
        // gets whatever is left under the ceiling.
        const double subPeak = b[subLevel] * lr4LowpassMagnitude(incM * fo, std::exp(sm(subFreqLog)), fs);
        b[growlCeiling] = std::max(b[ceilingGain] * kGrowlCeilingMargin - subPeak, kGrowlCeilingFloor);
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
        phaseLockA += os[incLockA].value;
        phaseLockB += os[incLockB].value;
        phaseMaster -= std::floor(phaseMaster);
        phaseA -= std::floor(phaseA);
        phaseB -= std::floor(phaseB);
        phaseLockA -= std::floor(phaseLockA);
        phaseLockB -= std::floor(phaseLockB);

        // The master modulates the voices; the sub is the same oscillator
        // rotated by Fit (and the sub low-pass's lag).
        const float master = std::sin(static_cast<float>(kTwoPi * phaseMaster));
        const float subSine = std::sin(static_cast<float>(kTwoPi * frac(phaseMaster + os[subRot].value)));
        // Feedback: one tap, or in cycles the mean of n and 2n cycles back.
        const double second = os[fbSecond].value;
        double fbRaw = feedback.read(os[fbDelay].value);
        if (second > 0.0) fbRaw += second * (feedback.read(2.0 * os[fbDelay].value) - fbRaw);
        const double fb = os[fbGain].value * std::tanh(feedbackDc.process(fbRaw));
        const double a = std::sin(static_cast<float>(kTwoPi * frac(phaseA + os[offA].value) + os[pmA].value * master + fb));
        const double b = std::sin(static_cast<float>(kTwoPi * frac(phaseB + os[offB].value) + os[pmB].value * master + fb));
        // The loop hears the voices as they would be without detune: periodic
        // in f0, so the feedback is too, and each voice stays a carrier at its
        // own frequency phase-modulated only by things locked to the master.
        const double lockA = std::sin(static_cast<float>(kTwoPi * frac(phaseLockA + os[offA].value) + os[pmA].value * master + fb));
        const double lockB = std::sin(static_cast<float>(kTwoPi * frac(phaseLockB + os[offB].value) + os[pmB].value * master + fb));

        const ReachCoefs reach = ReachCoefs::make(os[reachG].value);
        feedback.write(static_cast<float>(feedbackLp.process(reach, 0.5 * (lockA + lockB))));

        // Each voice through its own tract and fold: detuned voices never meet
        // in one nonlinearity. The fold runs at the local (16x) rate; the sub
        // sine takes the same trip so both paths keep the same delay.
        const double tractAmount = os[tractAmt].value;
        const double va = tractA.process(tractCoefs, tractAmount, a);
        const double vb = tractB.process(tractCoefs, tractAmount, b);
        const double g = os[driveGain].value, off = os[asymOffset].value, warp = os[warpAmt].value;
        const int local = localA.factor();
        float* buf = bufLocal.data();
        localA.up(static_cast<float>(va), buf);
        for (int k = 0; k < local; ++k) buf[k] = static_cast<float>(foldA.process(buf[k], g, off, warp, foldFrom, foldTo, foldMix));
        double ya = localA.down(buf);
        localB.up(static_cast<float>(vb), buf);
        for (int k = 0; k < local; ++k) buf[k] = static_cast<float>(foldB.process(buf[k], g, off, warp, foldFrom, foldTo, foldMix));
        double yb = localB.down(buf);
        localSub.up(subSine, buf);
        const float sub = localSub.down(buf);

        // Reach, then Weight, per voice.
        const WeightMix mix { os[weightG].value, os[weightDry].value, os[weightOne].value, os[weightTwo].value };
        ya = weightA.process(mix, reachA.process(reach, ya));
        yb = weightB.process(mix, reachB.process(reach, yb));

        // Width: L = (A + (1 - w) B) / (2 - w) and the mirror; exactly mono at 0.
        const double w = os[width].value;
        const double l = (ya + (1.0 - w) * yb) / (2.0 - w);
        const double r = (yb + (1.0 - w) * ya) / (2.0 - w);

        const auto idx = static_cast<std::size_t>(j);
        bufSub[idx] = sub;
        bufL[idx] = static_cast<float>(l);
        bufR[idx] = static_cast<float>(r);
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
        clipMaxDb = clipSumDb = 0.0;
        glideMs = p[ParamId::glide];
        phaseResetOn = p.on(ParamId::phaseReset);
        beatLockOn = p.on(ParamId::beatLock);
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
                gl *= base[growlLevel].value;
                gr *= base[growlLevel].value;

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
                std::array<float, ClipTrip::kFactor> upSub {}, upMaster {};
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
                std::array<float, ClipTrip::kFactor> upL {}, upR {};
                clip2xL.up(static_cast<float>(mid + side), upL.data());
                clip2xR.up(static_cast<float>(mid - side), upR.data());
                double clipDbNow = 0.0;
                for (std::size_t k = 0; k < ClipTrip::kFactor; ++k) {
                    double a = upL[k], c = upR[k];
                    for (std::size_t pass = 0; pass < kClipPasses; ++pass) {
                        const double db = -gainToDb(growlClip(a, c, ceilingG));
                        if (pass == 0) clipDbNow = std::max(clipDbNow, db);
                        a = postClipHp[pass][0].process(cSubClip, a);
                        c = postClipHp[pass][1].process(cSubClip, c);
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

                // Gate, then two look-ahead limiters with the same delay. The
                // growl's catches what the clipper and the high-passes after it
                // overshoot, against the same static ceiling G, with a release
                // long enough not to ripple at the note rate. It never sees the
                // sub. The sub's only acts if the sub alone exceeds the ceiling.
                const double subGated = subOut * env;
                const double ceilingLin = base[ceilingGain].value;
                double outL = gl * env, outR = gr * env;
                std::array<double, 1> growlDelayed { 0.5 * (gl + gr) };
                limiter.process(outL, outR, growlDelayed, ceilingG, base[limitAmt].value);
                double subL = subGated, subR = subGated;
                std::array<double, 1> subDelayed { masterTap };
                subLimiter.process(subL, subR, subDelayed, ceilingLin, base[limitAmt].value);
                outL += subL;
                outR += subR;
                outputClip(outL, outR, ceilingLin);

                left[n] = static_cast<float>(outL);
                right[n] = static_cast<float>(outR);
                if (taps.master != nullptr) taps.master[n] = static_cast<float>(subDelayed[0]);
                if (taps.growl != nullptr) taps.growl[n] = static_cast<float>(growlDelayed[0]);

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
            grDb = static_cast<float>(-gainToDb(std::min(limiter.gain(), subLimiter.gain())));
        }
        clipDb = static_cast<float>(clipMaxDb);
        clipMeanDb = static_cast<float>(clipSumDb / numSamples);

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
float Engine::growlClipDb() const noexcept TF_NONBLOCKING { return impl_->clipDb; }
float Engine::growlClipMeanDb() const noexcept TF_NONBLOCKING { return impl_->clipMeanDb; }
std::array<float, kLaneCount> Engine::lanePhases() const noexcept TF_NONBLOCKING { return impl_->uiPhase; }
std::array<float, kLaneCount> Engine::laneValues() const noexcept TF_NONBLOCKING { return impl_->uiValue; }

}
