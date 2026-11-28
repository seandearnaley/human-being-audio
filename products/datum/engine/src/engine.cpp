// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dt/engine.h"

#include <hba/dsp/sample_exchange.h>

#include <hba/core/smf.h>
#include <hba/dsp/adaa.h>
#include <hba/dsp/allpass.h>
#include <hba/dsp/control.h>
#include <hba/dsp/crossover.h>
#include <hba/dsp/denormals.h>
#include <hba/dsp/gain.h>
#include <hba/dsp/harmonic_phase.h>
#include <hba/dsp/limiter.h>
#include <hba/dsp/oversampling.h>
#include <hba/dsp/random.h>
#include <hba/dsp/svf.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>

namespace dt {

namespace {

inline constexpr int kMaxVoices = 4;
inline constexpr int kDisperserPairs = 4; // 4 matched pairs = 8-stage allpass disperser
inline constexpr int kMembraneModes = 6;
inline constexpr int kHarmonicPartials = 6; // H1..H6
inline constexpr int kScRingSize = 4096;
inline constexpr int kCavityDelaySize = 512;

inline constexpr std::array<double, kMembraneModes> kBesselRatios{1.0, 1.5933, 2.1355, 2.2954, 2.6531, 2.9173};

struct ClickSlot {
    std::array<float, ClickSample::kMaxSamples> mono{};
    int length = 0;
    double sampleRate = 48000.0;
    int offsetSamples = 0;
    float polarity = 1.0f;
};

double evalPitchHz(const hba::dsp::EnvelopeCurve& curve, double u, double baseHz, double endY,
                   double sweepOctaves) noexcept DT_NONBLOCKING {
    const double y = hba::dsp::evaluateEnvelope(curve, u);
    const double rel = std::max(-0.5, y - endY);
    const double oct = rel * sweepOctaves;
    return std::clamp(baseHz * std::pow(2.0, oct), 18.0, 14000.0);
}

// 4-point Gauss-Legendre integral of instantaneous frequency across [u0, u1] in cycles (multiply by lengthSec).
double integratePitchCycles(const hba::dsp::EnvelopeCurve& curve, double u0, double u1, double baseHz, double endY,
                            double sweepOctaves) noexcept DT_NONBLOCKING {
    if (!(u1 > u0))
        return 0.0;
    constexpr std::array<double, 4> kX{-0.8611363115940526, -0.3399810435848563, 0.3399810435848563,
                                       0.8611363115940526};
    constexpr std::array<double, 4> kW{0.3478548451374538, 0.6521451548625461, 0.6521451548625461, 0.3478548451374538};
    const double mid = 0.5 * (u0 + u1);
    const double half = 0.5 * (u1 - u0);
    double sum = 0.0;
    for (std::size_t q = 0; q < 4; ++q)
        sum += kW[q] * evalPitchHz(curve, mid + half * kX[q], baseHz, endY, sweepOctaves);
    return half * sum;
}

double computeTotalPhaseRad(const hba::dsp::EnvelopeCurve& curve, double lengthSec, double baseHz, double endY,
                            double sweepOctaves) noexcept DT_NONBLOCKING {
    constexpr int kPanels = 32;
    double cycles = 0.0;
    for (int i = 0; i < kPanels; ++i) {
        const double u0 = static_cast<double>(i) / static_cast<double>(kPanels);
        const double u1 = static_cast<double>(i + 1) / static_cast<double>(kPanels);
        cycles += integratePitchCycles(curve, u0, u1, baseHz, endY, sweepOctaves);
    }
    return hba::dsp::kTwoPi * cycles * lengthSec;
}

// Envelope-native transient shaper: modulates Attack (0..12ms), Punch (12..55ms), Tail (55ms..end)
// directly on the analytical envelope with zero lookahead latency and zero follower lag.
double transientRegionGain(double timeMs, double envAttack, double envPunch, double envTail) noexcept DT_NONBLOCKING {
    // Smooth cosine partition of unity across Attack / Punch / Tail time windows
    const double wAttack = std::exp(-0.5 * (timeMs / 5.5) * (timeMs / 5.5));
    const double wPunch = std::exp(-0.5 * ((timeMs - 24.0) / 18.0) * ((timeMs - 24.0) / 18.0));
    const double wTail = 1.0 / (1.0 + std::exp(-(timeMs - 58.0) / 14.0));
    const double db = 6.5 * envAttack * wAttack + 5.5 * envPunch * wPunch + 7.0 * envTail * wTail;
    return std::pow(10.0, db / 20.0);
}

// Zero-phase spectral dip factor when instantaneous frequency passes through box-cut zone.
double zeroPhaseBoxDip(double freqHz, double boxHz, double boxDb) noexcept DT_NONBLOCKING {
    if (boxDb <= 0.05)
        return 1.0;
    const double ratio = std::log2(std::max(20.0, freqHz) / std::max(80.0, boxHz));
    const double bell = std::exp(-0.5 * (ratio / 0.42) * (ratio / 0.42));
    return std::pow(10.0, (-boxDb * bell) / 20.0);
}

struct Voice {
    bool active = false;
    bool subActive = false;
    int midiNote = 36;
    int slotIndex = 0;
    std::uint64_t age = 0;

    double timeSec = 0.0;
    double lengthSec = 0.145;
    double baseHz = 52.0;
    double endY = 0.19;
    double sweepOctaves = 5.0;
    double velocity = 1.0;
    double velPunch = 1.0;
    double clickScale = 1.0;
    double driveScale = 1.0;
    double levelScale = 1.0;

    double phaseRad = 0.0;
    double startPhaseRad = 0.0;
    std::array<double, kHarmonicPartials> harmPhases{};
    std::array<double, kMembraneModes> modePhases{};

    double fmCarrierPhase = 0.0;
    double fmModPhase = 0.0;

    double chokeGain = 1.0;
    double chokeStep = 0.0;
    bool choking = false;

    double clickReadPos = 0.0;
    hba::dsp::Lcg32 rng{0x51A7E123u};
    hba::dsp::Lcg32 rngR{0x9E3779B9u};
    hba::dsp::TwinTResonator twinT;
    hba::dsp::SvfState beaterFilter;
    hba::dsp::SvfState noiseFilterL;
    hba::dsp::SvfState noiseFilterR;
    hba::dsp::SvfState cavityFilter;
    hba::dsp::SvfState boxFilter;
    double cachedCavHz = -1.0;
    double cachedCavQ = -1.0;
    hba::dsp::SvfCoefs cachedCavCoefs{};
    double cachedBoxHz = -1.0;
    double cachedBoxDb = -1.0;
    hba::dsp::SvfCoefs cachedBoxCoefs{};
    double cachedBoxBellGain = 1.0;
    double cachedBeaterHz = -1.0;
    hba::dsp::SvfCoefs cachedBeaterCoefs{};
    double cachedNoiseHz = -1.0;
    double cachedNoiseQ = -1.0;
    hba::dsp::SvfCoefs cachedNoiseCoefs{};

    std::array<double, kCavityDelaySize> cavityDelay{};
    int cavityPos = 0;

    void reset(double fs) noexcept DT_NONBLOCKING {
        active = false;
        subActive = false;
        choking = false;
        chokeGain = 1.0;
        timeSec = 0.0;
        phaseRad = 0.0;
        fmCarrierPhase = 0.0;
        fmModPhase = 0.0;
        modePhases.fill(0.0);
        cavityDelay.fill(0.0);
        cavityPos = 0;
        twinT.prepare(fs);
        beaterFilter.reset();
        noiseFilterL.reset();
        noiseFilterR.reset();
        cavityFilter.reset();
        boxFilter.reset();
        cachedCavHz = cachedCavQ = -1.0;
        cachedBoxHz = cachedBoxDb = -1.0;
        cachedBeaterHz = -1.0;
        cachedNoiseHz = cachedNoiseQ = -1.0;
    }

    void beginChoke(double fs, double ms = 1.2) noexcept DT_NONBLOCKING {
        if (!active || choking)
            return;
        choking = true;
        const double samples = std::max(8.0, fs * ms * 0.001);
        chokeStep = chokeGain / samples;
    }

