#include "tf/engine.h"

#include "control.h"
#include "crossover.h"
#include "decimator.h"
#include "denormals.h"
#include "feedback_delay.h"
#include "fold.h"
#include "harmonic.h"
#include "limiter.h"
#include "squash.h"
#include "tract.h"
#include "voice.h"

#include "tf/feel.h"
#include "tf/lanes.h"
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
// The second feedback tap reads 2 x 4 cycles back: under 1.2 s for note 0 bent
// down two semitones.
constexpr double kMaxFeedbackDelaySeconds = 1.5;
// The voice path (spec, "Harmonic core"), per oversampled sample:
//   master -> voices A, B (phase-modulated by the master and the feedback)
//   -> each voice through its own tract, fold (at 16x), Reach, Weight, Sculpt
//   -> Width mixes them -> decimation. The sub is the master rotated by Fit.
// The feedback loop is described where it runs, in oversampledSample().
constexpr double kBendRange = 2.0;
constexpr double kMinBpm = 20.0;
constexpr double kMaxBpm = 999.0;
constexpr double kPpqTolerance = 1e-5;  // quarters; larger jumps re-anchor the clock
// Gain staging. The sub is a sine whose peak after its low-pass is known
// exactly every control period, so the growl gets its own static ceiling
// G = ceiling x margin - sub peak (lowered further only while the sub's own
// transients rise above that peak). The growl path, in order:
//   LR4 high-pass at the split -> Squash -> x kGrowlGain -> mid/side
//   -> at 2x, kClipPasses times: linked soft clipper towards G, then LR4
//      high-pass at the split on both channels (the clipper regenerates f0 and
//      2 f0 from a harmonic growl; the high-pass keeps them out of the sub
//      band but lifts the clipped peaks, which the next pass trims)
//   -> look-ahead true-peak limiter on the growl alone, threshold G, slow
//      release, catching what the clipper and high-pass overshoot
//   -> x growlLevel (linear), then the sub is added.
// So the clipper is driven by the patch and kGrowlGain alone, and growlLevel
// is a level: the growl reaches at most G x growlLevel <= G.
// The sub and the master tap take the same 2x trip and the limiter's delay
// and are never processed, so nothing the growl does modulates the sub band;
// a second limiter touches the sub only if the sub alone exceeds the ceiling.
// With limit on, sum peak <= G + sub peak <= ceiling. The final linked soft
// clip at 0 dBFS is the safety net (the only bound with limit off). Latency is
// unchanged. kGrowlGain sets how hard the clipper is driven: lightly on the
// default patch, hard on a dense one. At F1, lanes off: the default patch's
// clipper works 2.4 dB mean, the hot one's 2.1-2.5; the default's growl - sub
// is -9.4 dB at growlLevel 0.76 and -7.0 at 1. (More gain does not raise it:
// at 4 and 6 the clipped, limited growl at level 1 still reads -7.4 re sub,
// because its peaks are held to G whatever drives them.)
constexpr double kGrowlGain = 2.4;
constexpr double kGrowlCeilingMargin = 0.96605;  // -0.3 dB, for inter-sample peaks
constexpr double kGrowlCeilingFloor = 1e-3;      // -60 dBFS: a sub at or above the ceiling leaves the growl this
constexpr double kGrowlLimiterReleaseSeconds = 0.12;  // > 3 periods of E1 (41 Hz): no ripple at the note rate
constexpr double kSubHoldSeconds = 0.06;              // > 2 periods of E1
constexpr double kSubReleaseSeconds = 0.25;
// The growl clipper's oversampling (the sub and master tap take the same
// trip). 4x measured the same (-51.8 dB inharmonic, tube fold, growl 1) and
// costs a sample of latency.
using ClipTrip = BaseRate2x;
// Two 2x-oversampled growl clipper passes each followed by a 4th-order
// Butterworth high-pass at 1.05x splitHz (combined LR8: -48 dB/oct, -64 dB at f0):
// restores +13 dB of lower-growl punch at 3*f0 while trimming filter overshoot cleanly
// before the look-ahead limiter.
constexpr std::size_t kClipPasses = 2;
constexpr double kPostClipSplitRatio = 1.10;

