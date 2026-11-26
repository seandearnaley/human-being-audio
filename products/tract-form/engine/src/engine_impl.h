// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "tf/engine.h"

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

#include <hba/dsp/control.h>
#include <hba/dsp/crossover.h>
#include <hba/dsp/denormals.h>
#include <hba/dsp/limiter.h>
#include <hba/dsp/oversampling.h>
#include <hba/dsp/svf.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace tf {

namespace detail {

using namespace dsp;
using namespace hba::dsp;

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
constexpr double kPpqTolerance = 1e-5; // quarters; larger jumps re-anchor the clock
// Gain staging. The sub is a sine whose peak after its low-pass is known
// exactly every control period, so the growl gets its own static ceiling
// G = ceiling x margin - sub peak (lowered further only while the sub's own
// transients rise above that peak). The growl path, in order:
//   LR4 high-pass at the split -> detail::Squash -> x kGrowlGain -> mid/side
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
constexpr double kGrowlCeilingMargin = 0.96605;      // -0.3 dB, for inter-sample peaks
constexpr double kGrowlCeilingFloor = 1e-3;          // -60 dBFS: a sub at or above the ceiling leaves the growl this
constexpr double kGrowlLimiterReleaseSeconds = 0.12; // > 3 periods of E1 (41 Hz): no ripple at the note rate
constexpr double kSubHoldSeconds = 0.06;             // > 2 periods of E1
constexpr double kSubReleaseSeconds = 0.25;
// The growl clipper's oversampling (the sub and master tap take the same
// 4x trip + matched 2-pass linear midpoint so latency and phase stay exact).
using ClipTrip = BaseRate4x;
// Two 2x-oversampled growl clipper passes each followed by a 4th-order
// Butterworth high-pass at 1.05x splitHz (combined LR8: -48 dB/oct, -64 dB at f0):
// restores +13 dB of lower-growl punch at 3*f0 while trimming filter overshoot cleanly
// before the look-ahead limiter.
constexpr std::size_t kClipPasses = 2;
constexpr double kPostClipSplitRatio = 1.10;

inline int effectiveOversampling(double sampleRate, int requested) {
    int factor = 1;
    while (factor < requested && factor < detail::kMaxOversampling)
        factor *= 2;
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
inline double lr4LowpassMagnitude(double hz, double cutoff, double sampleRate) noexcept TF_NONBLOCKING {
    if (hz >= 0.499 * sampleRate)
        return 0.0;
    const double w = std::tan(kPi * std::max(0.0, hz) / sampleRate) / detail::SvfCoefs::prewarp(cutoff, sampleRate);
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

inline double noteToHz(double note) noexcept TF_NONBLOCKING { return 440.0 * std::exp2((note - 69.0) / 12.0); }

} // namespace detail

struct Engine::Impl {
    using ClipTrip = detail::ClipTrip;
    // Configuration, fixed by prepare().
    double fs = 48000.0;
    double fo = 192000.0;
    int factor = 4;
    int controlBase = 4; // base samples per control period
    int controlOs = 16;  // oversampled samples per control period
    int decimatorDelay = 0;
    double decimatorDelayExact = 0.0;
    double tickCoef = 1.0, delayTickCoef = 1.0;
    std::array<double, detail::kVowelCount> vowelComp{};

    // Oversampled domain. Voice A runs through channel A's tract, fold, Reach
    // and Weight, voice B through channel B's; Width mixes them after that.
    detail::Decimator decSub, decL, decR;
    detail::LocalOversampler localSub, localA, localB; // around the fold; the sub gets the same delay
    // Around the growl clipper (4x HIIR + pass-0 ADAA1 + pass-1 linked trim); the sub and master tap take the same
    // trip.
    ClipTrip clip2xL, clip2xR, clip2xSub, clip2xMaster;
    detail::GrowlClipAdaa growlClipAdaa;
    double subClipHalfDelay = 0.0, masterClipHalfDelay = 0.0;
    std::array<float, detail::kMaxOversampling> bufSub{}, bufL{}, bufR{}, bufLocal{};
    detail::FeedbackDelay feedback;
    detail::DcBlocker feedbackDc;
    detail::ReachFilter feedbackLp; // the loop's 24 dB/oct low-pass at min(reach, 8) x f0
    detail::TractCoefs tractCoefs;
    detail::TractChannel tractA, tractB;
    detail::TractChannel tractPostA, tractPostB;
    detail::Folder foldA, foldB;
    float subFoldDelay = 0.0f;
    detail::ReachFilter reachA, reachB;
    detail::WeightFilter weightA, weightB;
    detail::SculptCoefs sculptCoefsA, sculptCoefsB;
    std::array<detail::SculptChannel, 2> sculptA{}, sculptB{};
    int sculptSlot = 0;
    SculptMode sculptModeCur = SculptMode::bell;
    SculptMode sculptModePrev = SculptMode::bell;
    double sculptXfadeMix = 1.0;
    double phaseMaster = 0.0, phaseA = 0.0, phaseB = 0.0;
    double phaseLockA = 0.0, phaseLockB = 0.0;
    FoldType foldFrom = FoldType::fold, foldTo = FoldType::fold;
    double foldMix = 1.0, foldMixStep = 1.0;
    std::array<detail::Ramp, detail::osCount> os{};

    // Base rate.
    detail::Lr4Highpass growlHpL, growlHpR;
    detail::Lr4Highpass sideHp, sideHpPost;
    // After each pass of the growl clipper, at the clipper's 2x rate, per channel.
    std::array<std::array<hba::dsp::Bw4Highpass, 2>, detail::kClipPasses> postClipHp{};
    detail::Lr4Allpass midAp, midApPost;
    detail::Lr4Lowpass subLp;
    detail::SvfCoefs cSub, cXover, cXoverClip;
    detail::Bw4Coefs cSubClip;
    detail::Squash squash;
    detail::Limiter limiter;           // on the growl
    detail::Limiter subLimiter;        // on the sub, idle unless the sub alone exceeds the ceiling
    detail::MovingMinimum subPeakHold; // of -|sub|: a moving maximum
    double subEnvelope = 0.0, subEnvelopeRelease = 1.0;
    std::array<detail::Ramp, detail::baseCount> base{};

    // Control.
    std::array<detail::Smoother, detail::smCount> smooth{};
    std::array<detail::Smoother, kLaneCount> laneSmooth{};
    std::array<detail::Smoother, kFormantCount * 3 + 1> formantSmooth{};
    detail::Smoother cycleSmooth;                                             // feedback delay in cycles of the note
    std::array<detail::Smoother, detail::kSculptMaxBands> sculptGainSmooth{}; // dB, per band
    int countdown = 0;
    bool snapNext = true;

    // Notes and gate.
    detail::NoteStack notes;
    detail::NoteStack physicalNotes;
    bool sustainDown = false;
    detail::Glide glide;
    detail::Gate gate;
    detail::PitchPunch punch;
    detail::DelayLine<double> gateDelay;
    int pendingNote = -1;
    double pendingVelocity = 1.0;
    double noteVelocity = 1.0;
    double bendTarget = 0.0;
    double bendRange = detail::kBendRange;
    double modWheelTarget = 0.0;
    double timbreOffsetTarget = 0.0;
    double aftertouchTarget = 0.0;
    double glideMs = spec(ParamId::glide).def;
    bool phaseResetOn = true;
    bool beatLockOn = false;

    // Clock.
    bool wasPlaying = false;
    double anchorPpq = 0.0, anchorBpm = 0.0;
    long long sinceAnchor = 0, sinceNoteStart = 0, freeSamples = 0;
    double qps = 0.0; // quarters per base sample
    bool playing = false;

    // Idle.
    bool idle = true;
    int silentCount = 0;

    // Interface state.
    float hz = 0.0f;
    float grDb = 0.0f;
    float clipDb = 0.0f, clipMeanDb = 0.0f; // growl clipper over the last block
    double clipMaxDb = 0.0, clipSumDb = 0.0;
    std::array<float, kLaneCount> uiPhase{}, uiValue{};
    // The lanes' drawn curves (setLaneCurves), and each target's movement: the
    // sum of the smoothed sends of every lane aimed at it, in -1..1 per lane.
    LaneCurves laneCurves = defaultLaneCurves();
    std::array<double, kLaneTargetCount> laneMove{};
    double uiPpq = 0.0;     // the lane clock, host position or free-running
    double uiNotePpq = 0.0; // note-relative clock for Retrig / One-Shot lanes
    double uiFreeSec = 0.0; // monotonic elapsed seconds for Free lanes
    double uiNoteSec = 0.0; // note-relative elapsed seconds for Free R lanes
    EffectiveState effective{};

    void prepare(double sampleRate, int oversampling);
    static double measureClipTripDelay();
    static double measureDecimatorDelay(int factor, int foldTargetRate);
    void resetDsp() noexcept TF_NONBLOCKING;
    void reset() noexcept TF_NONBLOCKING;
    int latency() const noexcept TF_NONBLOCKING { return decimatorDelay + limiter.latency(); }

    void wake() noexcept TF_NONBLOCKING;
    void startNote(int note, double velocity) noexcept TF_NONBLOCKING;
    void beginNote(int note, double velocity) noexcept TF_NONBLOCKING;
    void noteOn(int note, float velocity = 1.0f) noexcept TF_NONBLOCKING;
    void noteOff(int note) noexcept TF_NONBLOCKING;
    void sustain(bool down) noexcept TF_NONBLOCKING;
    void allNotesOff() noexcept TF_NONBLOCKING;

    double sm(detail::Sm id) const noexcept TF_NONBLOCKING { return smooth[id].value(); }
    void pitchIncrements(double& master, double& a, double& b) const noexcept TF_NONBLOCKING;
    void retargetPitchNow() noexcept TF_NONBLOCKING;
    double lanePpqAhead(long long samplesAhead) const noexcept TF_NONBLOCKING;
    double notePpqAhead(long long samplesAhead) const noexcept TF_NONBLOCKING;
    double freeSecAhead(long long samplesAhead) const noexcept TF_NONBLOCKING;
    double noteSecAhead(long long samplesAhead) const noexcept TF_NONBLOCKING;
    void laneTick(const Params& p, double ppq, double nPpq, double fSec, double nSec, double c) noexcept TF_NONBLOCKING;
    double moved(LaneTarget target, double value, double lo, double hi) const noexcept TF_NONBLOCKING;
    double movedCycles(LaneTarget target) const noexcept TF_NONBLOCKING;
    void smoothParameters(const Params& p, double c, bool snap) noexcept TF_NONBLOCKING;
    void updateSculptTargets(const Params& p, double f0, double widthVal, double c, bool snap) noexcept TF_NONBLOCKING;
    void updateEffectiveState(double couplingMod, double reachHarmonic, double morphMod, double shiftMod,
                              double driveMod, double widthVal) noexcept TF_NONBLOCKING;
    void controlTick(const Params& p) noexcept TF_NONBLOCKING;
    double anchorBpmOrCurrent() const noexcept TF_NONBLOCKING { return qps * 60.0 * fs; }

    void oversampledSample(int j) noexcept TF_NONBLOCKING;
    void goIdle() noexcept TF_NONBLOCKING;
    void updateClock(const Transport& t) noexcept TF_NONBLOCKING;
    void process(const Params& p, const Transport& transport, float* left, float* right, int numSamples,
                 Taps taps) noexcept TF_NONBLOCKING;
};

} // namespace tf