    void trigger(double fs, const Params& p, const hba::dsp::EnvelopeCurve& pitchCurve, const ComposePattern& pat,
                 int note, int slot, float vel, std::uint64_t triggerId,
                 std::uint32_t randSeed) noexcept DT_NONBLOCKING {
        reset(fs);
        active = true;
        subActive = true;
        midiNote = note;
        slotIndex = std::clamp(slot, 0, kKitSlots - 1);
        age = triggerId;

        const double v = std::clamp(static_cast<double>(vel), 0.05, 1.0);
        velocity = v;
        const double vp = static_cast<double>(p[ParamId::velToPunch]);
        velPunch = (1.0 - vp) + vp * (0.45 + 0.55 * v);

        // Interpolate active slot toward next slot by slotMorph
        const auto& slA = pat.slots[static_cast<std::size_t>(slotIndex)];
        const auto& slB = pat.slots[static_cast<std::size_t>((slotIndex + 1) % kKitSlots)];
        const double morph = static_cast<double>(p[ParamId::slotMorph]);
        const double slTune = slA.tuneOffsetSt + (slB.tuneOffsetSt - slA.tuneOffsetSt) * morph;
        const double slLen = slA.lengthScale + (slB.lengthScale - slA.lengthScale) * morph;
        clickScale = slA.clickScale + (slB.clickScale - slA.clickScale) * morph;
        driveScale = slA.driveScale + (slB.driveScale - slA.driveScale) * morph;
        levelScale = slA.levelScale + (slB.levelScale - slA.levelScale) * morph;

        // Deterministic PRNG unless humanize > 0
        const double hum = static_cast<double>(p[ParamId::humanize]);
        rng = hba::dsp::Lcg32{hum > 1.0e-4 ? (randSeed ^ 0x51A7E123u) : 0x51A7E123u};
        rngR = hba::dsp::Lcg32{hum > 1.0e-4 ? (randSeed ^ 0x9E3779B9u) : 0x9E3779B9u};
        const double humPitch = hum > 1.0e-4 ? (rng.nextBipolar() * 0.004 * hum) : 0.0;
        const double humLen = hum > 1.0e-4 ? (1.0 + rng.nextBipolar() * 0.03 * hum) : 1.0;

        // Pitch: Kit mode uses tuneHz + slot offset; Chromatic transposes around C1 (MIDI 36)
        const double semiOffset = (p.choice(ParamId::keyTrack) == static_cast<int>(KeyTrackMode::chromatic))
                                      ? (static_cast<double>(note - 36) + slTune)
                                      : slTune;
        baseHz = std::clamp(
            static_cast<double>(p[ParamId::tuneHz]) * std::pow(2.0, semiOffset / 12.0) * (1.0 + humPitch), 22.0, 260.0);

        // Tight macro shortens decay smoothly and sharpens punch
        const double tight = static_cast<double>(p[ParamId::tight]);
        const double rawLenSec =
            static_cast<double>(p[ParamId::lengthMs]) * 0.001 * slLen * humLen * (1.0 - 0.42 * tight);
        lengthSec = std::clamp(rawLenSec, 0.038, 1.40);

        const int lastNode = std::clamp(pitchCurve.count, 1, hba::dsp::kMaxEnvNodes) - 1;
        endY = static_cast<double>(pitchCurve.nodes[static_cast<std::size_t>(lastNode)].y);
        const double punch = static_cast<double>(p[ParamId::punchAmt]);
        const double pDepth = static_cast<double>(p[ParamId::pitchDepth]);
        sweepOctaves = (2.2 + 5.6 * pDepth) * (0.55 + 0.65 * punch) * velPunch;

        startPhaseRad = static_cast<double>(p[ParamId::startPhase]) * (hba::dsp::kPi / 180.0);
        phaseRad = startPhaseRad;

        // Zero-Crossing Tail Snap: align final fade-out onto nearest positive-going zero crossing!
        if (p.flag(ParamId::zeroCross)) {
            const double totalPhi =
                startPhaseRad + computeTotalPhaseRad(pitchCurve, lengthSec, baseHz, endY, sweepOctaves);
            const double cycles = totalPhi / hba::dsp::kTwoPi;
            const double targetCycles = std::max(2.0, std::round(cycles));
            const double deltaCycles = targetCycles - cycles;
            const double endHz = std::max(22.0, evalPitchHz(pitchCurve, 1.0, baseHz, endY, sweepOctaves));
            const double deltaSec = std::clamp(deltaCycles / endHz, -0.015, 0.015);
            lengthSec = std::max(0.036, lengthSec + deltaSec);
        }

        // Precompute Schroeder / Peak-Min harmonic crest phases once at trigger
        const std::array<double, kHarmonicPartials> hAmps{
            1.0,
            static_cast<double>(p[ParamId::harm2]),
            static_cast<double>(p[ParamId::harm3]),
            static_cast<double>(p[ParamId::harm4]),
            static_cast<double>(p[ParamId::harm5]),
            static_cast<double>(p[ParamId::harm6]),
        };
        const int hMode = p.choice(ParamId::headroomMode);
        if (hMode == static_cast<int>(HeadroomMode::schroeder)) {
            harmPhases = hba::dsp::schroederPhases(hAmps, startPhaseRad);
        } else if (hMode == static_cast<int>(HeadroomMode::peakMin)) {
            harmPhases = hba::dsp::optimizeCrestPhases(hAmps, startPhaseRad);
        } else {
            harmPhases.fill(startPhaseRad);
        }

        twinT.prepare(fs);
        twinT.trigger(0.95 * velPunch, startPhaseRad);
        clickReadPos = 0.0;
    }

    struct Bands {
        double sub = 0.0;
        double body = 0.0;
        double clickL = 0.0;
        double clickR = 0.0;
        double envAmp = 0.0;
    };