int effectiveOversampling(double sampleRate, int requested) {
    int factor = 1;
    while (factor < requested && factor < kMaxOversampling) factor *= 2;
    const int divisor = sampleRate >= 176400.0 ? 4 : sampleRate >= 88200.0 ? 2 : 1;
    const int minimum = requested >= 2 ? 2 : 1;
    return std::max(minimum, factor / divisor);
}

// Oversampled-domain control values, ramped per oversampled sample.
enum Os : std::size_t {
    incMaster,
    incA,
    incB,
    offA,
    offB,
    pmA,
    pmB,
    fbGain,
    fbDelay,
    fbPeriod,
    width,
    tractAmt,
    driveGain,
    asymOffset,
    warpAmt,
    incLockA,
    incLockB,   // the voices at exactly f0 x ratio: what the feedback loop hears
    subRot,     // the sub's phase against the master, cycles, unwrapped
    subWarmth_, // phase-locked 2nd/3rd sub saturation amount (0..1)
    oscShape_,  // carrier harmonic richness morph (0 = pure sine .. 1 = rich phase-warped core)
    tractPost_, // 0 = Tract pre-Fold, 1 = Tract post-Fold
    reachG,     // Reach low-pass, pre-warped cutoff
    loopG,      // the feedback loop's low-pass, pre-warped cutoff
    weightG,    // Weight one-poles, TPT gain G = g / (1 + g)
    weightDry,
    weightOne,
    weightTwo, // Weight blend, level compensation included
    osCount
};
// Base-rate control values, ramped per base sample.
enum Base : std::size_t {
    subLevel,
    growlLevel,
    midGain,
    sideGain,
    squashAmt,
    ceilingGain,
    limitAmt,
    growlCeiling,
    ampVel,
    baseCount
};

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
    coupling,
    beat,
    oscShapeSm,
    punchAmt_,
    aDetune,
    bDetune,
    aRatio,
    bRatio,
    aPhase,
    bPhase,
    aPm,
    bPm,
    fbAmount,
    width_,
    tract_,
    tractShift_,
    tractQ_,
    tractPos_,
    drive_,
    asym_,
    warp_,
    midGainDb,
    sideGainDb,
    xoverLog,
    velSens_,
    subSplitLog,
    subWarmthSm,
    subLevel_,
    growlLevel_,
    squash_,
    ceilingDb,
    limitOn,
    bend,
    reachLog,
    weight_,
    fitCycles, // fitCycles is unwrapped: it moves the short way round
    sculptLog,
    spreadLog,
    sculptRes_,
    sculptDepth_,
    modWheel_,
    timbreOffset_,
    aftertouch_,
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
    ReachFilter feedbackLp;  // the loop's 24 dB/oct low-pass at min(reach, 8) x f0
    TractCoefs tractCoefs;
    TractChannel tractA, tractB;
    TractChannel tractPostA, tractPostB;
    Folder foldA, foldB;
    float subFoldDelay = 0.0f;
    ReachFilter reachA, reachB;
    WeightFilter weightA, weightB;
    SculptCoefs sculptCoefsA, sculptCoefsB;
    std::array<SculptChannel, 2> sculptA{}, sculptB{};
    int sculptSlot = 0;
    SculptMode sculptModeCur = SculptMode::bell;
    SculptMode sculptModePrev = SculptMode::bell;
    double sculptXfadeMix = 1.0;
    double phaseMaster = 0.0, phaseA = 0.0, phaseB = 0.0;
    double phaseLockA = 0.0, phaseLockB = 0.0;
    FoldType foldFrom = FoldType::fold, foldTo = FoldType::fold;
    double foldMix = 1.0, foldMixStep = 1.0;
    std::array<Ramp, osCount> os {};

    // Base rate.
    Lr4Highpass growlHpL, growlHpR;
    Lr4Highpass sideHp, sideHpPost;
    // After each pass of the growl clipper, at the clipper's 2x rate, per channel.
    std::array<std::array<Bw4Highpass, 2>, kClipPasses> postClipHp{};
    Lr4Allpass midAp, midApPost;
    Lr4Lowpass subLp;
    SvfCoefs cSub, cXover, cXoverClip;
    Bw4Coefs cSubClip;
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
    Smoother cycleSmooth;  // feedback delay in cycles of the note
    std::array<Smoother, kSculptMaxBands> sculptGainSmooth {};  // dB, per band
    int countdown = 0;
    bool snapNext = true;

    // Notes and gate.
    NoteStack notes;
    NoteStack physicalNotes;
    bool sustainDown = false;
    Glide glide;
    Gate gate;
    PitchPunch punch;
    DelayLine<double> gateDelay;
    int pendingNote = -1;
    double pendingVelocity = 1.0;
    double noteVelocity = 1.0;
    double bendTarget = 0.0;
    double bendRange = kBendRange;
    double modWheelTarget = 0.0;
    double timbreOffsetTarget = 0.0;
    double aftertouchTarget = 0.0;
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
    // The lanes' drawn curves (setLaneCurves), and each target's movement: the
    // sum of the smoothed sends of every lane aimed at it, in -1..1 per lane.
    LaneCurves laneCurves = defaultLaneCurves();
    std::array<double, kLaneTargetCount> laneMove {};
    double uiPpq = 0.0;     // the lane clock, host position or free-running
    double uiNotePpq = 0.0; // note-relative clock for Retrig / One-Shot lanes
    EffectiveState effective{};

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
    static double measureDecimatorDelay(int factor, int foldTargetRate) {
        Decimator d;
        d.prepare(factor);
        LocalOversampler local;
        local.prepare(factor, foldTargetRate);
        std::array<float, kMaxOversampling> buf {}, scratch {};
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
            for (auto& f : pass) f.reset();
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

    void reset() noexcept TF_NONBLOCKING {
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
        for (auto& s : smooth) s.reset();
        for (auto& s : laneSmooth) s.reset();
        laneMove.fill(0.0);
        for (auto& s : formantSmooth) s.reset();
        cycleSmooth.reset();
        for (auto& g : sculptGainSmooth) g.reset();
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
        sinceAnchor = sinceNoteStart = 0;
        grDb = 0.0f;
        effective = EffectiveState{};
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
    void startNote(int note, double velocity) noexcept TF_NONBLOCKING {
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

    void beginNote(int note, double velocity) noexcept TF_NONBLOCKING {
        if (phaseResetOn) phaseMaster = phaseA = phaseB = phaseLockA = phaseLockB = 0.0;
        noteVelocity = std::clamp(velocity, 0.01, 1.0);
        punch.trigger(0.4 + 0.6 * noteVelocity);
        glide.jump(note);
        sinceNoteStart = 0;
        if (!snapNext) retargetPitchNow();
        gate.open();
    }

    void noteOn(int note, float velocity = 1.0f) noexcept TF_NONBLOCKING {
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

    void noteOff(int note) noexcept TF_NONBLOCKING {
        note = std::clamp(note, 0, 127);
        physicalNotes.remove(note);
        if (sustainDown)
            return;
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

    void sustain(bool down) noexcept TF_NONBLOCKING {
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

    void allNotesOff() noexcept TF_NONBLOCKING {
        notes.clear();
        physicalNotes.clear();
        sustainDown = false;
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
    void retargetPitchNow() noexcept TF_NONBLOCKING {
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
    double lanePpqAhead(long long samplesAhead) const noexcept TF_NONBLOCKING {
        return playing ? anchorPpq + static_cast<double>(sinceAnchor + samplesAhead) * qps
                       : static_cast<double>(sinceNoteStart + samplesAhead) * qps;
    }

    // One control period of the lanes, at host position `ppq`: each lane's send
    // (tf::laneSend, the function the interface draws with) through its
    // smoother, then summed by target. The smoothing covers any step in a send
    // (a curve swapped by setLaneCurves, a retarget, a lane switched on), and
    // the same code runs while the DSP sleeps, so the interface sees the same
    // values either way.
    double notePpqAhead(long long samplesAhead) const noexcept TF_NONBLOCKING {
        return static_cast<double>(sinceNoteStart + samplesAhead) * qps;
    }

    void laneTick(const Params& p, double ppq, double nPpq, double c) noexcept TF_NONBLOCKING {
        uiPpq = ppq;
        uiNotePpq = nPpq;
        laneMove.fill(0.0);
        for (int i = 0; i < kLaneCount; ++i) {
            const auto lane = static_cast<std::size_t>(i);
            const double v = laneSmooth[lane].step(laneSend(p, laneCurves, i, ppq, nPpq), c);
            uiPhase[lane] = static_cast<float>(lanePhaseFor(p, i, ppq, nPpq));
            uiValue[lane] = static_cast<float>(v);
            laneMove[static_cast<std::size_t>(laneTargetOf(p, i))] += v;
        }
    }

    // A target's value after movement, by kLaneSpans (as tf::laneApply, in
    // double): additive in the parameter's own units, or by octaves for Reach
    // and Sculpt, clamped to [lo, hi]. Phase and Fit wrap instead and are
    // applied where they are used.
    double moved(LaneTarget target, double value, double lo, double hi) const noexcept TF_NONBLOCKING {
        const auto t = static_cast<std::size_t>(target);
        const LaneSpan span = kLaneSpans[t];
        const double amount = span.amount;
        const double v = span.octaves ? value * std::exp2(amount * laneMove[t]) : value + amount * laneMove[t];
        return std::clamp(v, lo, hi);
    }
    // A wrapping target's movement in cycles (its span is in degrees).
    double movedCycles(LaneTarget target) const noexcept TF_NONBLOCKING {
        const auto t = static_cast<std::size_t>(target);
        return kLaneSpans[t].amount / 360.0 * laneMove[t];
    }

    void controlTick(const Params& p) noexcept TF_NONBLOCKING {
        const bool snap = snapNext;
        snapNext = false;
        const double c = snap ? 1.0 : tickCoef;
        const auto step = [&](Sm id, double target) { return smooth[id].step(target, c); };

        // Lanes, evaluated at the end of this control period.
        laneTick(p, lanePpqAhead(controlBase), notePpqAhead(controlBase), c);

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
        step(spreadLog,
             std::log(std::clamp(p[ParamId::sculptSpread], spec(ParamId::sculptSpread).min, spec(ParamId::sculptSpread).max)));
        step(sculptRes_, p[ParamId::sculptRes]);
        step(sculptDepth_, p[ParamId::sculptDepth]);
        step(modWheel_, modWheelTarget);
        step(timbreOffset_, timbreOffsetTarget);
        step(aftertouch_, aftertouchTarget);
        // Fit is a phase: smooth towards the nearest copy of the target, so
        // 350 -> 10 degrees moves 20 degrees, not 340.
        {
            const double target = static_cast<double>(p[ParamId::fit]) / 360.0;
            const double current = smooth[fitCycles].value();
            step(fitCycles, snap ? target : current + wrapHalf(target - current));
        }

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
        std::array<double, osCount> t {};
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
        const auto fbChoice = static_cast<std::size_t>(
            std::clamp(p.choice(ParamId::fbTime), 0, static_cast<int>(kFbCycles.size()) - 1));
        const double cycles = cycleSmooth.step(kFbCycles[fbChoice], snap ? 1.0 : delayTickCoef);
        const double periodOs = std::clamp(cycles / std::max(incM, 1e-9), 2.0, 0.5 * feedback.maxDelay());
        t[fbPeriod] = periodOs;
        t[fbDelay] = periodOs;

        t[width] = moved(LaneTarget::width, sm(width_), 0.0, 1.0);
        t[tractAmt] = std::clamp(sm(tract_), 0.0, 1.0);
        const double driveMod = std::clamp(
            moved(LaneTarget::drive, sm(drive_), 0.0, 1.0) + 0.35 * sm(modWheel_) + driveVelOffset, 0.0, 1.0);
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
        const auto blend = weightBlend(feel::weightSlopeDb(static_cast<float>(moved(LaneTarget::weight, sm(weight_), 0.0, 1.0))),
                                       weightPivotHz(f0, splitHz), fo);
        t[weightG] = blend.gain;
        t[weightDry] = blend.dry;
        t[weightOne] = blend.one;
        t[weightTwo] = blend.two;
        for (std::size_t i = 0; i < osCount; ++i) {
            if (snap) os[i].snap(t[i]);
            else os[i].to(t[i], controlOs);
        }

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
            double first = moved(LaneTarget::sculpt, std::exp(sm(sculptLog)), spec(ParamId::sculpt).min,
                                 spec(ParamId::sculpt).max);
            if (sm(aftertouch_) > 0.0)
                first = std::clamp(first * std::exp2(sm(aftertouch_)), static_cast<double>(spec(ParamId::sculpt).min),
                                   static_cast<double>(spec(ParamId::sculpt).max));
            const double spread = std::exp(sm(spreadLog));
            // Bell tracks exact harmonics; Phaser and Comb apply subtle opposing stereo offset when Width > 0.
            const double stereoRatio = sMode == SculptMode::bell ? 1.0 : std::exp2(0.035 * t[width]);

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
        if (snap) tractCoefs.snap(f, fo);
        else tractCoefs.setTarget(f, fo, controlOs);

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
        std::array<double, baseCount> b {};
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
            if (snap) base[i].snap(b[i]);
            else base[i].to(b[i], controlBase);
        }

        effective.coupling = static_cast<float>(couplingMod);
        effective.beat = static_cast<float>(moved(LaneTarget::beat, sm(beat), 0.0, 1.0));
        effective.reach = static_cast<float>(reachHarmonic);
        effective.morph = static_cast<float>(morphMod);
        effective.shift = static_cast<float>(shiftMod);
        effective.drive = static_cast<float>(driveMod);
        {
            double sc = moved(LaneTarget::sculpt, std::exp(sm(sculptLog)), spec(ParamId::sculpt).min,
                              spec(ParamId::sculpt).max);
            if (sm(aftertouch_) > 0.0)
                sc = std::clamp(sc * std::exp2(sm(aftertouch_)), static_cast<double>(spec(ParamId::sculpt).min),
                                static_cast<double>(spec(ParamId::sculpt).max));
            effective.sculpt = static_cast<float>(sc);
        }
        effective.width = static_cast<float>(t[width]);
        effective.active = true;
    }

    double anchorBpmOrCurrent() const noexcept TF_NONBLOCKING { return qps * 60.0 * fs; }

    // ---- audio -------------------------------------------------------------

    void oversampledSample(int j) noexcept TF_NONBLOCKING {
        for (auto& r : os) r.next();
        tractCoefs.advance();
        sculptCoefsA.advance();
        sculptCoefsB.advance();
        if (foldMix < 1.0) foldMix = std::min(1.0, foldMix + foldMixStep);
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
        const float subSine = static_cast<float>(
            sw <= 1.0e-5
                ? subFund
                : (subFund + sw * (0.24 * std::sin(2.0 * pSub) + 0.14 * std::sin(3.0 * pSub))) / (1.0 + 0.24 * sw));

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
        const double lockA = std::sin(static_cast<float>(kTwoPi * frac(phaseLockA + os[offA].value) + os[pmA].value * master + fb));
        const double lockB = std::sin(static_cast<float>(kTwoPi * frac(phaseLockB + os[offB].value) + os[pmB].value * master + fb));

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
        for (int k = 0; k < local; ++k) buf[k] = static_cast<float>(foldA.process(buf[k], g, off, warp, foldFrom, foldTo, foldMix));
        double ya = localA.down(buf);
        localB.up(static_cast<float>(vbIn), buf);
        for (int k = 0; k < local; ++k) buf[k] = static_cast<float>(foldB.process(buf[k], g, off, warp, foldFrom, foldTo, foldMix));
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
        const WeightMix mix { os[weightG].value, os[weightDry].value, os[weightOne].value, os[weightTwo].value };
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

                for (int j = 0; j < factor; ++j) oversampledSample(j);
                const double sub = decSub.process(bufSub.data());
                const double l = decL.process(bufL.data());
                const double r = decR.process(bufR.data());

                for (auto& ramp : base) ramp.next();

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
                const double lim = base[limitAmt].value;
                // Clip 1.4 dB below ceilingG so post-clip BW4 overshoot lands under the
                // look-ahead limiter's threshold, keeping the limiter transparent and ripple-free.
                const double clipCeilEff = lim * (0.85 * ceilingG) + (1.0 - lim) * 2.5;
                std::array<float, ClipTrip::kFactor> upL {}, upR {};
                const double preClip = mid;  // (L + R) / 2
                clip2xL.up(static_cast<float>(mid + side), upL.data());
                clip2xR.up(static_cast<float>(mid - side), upR.data());
                double clipDbNow = 0.0;
                for (std::size_t k = 0; k < ClipTrip::kFactor; ++k) {
                    double a = upL[k], c = upR[k];
                    for (std::size_t pass = 0; pass < kClipPasses; ++pass) {
                        const double db = -gainToDb(growlClip(a, c, clipCeilEff));
                        if (pass == 0) clipDbNow = std::max(clipDbNow, db);
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
                std::array<double, 1> growlDelayed { 0.5 * (gl + gr) };
                limiter.process(outL, outR, growlDelayed, ceilingG, lim);
                const double level = base[growlLevel].value;
                outL *= level;
                outR *= level;
                growlDelayed[0] *= level;
                double subL = subGated, subR = subGated;
                std::array<double, 1> subDelayed { masterTap };
                subLimiter.process(subL, subR, subDelayed, ceilingLin, lim);
                blockMinLimGain = std::min(blockMinLimGain, std::min(limiter.gain(), subLimiter.gain()));
                outL += subL;
                outR += subR;
                outputClip(outL, outR, ceilingLin, lim);

                left[n] = static_cast<float>(outL);
                right[n] = static_cast<float>(outR);
                if (taps.master != nullptr) taps.master[n] = static_cast<float>(subDelayed[0]);
                if (taps.growl != nullptr) taps.growl[n] = static_cast<float>(growlDelayed[0]);
                if (taps.growlPreClip != nullptr) taps.growlPreClip[n] = static_cast<float>(preClip);

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
            if (taps.master != nullptr) std::fill_n(taps.master + n, rest, 0.0f);
            if (taps.growl != nullptr) std::fill_n(taps.growl + n, rest, 0.0f);
            if (taps.growlPreClip != nullptr) std::fill_n(taps.growlPreClip + n, rest, 0.0f);
            grDb = 0.0f;
            // The lanes keep their control periods while the DSP sleeps.
            for (; n < numSamples; ++n) {
                if (countdown == 0) {
                    laneTick(p, lanePpqAhead(controlBase), notePpqAhead(controlBase), tickCoef);
                    const double mw = smooth[modWheel_].step(modWheelTarget, tickCoef);
                    const double ti = smooth[timbreOffset_].step(timbreOffsetTarget, tickCoef);
                    const double at = smooth[aftertouch_].step(aftertouchTarget, tickCoef);
                    effective.coupling = static_cast<float>(std::clamp(
                        moved(LaneTarget::coupling, static_cast<double>(p[ParamId::coupling]), 0.0, 1.0) + 0.35 * at,
                        0.0, 1.0));
                    effective.beat =
                        static_cast<float>(moved(LaneTarget::beat, static_cast<double>(p[ParamId::beat]), 0.0, 1.0));
                    effective.reach = static_cast<float>(
                        moved(LaneTarget::reach,
                              static_cast<double>(
                                  std::clamp(p[ParamId::reach], spec(ParamId::reach).min, spec(ParamId::reach).max)),
                              spec(ParamId::reach).min, spec(ParamId::reach).max));
                    effective.morph = static_cast<float>(std::clamp(
                        moved(LaneTarget::morph, static_cast<double>(p[ParamId::morph]), 0.0, 1.0) + ti, 0.0, 1.0));
                    effective.shift = static_cast<float>(
                        moved(LaneTarget::shift, static_cast<double>(p[ParamId::tractShift]), -24.0, 24.0));
                    effective.drive = static_cast<float>(std::clamp(
                        moved(LaneTarget::drive, static_cast<double>(p[ParamId::drive]), 0.0, 1.0) + 0.35 * mw, 0.0,
                        1.0));
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
};

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
