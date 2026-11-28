// SPDX-License-Identifier: AGPL-3.0-or-later
#include "engine_helpers.h"
#include "sb/engine.h"

#include <hba/dsp/denormals.h>
#include <hba/dsp/gain.h>

#include <algorithm>
#include <cmath>

namespace sb {

namespace {
using detail::ArtProfile;
using detail::profileFor;
using detail::semitoneRatio;
using detail::tightDecayScale;

inline hba::dsp::ShaperModel shaperFromChoice(int idx) noexcept HBA_NONBLOCKING {
    switch (std::clamp(idx, 0, 4)) {
    case 0:
        return hba::dsp::ShaperModel::clip;
    case 1:
        return hba::dsp::ShaperModel::tube;
    case 2:
        return hba::dsp::ShaperModel::buchla;
    case 3:
        return hba::dsp::ShaperModel::fold;
    case 4:
        return hba::dsp::ShaperModel::sine;
    }
    return hba::dsp::ShaperModel::clip;
}

inline double bodyShape(BodyWave wave, double theta) noexcept HBA_NONBLOCKING {
    const double s1 = std::sin(theta);
    switch (wave) {
    case BodyWave::pureSine:
        return s1;
    case BodyWave::warm909:
        return (s1 + 0.24 * std::sin(2.0 * theta) + 0.08 * std::sin(3.0 * theta)) * 0.84;
    case BodyWave::triPunch:
        return (s1 + 0.19 * std::sin(3.0 * theta) - 0.06 * std::sin(5.0 * theta)) * 0.90;
    case BodyWave::subSquare:
        return std::tanh(2.35 * s1) * 1.018;
    }
    return s1;
}
} // namespace

void Engine::prepare(double sampleRate, int maxBlockSize, int oversamplingFactor) {
    sampleRate_ = sampleRate > 8000.0 ? sampleRate : 48000.0;
    maxBlock_ = std::max(1, maxBlockSize);
    oversamplingFactor_ =
        (oversamplingFactor == 2 || oversamplingFactor == 4 || oversamplingFactor == 8) ? oversamplingFactor : 4;

    oversamplerL_.prepare(oversamplingFactor_);
    oversamplerR_.prepare(oversamplingFactor_);
    dcL_.prepare(22.0, sampleRate_);
    dcR_.prepare(22.0, sampleRate_);
    limiter_.prepare(sampleRate_);
    room_.prepare(sampleRate_);

    scopeTotalSamples_ = std::max(kScopeSamples, static_cast<int>(std::lround(0.040 * sampleRate_)));
    lockWindowSamples_ = std::max(64, static_cast<int>(std::lround(0.014 * sampleRate_)));
    reset();
}

void Engine::reset() noexcept HBA_NONBLOCKING {
    triggerSeq_ = 0;
    for (auto& v : voices_)
        v = {};
    delBodyL_.reset();
    delBodyR_.reset();
    delShellL_.reset();
    delShellR_.reset();
    delWireL_.reset();
    delWireR_.reset();
    bodyDelSmooth_.reset();
    shellDelSmooth_.reset();
    wireDelSmooth_.reset();
    scopeLatBody_.reset();
    scopeLatShell_.reset();
    scopeLatWire_.reset();
    for (auto& ap : dispL_)
        ap.reset();
    for (auto& ap : dispR_)
        ap.reset();
    scoopBellL_.reset();
    scoopBellR_.reset();
    scoopShelfL_.reset();
    scoopShelfR_.reset();
    room_.reset();
    oversamplerL_.reset();
    oversamplerR_.reset();
    shaperL_.reset();
    shaperR_.reset();
    subSplitL_.reset();
    subSplitR_.reset();
    dcL_.reset();
    dcR_.reset();
    limiter_.reset();
    outGainSmooth_.reset();
    driveSmooth_.reset();

    capturingScope_ = false;
    scopeCaptureSample_ = 0;
    scopeRmsSum_ = 0.0;
    scopePeakAbs_ = 0.0;
    lockBandBodySq_ = 0.0;
    lockBandOtherSq_ = 0.0;
    lockBandSumSq_ = 0.0;
    lockLpBody_ = 0.0;
    lockLpOther_ = 0.0;
    capBody_.fill(0.0f);
    capShell_.fill(0.0f);
    capWire_.fill(0.0f);
    capSum_.fill(0.0f);
    telemetry_ = {};
}

int Engine::latencySamples() const noexcept HBA_NONBLOCKING {
    return limiter_.latency() + oversamplerL_.latencySamples();
}

void Engine::process(const Params& params, float* outL, float* outR, int numSamples) noexcept HBA_NONBLOCKING {
    hba::dsp::ScopedFlushDenormals flushDenormals;
    cachedParams_ = params;
    havePendingParams_ = true;

    if (numSamples <= 0 || outL == nullptr || outR == nullptr)
        return;

    if (params.flag(ParamId::bypass)) {
        for (int i = 0; i < numSamples; ++i) {
            outL[i] = 0.0f;
            outR[i] = 0.0f;
        }
        return;
    }

    const double sr = sampleRate_;
    const double invSr = 1.0 / sr;

    // Performance Macros (non-destructive musical modifiers)
    const double mPunch = std::clamp(static_cast<double>(params.real(ParamId::macroPunch)) * 0.01, 0.0, 1.0);
    const double mTight = std::clamp(static_cast<double>(params.real(ParamId::macroTight)) * 0.01, -1.0, 1.0);
    const double mMetal = std::clamp(static_cast<double>(params.real(ParamId::macroMetal)) * 0.01, 0.0, 1.0);
    const double mSmack = std::clamp(static_cast<double>(params.real(ParamId::macroSmack)) * 0.01, 0.0, 1.0);
    const double decayScale = tightDecayScale(mTight);

    // Layer Mute / Solo resolution
    const auto solo = static_cast<LayerSolo>(std::clamp(params.choice(ParamId::layerSolo), 0, 3));
    const bool bodyOn = (solo == LayerSolo::none) ? !params.flag(ParamId::bodyMute) : (solo == LayerSolo::body);
    const bool shellOn = (solo == LayerSolo::none) ? !params.flag(ParamId::shellMute) : (solo == LayerSolo::shell);
    const bool wireOn = (solo == LayerSolo::none) ? !params.flag(ParamId::wireMute) : (solo == LayerSolo::wire);

    // Extract block parameters + macro modulation
    const double baseBodyHz = std::clamp(static_cast<double>(params.real(ParamId::bodyHz)), 32.0, 360.0);
    const auto bodyWave = static_cast<BodyWave>(std::clamp(params.choice(ParamId::bodyWave), 0, 3));
    const double pitchDropSt =
        std::clamp(static_cast<double>(params.real(ParamId::pitchDrop)) + mPunch * 6.0, 0.0, 36.0);
    const double pitchCurveExp =
        1.0 + 2.4 * std::clamp(static_cast<double>(params.real(ParamId::pitchCurve)) * 0.01 + mPunch * 0.25, 0.0, 1.0);
    const double pitchTau =
        std::max(0.002, static_cast<double>(params.real(ParamId::pitchTime)) * 0.001 * (1.0 - 0.35 * mPunch));
    const double bodyDecaySec =
        std::max(0.018, static_cast<double>(params.real(ParamId::bodyDecay)) * 0.001 * decayScale);
    const double bodyFm =
        std::clamp(static_cast<double>(params.real(ParamId::bodyFm)) * 0.01 + mPunch * 0.14 + mMetal * 0.12, 0.0, 1.0);
    const double bodyFmRatio = std::clamp(static_cast<double>(params.real(ParamId::bodyFmRatio)), 1.0, 7.0);
    const double bodyLevel = bodyOn ? std::clamp(static_cast<double>(params.real(ParamId::bodyLevel)) * 0.01 *
                                                     (1.0 + 0.12 * mPunch - 0.25 * mMetal),
                                                 0.0, 1.15)
                                    : 0.0;

    const double shellTension = std::clamp(
        static_cast<double>(params.real(ParamId::shellTension)) * 0.01 + mMetal * 0.38 + mPunch * 0.14, 0.0, 1.0);
    const double shellLevel =
        shellOn ? std::clamp(static_cast<double>(params.real(ParamId::shellLevel)) * 0.01 * (1.0 + 0.38 * mMetal), 0.0,
                             1.35)
                : 0.0;
    const double shellPol = params.flag(ParamId::shellInvert) ? -1.0 : 1.0;

    const auto wireColor = static_cast<WireColor>(std::clamp(params.choice(ParamId::wireColor), 0, 2));
    const double wireBuzz = std::clamp(static_cast<double>(params.real(ParamId::wireBuzz)) * 0.01, 0.0, 1.0);
    const double clickAmt =
        wireOn ? std::clamp(static_cast<double>(params.real(ParamId::clickAmt)) * 0.01 + mPunch * 0.32, 0.0, 1.25)
               : 0.0;
    const double clickToneHz = std::clamp(static_cast<double>(params.real(ParamId::clickTone)), 800.0, 12000.0);
    const double wireDuck =
        std::clamp(static_cast<double>(params.real(ParamId::wireDuck)) * 0.01 + mPunch * 0.22, 0.0, 1.0);
    const double wireDuckSec = std::max(0.002, static_cast<double>(params.real(ParamId::wireDuckMs)) * 0.001);
    const double wireToneHz = std::clamp(static_cast<double>(params.real(ParamId::wireTone)), 600.0, 14000.0);
    const double wireDecaySec =
        std::max(0.015, static_cast<double>(params.real(ParamId::wireDecay)) * 0.001 * decayScale);
    const double wireWidth = std::clamp(static_cast<double>(params.real(ParamId::wireWidth)) * 0.01, 0.0, 1.0);
    const double wireLevel =
        wireOn ? std::clamp(static_cast<double>(params.real(ParamId::wireLevel)) * 0.01, 0.0, 1.0) : 0.0;
    const double wirePol = params.flag(ParamId::wireInvert) ? -1.0 : 1.0;

    // Micro-offset alignment (+/- 5 ms, smoothed so automation never clicks!)
    const double shellOffMs = std::clamp(static_cast<double>(params.real(ParamId::shellOffsetMs)), -5.0, 5.0);
    const double wireOffMs = std::clamp(static_cast<double>(params.real(ParamId::wireOffsetMs)), -5.0, 5.0);
    const double minOffMs = std::min(0.0, std::min(shellOffMs, wireOffMs));
    const double targetBodyDel = (0.0 - minOffMs) * 0.001 * sr;
    const double targetShellDel = (shellOffMs - minOffMs) * 0.001 * sr;
    const double targetWireDel = (wireOffMs - minOffMs) * 0.001 * sr;

    // 16-stage (8 matched pairs) 100% Series Flat-Magnitude Allpass Disperser
    const double dispAmt =
        std::clamp(static_cast<double>(params.real(ParamId::disperserAmt)) * 0.01 + mSmack * 0.55, 0.0, 1.0);
    const double dispHz = std::clamp(static_cast<double>(params.real(ParamId::disperserHz)), 70.0, 2400.0);
    const double dispBudget = dispAmt * static_cast<double>(kDisperserPairs);
    int activeDispPairs = 0;
    if (dispAmt > 0.003) {
        const double maxCorner = 0.44 * sr;
        for (int p = 0; p < kDisperserPairs; ++p) {
            const double frac = std::clamp(dispBudget - static_cast<double>(p), 0.0, 1.0);
            if (frac <= 1e-4)
                break;
            activeDispPairs = p + 1;
            const double targetHz = std::min(maxCorner, dispHz * (0.82 + 0.052 * static_cast<double>(p)));
            // Logarithmic sweep from near-Nyquist (zero bass group delay) down to targetHz!
            const double pairHz = maxCorner * std::pow(targetHz / maxCorner, frac);
            dispL_[static_cast<std::size_t>(p)].setFreq(pairHz, sr);
            dispR_[static_cast<std::size_t>(p)].setFreq(pairHz, sr);
        }
    }

    // Tone Scoop (380 Hz boxiness carve + 6.2 kHz presence/air lift)
    const double scoop = std::clamp(
        static_cast<double>(params.real(ParamId::scoopAmt)) * 0.01 + mSmack * 0.25 + mPunch * 0.12, -1.0, 1.0);
    const bool useScoop = std::abs(scoop) > 0.01;
    if (useScoop) {
        const double bellDb = (scoop >= 0.0) ? (-5.5 * scoop) : (3.5 * -scoop);
        const double shelfDb = (scoop >= 0.0) ? (4.2 * scoop) : (-4.0 * -scoop);
        scoopBellL_.setPeak(380.0, 1.05, bellDb, sr);
        scoopBellR_.setPeak(380.0, 1.05, bellDb, sr);
        scoopShelfL_.setHighShelf(6200.0, 0.72, shelfDb, sr);
        scoopShelfR_.setHighShelf(6200.0, 0.72, shelfDb, sr);
    }

    // Studio Micro-Room
    const double roomAmt = std::clamp(static_cast<double>(params.real(ParamId::roomAmt)) * 0.01, 0.0, 1.0);
    const double roomFb = std::clamp((0.24 + 0.38 * roomAmt) * std::clamp(decayScale, 0.45, 1.25), 0.12, 0.72);

    // Master Punch ADAA2 + Sub-Mono + Limiter
    const double targetDrive =
        std::clamp(static_cast<double>(params.real(ParamId::drive)) * 0.01 + mSmack * 0.42 + mPunch * 0.18, 0.0, 1.0);
    const auto shaperModel = shaperFromChoice(params.choice(ParamId::driveModel));
    const bool subMono = params.flag(ParamId::subMono);
    const bool useLimiter = params.flag(ParamId::limiter);
    const double targetOutGain = hba::dsp::dbToGain(static_cast<double>(params.real(ParamId::outputDb)));

    // Precompute block-rate filters & constants (zero per-sample exp() in inner loop!)
    const auto clickCoefs = hba::dsp::SvfCoefs::make(clickToneHz, 1.4, sr);
    const auto wireBpCoefs = hba::dsp::SvfCoefs::make(wireToneHz, wireColor == WireColor::snareWire ? 1.5 : 0.85, sr);
    const auto wireHpCoefs = hba::dsp::SvfCoefs::make(std::max(280.0, wireToneHz * 0.35), 0.707, sr);
    const auto malletLpCoefs = hba::dsp::SvfCoefs::make(2400.0, 0.75, sr);
    const auto subCoefs = hba::dsp::butterworth(120.0, sr);

    const double pitchDecayCoef = std::exp(-1.0 / (pitchTau * sr));
    const double duckDecayCoef = std::exp(-1.0 / (wireDuckSec * sr));
    const double clickDecayCoef = std::exp(-1.0 / (0.0022 * sr));
    const double stealFadeCoef = std::exp(-1.0 / (0.0022 * sr));
    const double bodyAttackSamples = std::max(6.0, 0.00020 * sr); // 0.20 ms anti-click micro-attack!
    const double smoothFast = hba::dsp::onePoleCoef(0.005, invSr);
    const double shellTensionDecay = std::exp(-1.0 / (0.016 * sr));
    const double crushInc = std::clamp((wireToneHz * 1.4) / sr, 0.02, 0.48);
    const double lockLpAlpha = 1.0 - std::exp(-hba::dsp::kTwoPi * 700.0 * invSr);
    const int totalLat = latencySamples();

    // Hoist per-voice decay coefficients out of the inner sample loop
    std::array<double, kMaxVoices> voiceBodyDecays{};
    std::array<double, kMaxVoices> voiceWireDecays{};
    for (int vi = 0; vi < kMaxVoices; ++vi) {
        const auto& v = voices_[static_cast<std::size_t>(vi)];
        if (!v.active)
            continue;
        const ArtProfile prof = profileFor(v.art);
        voiceBodyDecays[static_cast<std::size_t>(vi)] =
            std::exp(-1.0 / (std::max(0.016, bodyDecaySec * prof.bodyDecayMult * v.humDecayMult) * sr));
        voiceWireDecays[static_cast<std::size_t>(vi)] =
            std::exp(-1.0 / (std::max(0.014, wireDecaySec * prof.wireDecayMult * v.humDecayMult) * sr));
    }

    float blockPeakL = 0.0f;
    float blockPeakR = 0.0f;
    bool anyVoiceActive = false;

    for (int i = 0; i < numSamples; ++i) {
        double rawBodyL = 0.0, rawBodyR = 0.0;
        double rawShellL = 0.0, rawShellR = 0.0;
        double rawWireL = 0.0, rawWireR = 0.0;

        for (int vi = 0; vi < kMaxVoices; ++vi) {
            auto& v = voices_[static_cast<std::size_t>(vi)];
            if (!v.active)
                continue;
            anyVoiceActive = true;

            const ArtProfile prof = profileFor(v.art);
            const double velGain = 0.25 + 0.75 * v.velocity;

            if (v.stealing) {
                v.fadeOutGain *= stealFadeCoef;
                if (v.fadeOutGain < 1e-4) {
                    v.active = false;
                    continue;
                }
            }
            if (v.bodyStealing) {
                v.bodyFadeGain *= stealFadeCoef;
                if (v.bodyFadeGain < 1e-4)
                    v.bodyFadeGain = 0.0;
            }

            // ----------------------------------------------------------------
            // Layer 1: Fundamental Body (anti-click 0.2 ms ramp + shaped pitch whip + BodyWave + PM FM)
            // ----------------------------------------------------------------
            double bodySample = 0.0;
            if (v.bodyEnv > 1e-5 && v.bodyFadeGain > 1e-4 && bodyLevel > 1e-4) {
                const double shapedPitchEnv = std::pow(std::max(0.0, v.pitchEnv), pitchCurveExp);
                const double instBodyHz =
                    baseBodyHz * v.pitchRatio * v.humPitchMult * semitoneRatio(pitchDropSt * shapedPitchEnv);
                v.bodyPhase = hba::dsp::frac(v.bodyPhase + std::min(instBodyHz, 0.44 * sr) * invSr);
                v.fmPhase = hba::dsp::frac(v.fmPhase + std::min(instBodyHz * bodyFmRatio, 0.44 * sr) * invSr);

                const double fmIdx = bodyFm * 2.8 * (0.15 + 0.85 * shapedPitchEnv) * v.bodyEnv;
                const double attackRamp =
                    (v.ageSamples < bodyAttackSamples)
                        ? (0.5 - 0.5 * std::cos(hba::dsp::kPi * (v.ageSamples + 1.0) / bodyAttackSamples))
                        : 1.0;
                const double theta = hba::dsp::kTwoPi * v.bodyPhase + fmIdx * std::sin(hba::dsp::kTwoPi * v.fmPhase);
                bodySample = bodyShape(bodyWave, theta) * v.bodyEnv * attackRamp;
                const double bOut = bodySample * bodyLevel * prof.bodyGain * velGain * v.fadeOutGain * v.bodyFadeGain;
                rawBodyL += bOut;
                rawBodyR += bOut;
            }
            v.bodyEnv *= voiceBodyDecays[static_cast<std::size_t>(vi)];
            v.pitchEnv *= pitchDecayCoef;

            // ----------------------------------------------------------------
            // Layer 2: 24-Mode Nonlinear Tensioned Modal Shell + 6 Mallet Materials
            // ----------------------------------------------------------------
            double mallet = 0.0;
            switch (v.malletType) {
            case MalletType::woodStick: {
                const double len = std::max(6.0, 0.00038 * sr);
                if (v.ageSamples < len) {
                    const double u = v.ageSamples / len;
                    mallet = std::sin(hba::dsp::kTwoPi * u) * std::sin(hba::dsp::kPi * u) * (2.6 / len);
                }
                break;
            }
            case MalletType::hardFelt: {
                const double len = std::max(14.0, 0.00145 * sr);
                if (v.ageSamples < len) {
                    const double u = v.ageSamples / len;
                    const double env = std::sin(hba::dsp::kPi * u);
                    mallet = std::sin(hba::dsp::kTwoPi * u) * env * env * (3.1 / len);
                }
                break;
            }
            case MalletType::metalHammer: {
                const double len = std::max(4.0, 0.00018 * sr);
                if (v.ageSamples < len) {
                    const double u = v.ageSamples / len;
                    mallet = (std::sin(hba::dsp::kTwoPi * u) + 0.55 * std::sin(4.0 * hba::dsp::kPi * u)) *
                             std::sin(hba::dsp::kPi * u) * (2.4 / len);
                }
                break;
            }
            case MalletType::rimFlam: {
                const double len = std::max(5.0, 0.00030 * sr);
                const double flamGap = 0.0022 * sr;
                if (v.ageSamples < len) {
                    const double u = v.ageSamples / len;
                    mallet = std::sin(hba::dsp::kTwoPi * u) * std::sin(hba::dsp::kPi * u) * (2.0 / len);
                } else if (v.ageSamples >= flamGap && v.ageSamples < flamGap + len) {
                    const double u = (v.ageSamples - flamGap) / len;
                    mallet = -std::sin(hba::dsp::kTwoPi * u) * std::sin(hba::dsp::kPi * u) * (1.75 / len);
                }
                break;
            }
            case MalletType::skinThump: {
                const double len = std::max(16.0, 0.0018 * sr);
                if (v.ageSamples < len) {
                    const double u = v.ageSamples / len;
                    const double rawN = nextNoise(v.rngL) * std::sin(hba::dsp::kPi * u);
                    double mv1 = 0.0, mv2 = 0.0;
                    v.malletLp.tick(malletLpCoefs, rawN, mv1, mv2);
                    mallet = mv2 * (5.2 / std::sqrt(len));
                }
                break;
            }
            case MalletType::laserSnap: {
                const double len = std::max(10.0, 0.00095 * sr);
                if (v.ageSamples < len) {
                    const double u = v.ageSamples / len;
                    const double chirpHz = 3600.0 * std::pow(0.09, u);
                    v.malletChirpPhase += hba::dsp::kTwoPi * std::min(chirpHz, 0.42 * sr) * invSr;
                    mallet = std::sin(v.malletChirpPhase) * std::sin(hba::dsp::kPi * u) * (3.0 / len);
                }
                break;
            }
            }

            // Skip tension trig updates once shellEnergy decays below 1e-4!
            const bool updateTension =
                ((static_cast<int>(v.ageSamples) & 7) == 0) && (shellTension > 0.005) && (v.shellEnergy > 1e-4);
            const double tensionScale = shellTension * 0.22 * v.shellEnergy * v.velocity;

            double sL = 0.0;
            double sR = 0.0;
            double modePower = 0.0;
            const double shellDriveScale = shellLevel * prof.shellGain * velGain * v.fadeOutGain * 0.48;

            for (int k = 0; k < kModeCount; ++k) {
                auto& m = v.modes[static_cast<std::size_t>(k)];
                if (m.excWeight <= 1e-6)
                    continue;

                if (updateTension) {
                    const double modHz = std::min(
                        0.44 * sr, m.baseFreqHz * (1.0 + tensionScale * (1.0 + 0.03 * static_cast<double>(k))));
                    const double w = hba::dsp::kTwoPi * modHz * invSr;
                    m.cosW = std::cos(w);
                    m.sinW = std::sin(w);
                }

                const double nextRe = (m.re * m.cosW - m.im * m.sinW) * m.decayCoef + mallet * m.excWeight;
                const double nextIm = (m.re * m.sinW + m.im * m.cosW) * m.decayCoef;
                m.re = nextRe;
                m.im = nextIm;

                sL += nextRe * m.gainL;
                sR += nextRe * m.gainR;
                modePower += nextRe * nextRe + nextIm * nextIm;
            }
            v.shellEnergy *= shellTensionDecay;
            const double shellOutL = sL * shellDriveScale * shellPol;
            const double shellOutR = sR * shellDriveScale * shellPol;
            rawShellL += shellOutL;
            rawShellR += shellOutR;

            // ----------------------------------------------------------------
            // Layer 3: Phase-Aligned Crack Click + Transient-Ducked & Sympathetic-Buzz Wire
            // ----------------------------------------------------------------
            const double nL = nextNoise(v.rngL);
            const double nR = nextNoise(v.rngR);

            double clickSig = 0.0;
            if (v.clickEnv > 1e-5) {
                if (clickAmt > 0.005) {
                    const double burst = (v.ageSamples < 5.0 ? 1.0 : nL * 0.90) * v.clickEnv;
                    double cv1 = 0.0, cv2 = 0.0;
                    v.clickBp.tick(clickCoefs, burst, cv1, cv2);
                    const double cBp = clickCoefs.k * cv1;
                    clickSig = (cBp * 1.15 + burst * 0.42) * clickAmt * prof.clickGain * 1.05;
                }
                v.clickEnv *= clickDecayCoef;
            }

            double wireL = 0.0, wireR = 0.0;
            if (v.wireEnv > 1e-5) {
                if (wireLevel > 0.005) {
                    double srcL = nL;
                    double srcR = nL * (1.0 - wireWidth) + nR * wireWidth;

                    if (wireColor == WireColor::crushedMetal) {
                        v.crushPhase += crushInc;
                        if (v.crushPhase >= 1.0) {
                            v.crushPhase -= 1.0;
                            v.crushHoldL = srcL;
                            v.crushHoldR = srcR;
                        }
                        srcL = v.crushHoldL;
                        srcR = v.crushHoldR;
                    }

                    double wv1L = 0.0, wv2L = 0.0, wv1R = 0.0, wv2R = 0.0;
                    v.wireBpL.tick(wireBpCoefs, srcL, wv1L, wv2L);
                    v.wireBpR.tick(wireBpCoefs, srcR, wv1R, wv2R);
                    const double bpL = wireBpCoefs.k * wv1L;
                    const double bpR = wireBpCoefs.k * wv1R;
                    const double hpL = v.wireHpL.highpass(wireHpCoefs, srcL);
                    const double hpR = v.wireHpR.highpass(wireHpCoefs, srcR);

                    const double texL =
                        wireColor == WireColor::airSizzle ? (hpL * 0.85 + bpL * 0.45) : (bpL * 0.95 + hpL * 0.62);
                    const double texR =
                        wireColor == WireColor::airSizzle ? (hpR * 0.85 + bpR * 0.45) : (bpR * 0.95 + hpR * 0.62);

                    // Sympathetic snare-wire rattle coupled to vibrating drumhead + body!
                    const double headDrive =
                        std::clamp(std::abs(sL + sR) * 1.15 + std::abs(bodySample) * 0.65, 0.0, 1.5);
                    const double buzzMod = (1.0 - 0.52 * wireBuzz) + 0.92 * wireBuzz * headDrive;

                    const double duckGain = 1.0 - wireDuck * v.duckEnv;
                    const double envW = v.wireEnv * duckGain * buzzMod * wireLevel * prof.wireGain * 1.18;
                    wireL = texL * envW;
                    wireR = texR * envW;
                }
                v.wireEnv *= voiceWireDecays[static_cast<std::size_t>(vi)];
                v.duckEnv *= duckDecayCoef;
            }

            rawWireL += (clickSig + wireL) * velGain * v.fadeOutGain * wirePol;
            rawWireR += (clickSig + wireR) * velGain * v.fadeOutGain * wirePol;

            v.ageSamples += 1.0;

            if (v.ageSamples > 64.0 && v.bodyEnv < 1.5e-5 && v.wireEnv < 1.5e-5 && v.clickEnv < 1.5e-5 &&
                modePower < 1e-10) {
                v.active = false;
            }
        }

        // Smoothed phase-aligned fractional micro-delays (zero zippering on automation!)
        const double bDel = bodyDelSmooth_.step(targetBodyDel, smoothFast);
        const double sDel = shellDelSmooth_.step(targetShellDel, smoothFast);
        const double wDel = wireDelSmooth_.step(targetWireDel, smoothFast);

        delBodyL_.push(rawBodyL);
        delBodyR_.push(rawBodyR);
        delShellL_.push(rawShellL);
        delShellR_.push(rawShellR);
        delWireL_.push(rawWireL);
        delWireR_.push(rawWireR);

        const double alignedBodyL = delBodyL_.readFrac(bDel);
        const double alignedBodyR = delBodyR_.readFrac(bDel);
        const double alignedShellL = delShellL_.readFrac(sDel);
        const double alignedShellR = delShellR_.readFrac(sDel);
        const double alignedWireL = delWireL_.readFrac(wDel);
        const double alignedWireR = delWireR_.readFrac(wDel);

        // Feed latency-matching scope buffers so BODY/SHELL/WIRE align sample-for-sample with post-FX SUM!
        scopeLatBody_.push(0.5 * (alignedBodyL + alignedBodyR));
        scopeLatShell_.push(0.5 * (alignedShellL + alignedShellR));
        scopeLatWire_.push(0.5 * (alignedWireL + alignedWireR));

        double sumL = alignedBodyL + alignedShellL + alignedWireL;
        double sumR = alignedBodyR + alignedShellR + alignedWireR;

        // 16-stage (8 matched pairs) 100% Series Flat-Magnitude Allpass Micro-Disperser!
        for (int p = 0; p < activeDispPairs; ++p) {
            sumL = dispL_[static_cast<std::size_t>(p)].process(sumL);
            sumR = dispR_[static_cast<std::size_t>(p)].process(sumR);
        }

        // Tone Scoop (380 Hz boxiness carve + 6.2 kHz crack/air shelf before clipping)
        if (useScoop) {
            sumL = scoopShelfL_.process(scoopBellL_.process(sumL));
            sumR = scoopShelfR_.process(scoopBellR_.process(sumR));
        }

        // Tight Stereo Early-Reflection Micro-Room (glues Body + Shell + Wire into one physical space!)
        if (roomAmt > 0.003)
            room_.process(sumL, sumR, roomAmt, roomFb, sumL, sumR);

        // HIIR Oversampled ADAA2 Shaper / Clipper
        const double curDrive = driveSmooth_.step(targetDrive, smoothFast);
        {
            const int osN = oversamplerL_.factor();
            const double driveGain = 1.0 + curDrive * 7.5;
            const double wetMix = std::clamp(curDrive * 1.35, 0.0, 1.0);
            const auto shapeCh = [&](double xIn, hba::dsp::Oversampler& os,
                                     hba::dsp::MultiShaperAdaa2& shaper) noexcept HBA_NONBLOCKING {
                std::array<float, 8> up{};
                os.up(static_cast<float>(xIn), up.data());
                for (int s = 0; s < osN; ++s) {
                    const double u = static_cast<double>(up[static_cast<std::size_t>(s)]);
                    if (curDrive > 0.005) {
                        double dryAl = 0.0;
                        const double wet = shaper.process(u, driveGain, 0.0, shaperModel, shaperModel, 1.0, dryAl);
                        up[static_cast<std::size_t>(s)] = static_cast<float>(dryAl + wetMix * (wet * 0.92 - dryAl));
                    } else {
                        up[static_cast<std::size_t>(s)] = static_cast<float>(shaper.bypass(u));
                    }
                }
                return static_cast<double>(os.down(up.data()));
            };
            sumL = shapeCh(sumL, oversamplerL_, shaperL_);
            sumR = shapeCh(sumR, oversamplerR_, shaperR_);
        }

        // LR4 Sub-Mono Safety below 120 Hz
        if (subMono) {
            double loL = 0.0, hiL = 0.0, loR = 0.0, hiR = 0.0;
            subSplitL_.process(subCoefs, sumL, loL, hiL);
            subSplitR_.process(subCoefs, sumR, loR, hiR);
            const double monoSub = 0.5 * (loL + loR);
            sumL = monoSub + hiL;
            sumR = monoSub + hiR;
        }

        // 22 Hz DC blocker + output gain + 4x true-peak look-ahead safety limiter
        const double outGain = outGainSmooth_.step(targetOutGain, smoothFast);
        double finL = dcL_.process(sumL) * outGain;
        double finR = dcR_.process(sumR) * outGain;
        limiter_.process(finL, finR, 0.96, useLimiter ? 1.0 : 0.0);
        if (!useLimiter) {
            finL = hba::dsp::softGuard(finL, 0.96, 1.25);
            finR = hba::dsp::softGuard(finR, 0.96, 1.25);
        }

        // Latency-compensated 40 ms Transient Microscope capture!
        if (capturingScope_) {
            const double bM = scopeLatBody_.readInt(totalLat);
            const double sM = scopeLatShell_.readInt(totalLat);
            const double wM = scopeLatWire_.readInt(totalLat);
            const double finM = 0.5 * (finL + finR);

            // Start recording right as latency clears so t=0 ms on the microscope is sample 0 of the hit!
            const int recSample = scopeCaptureSample_ - totalLat;
            if (recSample >= 0) {
                scopeRmsSum_ += finM * finM;
                scopePeakAbs_ = std::max(scopePeakAbs_, std::abs(finM));

                // Band-limited (700 Hz LP) 0-14 ms constructive summation score -> intuitive 0..100% LOCK meter!
                if (recSample < lockWindowSamples_) {
                    lockLpBody_ += lockLpAlpha * (bM - lockLpBody_);
                    lockLpOther_ += lockLpAlpha * ((sM + wM) - lockLpOther_);
                    const double bLo = lockLpBody_;
                    const double oLo = lockLpOther_;
                    lockBandBodySq_ += bLo * bLo;
                    lockBandOtherSq_ += oLo * oLo;
                    lockBandSumSq_ += (bLo + oLo) * (bLo + oLo);
                }

                const int bucket =
                    std::min(kScopeSamples - 1, (recSample * kScopeSamples) / std::max(1, scopeTotalSamples_));
                const int prevBucket = recSample == 0
                                           ? -1
                                           : std::min(kScopeSamples - 1, ((recSample - 1) * kScopeSamples) /
                                                                             std::max(1, scopeTotalSamples_));
                if (bucket != prevBucket) {
                    capBody_[static_cast<std::size_t>(bucket)] = static_cast<float>(bM);
                    capShell_[static_cast<std::size_t>(bucket)] = static_cast<float>(sM);
                    capWire_[static_cast<std::size_t>(bucket)] = static_cast<float>(wM);
                    capSum_[static_cast<std::size_t>(bucket)] = static_cast<float>(finM);
                } else {
                    auto keepPeak = [](float& dst, double cand) {
                        if (std::abs(cand) > std::abs(static_cast<double>(dst)))
                            dst = static_cast<float>(cand);
                    };
                    keepPeak(capBody_[static_cast<std::size_t>(bucket)], bM);
                    keepPeak(capShell_[static_cast<std::size_t>(bucket)], sM);
                    keepPeak(capWire_[static_cast<std::size_t>(bucket)], wM);
                    keepPeak(capSum_[static_cast<std::size_t>(bucket)], finM);
                }
            }

            ++scopeCaptureSample_;
            if (recSample + 1 >= scopeTotalSamples_) {
                capturingScope_ = false;
                telemetry_.bodyWave = capBody_;
                telemetry_.shellWave = capShell_;
                telemetry_.wireWave = capWire_;
                telemetry_.sumWave = capSum_;
                telemetry_.scopePeak = static_cast<float>(std::max(0.15, scopePeakAbs_));

                const double rms = std::sqrt(scopeRmsSum_ / static_cast<double>(scopeTotalSamples_));
                telemetry_.peakDb = static_cast<float>(hba::dsp::gainToDb(std::max(1e-4, scopePeakAbs_)));
                telemetry_.crestDb =
                    static_cast<float>(hba::dsp::gainToDb(std::max(1e-4, scopePeakAbs_) / std::max(1e-5, rms)));

                // Constructive summation index in [0..100%]:
                // 0% = total destructive cancellation, 50% = orthogonal/uncorrelated, 100% = full constructive lock!
                if (lockBandBodySq_ > 1e-8 && lockBandOtherSq_ > 1e-8) {
                    const double cross = 0.5 * (lockBandSumSq_ - lockBandBodySq_ - lockBandOtherSq_);
                    const double r = std::clamp(cross / std::sqrt(lockBandBodySq_ * lockBandOtherSq_), -1.0, 1.0);
                    telemetry_.phaseLockPct = static_cast<float>(std::clamp(50.0 + 50.0 * r, 0.0, 100.0));
                } else {
                    telemetry_.phaseLockPct = 100.0f;
                }
            }
        }

        const float fL = static_cast<float>(finL);
        const float fR = static_cast<float>(finR);
        outL[i] = fL;
        outR[i] = fR;
        blockPeakL = std::max(blockPeakL, std::abs(fL));
        blockPeakR = std::max(blockPeakR, std::abs(fR));
    }

    // Publish live 24-mode energy snapshot for Chladni-ring UI
    telemetry_.modeEnergy.fill(0.0f);
    for (const auto& v : voices_) {
        if (!v.active)
            continue;
        for (int k = 0; k < kModeCount; ++k) {
            const auto ki = static_cast<std::size_t>(k);
            const double amp = std::sqrt(v.modes[ki].re * v.modes[ki].re + v.modes[ki].im * v.modes[ki].im);
            telemetry_.modeEnergy[ki] =
                std::max(telemetry_.modeEnergy[ki], static_cast<float>(std::clamp(amp * 3.8, 0.0, 1.0)));
        }
    }

    telemetry_.peakL = blockPeakL;
    telemetry_.peakR = blockPeakR;
    telemetry_.gainReductionDb =
        static_cast<float>(std::max(0.0, -hba::dsp::gainToDb(std::max(1e-4, limiter_.gain()))));
    telemetry_.active = anyVoiceActive;
}

} // namespace sb