    Bands renderSample(double fs, const Params& p, const hba::dsp::EnvelopeCurve& pitchCurve,
                       const hba::dsp::EnvelopeCurve& ampCurve, const ClickSlot* clickSlot) noexcept DT_NONBLOCKING {
        Bands out{};
        if (!active)
            return out;

        const double dt = 1.0 / fs;
        const double u0 = timeSec / lengthSec;
        const double u1 = std::min(1.0, (timeSec + dt) / lengthSec);
        const double timeMs = timeSec * 1000.0;

        if (u0 >= 1.0) {
            active = false;
            subActive = false;
            return out;
        }

        // Analytical phase integration across [u0, u1]
        const double dCycles = integratePitchCycles(pitchCurve, u0, u1, baseHz, endY, sweepOctaves) * lengthSec;
        const double instHz = evalPitchHz(pitchCurve, u0, baseHz, endY, sweepOctaves);

        // Amplitude envelope + envelope-native Attack/Punch/Tail sculpting + smooth Zero-X end taper
        double amp = hba::dsp::evaluateEnvelope(ampCurve, u0);
        // Ultra-fast 0.15ms anti-click rise only if startPhase != 0 (preserves sample-0 punch!)
        if (timeSec < 0.00015 && std::abs(std::sin(startPhaseRad)) > 0.05)
            amp *= (timeSec / 0.00015);
        // Smooth closure over final 6ms so zero-cross tail reaches exact zero cleanly
        const double remSec = lengthSec - timeSec;
        const double closeSec = std::min(0.008, lengthSec * 0.12);
        if (remSec < closeSec) {
            const double w = std::clamp(remSec / closeSec, 0.0, 1.0);
            amp *= 0.5 - 0.5 * std::cos(hba::dsp::kPi * w);
        }

        const double transGain =
            transientRegionGain(timeMs, static_cast<double>(p[ParamId::envAttack]),
                                static_cast<double>(p[ParamId::envPunch]), static_cast<double>(p[ParamId::envTail]));
        amp = std::min(1.8, amp * transGain) * velocity * levelScale;

        if (choking) {
            chokeGain -= chokeStep;
            if (chokeGain <= 0.0) {
                active = false;
                subActive = false;
                return out;
            }
            amp *= chokeGain;
        }
        out.envAmp = amp;

        // Berger nonlinear tension pitch glide (Membrane mode or general subtle tension)
        const int topo = p.choice(ParamId::coreTopology);
        const double tens = static_cast<double>(p[ParamId::tension]);
        const double bergerMult = (topo == static_cast<int>(CoreTopology::membrane))
                                      ? (1.0 + 0.42 * tens * std::pow(std::min(1.0, amp), 1.5))
                                      : (1.0 + 0.08 * tens * amp);
        const double dPhi = hba::dsp::kTwoPi * dCycles * bergerMult;

        const double boxHz = static_cast<double>(p[ParamId::boxCutHz]);
        const double boxDb = static_cast<double>(p[ParamId::boxCutDb]) + 2.5 * static_cast<double>(p[ParamId::tight]);
        const bool boxZeroPh = p.flag(ParamId::boxZeroPhase);
        const double zeroPhDip = boxZeroPh ? zeroPhaseBoxDip(instHz, boxHz, boxDb) : 1.0;

        // --- 1. CORE TOPOLOGY (Native Sub + Body separation) ---
        const double pureSin = std::sin(phaseRad);
        // Frequency-proportional split: below 1.45*baseHz lives in pure Sub; upper pitch sweep feeds Body!
        const double sweepWeight = std::clamp((instHz / std::max(24.0, baseHz) - 1.15) / 1.4, 0.0, 1.0);

        double subSig = 0.0;
        double bodySig = 0.0;

        if (topo == static_cast<int>(CoreTopology::sine)) {
            subSig = pureSin * (1.0 - 0.45 * sweepWeight);
            bodySig = pureSin * (0.45 * sweepWeight) * zeroPhDip;
        } else if (topo == static_cast<int>(CoreTopology::twinT)) {
            const double ttOut = twinT.process(instHz * bergerMult, lengthSec * 0.55, 0.35 + 0.5 * tens);
            // Anchor pure sub phase while preserving Twin-T analog warmth & punch
            subSig = 0.70 * pureSin + 0.30 * ttOut;
            bodySig =
                (0.55 * ttOut * sweepWeight + 0.25 * std::sin(2.0 * phaseRad + harmPhases[1]) * std::exp(-u0 * 3.5)) *
                zeroPhDip;
        } else if (topo == static_cast<int>(CoreTopology::harmonic)) {
            subSig = pureSin;
            const std::array<double, kHarmonicPartials> hAmps{
                0.0,
                static_cast<double>(p[ParamId::harm2]),
                static_cast<double>(p[ParamId::harm3]),
                static_cast<double>(p[ParamId::harm4]),
                static_cast<double>(p[ParamId::harm5]),
                static_cast<double>(p[ParamId::harm6]),
            };
            const double tilt = static_cast<double>(p[ParamId::harmTilt]);
            double hSum = pureSin * (0.35 * sweepWeight) * zeroPhDip;
            for (std::size_t k = 1; k < kHarmonicPartials; ++k) {
                const double a = hAmps[k];
                if (a <= 1.0e-5)
                    continue;
                const double kNum = static_cast<double>(k + 1);
                const double pHz = instHz * kNum;
                if (pHz >= 0.44 * fs)
                    continue;
                const double decayEnv = std::exp(-u0 * (1.2 + 5.8 * tilt * (kNum - 1.0)));
                const double dip = boxZeroPh ? zeroPhaseBoxDip(pHz, boxHz, boxDb) : 1.0;
                hSum += a * decayEnv * dip * std::sin(kNum * (phaseRad - startPhaseRad) + harmPhases[k]);
            }
            bodySig = hSum;
        } else { // Membrane: Berger tension + 6 stiff Bessel partials + Helmholtz Cavity Port Resonator
            subSig = pureSin;
            double mSum = pureSin * (0.35 * sweepWeight) * zeroPhDip;
            const double stiffB = 0.008 + 0.045 * tens;
            for (std::size_t m = 1; m < kMembraneModes; ++m) {
                const double ratio = kBesselRatios[m] * std::sqrt(1.0 + stiffB * static_cast<double>(m * m));
                const double mHz = instHz * ratio;
                if (mHz >= 0.44 * fs)
                    continue;
                modePhases[m] += hba::dsp::kTwoPi * mHz * dt;
                const double mDecay = std::exp(-timeSec / (0.012 + 0.055 / static_cast<double>(m)));
                const double dip = boxZeroPh ? zeroPhaseBoxDip(mHz, boxHz, boxDb) : 1.0;
                mSum += (0.28 / static_cast<double>(m)) * (0.35 + 0.65 * tens) * mDecay * dip * std::sin(modePhases[m]);
            }
            bodySig = mSum;
        }

        // Helmholtz Cavity Port Resonator (active whenever cavityQ > 0 on any topology!)
        const double cavAmt = static_cast<double>(p[ParamId::cavityQ]);
        if (cavAmt > 0.01) {
            const double cavHz = static_cast<double>(p[ParamId::cavityHz]);
            const double cavQ = 1.4 + 7.5 * cavAmt;
            if (cavHz != cachedCavHz || cavQ != cachedCavQ) {
                cachedCavCoefs = hba::dsp::SvfCoefs::make(cavHz, cavQ, fs);
                cachedCavHz = cavHz;
                cachedCavQ = cavQ;
            }
            const int delaySamples =
                std::clamp(static_cast<int>(fs / std::max(45.0, cavHz * 2.0)), 8, kCavityDelaySize - 2);
            const int readIdx = (cavityPos - delaySamples + kCavityDelaySize) % kCavityDelaySize;
            const double backPress = cavityDelay[static_cast<std::size_t>(readIdx)] * (0.25 * cavAmt);
            const double cavIn = (subSig + bodySig) * amp + backPress;
            const double cavOut = cavityFilter.bandpass(cachedCavCoefs, cavIn);
            cavityDelay[static_cast<std::size_t>(cavityPos)] = std::tanh(cavOut);
            cavityPos = (cavityPos + 1) % kCavityDelaySize;
            bodySig += cavOut * (0.42 * cavAmt);
        }

        // Minimum-phase SVF Box-Cut when zero-phase dip is switched off
        if (!boxZeroPh && boxDb > 0.05) {
            if (boxHz != cachedBoxHz) {
                cachedBoxCoefs = hba::dsp::SvfCoefs::make(boxHz, 1.35, fs);
                cachedBoxHz = boxHz;
            }
            if (boxDb != cachedBoxDb) {
                cachedBoxBellGain = hba::dsp::dbToGain(-boxDb);
                cachedBoxDb = boxDb;
            }
            const double bp = boxFilter.bandpass(cachedBoxCoefs, bodySig);
            bodySig += (cachedBoxBellGain - 1.0) * bp;
        }

        phaseRad += dPhi;

        out.sub = subActive ? (subSig * amp) : 0.0;
        out.body = bodySig * amp;

        // --- 2. IMPACT ENGINE (Pure-DSP Beater + 2-Op FM + Dynamic Filter Noise + Optional Click WAV) ---
        const double clickAmt = static_cast<double>(p[ParamId::clickAmt]) * clickScale * velPunch;
        if (clickAmt > 1.0e-4 && timeMs < 140.0) {
            const double bHard = static_cast<double>(p[ParamId::beaterHard]);
            const double bTone = static_cast<double>(p[ParamId::beaterTone]);
            const int bType = p.choice(ParamId::beaterType);

            double beater = 0.0;
            const double bDecayMs = 1.2 + (1.0 - bHard) * 7.5;
            if (timeMs < bDecayMs * 5.0) {
                const double envB = std::exp(-timeMs / bDecayMs);
                const double tSec = timeSec;
                switch (bType) {
                case static_cast<int>(BeaterType::felt):
                    beater = std::sin(hba::dsp::kTwoPi * (140.0 + 420.0 * bHard) * tSec) * envB * 0.9;
                    break;
                case static_cast<int>(BeaterType::wood):
                    beater = (std::sin(hba::dsp::kTwoPi * 720.0 * tSec) +
                              0.55 * std::sin(hba::dsp::kTwoPi * 1680.0 * tSec)) *
                             envB * 0.72;
                    break;
                case static_cast<int>(BeaterType::plastic):
                    beater = (std::sin(hba::dsp::kTwoPi * 1950.0 * tSec) +
                              0.6 * std::sin(hba::dsp::kTwoPi * 4200.0 * tSec)) *
                             std::exp(-timeMs / (0.8 + (1.0 - bHard) * 3.0)) * 0.75;
                    break;
                case static_cast<int>(BeaterType::leather):
                    beater = (std::sin(hba::dsp::kTwoPi * 360.0 * tSec) + 0.4 * rng.nextBipolar()) * envB * 0.85;
                    break;
                case static_cast<int>(BeaterType::metal):
                    beater = (std::sin(hba::dsp::kTwoPi * 2650.0 * tSec) +
                              0.65 * std::sin(hba::dsp::kTwoPi * 4370.0 * tSec) +
                              0.4 * std::sin(hba::dsp::kTwoPi * 6890.0 * tSec)) *
                             std::exp(-timeMs / (1.8 + (1.0 - bHard) * 8.0)) * 0.55;
                    break;
                case static_cast<int>(BeaterType::dirac):
                default: {
                    // Bandlimited single-cycle Dirac click (0.4..1.8 ms)
                    const double widthMs = 0.45 + (1.0 - bHard) * 1.4;
                    if (timeMs < widthMs)
                        beater = std::sin(hba::dsp::kTwoPi * (timeMs / widthMs)) * 1.15;
                    break;
                }
                }
                const double bHz = std::clamp(bTone * (0.65 + 0.65 * velocity), 220.0, 0.44 * fs);
                if (bHz != cachedBeaterHz) {
                    cachedBeaterCoefs = hba::dsp::SvfCoefs::make(bHz, 0.75, fs);
                    cachedBeaterHz = bHz;
                }
                beater = beaterFilter.lowpass(cachedBeaterCoefs, beater);
            }

            // Phase-locked 2-op transient FM knock
            double fmSig = 0.0;
            const double fmDepth = static_cast<double>(p[ParamId::fmDepth]);
            if (fmDepth > 1.0e-4) {
                const double fmDec = static_cast<double>(p[ParamId::fmDecayMs]);
                if (timeMs < fmDec * 5.5) {
                    const double fmEnv = std::exp(-timeMs / fmDec);
                    const double cHz = std::clamp(instHz * (1.8 + 3.2 * fmEnv), 60.0, 0.42 * fs);
                    const double mHz = std::clamp(cHz * static_cast<double>(p[ParamId::fmRatio]), 60.0, 0.44 * fs);
                    const double idx = 4.8 * fmDepth * fmEnv;
                    fmSig = std::sin(fmCarrierPhase + idx * std::sin(fmModPhase)) * fmEnv * fmDepth * 0.72;
                    fmCarrierPhase += hba::dsp::kTwoPi * cHz * dt;
                    fmModPhase += hba::dsp::kTwoPi * mHz * dt;
                }
            }

            // Nord Dynamic Filter Noise (LP / BP / HP with upper-band stereo air)
            double nL = 0.0;
            double nR = 0.0;
            const double nLvl = static_cast<double>(p[ParamId::noiseLevel]);
            if (nLvl > 1.0e-4) {
                const double nDec = static_cast<double>(p[ParamId::noiseDecayMs]);
                if (timeMs < nDec * 5.5) {
                    const double nEnv = std::exp(-timeMs / nDec);
                    const double nHz = std::clamp(static_cast<double>(p[ParamId::noiseHz]) * (0.55 + 0.65 * velocity),
                                                  220.0, 0.44 * fs);
                    const double nQ = 0.65 + 4.5 * static_cast<double>(p[ParamId::noiseQ]);
                    if (nHz != cachedNoiseHz || nQ != cachedNoiseQ) {
                        cachedNoiseCoefs = hba::dsp::SvfCoefs::make(nHz, nQ, fs);
                        cachedNoiseHz = nHz;
                        cachedNoiseQ = nQ;
                    }
                    const int nMode = p.choice(ParamId::noiseMode);
                    const double rawL = rng.nextBipolar();
                    const double airW = static_cast<double>(p[ParamId::airWidth]);
                    const double rawR = rawL * (1.0 - airW) + rngR.nextBipolar() * airW;
                    double fL = 0.0;
                    double fR = 0.0;
                    if (nMode == static_cast<int>(NoiseMode::lp)) {
                        fL = noiseFilterL.lowpass(cachedNoiseCoefs, rawL);
                        fR = noiseFilterR.lowpass(cachedNoiseCoefs, rawR);
                    } else if (nMode == static_cast<int>(NoiseMode::hp)) {
                        fL = noiseFilterL.highpass(cachedNoiseCoefs, rawL);
                        fR = noiseFilterR.highpass(cachedNoiseCoefs, rawR);
                    } else {
                        fL = noiseFilterL.bandpass(cachedNoiseCoefs, rawL);
                        fR = noiseFilterR.bandpass(cachedNoiseCoefs, rawR);
                    }
                    nL = fL * nEnv * nLvl * 0.85;
                    nR = fR * nEnv * nLvl * 0.85;
                }
            }

            // Optional user click WAV (lock-free read, cubic/linear interpolation)
            double wavClick = 0.0;
            const double wavMix = static_cast<double>(p[ParamId::clickWavMix]);
            if (clickSlot != nullptr && clickSlot->length > 8 && wavMix > 1.0e-4) {
                const double step = clickSlot->sampleRate / fs;
                const double pos = clickReadPos + static_cast<double>(clickSlot->offsetSamples);
                clickReadPos += step;
                if (pos >= 0.0 && pos < static_cast<double>(clickSlot->length - 1)) {
                    const int idx = static_cast<int>(pos);
                    const double frac = pos - static_cast<double>(idx);
                    const double s0 = clickSlot->mono[static_cast<std::size_t>(idx)];
                    const double s1 = clickSlot->mono[static_cast<std::size_t>(idx + 1)];
                    wavClick = (s0 + (s1 - s0) * frac) * static_cast<double>(clickSlot->polarity) * wavMix;
                }
            }

            const double monoClick = beater + fmSig + wavClick;
            const double envScale = velocity * levelScale * (choking ? chokeGain : 1.0);
            out.clickL = (monoClick + nL) * clickAmt * envScale;
            out.clickR = (monoClick + nR) * clickAmt * envScale;
        }

        timeSec += dt;
        return out;
    }
};

} // namespace

struct Engine::Impl {
    double fs = 48000.0;
    int osFactor = 4;

    hba::dsp::EnvelopeCurve pitchCurve = hba::dsp::defaultKickPitchCurve();
    hba::dsp::EnvelopeCurve ampCurve = hba::dsp::defaultKickAmpCurve();
    ComposePattern pattern = defaultComposePattern();

    std::array<Voice, kMaxVoices> voices{};
    std::uint64_t triggerCounter = 0;
    std::uint32_t randSeed = 0x13579BDFu;

    hba::dsp::SampleExchange<ClickSlot> clicks;

    // Up to 16x HIIR Oversampled ADAA2 Body Shaper (mono — body + sub are strictly mono)
    hba::dsp::Oversampler osBody;
    hba::dsp::Adaa2<hba::dsp::TanhShaper> tubeBody;
    hba::dsp::Adaa2<hba::dsp::HardClipShaper> hardBody;
    hba::dsp::Adaa2<hba::dsp::SineFoldShaper> foldBody;
    hba::dsp::SoftClipAdaa1 softClipBody;

    // Master Orchard/Bedrosian PhaseRotator (0..360 deg) + 8-stage Allpass Disperser
    hba::dsp::PhaseRotator rotL, rotR;
    std::array<hba::dsp::AllpassPair, kDisperserPairs> dispL{}, dispR{};

    // Zero-latency True-Peak Detector
    hba::dsp::TruePeakDetector truePeak;

    // Context Sequencer + Built-in Reference Sub-Bass state
    double internalPpq = 0.0;
    int lastSeqStep = -1;
    int lastRatchetSub = -1;
    double refBassPhase = 0.0;
    double refBassDuck = 1.0;

    // Sub-Link Sidechain capture & live scope ring
    std::array<float, kScRingSize> scLowRing{};
    int scRingPos = 0;
    hba::dsp::SvfState scLowpass;

    // Trigger-synced 256-point oscilloscope capture (~180 ms window)
    bool scopeCapturing = false;
    int scopeSampleIdx = 0;
    int scopeWindowSamples = 8640; // ~180 ms @ 48 kHz
    std::array<float, kScopePoints> capKick{};
    std::array<float, kScopePoints> capSc{};
    std::array<float, kScopePoints> capSum{};
    double capPeakKick = 0.0;
    double capPeakSum = 0.0;
    double capSumSq = 0.0;
    double capDcAcc = 0.0;
    double capDot = 0.0;
    double capKickSq = 0.0;
    double capScSq = 0.0;
    int capCount = 0;

    // Smoothed controls
    double phaseCos = 1.0;
    double phaseSin = 0.0;

    TelemetrySnapshot tel{};

    void prepare(double sampleRate, int /*maxBlockSize*/, int factor) {
        fs = std::max(8000.0, sampleRate);
        osFactor = (factor == 16) ? 16 : ((factor == 8) ? 8 : ((factor == 2) ? 2 : 4));
        scopeWindowSamples = std::max(kScopePoints, static_cast<int>(std::round(fs * 0.180)));
        osBody.prepare(osFactor);
        rotL.prepare(fs);
        rotR.prepare(fs);
        scLowpass.reset();
        reset();
    }

    void reset() noexcept DT_NONBLOCKING {
        for (auto& v : voices)
            v.reset(fs);
        osBody.reset();
        tubeBody.reset();
        hardBody.reset();
        foldBody.reset();
        softClipBody.reset();
        rotL.reset();
        rotR.reset();
        for (auto& ap : dispL)
            ap.reset();
        for (auto& ap : dispR)
            ap.reset();
        truePeak.reset();
        scLowRing.fill(0.0f);
        scRingPos = 0;
        scLowpass.reset();
        scopeCapturing = false;
        scopeSampleIdx = 0;
        internalPpq = 0.0;
        lastSeqStep = -1;
        lastRatchetSub = -1;
        refBassPhase = 0.0;
        refBassDuck = 1.0;
        tel = {};
    }

    void startVoice(const Params& p, int midiNote, int slotIndex, float velocity) noexcept DT_NONBLOCKING {
        const int choke = p.choice(ParamId::chokeMode);
        int chosen = 0;

        if (choke == static_cast<int>(ChokeMode::mono)) {
            // Choke all currently sounding voices smoothly over 1.2ms, pick quietest/oldest slot
            for (int i = 0; i < kMaxVoices; ++i) {
                if (voices[static_cast<std::size_t>(i)].active)
                    voices[static_cast<std::size_t>(i)].beginChoke(fs, 1.2);
            }
            std::uint64_t oldest = UINT64_MAX;
            for (int i = 0; i < kMaxVoices; ++i) {
                const auto& v = voices[static_cast<std::size_t>(i)];
                if (!v.active) {
                    chosen = i;
                    break;
                }
                if (v.age < oldest) {
                    oldest = v.age;
                    chosen = i;
                }
            }
        } else if (choke == static_cast<int>(ChokeMode::splitSub)) {
            // Mute sub on older voices while allowing their click/air tails to ring cleanly
            for (auto& v : voices)
                v.subActive = false;
            chosen = 0;
            std::uint64_t oldest = UINT64_MAX;
            for (int i = 0; i < kMaxVoices; ++i) {
                const auto& v = voices[static_cast<std::size_t>(i)];
                if (!v.active) {
                    chosen = i;
                    break;
                }
                if (v.age < oldest) {
                    oldest = v.age;
                    chosen = i;
                }
            }
        } else { // Poly 4
            std::uint64_t oldest = UINT64_MAX;
            for (int i = 0; i < kMaxVoices; ++i) {
                const auto& v = voices[static_cast<std::size_t>(i)];
                if (!v.active) {
                    chosen = i;
                    break;
                }
                if (v.age < oldest) {
                    oldest = v.age;
                    chosen = i;
                }
            }
        }

        ++triggerCounter;
        randSeed = randSeed * 1664525u + 1013904223u;
        voices[static_cast<std::size_t>(chosen)].trigger(fs, p, pitchCurve, pattern, midiNote, slotIndex, velocity,
                                                         triggerCounter, randSeed);

        // Start trigger-synced oscilloscope capture window
        scopeCapturing = true;
        scopeSampleIdx = 0;
        capKick.fill(0.0f);
        capSc.fill(0.0f);
        capSum.fill(0.0f);
        capPeakKick = 0.0;
        capPeakSum = 0.0;
        capSumSq = 0.0;
        capDcAcc = 0.0;
        capDot = 0.0;
        capKickSq = 0.0;
        capScSq = 0.0;
        capCount = 0;
        refBassDuck = 0.0;
        tel.lastSlot = slotIndex;
        tel.lastMidiNote = midiNote;
    }

    double shapeBodySample(double x, double envAmp, double drive, int shaperType, hba::dsp::Oversampler& os,
                           hba::dsp::Adaa2<hba::dsp::TanhShaper>& tube, hba::dsp::Adaa2<hba::dsp::HardClipShaper>& hard,
                           hba::dsp::Adaa2<hba::dsp::SineFoldShaper>& fold,
                           hba::dsp::SoftClipAdaa1& soft) noexcept DT_NONBLOCKING {
        if (drive <= 1.0e-4)
            return x;
        const double gain = 1.0 + drive * 6.5;
        const double invComp = 1.0 / (1.0 + drive * 1.45);
        const double asymAmt = 0.38 * drive;
        const double asymDc =
            (shaperType == static_cast<int>(ShaperType::asym) || shaperType > static_cast<int>(ShaperType::asym))
                ? hba::dsp::analyticAsymDcOffset(asymAmt, std::min(1.5, envAmp * gain))
                : 0.0;
        float osBuf[hba::dsp::kMaxOversampling]{};
        os.up(static_cast<float>(x * gain), osBuf);
        const int fac = os.factor();
        for (int s = 0; s < fac; ++s) {
            const double u = static_cast<double>(osBuf[s]);
            double y = u;
            switch (shaperType) {
            case static_cast<int>(ShaperType::tube):
                y = tube.process(u);
                break;
            case static_cast<int>(ShaperType::tape):
                y = soft.process(u, 0.55, 1.0);
                break;
            case static_cast<int>(ShaperType::soft):
                y = soft.process(u, 0.72, 1.0);
                break;
            case static_cast<int>(ShaperType::hard):
                y = hard.process(u);
                break;
            case static_cast<int>(ShaperType::fold):
                y = fold.process(u * 0.75);
                break;
            case static_cast<int>(ShaperType::asym):
            default:
                y = hba::dsp::analyticAsymShapeWithDc(u, asymAmt, asymDc);
                break;
            }
            osBuf[s] = static_cast<float>(y);
        }
        return static_cast<double>(os.down(osBuf)) * invComp;
    }

    // Lightweight YIN fundamental detector over scLowRing (30..140 Hz), strictly [[clang::nonblocking]]
    float estimateSidechainRootHz() const noexcept DT_NONBLOCKING {
        constexpr int kWin = 1024;
        const int minLag = std::max(8, static_cast<int>(fs / 140.0));
        const int maxLag = std::min(kScRingSize - kWin - 1, static_cast<int>(fs / 30.0));
        if (maxLag <= minLag)
            return 0.0f;

        // Check signal energy first
        double energy = 0.0;
        for (int i = 0; i < kWin; i += 4) {
            const float s = scLowRing[static_cast<std::size_t>((scRingPos - 1 - i + kScRingSize) % kScRingSize)];
            energy += static_cast<double>(s) * static_cast<double>(s);
        }
        if (energy < 1.0e-4)
            return 0.0f;

        double bestCmn = 1.0;
        int bestLag = 0;
        double runningSum = 0.0;
        bool inValley = false;
        double prevCmn = 1.0;
        double nextCmnVal = 1.0;
        for (int lag = minLag; lag <= maxLag; lag += 2) {
            double diff = 0.0;
            for (int i = 0; i < kWin; i += 4) {
                const int idxA = (scRingPos - 1 - i + kScRingSize) % kScRingSize;
                const int idxB = (idxA - lag + kScRingSize) % kScRingSize;
                const double d = static_cast<double>(scLowRing[static_cast<std::size_t>(idxA)]) -
                                 static_cast<double>(scLowRing[static_cast<std::size_t>(idxB)]);
                diff += d * d;
            }
            runningSum += diff;
            const double cmn =
                (runningSum > 1.0e-12) ? (diff * static_cast<double>((lag - minLag) / 2 + 1) / runningSum) : 1.0;
            if (cmn < bestCmn) {
                bestCmn = cmn;
                bestLag = lag;
            }
            if (cmn < 0.14 && lag > minLag + 8) {
                inValley = true;
            } else if (inValley && cmn > prevCmn) {
                nextCmnVal = cmn;
                break;
            }
            if (inValley && cmn > bestCmn + 0.01) {
                nextCmnVal = cmn;
                break;
            }
            prevCmn = cmn;
        }
        (void)nextCmnVal;
        if (bestLag <= 0 || bestCmn > 0.35)
            return 0.0f;
        return static_cast<float>(fs / static_cast<double>(bestLag));
    }

    // Queued trigger requests (supports triggerMidi/triggerSlot before process() runs with params!)
    struct PendingTrig {
        bool valid = false;
        bool isSlot = false;
        int indexOrNote = 36;
        float velocity = 0.95f;
    };
    std::array<PendingTrig, 8> pendingTrigs{};
    int pendingCount = 0;

    void enqueueTrigger(bool isSlot, int val, float vel) noexcept DT_NONBLOCKING {
        if (pendingCount < static_cast<int>(pendingTrigs.size()))
            pendingTrigs[static_cast<std::size_t>(pendingCount++)] = {true, isSlot, val, vel};
    }

    void process(const Params& p, const Transport& tr, float* left, float* right, int numSamples, const float* scL,
                 const float* scR) noexcept DT_NONBLOCKING {
        hba::dsp::ScopedFlushDenormals noDenormals;

        // Flush pending MIDI/slot triggers with current parameter snapshot
        for (int t = 0; t < pendingCount; ++t) {
            const auto& pt = pendingTrigs[static_cast<std::size_t>(t)];
            if (!pt.valid)
                continue;
            if (pt.isSlot) {
                startVoice(p, 36 + pt.indexOrNote, pt.indexOrNote, pt.velocity);
            } else {
                const int slot = (p.choice(ParamId::keyTrack) == static_cast<int>(KeyTrackMode::kit))
                                     ? std::clamp(pt.indexOrNote - 36, 0, kKitSlots - 1)
                                     : p.choice(ParamId::activeSlot);
                startVoice(p, pt.indexOrNote, slot, pt.velocity);
            }
        }
        pendingCount = 0;

        clicks.pull();
        const ClickSlot* clickPtr = &clicks.current();

        const double subLvl = static_cast<double>(p[ParamId::subLevel]);
        const double bodyLvl = static_cast<double>(p[ParamId::bodyLevel]);
        const double clickLvl = static_cast<double>(p[ParamId::clickLevel]);
        const double drive = static_cast<double>(p[ParamId::drive]);
        const double glue = static_cast<double>(p[ParamId::shaperGlue]);
        const int shaperType = p.choice(ParamId::shaperType);

        const double rotRad = static_cast<double>(p[ParamId::phaseRotate]) * (hba::dsp::kPi / 180.0);
        const double targetCos = std::cos(rotRad);
        const double targetSin = std::sin(rotRad);
        const bool useRot =
            std::abs(p[ParamId::phaseRotate]) > 0.25f && std::abs(p[ParamId::phaseRotate] - 360.0f) > 0.25f;

        const double dispAmt = static_cast<double>(p[ParamId::disperseAmt]);
        const double dispHz = static_cast<double>(p[ParamId::disperseHz]);
        if (dispAmt > 1.0e-4) {
            for (int k = 0; k < kDisperserPairs; ++k) {
                const double f = dispHz * (1.0 + 0.14 * static_cast<double>(k));
                dispL[static_cast<std::size_t>(k)].setFreq(f, fs);
                dispR[static_cast<std::size_t>(k)].setFreq(f, fs);
            }
        }

        const double outGain = hba::dsp::dbToGain(static_cast<double>(p[ParamId::outputDb]));
        const double ceilLin = hba::dsp::dbToGain(static_cast<double>(p[ParamId::ceilingDb]));
        const bool limitOn = p.flag(ParamId::limitOn);

        // Context sequencer timing
        const bool seqOn = p.flag(ParamId::seqRun);
        const int seqLen = p.choice(ParamId::seqSteps) == 1 ? 32 : 16;
        const int seqRateIdx = p.choice(ParamId::seqRate);
        const double stepsPerBeat = (seqRateIdx == 0) ? 4.0 : (seqRateIdx == 1 ? 6.0 : 8.0);
        const double bpm = std::clamp(tr.bpm > 20.0 ? tr.bpm : 172.0, 40.0, 260.0);
        const double ppqPerSample = (bpm / 60.0) / fs;
        const double swing = static_cast<double>(p[ParamId::seqSwing]);
        const int refMode = p.choice(ParamId::refBassMode);

        const auto scLpCoefs = hba::dsp::SvfCoefs::make(180.0, 0.707, fs);
        const double refDuckCoef = 1.0 - std::exp(-1.0 / (0.045 * fs));

        float blockPeakL = 0.0f;
        float blockPeakR = 0.0f;
        float blockGrDb = 0.0f;
        double blockTruePeak = 0.0;
        double scEnergy = 0.0;

        for (int i = 0; i < numSamples; ++i) {
            // Advance context sequencer
            const double curPpq = tr.playing ? (tr.ppq + ppqPerSample * static_cast<double>(i)) : internalPpq;
            if (!tr.playing)
                internalPpq += ppqPerSample;

            if (seqOn) {
                const double rawStep = std::fmod(std::max(0.0, curPpq) * stepsPerBeat, static_cast<double>(seqLen));
                // Apply swing delay to odd steps
                const int baseIdx = static_cast<int>(std::floor(rawStep)) % seqLen;
                double frac = rawStep - std::floor(rawStep);
                int trigStep = baseIdx;
                if ((baseIdx & 1) == 1) {
                    if (frac < swing)
                        trigStep = -1; // waiting for swung onset
                    else
                        frac = (frac - swing) / std::max(0.05, 1.0 - swing);
                }

                if (trigStep >= 0) {
                    const auto& st = pattern.steps[static_cast<std::size_t>(trigStep)];
                    const int ratchet = std::clamp<int>(st.ratchet, 1, 3);
                    const int subIdx = std::min(ratchet - 1, static_cast<int>(frac * static_cast<double>(ratchet)));
                    if (trigStep != lastSeqStep || subIdx != lastRatchetSub) {
                        lastSeqStep = trigStep;
                        lastRatchetSub = subIdx;
                        tel.currentSeqStep = trigStep;
                        if (st.velocity > 0) {
                            const float rVel = static_cast<float>(st.velocity) / 127.0f *
                                               (subIdx == 0 ? 1.0f : (0.82f - 0.08f * static_cast<float>(subIdx)));
                            startVoice(p, 36 + static_cast<int>(st.slot), static_cast<int>(st.slot), rVel);
                        }
                    }
                }
            } else {
                tel.currentSeqStep = -1;
            }

            // Sum all active voices in native Sub / Body / Click bands
            double sumSub = 0.0;
            double sumBody = 0.0;
            double sumClickL = 0.0;
            double sumClickR = 0.0;
            double maxEnvAmp = 0.0;
            double activeDriveScale = 1.0;

            for (auto& v : voices) {
                if (!v.active)
                    continue;
                const auto b = v.renderSample(fs, p, pitchCurve, ampCurve, clickPtr);
                sumSub += b.sub;
                sumBody += b.body;
                sumClickL += b.clickL;
                sumClickR += b.clickR;
                if (b.envAmp > maxEnvAmp) {
                    maxEnvAmp = b.envAmp;
                    activeDriveScale = v.driveScale;
                }
            }

            const double subDry = sumSub * subLvl;
            const double bodyDry = sumBody * bodyLvl;
            const double effDrive = std::clamp(drive * activeDriveScale, 0.0, 1.0);

            // Phase-Native 3-Band Shaping: mono Body (plus optional Glue share of Sub) through oversampled ADAA2!
            const double intoShaper = bodyDry + subDry * glue;
            const double shaped = shapeBodySample(intoShaper, maxEnvAmp, effDrive, shaperType, osBody, tubeBody,
                                                  hardBody, foldBody, softClipBody);

            double outL = subDry * (1.0 - glue) + shaped + sumClickL * clickLvl;
            double outR = subDry * (1.0 - glue) + shaped + sumClickR * clickLvl;

            // Smooth Orchard/Bedrosian PhaseRotator (0..360 deg)
            phaseCos += (targetCos - phaseCos) * 0.01;
            phaseSin += (targetSin - phaseSin) * 0.01;
            if (useRot) {
                outL = rotL.processCosSin(outL, phaseCos, phaseSin);
                outR = rotR.processCosSin(outR, phaseCos, phaseSin);
            }

            // Allpass Disperser (flat magnitude!)
            if (dispAmt > 1.0e-4) {
                double dL = outL;
                double dR = outR;
                for (int k = 0; k < kDisperserPairs; ++k) {
                    dL = dispL[static_cast<std::size_t>(k)].process(dL);
                    dR = dispR[static_cast<std::size_t>(k)].process(dR);
                }
                outL = outL * (1.0 - dispAmt) + dL * dispAmt;
                outR = outR * (1.0 - dispAmt) + dR * dispAmt;
            }

            // Output gain + Zero-Latency C^2 Safety Limiter
            outL *= outGain;
            outR *= outGain;
            if (limitOn) {
                const double g = hba::dsp::softClipLinked(outL, outR, 0.85 * ceilLin, ceilLin);
                if (g < 0.9999)
                    blockGrDb = std::max(blockGrDb, static_cast<float>(-hba::dsp::gainToDb(std::max(1.0e-4, g))));
            } else {
                hba::dsp::softClipLinked(outL, outR, 0.92, 1.15);
            }

            // True-peak & level metering on kick bus (before optional reference bass)
            blockTruePeak = std::max(blockTruePeak, truePeak.process(outL, outR));

            // Sidechain capture (Sub-Link)
            double scSample = 0.0;
            if (scL != nullptr && scR != nullptr) {
                scSample = 0.5 * (static_cast<double>(scL[i]) + static_cast<double>(scR[i]));
                const double scLow = scLowpass.lowpass(scLpCoefs, scSample);
                scLowRing[static_cast<std::size_t>(scRingPos)] = static_cast<float>(scLow);
                scRingPos = (scRingPos + 1) % kScRingSize;
                scEnergy += scSample * scSample;
            }

            // Trigger-synced 256-point oscilloscope capture (Kick, Sidechain, Sum)
            if (scopeCapturing) {
                const double kickMono = 0.5 * (outL + outR);
                const double sumMono = kickMono + scSample;
                const int bin =
                    std::min(kScopePoints - 1, (scopeSampleIdx * kScopePoints) / std::max(1, scopeWindowSamples));
                // Keep signed peak sample per bin for crisp waveform rendering
                if (std::abs(kickMono) >= std::abs(capKick[static_cast<std::size_t>(bin)]))
                    capKick[static_cast<std::size_t>(bin)] = static_cast<float>(kickMono);
                if (std::abs(scSample) >= std::abs(capSc[static_cast<std::size_t>(bin)]))
                    capSc[static_cast<std::size_t>(bin)] = static_cast<float>(scSample);
                if (std::abs(sumMono) >= std::abs(capSum[static_cast<std::size_t>(bin)]))
                    capSum[static_cast<std::size_t>(bin)] = static_cast<float>(sumMono);

                capPeakKick = std::max(capPeakKick, std::abs(kickMono));
                capPeakSum = std::max(capPeakSum, std::abs(sumMono));
                capSumSq += kickMono * kickMono;
                capDcAcc += kickMono;
                capDot += kickMono * scSample;
                capKickSq += kickMono * kickMono;
                capScSq += scSample * scSample;
                ++capCount;
                ++scopeSampleIdx;

                if (scopeSampleIdx >= scopeWindowSamples) {
                    scopeCapturing = false;
                    tel.kickScope = capKick;
                    tel.scScope = capSc;
                    tel.sumScope = capSum;
                    const double rms = std::sqrt(capSumSq / std::max(1, capCount));
                    tel.rmsDb = static_cast<float>(hba::dsp::gainToDb(std::max(1.0e-4, rms)));
                    tel.crestDb = (capPeakKick > 1.0e-4 && rms > 1.0e-4)
                                      ? static_cast<float>(hba::dsp::gainToDb(capPeakKick / rms))
                                      : 0.0f;
                    tel.dcPercent = static_cast<float>(std::abs(capDcAcc / std::max(1, capCount)) * 100.0);
                    if (capKickSq > 1.0e-6 && capScSq > 1.0e-6) {
                        tel.scCorrelation =
                            static_cast<float>(std::clamp(capDot / std::sqrt(capKickSq * capScSq), -1.0, 1.0));
                        tel.scPeakDeltaDb = static_cast<float>(
                            hba::dsp::gainToDb(std::max(1.0e-4, capPeakSum) / std::max(1.0e-4, capPeakKick)));
                    }
                }
            }

            // Optional built-in Reference Sub-Bass audition loop (locked to kick Tune Hz!)
            if (refMode != static_cast<int>(RefBassMode::off) && (seqOn || tr.playing)) {
                const double refHz = static_cast<double>(p[ParamId::tuneHz]);
                refBassPhase += hba::dsp::kTwoPi * refHz / fs;
                if (refBassPhase >= hba::dsp::kTwoPi)
                    refBassPhase -= hba::dsp::kTwoPi;

                // Clean ducking envelope recovering over ~65 ms after kick trigger
                refBassDuck += (1.0 - refBassDuck) * refDuckCoef;
                double gate = 1.0;
                const double beatFrac = std::fmod(std::max(0.0, curPpq), 1.0);
                if (refMode == static_cast<int>(RefBassMode::tech132))
                    gate = (beatFrac > 0.35 && beatFrac < 0.92) ? 1.0 : 0.15;
                else if (refMode == static_cast<int>(RefBassMode::half140))
                    gate = (std::fmod(std::max(0.0, curPpq), 4.0) > 0.4) ? 0.9 : 0.1;

                const double refSub =
                    (0.75 * std::sin(refBassPhase) + 0.14 * std::sin(2.0 * refBassPhase)) * 0.34 * refBassDuck * gate;
                outL = hba::dsp::softClip(outL + refSub, 0.90, 1.0);
                outR = hba::dsp::softClip(outR + refSub, 0.90, 1.0);
            }

            left[i] = static_cast<float>(outL);
            right[i] = static_cast<float>(outR);
            blockPeakL = std::max(blockPeakL, std::abs(left[i]));
            blockPeakR = std::max(blockPeakR, std::abs(right[i]));
        }

        // Update block-level telemetry
        const float blockTpDb = static_cast<float>(hba::dsp::gainToDb(std::max(1.0e-4, blockTruePeak)));
        tel.truePeakDb = std::max(blockTpDb, tel.truePeakDb - 0.002f * static_cast<float>(numSamples));
        int activeCount = 0;
        for (const auto& v : voices)
            if (v.active)
                ++activeCount;
        tel.activeVoices = activeCount;
        tel.sounding = activeCount > 0;
        tel.peakL = std::max(blockPeakL, tel.peakL * 0.85f);
        tel.peakR = std::max(blockPeakR, tel.peakR * 0.85f);
        tel.limiterGrDb = std::max(blockGrDb, tel.limiterGrDb * 0.82f);

        const bool hasScAudio = scL != nullptr && (scEnergy / std::max(1, numSamples) > 1.0e-7);
        tel.scActive = hasScAudio;
        if (hasScAudio)
            tel.scRootHz = estimateSidechainRootHz();
    }
};

Engine::Engine() : impl_(std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::prepare(double sampleRate, int maxBlockSize, int oversamplingFactor) {
    impl_->prepare(sampleRate, maxBlockSize, oversamplingFactor);
}

void Engine::reset() noexcept DT_NONBLOCKING { impl_->reset(); }

void Engine::triggerMidi(int midiNote, float velocity) noexcept DT_NONBLOCKING {
    impl_->enqueueTrigger(false, std::clamp(midiNote, 0, 127), velocity);
}

void Engine::triggerSlot(int slotIndex, float velocity) noexcept DT_NONBLOCKING {
    impl_->enqueueTrigger(true, std::clamp(slotIndex, 0, kKitSlots - 1), velocity);
}

void Engine::noteOff(int) noexcept DT_NONBLOCKING {
    // Kicks complete their natural analytical zero-crossing envelope cleanly.
}

void Engine::allNotesOff() noexcept DT_NONBLOCKING {
    for (auto& v : impl_->voices)
        v.beginChoke(impl_->fs, 2.0);
}

void Engine::setCurves(const hba::dsp::EnvelopeCurve& pitchCurve,
                       const hba::dsp::EnvelopeCurve& ampCurve) noexcept DT_NONBLOCKING {
    impl_->pitchCurve = pitchCurve;
    impl_->ampCurve = ampCurve;
}

void Engine::setPattern(const ComposePattern& pattern) noexcept DT_NONBLOCKING { impl_->pattern = pattern; }

void Engine::publishClickSample(const ClickSample& sample) {
    if (!sample.valid()) {
        clearClickSample();
        return;
    }
    auto& slot = impl_->clicks.writable();
    const int n = std::min(ClickSample::kMaxSamples, static_cast<int>(sample.mono.size()));
    std::copy_n(sample.mono.data(), n, slot.mono.data());
    slot.length = n;
    slot.sampleRate = sample.sampleRate > 8000.0 ? sample.sampleRate : 48000.0;
    slot.offsetSamples = sample.offsetSamples;
    slot.polarity = sample.polarity >= 0.0f ? 1.0f : -1.0f;
    impl_->clicks.publish();
}

void Engine::clearClickSample() noexcept DT_NONBLOCKING {
    impl_->clicks.writable().length = 0;
    impl_->clicks.publish();
}

void Engine::process(const Params& params, const Transport& transport, float* left, float* right, int numSamples,
                     const float* sidechainL, const float* sidechainR) noexcept DT_NONBLOCKING {
    impl_->process(params, transport, left, right, numSamples, sidechainL, sidechainR);
}

int Engine::latencySamples() const noexcept DT_NONBLOCKING { return 0; }
int Engine::oversamplingFactor() const noexcept DT_NONBLOCKING { return impl_->osFactor; }
const TelemetrySnapshot& Engine::telemetry() const noexcept DT_NONBLOCKING { return impl_->tel; }

PreviewSnapshot Engine::renderPreview(const Params& params, const hba::dsp::EnvelopeCurve& pitchCurve,
                                      const hba::dsp::EnvelopeCurve& ampCurve, const ComposePattern& pattern,
                                      const ClickSample* click, int slotIndex, double sampleRate) {
    PreviewSnapshot snap{};
    const double fs = std::max(22050.0, sampleRate);

    const Params p = previewParameters(params);

    ClickSlot clickSlot{};
    const ClickSlot* clickPtr = nullptr;
    if (click != nullptr && click->valid()) {
        const int n = std::min(ClickSample::kMaxSamples, static_cast<int>(click->mono.size()));
        std::copy_n(click->mono.data(), n, clickSlot.mono.data());
        clickSlot.length = n;
        clickSlot.sampleRate = click->sampleRate;
        clickSlot.offsetSamples = click->offsetSamples;
        clickSlot.polarity = click->polarity;
        clickPtr = &clickSlot;
    }

    Voice voice;
    voice.trigger(fs, p, pitchCurve, pattern, 36 + slotIndex, slotIndex, 0.96f, 1, 0x51A7E123u);
    snap.effectiveLengthMs = static_cast<float>(voice.lengthSec * 1000.0);
    snap.zeroCrossNorm = p.flag(ParamId::zeroCross) ? 1.0f : 0.0f;

    const int totalSamples = std::max(kScopePoints, static_cast<int>(std::ceil(voice.lengthSec * fs)) + 64);

    hba::dsp::Oversampler os;
    os.prepare(4);
    hba::dsp::Adaa2<hba::dsp::TanhShaper> tube;
    hba::dsp::Adaa2<hba::dsp::HardClipShaper> hard;
    hba::dsp::Adaa2<hba::dsp::SineFoldShaper> fold;
    hba::dsp::SoftClipAdaa1 soft;
    hba::dsp::PhaseRotator rot;
    rot.prepare(fs);
    std::array<hba::dsp::AllpassPair, kDisperserPairs> disp{};
    const double dispAmt = static_cast<double>(p[ParamId::disperseAmt]);
    const double dispHz = static_cast<double>(p[ParamId::disperseHz]);
    for (int k = 0; k < kDisperserPairs; ++k)
        disp[static_cast<std::size_t>(k)].setFreq(dispHz * (1.0 + 0.14 * static_cast<double>(k)), fs);
    hba::dsp::TruePeakDetector tpDet;

    const double subLvl = static_cast<double>(p[ParamId::subLevel]);
    const double bodyLvl = static_cast<double>(p[ParamId::bodyLevel]);
    const double clickLvl = static_cast<double>(p[ParamId::clickLevel]);
    const double drive = std::clamp(static_cast<double>(p[ParamId::drive]) * voice.driveScale, 0.0, 1.0);
    const double glue = static_cast<double>(p[ParamId::shaperGlue]);
    const int shaperType = p.choice(ParamId::shaperType);
    const bool useRot = std::abs(p[ParamId::phaseRotate]) > 0.25f && std::abs(p[ParamId::phaseRotate] - 360.0f) > 0.25f;
    const double outGain = hba::dsp::dbToGain(static_cast<double>(p[ParamId::outputDb]));
    const double ceilLin = hba::dsp::dbToGain(static_cast<double>(p[ParamId::ceilingDb]));
    const bool limitOn = p.flag(ParamId::limitOn);

    double peak = 0.0;
    double truePk = 0.0;
    double sumSq = 0.0;
    double dcSum = 0.0;

    for (int i = 0; i < totalSamples; ++i) {
        const auto b = voice.renderSample(fs, p, pitchCurve, ampCurve, clickPtr);
        const double subDry = b.sub * subLvl;
        const double bodyDry = b.body * bodyLvl;
        double shaped = bodyDry + subDry * glue;
        if (drive > 1.0e-4) {
            const double gain = 1.0 + drive * 6.5;
            const double invComp = 1.0 / (1.0 + drive * 1.45);
            const double asymAmt = 0.38 * drive;
            const double asymDc =
                (shaperType == static_cast<int>(ShaperType::asym) || shaperType > static_cast<int>(ShaperType::asym))
                    ? hba::dsp::analyticAsymDcOffset(asymAmt, std::min(1.5, b.envAmp * gain))
                    : 0.0;
            float buf[4]{};
            os.up(static_cast<float>(shaped * gain), buf);
            for (int s = 0; s < 4; ++s) {
                const double u = static_cast<double>(buf[s]);
                double y = u;
                switch (shaperType) {
                case static_cast<int>(ShaperType::tube):
                    y = tube.process(u);
                    break;
                case static_cast<int>(ShaperType::tape):
                    y = soft.process(u, 0.55, 1.0);
                    break;
                case static_cast<int>(ShaperType::soft):
                    y = soft.process(u, 0.72, 1.0);
                    break;
                case static_cast<int>(ShaperType::hard):
                    y = hard.process(u);
                    break;
                case static_cast<int>(ShaperType::fold):
                    y = fold.process(u * 0.75);
                    break;
                case static_cast<int>(ShaperType::asym):
                default:
                    y = hba::dsp::analyticAsymShapeWithDc(u, asymAmt, asymDc);
                    break;
                }
                buf[s] = static_cast<float>(y);
            }
            shaped = static_cast<double>(os.down(buf)) * invComp;
        }
        const double clk = 0.5 * (b.clickL + b.clickR) * clickLvl;
        double sum = subDry * (1.0 - glue) + shaped + clk;
        if (useRot)
            sum = rot.process(sum, static_cast<double>(p[ParamId::phaseRotate]));
        if (dispAmt > 1.0e-4) {
            double d = sum;
            for (int k = 0; k < kDisperserPairs; ++k)
                d = disp[static_cast<std::size_t>(k)].process(d);
            sum = sum * (1.0 - dispAmt) + d * dispAmt;
        }
        sum *= outGain;
        if (limitOn)
            sum = hba::dsp::softClip(sum, 0.85 * ceilLin, ceilLin);
        else
            sum = hba::dsp::softClip(sum, 0.92, 1.15);

        truePk = std::max(truePk, tpDet.process(sum, sum));
        peak = std::max(peak, std::abs(sum));
        sumSq += sum * sum;
        dcSum += sum;

        const int bin = std::min(kScopePoints - 1, (i * kScopePoints) / totalSamples);
        const auto bi = static_cast<std::size_t>(bin);
        const double subScaled = subDry * outGain;
        const double bodyScaled = shaped * outGain;
        const double clkScaled = clk * outGain;
        if (std::abs(subScaled) >= std::abs(snap.subWave[bi]))
            snap.subWave[bi] = static_cast<float>(subScaled);
        if (std::abs(bodyScaled) >= std::abs(snap.bodyWave[bi]))
            snap.bodyWave[bi] = static_cast<float>(bodyScaled);
        if (std::abs(clkScaled) >= std::abs(snap.clickWave[bi]))
            snap.clickWave[bi] = static_cast<float>(clkScaled);
        if (std::abs(sum) >= std::abs(snap.sumWave[bi]))
            snap.sumWave[bi] = static_cast<float>(sum);
    }

    for (int k = 0; k < kScopePoints; ++k) {
        const double u = static_cast<double>(k) / static_cast<double>(kScopePoints - 1);
        snap.pitchNorm[static_cast<std::size_t>(k)] = static_cast<float>(hba::dsp::evaluateEnvelope(pitchCurve, u));
    }

    const double rms = std::sqrt(sumSq / std::max(1, totalSamples));
    snap.truePeakDb = static_cast<float>(hba::dsp::gainToDb(std::max(1.0e-4, truePk)));
    snap.rmsDb = static_cast<float>(hba::dsp::gainToDb(std::max(1.0e-4, rms)));
    snap.crestDb = (peak > 1.0e-4 && rms > 1.0e-4) ? static_cast<float>(hba::dsp::gainToDb(peak / rms)) : 0.0f;
    snap.dcPercent = static_cast<float>(std::abs(dcSum / std::max(1, totalSamples)) * 100.0);
    return snap;
}

float Engine::solveSubLinkPhase(const Params& params, const hba::dsp::EnvelopeCurve& pitchCurve,
                                const hba::dsp::EnvelopeCurve& ampCurve, const ComposePattern& pattern,
                                const std::array<float, kScopePoints>& sidechainScope, float* outBestCorr) {
    double scNormSq = 0.0;
    for (float s : sidechainScope)
        scNormSq += static_cast<double>(s) * static_cast<double>(s);
    if (scNormSq < 1.0e-6) {
        if (outBestCorr)
            *outBestCorr = 0.0f;
        return params[ParamId::startPhase];
    }

    // Match the exact 180 ms capture window of TelemetrySnapshot::scScope so time axes align 1:1!
    constexpr double kFs = 24000.0;
    constexpr int kWinSamples = static_cast<int>(kFs * 0.180);
    float bestAngle = params[ParamId::startPhase];
    double bestCorr = -2.0;

    auto evalAngle = [&](float deg) -> double {
        Params trial = params;
        trial[ParamId::startPhase] = std::fmod(deg + 360.0f, 360.0f);
        trial[ParamId::seqRun] = 0.0f;
        trial[ParamId::refBassMode] = 0.0f;
        trial[ParamId::humanize] = 0.0f;

        Engine eng;
        eng.prepare(kFs, 256, 2);
        eng.setCurves(pitchCurve, ampCurve);
        eng.setPattern(pattern);
        eng.triggerSlot(0, 0.95f);

        std::array<float, kScopePoints> kBins{};
        std::array<float, 256> bufL{}, bufR{};
        for (int pos = 0; pos < kWinSamples; pos += 256) {
            const int n = std::min(256, kWinSamples - pos);
            eng.process(trial, {172.0, 0.0, false}, bufL.data(), bufR.data(), n);
            for (int i = 0; i < n; ++i) {
                const int bin = std::min(kScopePoints - 1, ((pos + i) * kScopePoints) / kWinSamples);
                const float m = 0.5f * (bufL[static_cast<std::size_t>(i)] + bufR[static_cast<std::size_t>(i)]);
                if (std::abs(m) >= std::abs(kBins[static_cast<std::size_t>(bin)]))
                    kBins[static_cast<std::size_t>(bin)] = m;
            }
        }

        double dot = 0.0;
        double kSq = 0.0;
        double sSq = 0.0;
        // Weight strictly over the sub-body overlap zone (after initial 20 ms pitch chirp, where kick is audible!)
        constexpr std::size_t kSubStartBin = kScopePoints / 8; // ~22 ms
        for (std::size_t i = kSubStartBin; i < kScopePoints; ++i) {
            const double k = static_cast<double>(kBins[i]);
            if (std::abs(k) < 0.04)
                continue;
            const double s = static_cast<double>(sidechainScope[i]);
            dot += k * s;
            kSq += k * k;
            sSq += s * s;
        }
        return (kSq > 1.0e-8 && sSq > 1.0e-8) ? (dot / std::sqrt(kSq * sSq)) : -1.0;
    };

    for (int a = 0; a < 360; a += 15) {
        const double c = evalAngle(static_cast<float>(a));
        if (c > bestCorr) {
            bestCorr = c;
            bestAngle = static_cast<float>(a);
        }
    }
    const float coarse = bestAngle;
    for (int d = -14; d <= 14; d += 2) {
        const float ang = std::fmod(coarse + static_cast<float>(d) + 360.0f, 360.0f);
        const double c = evalAngle(ang);
        if (c > bestCorr) {
            bestCorr = c;
            bestAngle = ang;
        }
    }
    if (outBestCorr)
        *outBestCorr = static_cast<float>(bestCorr);
    return bestAngle;
}

void Engine::autoAlignClick(const Params& params, const hba::dsp::EnvelopeCurve& pitchCurve,
                            const hba::dsp::EnvelopeCurve& ampCurve, ClickSample& sample) {
    if (!sample.valid())
        return;
    // Locate click sample's main peak within first 20 ms
    const int searchN =
        std::min<int>(static_cast<int>(sample.mono.size()), static_cast<int>(sample.sampleRate * 0.020));
    int clickPeakIdx = 0;
    float clickPeakVal = 0.0f;
    for (int i = 0; i < searchN; ++i) {
        if (std::abs(sample.mono[static_cast<std::size_t>(i)]) > std::abs(clickPeakVal)) {
            clickPeakVal = sample.mono[static_cast<std::size_t>(i)];
            clickPeakIdx = i;
        }
    }

    // Measure core's first peak polarity & time within first 8 ms
    Params coreOnly = params;
    coreOnly[ParamId::clickAmt] = 0.0f;
    const auto pv = renderPreview(coreOnly, pitchCurve, ampCurve, defaultComposePattern(), nullptr, 0, 48000.0);
    float corePeakVal = 0.0f;
    for (int i = 0; i < 32; ++i) {
        if (std::abs(pv.sumWave[static_cast<std::size_t>(i)]) > std::abs(corePeakVal))
            corePeakVal = pv.sumWave[static_cast<std::size_t>(i)];
    }
    sample.offsetSamples = std::max(0, clickPeakIdx - 4);
    sample.polarity = (corePeakVal * clickPeakVal < 0.0f) ? -1.0f : 1.0f;
}

void Engine::renderOffline(const Params& params, const hba::dsp::EnvelopeCurve& pitchCurve,
                           const hba::dsp::EnvelopeCurve& ampCurve, const ComposePattern& pattern,
                           const ClickSample* click, double sampleRate, double bpm, bool renderPatternLoop,
                           int slotIndex, std::vector<float>& outLeft, std::vector<float>& outRight) {
    const double fs = std::max(22050.0, sampleRate);
    Engine eng;
    eng.prepare(fs, 256, 16);
    eng.setCurves(pitchCurve, ampCurve);
    eng.setPattern(pattern);
    if (click != nullptr && click->valid())
        eng.publishClickSample(*click);

    Params p = params;
    p[ParamId::humanize] = 0.0f;
    p[ParamId::refBassMode] = 0.0f;

    int totalFrames = 0;
    if (renderPatternLoop) {
        p[ParamId::seqRun] = 1.0f;
        const int steps = p.choice(ParamId::seqSteps) == 1 ? 32 : 16;
        const int rateIdx = p.choice(ParamId::seqRate);
        const double stepsPerBeat = (rateIdx == 0) ? 4.0 : (rateIdx == 1 ? 6.0 : 8.0);
        const double beats = static_cast<double>(steps) / stepsPerBeat;
        const double sec = beats * (60.0 / std::clamp(bpm, 40.0, 260.0));
        totalFrames = std::max(4096, static_cast<int>(std::round(sec * fs)));
    } else {
        p[ParamId::seqRun] = 0.0f;
        const auto pv = renderPreview(p, pitchCurve, ampCurve, pattern, click, slotIndex, fs);
        const double tailSec = static_cast<double>(pv.effectiveLengthMs) * 0.001 + 0.045;
        totalFrames = std::max(2048, static_cast<int>(std::ceil(tailSec * fs)));
        eng.triggerSlot(slotIndex, 0.96f);
    }

    outLeft.assign(static_cast<std::size_t>(totalFrames), 0.0f);
    outRight.assign(static_cast<std::size_t>(totalFrames), 0.0f);
    const double ppqPerSample = (std::clamp(bpm, 40.0, 260.0) / 60.0) / fs;

    for (int pos = 0; pos < totalFrames; pos += 256) {
        const int n = std::min(256, totalFrames - pos);
        Transport tr{bpm, ppqPerSample * static_cast<double>(pos), renderPatternLoop};
        eng.process(p, tr, outLeft.data() + pos, outRight.data() + pos, n);
    }
}

std::vector<std::uint8_t> Engine::encodePatternMidi(const Params& params, const ComposePattern& pattern, double bpm) {
    constexpr int kPpq = 960;
    const int steps = params.choice(ParamId::seqSteps) == 1 ? 32 : 16;
    const int rateIdx = params.choice(ParamId::seqRate);
    const double stepsPerBeat = (rateIdx == 0) ? 4.0 : (rateIdx == 1 ? 6.0 : 8.0);
    const double ticksPerStep = static_cast<double>(kPpq) / stepsPerBeat;
    const double swing = static_cast<double>(params[ParamId::seqSwing]);

    std::vector<hba::core::SmfEvent> events;
    events.reserve(static_cast<std::size_t>(steps) * 4u);

    for (int i = 0; i < steps; ++i) {
        const auto& st = pattern.steps[static_cast<std::size_t>(i)];
        if (st.velocity == 0)
            continue;
        const double swingTicks = ((i & 1) == 1) ? (swing * ticksPerStep) : 0.0;
        const int ratchet = std::clamp<int>(st.ratchet, 1, 3);
        const double subStepTicks = ticksPerStep / static_cast<double>(ratchet);
        const std::uint8_t note = static_cast<std::uint8_t>(36 + std::clamp<int>(st.slot, 0, kKitSlots - 1));

        for (int r = 0; r < ratchet; ++r) {
            const auto startTick =
                static_cast<std::uint32_t>(std::max(0.0, std::round(static_cast<double>(i) * ticksPerStep + swingTicks +
                                                                    static_cast<double>(r) * subStepTicks)));
            const auto durTick = static_cast<std::uint32_t>(std::max(24.0, std::round(subStepTicks * 0.6)));
            const auto vel = static_cast<std::uint8_t>(std::clamp(
                static_cast<int>(std::round(static_cast<float>(st.velocity) * (r == 0 ? 1.0f : 0.82f))), 1, 127));
            events.push_back(hba::core::SmfEvent::msg(startTick, 1, 0x90u, note, vel));
            events.push_back(hba::core::SmfEvent::msg(startTick + durTick, 0, 0x80u, note, 0u));
        }
    }

    const auto totalTicks = static_cast<std::uint32_t>(std::round(static_cast<double>(steps) * ticksPerStep));
    return hba::core::encodeSmfType0(std::move(events), static_cast<std::uint16_t>(kPpq), bpm, totalTicks);
}

} // namespace dt
