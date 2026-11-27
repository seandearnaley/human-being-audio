// SPDX-License-Identifier: AGPL-3.0-or-later
#include "tg/engine.h"

#include <hba/dsp/denormals.h>
#include <hba/dsp/gain.h>
#include <hba/dsp/oscillator.h>
#include <hba/dsp/pitch.h>

#include <algorithm>
#include <cmath>

namespace tg {

namespace {
constexpr double kFdnModulationMs = 0.42;

inline hba::dsp::ShaperModel shaperFromChoice(int idx) noexcept HBA_NONBLOCKING {
    switch (std::clamp(idx, 0, 4)) {
    case 0:
        return hba::dsp::ShaperModel::tube;
    case 1:
        return hba::dsp::ShaperModel::buchla;
    case 2:
        return hba::dsp::ShaperModel::fold;
    case 3:
        return hba::dsp::ShaperModel::sine;
    case 4:
        return hba::dsp::ShaperModel::clip;
    }
    return hba::dsp::ShaperModel::tube;
}

// Continuous signed velocity warp for Spiral / Mobius X-axis:
// 0% -> -2x reverse, 25% -> 0x exact freeze, 50% -> +1x bar-locked, 100% -> +3x hyper-ascension.
inline double padVelocityCurve(double px, double minVel, double maxVel) noexcept HBA_NONBLOCKING {
    if (px < 0.25)
        return minVel * (1.0 - 4.0 * px);
    if (px < 0.50)
        return 4.0 * (px - 0.25);
    return 1.0 + (maxVel - 1.0) * 2.0 * (px - 0.50);
}
} // namespace

void Engine::prepare(double sampleRate, int maxBlockSize, int oversamplingFactor) {
    sampleRate_ = sampleRate > 8000.0 ? sampleRate : 48000.0;
    maxBlock_ = std::max(1, maxBlockSize);
    oversamplingFactor_ =
        (oversamplingFactor == 2 || oversamplingFactor == 4 || oversamplingFactor == 8) ? oversamplingFactor : 4;

    oversamplerL_.prepare(oversamplingFactor_);
    oversamplerR_.prepare(oversamplingFactor_);
    dcL_.prepare(16.0, sampleRate_);
    dcR_.prepare(16.0, sampleRate_);
    limiter_.prepare(sampleRate_);

    // Mutually prime FDN diffusion lengths scaled smoothly with sample rate (zero allocation in process!).
    constexpr std::array<double, kFdnLines> kFdnMs{29.73, 37.15, 43.98, 53.27};
    const int modulationHeadroom =
        std::max(24, static_cast<int>(std::ceil(kFdnModulationMs * 0.001 * sampleRate_)) + 2);
    for (int j = 0; j < kFdnLines; ++j) {
        const auto ji = static_cast<std::size_t>(j);
        // Keep the nominal tap used by the original reverb and reserve its full excursion at this rate.
        const int delay = static_cast<int>(std::lround(kFdnMs[ji] * 0.001 * sampleRate_)) + 40;
        fdnDelaySamples_[ji] = delay;
        fdnBuf_[ji].assign(static_cast<std::size_t>(delay + modulationHeadroom), 0.0);
    }

    reset();
}

void Engine::reset() noexcept HBA_NONBLOCKING {
    keysDown_.fill(false);
    sustainDown_ = false;
    gateOpen_ = false;
    keySwitchScene_ = -1;
    envLevel_ = 0.0;
    freePpq_ = 0.0;
    retrigPpqOffset_ = 0.0;
    lastTransportPpq_ = 0.0;
    wasPlaying_ = false;
    prevMorphCycle_ = 0.0;
    prevLoopCycles_ = 0.0;
    hasPrevLoopCycles_ = false;
    padSpiralOffset_ = 0.0;
    padMobiusOffset_ = 0.0;
    mobiusCycleOffset_ = 0;

    rootMidiSmooth_.reset();
    morphSmooth_.reset();
    filterSmooth_.reset();
    driveSmooth_.reset();
    spaceSmooth_.reset();
    subAnchorSmooth_.reset();
    bellCenterSmooth_.reset();
    outputGainSmooth_.reset();
    padActiveSmooth_.reset();
    padXSmooth_.reset();
    padYSmooth_.reset();
    spiralVelSmooth_.reset();
    detuneSmooth_.reset();
    widthSmooth_.reset();
    for (auto& s : voicePitchSmooth_)
        s.reset();
    for (auto& r : voicePitchRamp_)
        r.snap(41.0);
    voiceRampPrimed_ = false;

    for (int v = 0; v < kVoiceCount; ++v) {
        for (int k = 0; k < kOctaveCount; ++k) {
            // Deterministic golden-ratio phase spread prevents constructive peak pileup at note start.
            const double seed = hba::dsp::frac(0.6180339887 * static_cast<double>(v * kOctaveCount + k));
            partials_[static_cast<std::size_t>(v)][static_cast<std::size_t>(k)] = {seed, hba::dsp::frac(seed + 0.37),
                                                                                   seed};
        }
    }
    subOscPhase_ = 0.0;

    bodyHpL_.reset();
    bodyHpR_.reset();
    subLp_.reset();
    subWarmthLp_.reset();
    oversamplerL_.reset();
    oversamplerR_.reset();
    shaperL_.reset();
    shaperR_.reset();
    filterL_.reset();
    filterR_.reset();
    filter2L_.reset();
    filter2R_.reset();
    spaceBufL_.fill(0.0);
    spaceBufR_.fill(0.0);
    spaceWritePos_ = 0;
    spaceLfo1_ = 0.0;
    spaceLfo2_ = 0.33;
    spaceLfo3_ = 0.67;

    for (int j = 0; j < kFdnLines; ++j) {
        const auto ji = static_cast<std::size_t>(j);
        for (double& sample : fdnBuf_[ji])
            sample = 0.0;
        fdnPos_[ji] = 0;
        fdnLp_[ji] = 0.0;
        fdnHp_[ji] = 0.0;
        fdnLfo_[ji] = static_cast<double>(j) * 0.25;
    }

    dcL_.reset();
    dcR_.reset();
    limiter_.reset();
    silentTailSamples_ = 0;

    mpeConfigured_ = false;
    mpeThrottleCounter_ = 0;
    for (auto& mv : mpeVoices_)
        mv = {};
    telemetry_ = {};
}

int Engine::latencySamples() const noexcept HBA_NONBLOCKING {
    return limiter_.latency() + oversamplerL_.latencySamples();
}

void Engine::process(const Params& params, const Transport& transport, float* outL, float* outR, int numSamples,
                     OutMidiBuffer* mpeOut) noexcept HBA_NONBLOCKING {
    hba::dsp::ScopedFlushDenormals flushDenormals;

    if (mpeOut != nullptr)
        mpeOut->clear();
    if (numSamples <= 0 || outL == nullptr || outR == nullptr)
        return;

    if (params.flag(ParamId::bypass)) {
        for (int i = 0; i < numSamples; ++i) {
            outL[i] = 0.0f;
            outR[i] = 0.0f;
        }
        if (mpeOut != nullptr)
            emitMpeAllNotesOff(*mpeOut, 0);
        return;
    }

    const double sr = sampleRate_;
    const double invSr = 1.0 / sr;
    const double bpm = std::clamp(transport.bpm, 30.0, 300.0);
    const double beatsPerSample = (bpm / 60.0) * invSr;

    if (transport.playing) {
        freePpq_ = transport.ppq;
        wasPlaying_ = true;
    } else if (wasPlaying_) {
        wasPlaying_ = false;
    }

    const auto mode = static_cast<Mode>(std::clamp(params.choice(ParamId::mode), 0, 2));
    const int rootPc = std::clamp(params.choice(ParamId::rootKey), 0, 11);
    const int scaleIdx = std::clamp(params.choice(ParamId::scale), 0, kScaleCount - 1);
    const auto spiralDir = static_cast<SpiralDir>(std::clamp(params.choice(ParamId::spiralDir), 0, 2));
    const int loopBarsIdx = std::clamp(params.choice(ParamId::loopBars), 0, static_cast<int>(kLoopBarBeats.size()) - 1);
    const double loopBeats = kLoopBarBeats[static_cast<std::size_t>(loopBarsIdx)];
    const int ratioIdx =
        std::clamp(params.choice(ParamId::spiralRatio), 0, static_cast<int>(kSpiralRatioSemitones.size()) - 1);
    const double ratioSemi = kSpiralRatioSemitones[static_cast<std::size_t>(ratioIdx)];
    const SpiralChord spiralChord = spiralChordForStack(params.choice(ParamId::spiralStack));
    const double baseBellWidth = std::clamp(static_cast<double>(params.real(ParamId::bellWidth)) * 0.01, 0.25, 1.0);
    const double phaseOffset = std::clamp(static_cast<double>(params.real(ParamId::phaseOffset)) * 0.01, 0.0, 1.0);

    const bool morphAuto = params.flag(ParamId::morphAuto);
    const double baseMorph = std::clamp(static_cast<double>(params.real(ParamId::morph)) * 0.01, 0.0, 1.0);
    const double baseGravity = std::clamp(static_cast<double>(params.real(ParamId::gravity)) * 0.01, 0.0, 1.0);
    const auto gravMode = static_cast<GravityMode>(std::clamp(params.choice(ParamId::gravityMode), 0, 2));
    const double baseStagger = std::clamp(static_cast<double>(params.real(ParamId::stagger)) * 0.01, 0.0, 1.0);
    const auto staggerOrder = static_cast<StaggerOrder>(std::clamp(params.choice(ParamId::staggerOrder), 0, 3));
    const bool anchorSub = params.flag(ParamId::anchorSub);

    const auto timbre = static_cast<TimbreKind>(std::clamp(params.choice(ParamId::timbre), 0, 6));
    const double baseDetuneCt = std::clamp(static_cast<double>(params.real(ParamId::detune)), 0.0, 45.0);
    const double baseWidth = std::clamp(static_cast<double>(params.real(ParamId::width)) * 0.01, 0.0, 1.0);
    const double targetSpaceBase = std::clamp(static_cast<double>(params.real(ParamId::space)) * 0.01, 0.0, 1.0);
    const double baseSubWarmth = std::clamp(static_cast<double>(params.real(ParamId::subWarmth)) * 0.01, 0.0, 1.0);
    const double subCutoffHz = std::clamp(static_cast<double>(params.real(ParamId::subCutoff)), 45.0, 120.0);
    const auto shaperModel = shaperFromChoice(params.choice(ParamId::driveModel));
    const auto filterMode = static_cast<FilterMode>(std::clamp(params.choice(ParamId::filterMode), 0, 3));
    const double baseFilterRes = std::clamp(static_cast<double>(params.real(ParamId::filterRes)) * 0.01, 0.0, 0.92);
    const double baseRissetGate = std::clamp(static_cast<double>(params.real(ParamId::rissetGate)) * 0.01, 0.0, 1.0);

    // 2D Playable Pitch-Manifold Pad state
    const auto padProg =
        static_cast<PadProgram>(std::clamp(params.choice(ParamId::padProgram), 0, kPadProgramCount - 1));
    const double targetPadX = std::clamp(static_cast<double>(params.real(ParamId::padX)) * 0.01, 0.0, 1.0);
    const double targetPadY = std::clamp(static_cast<double>(params.real(ParamId::padY)) * 0.01, 0.0, 1.0);
    const bool padEngaged = params.flag(ParamId::padTouch) || params.flag(ParamId::padHold);

    const double glideTau = std::max(0.001, static_cast<double>(params.real(ParamId::glideMs)) * 0.001);
    const double glideCoef = hba::dsp::onePoleCoef(glideTau, invSr);
    const double smoothFast = hba::dsp::onePoleCoef(0.008, invSr);
    const double smoothPad = hba::dsp::onePoleCoef(0.014, invSr);
    const double voicePitchCoef = hba::dsp::onePoleCoef(0.0022, invSr);
    const double attackCoef =
        hba::dsp::onePoleCoef(std::max(0.001, static_cast<double>(params.real(ParamId::attackMs)) * 0.001), invSr);
    const double releaseCoef =
        hba::dsp::onePoleCoef(std::max(0.008, static_cast<double>(params.real(ParamId::releaseMs)) * 0.001), invSr);

    const int lane1Idx = std::clamp(params.choice(ParamId::lane1Rate), 0, static_cast<int>(kLoopBarBeats.size()) - 1);
    const double lane1Beats = kLoopBarBeats[static_cast<std::size_t>(lane1Idx)];
    const double lane1Depth = static_cast<double>(params.real(ParamId::lane1Depth)) * 0.01;
    const auto lane1Target = static_cast<LaneTarget>(std::clamp(params.choice(ParamId::lane1Target), 0, 6));

    const int lane2Idx = std::clamp(params.choice(ParamId::lane2Rate), 0, static_cast<int>(kLane2BarBeats.size()) - 1);
    const double lane2Beats = kLane2BarBeats[static_cast<std::size_t>(lane2Idx)];
    const double lane2Depth = static_cast<double>(params.real(ParamId::lane2Depth)) * 0.01;
    const auto lane2Target = static_cast<LaneTarget>(std::clamp(params.choice(ParamId::lane2Target), 0, 6));

    const bool retrig = params.flag(ParamId::retrig);
    const bool useKeySwitch = params.flag(ParamId::keySwitch);
    const bool useLimiter = params.flag(ParamId::limiter);
    const bool mpeEnabled = params.flag(ParamId::mpeOut);
    const int mpeRangeSt =
        static_cast<int>(kMpeRanges[static_cast<std::size_t>(std::clamp(params.choice(ParamId::mpeRange), 0, 2))]);
    const double targetOutGain = hba::dsp::dbToGain(static_cast<double>(params.real(ParamId::outputDb)));
    const double targetSubAnchorBase =
        std::clamp(static_cast<double>(params.real(ParamId::subAnchor)) * 0.01, 0.0, 1.0);
    const double targetBellCenter = std::clamp(static_cast<double>(params.real(ParamId::bellCenter)), 80.0, 2400.0);
    const double targetFilterBase = std::clamp(static_cast<double>(params.real(ParamId::filterHz)), 120.0, 16000.0);
    const double targetDriveBase = std::clamp(static_cast<double>(params.real(ParamId::drive)) * 0.01, 0.0, 1.0);

    const hba::dsp::SvfCoefs subSplitCoefs = hba::dsp::butterworth(subCutoffHz, sr);
    const hba::dsp::SvfCoefs subWarmthCoefs = hba::dsp::butterworth(std::min(280.0, subCutoffHz * 2.6), sr);
    const double fdnLpCoef = hba::dsp::onePoleCoef(1.0 / (hba::dsp::kTwoPi * 5400.0), invSr);
    const double fdnHpCoef = hba::dsp::onePoleCoef(1.0 / (hba::dsp::kTwoPi * 155.0), invSr);

    constexpr int kControlInterval = 32;
    const int mpeThrottleSamples = std::max(64, static_cast<int>(sr * 0.0025));

    // Control variables cached per sub-block
    double curSpiralPos = 0.0;
    double curBarPhase = 0.0;
    double curMorph = baseMorph;
    double curGravity = baseGravity;
    double curStagger = baseStagger;
    double curBellWidth = baseBellWidth;
    double curDrive = targetDriveBase;
    double curSpace = targetSpaceBase;
    double curSubWarmth = baseSubWarmth;
    double curFilterHz = targetFilterBase;
    double curBellCenter = targetBellCenter;
    double curRissetGate = baseRissetGate;
    double curLane1Val = 0.0;
    double curLane2Val = 0.0;
    double curLane1Phase = 0.0;
    double curLane2Phase = 0.0;
    double targetSpiralVel = 1.0;
    double targetSubAnchorEff = targetSubAnchorBase;
    double targetDetuneCtEff = baseDetuneCt;
    double targetWidthEff = baseWidth;
    double voidShimmerAmt = 0.0;
    int curActiveScene = 0;
    std::array<double, kVoiceCount> targetVoicePitches{};
    const double initQ = 0.707 + baseFilterRes * baseFilterRes * 7.5;
    hba::dsp::SvfCoefs filterCoefs = hba::dsp::SvfCoefs::make(curFilterHz, initQ, sr);
    hba::dsp::SvfCoefs filter2Coefs =
        hba::dsp::SvfCoefs::make(std::min(16000.0, curFilterHz * 1.85), std::max(0.707, initQ * 0.85), sr);

    float blockPeakL = 0.0f;
    float blockPeakR = 0.0f;
    float blockSubPeak = 0.0f;
    double blockRmsSum = 0.0;
    double lastGateEnv = 1.0;
    double lastEffectiveBellHz = targetBellCenter;
    double lastSpiralVel = 1.0;
    double lastPadAmt = 0.0;

    for (int i = 0; i < numSamples; ++i) {
        const double ppqNow = transport.playing ? (transport.ppq + static_cast<double>(i) * beatsPerSample) : freePpq_;
        if (!transport.playing)
            freePpq_ += beatsPerSample;
        lastTransportPpq_ = ppqNow;

        // Smooth X/Y Manifold Pad state (click-free engagement + continuous coordinates)
        const double padAmt = padActiveSmooth_.step(padEngaged ? 1.0 : 0.0, smoothPad);
        const double padX = padXSmooth_.step(targetPadX, smoothFast);
        const double padY = padYSmooth_.step(targetPadY, smoothFast);
        lastPadAmt = padAmt;

        // Envelope: active whenever MIDI keys are held OR the playable X/Y Manifold / Drone is engaged!
        const bool anyGate = gateOpen_ || padEngaged;
        const double envTarget = anyGate ? (gateOpen_ ? static_cast<double>(velocity_) : 0.92) : 0.0;
        envLevel_ += (anyGate ? attackCoef : releaseCoef) * (envTarget - envLevel_);

        // Root MIDI note: MIDI keys always take priority; Drone/Touch anchors smoothly to Root Key Octave 2!
        const double baseNoteMidi = gateOpen_ ? static_cast<double>(currentNote_) : static_cast<double>(36 + rootPc);
        const double targetRootMidi = baseNoteMidi + static_cast<double>(pitchBendSt_);
        const double rootMidi = rootMidiSmooth_.step(targetRootMidi, glideCoef);

        // Continuous sample-rate loop cycle integrator (zero stair-stepping, bar-locked when pad inactive!)
        const double relPpq = retrig ? std::max(0.0, ppqNow - retrigPpqOffset_) : ppqNow;
        const double rawCycles = (relPpq / loopBeats) + phaseOffset;
        double dCycle = 0.0;
        if (!hasPrevLoopCycles_ || std::abs(rawCycles - prevLoopCycles_) > 0.25) {
            prevLoopCycles_ = rawCycles;
            hasPrevLoopCycles_ = true;
        } else {
            dCycle = rawCycles - prevLoopCycles_;
            prevLoopCycles_ = rawCycles;
        }

        const double curVel = spiralVelSmooth_.step(targetSpiralVel, smoothFast);
        lastSpiralVel = curVel;
        if (padAmt > 1e-4 && (padProg == PadProgram::spiral || padProg == PadProgram::mobius)) {
            if (mode == Mode::spiral)
                padSpiralOffset_ += (curVel - 1.0) * dCycle;
            else if (mode == Mode::mobius)
                padMobiusOffset_ += (curVel - 1.0) * dCycle;
        }

        const double effSpiralCycle = rawCycles + padSpiralOffset_;
        curBarPhase = hba::dsp::frac(effSpiralCycle);
        if (spiralDir == SpiralDir::rise)
            curSpiralPos = effSpiralCycle;
        else if (spiralDir == SpiralDir::fall)
            curSpiralPos = -effSpiralCycle;
        else
            curSpiralPos = 0.5 - 0.5 * std::cos(hba::dsp::kTwoPi * curBarPhase);

        // Update modulation lanes, X/Y macro mapping, chord manifold, and filter coefficients every 32 samples
        if ((i & (kControlInterval - 1)) == 0) {
            const int rampSteps = std::min(kControlInterval, numSamples - i);
            curLane1Phase = hba::dsp::frac(relPpq / lane1Beats);
            curLane2Phase = hba::dsp::frac(relPpq / lane2Beats);
            curLane1Val = static_cast<double>(hba::dsp::evaluate(curves_[0], curLane1Phase));
            curLane2Val = static_cast<double>(hba::dsp::evaluate(curves_[1], curLane2Phase));

            double modMorph = 0.0;
            double modBell = 0.0;
            double modFilter = 0.0;
            double modDrive = 0.0;
            double modGrav = 0.0;
            double modGate = 0.0;
            double modSpace = 0.0;

            const auto applyLane = [&](LaneTarget tgt, double val, double depth) noexcept HBA_NONBLOCKING {
                const double uni = 0.5 * (val + 1.0);
                const double bi = val;
                switch (tgt) {
                case LaneTarget::morph:
                    modMorph += (morphAuto ? uni * depth : bi * 0.5 * depth);
                    break;
                case LaneTarget::bellCenter:
                    modBell += bi * depth;
                    break;
                case LaneTarget::filter:
                    modFilter += bi * depth;
                    break;
                case LaneTarget::drive:
                    modDrive += bi * depth;
                    break;
                case LaneTarget::gravity:
                    modGrav += bi * depth;
                    break;
                case LaneTarget::rissetGate:
                    modGate += uni * depth;
                    break;
                case LaneTarget::space:
                    modSpace += bi * depth;
                    break;
                }
            };
            applyLane(lane1Target, curLane1Val, lane1Depth);
            applyLane(lane2Target, curLane2Val, lane2Depth);

            // Compute X/Y Manifold Pad macro overrides/offsets (exact 0 effect when padAmt == 0!)
            targetSpiralVel = 1.0;
            double padBellOct = 0.0;
            double padFilterOct = 0.0;
            double padResAdd = 0.0;
            double padDriveAdd = 0.0;
            double padSpaceTarget = targetSpaceBase + modSpace * 0.5;
            double padGateTarget = baseRissetGate + modGate * 0.65;
            double padGravTarget = baseGravity + modGrav * 0.5;
            double padStaggerTarget = baseStagger;
            double padBellWidthTarget = baseBellWidth;
            targetSubAnchorEff = targetSubAnchorBase;
            curSubWarmth = baseSubWarmth;
            targetDetuneCtEff = baseDetuneCt;
            targetWidthEff = baseWidth;
            voidShimmerAmt = 0.0;

            if (padAmt > 1e-4) {
                switch (padProg) {
                case PadProgram::spiral:
                    targetSpiralVel = 1.0 + padAmt * (padVelocityCurve(padX, -2.0, 3.0) - 1.0);
                    padBellOct = padAmt * (padY - 0.5) * 3.2;
                    padFilterOct = padAmt * (padY - 0.45) * 3.4;
                    padSpaceTarget += padAmt * padY * 0.35;
                    break;
                case PadProgram::mobius:
                    targetSpiralVel = 1.0 + padAmt * (padVelocityCurve(padX, -1.5, 2.5) - 1.0);
                    padGravTarget = padGravTarget + padAmt * (padY - padGravTarget);
                    padSpaceTarget += padAmt * (0.18 + 0.55 * padY);
                    targetWidthEff = std::clamp(baseWidth + padAmt * 0.35 * padY, 0.0, 1.0);
                    padFilterOct = padAmt * (padY - 0.4) * 2.2;
                    break;
                case PadProgram::copedent:
                    padStaggerTarget = baseStagger + padAmt * (padY - baseStagger);
                    padFilterOct = padAmt * (padY - 0.4) * 2.6;
                    padSpaceTarget += padAmt * padY * 0.42;
                    break;
                case PadProgram::abyss:
                    padBellOct = -padAmt * padX * 2.2;
                    targetSubAnchorEff = std::clamp(
                        targetSubAnchorBase + padAmt * (1.0 - targetSubAnchorBase) * (0.35 + 0.65 * padX), 0.0, 1.0);
                    curSubWarmth = std::clamp(baseSubWarmth + padAmt * (0.90 - baseSubWarmth) * padX, 0.0, 1.0);
                    padDriveAdd = padAmt * (0.72 * padY - 0.10);
                    padFilterOct = padAmt * ((padY - 0.5) * 3.2 - 0.8 * padX);
                    padResAdd = padAmt * 0.32 * padY;
                    break;
                case PadProgram::risset:
                    padGateTarget = padGateTarget + padAmt * (0.96 * padX - padGateTarget);
                    padFilterOct = padAmt * (padY - 0.45) * 3.2;
                    padResAdd = padAmt * 0.38 * padY;
                    padBellWidthTarget =
                        std::clamp(baseBellWidth + padAmt * (0.40 + 0.55 * padY - baseBellWidth), 0.25, 1.0);
                    break;
                case PadProgram::voidSpace:
                    padSpaceTarget = padSpaceTarget + padAmt * (0.18 + 0.82 * padX - padSpaceTarget);
                    voidShimmerAmt = padAmt * (0.25 + 0.75 * padX);
                    targetDetuneCtEff = std::clamp(baseDetuneCt + padAmt * 22.0 * padY, 0.0, 45.0);
                    targetWidthEff = std::clamp(baseWidth + padAmt * (1.0 - baseWidth) * (0.4 + 0.6 * padY), 0.0, 1.0);
                    padFilterOct = padAmt * (padY - 0.35) * 2.4;
                    break;
                }
            }

            // Morph position (0..1) — honors KeySwitch C0..D#0 and Copedent X-axis scrub!
            double rawMorph = baseMorph + static_cast<double>(modWheel_) + modMorph;
            if (padAmt > 1e-4 && padProg == PadProgram::copedent)
                rawMorph = rawMorph + padAmt * (padX - std::clamp(rawMorph, 0.0, 1.0));

            if (useKeySwitch && keySwitchScene_ >= 0 && mode != Mode::spiral) {
                const double targetKS = static_cast<double>(keySwitchScene_) /
                                        static_cast<double>(mode == Mode::mobius ? kSceneCount : (kSceneCount - 1));
                curMorph = morphSmooth_.step(targetKS, hba::dsp::onePoleCoef(0.055, invSr * kControlInterval));
            } else if (mode == Mode::mobius) {
                const double mobCycle =
                    morphAuto ? (rawCycles + padMobiusOffset_ + baseMorph + static_cast<double>(modWheel_))
                              : (rawMorph + padMobiusOffset_);
                const double dirMob = (spiralDir == SpiralDir::fall) ? -mobCycle : mobCycle;
                curMorph = hba::dsp::frac(dirMob);
                morphSmooth_.snap(curMorph);
                mobiusCycleOffset_ = static_cast<int>(std::floor(dirMob));
                prevMorphCycle_ = dirMob;
            } else {
                curMorph = morphSmooth_.step(std::clamp(rawMorph, 0.0, 1.0),
                                             hba::dsp::onePoleCoef(0.010, invSr * kControlInterval));
            }

            curGravity = std::clamp(padGravTarget, 0.0, 1.0);
            curStagger = std::clamp(padStaggerTarget, 0.0, 1.0);
            curBellWidth = padBellWidthTarget;
            curDrive = driveSmooth_.step(
                std::clamp(targetDriveBase + modDrive * 0.5 + padDriveAdd + static_cast<double>(aftertouch_) * 0.25,
                           0.0, 1.0),
                hba::dsp::onePoleCoef(0.012, invSr * kControlInterval));
            curSpace = spaceSmooth_.step(std::clamp(padSpaceTarget, 0.0, 1.0),
                                         hba::dsp::onePoleCoef(0.015, invSr * kControlInterval));
            curBellCenter = bellCenterSmooth_.step(
                std::clamp(targetBellCenter * std::pow(2.0, modBell * 2.0 + padBellOct), 80.0, 3200.0),
                hba::dsp::onePoleCoef(0.012, invSr * kControlInterval));
            const double filterModOct = modFilter * 3.0 + padFilterOct + (static_cast<double>(timbreCc_) - 0.5) * 2.0;
            curFilterHz = filterSmooth_.step(std::clamp(targetFilterBase * std::pow(2.0, filterModOct), 100.0, 18000.0),
                                             hba::dsp::onePoleCoef(0.010, invSr * kControlInterval));
            curRissetGate = std::clamp(padGateTarget, 0.0, 1.0);

            const double effFilterRes = std::clamp(baseFilterRes + padResAdd, 0.0, 0.94);
            const double filterQ = 0.707 + effFilterRes * effFilterRes * 7.5;
            filterCoefs = hba::dsp::SvfCoefs::make(curFilterHz, filterQ, sr);
            filter2Coefs = hba::dsp::SvfCoefs::make(std::clamp(curFilterHz * 1.85, 160.0, 18000.0),
                                                    std::max(0.707, filterQ * 0.85), sr);

            if (mode == Mode::spiral) {
                curActiveScene = 0;
                const int activeChords = spiralChord.count;
                for (int v = 0; v < kVoiceCount; ++v) {
                    const int ci = std::min(v, activeChords - 1);
                    targetVoicePitches[static_cast<std::size_t>(v)] =
                        rootMidi + spiralChord.intervals[static_cast<std::size_t>(ci)] + curBarPhase * ratioSemi;
                }
            } else {
                const MorphEval eval =
                    evaluateMorphPitches(matrix_, rootMidi, rootPc, scaleIdx, curMorph, curGravity, gravMode,
                                         curStagger, staggerOrder, anchorSub, mode == Mode::mobius);
                curActiveScene = eval.activeScene;
                targetVoicePitches = eval.pitches;
                const double cycleShift = mode == Mode::mobius ? static_cast<double>(mobiusCycleOffset_) * 12.0 : 0.0;
                for (int v = 0; v < kVoiceCount; ++v) {
                    const auto vi = static_cast<std::size_t>(v);
                    const double targetP = targetVoicePitches[vi] + cycleShift;
                    if (!voiceRampPrimed_) {
                        voicePitchRamp_[vi].snap(targetP);
                        voicePitchSmooth_[vi].snap(targetP);
                    } else {
                        voicePitchRamp_[vi].to(targetP, rampSteps);
                    }
                }
                voiceRampPrimed_ = true;
            }

            // Update MPE Lower Zone MIDI output cleanly at throttled intervals
            if (mpeOut != nullptr && mpeEnabled) {
                mpeThrottleCounter_ -= kControlInterval;
                if (mpeThrottleCounter_ <= 0 || !anyGate) {
                    updateMpeVoices(*mpeOut, i, targetVoicePitches, envLevel_ > 1e-4 && anyGate, mpeRangeSt,
                                    std::clamp(std::log2(curFilterHz / 120.0) / std::log2(16000.0 / 120.0), 0.0, 1.0));
                    mpeThrottleCounter_ = mpeThrottleSamples;
                }
            } else if (mpeOut != nullptr && !mpeEnabled) {
                emitMpeAllNotesOff(*mpeOut, i);
            }
        }

        const double subAnchor = subAnchorSmooth_.step(targetSubAnchorEff, smoothFast);
        const double outGain = outputGainSmooth_.step(targetOutGain, smoothFast);
        const double effDetuneCt = detuneSmooth_.step(targetDetuneCtEff, smoothFast);
        const double effWidth = widthSmooth_.step(targetWidthEff, smoothFast);
        const double detuneRatio = std::pow(2.0, (effDetuneCt * 0.5) / 1200.0);

        // Drain FDN reverb + chorus + oversampler + limiter lookahead buffers before sleeping
        const int tailDrainLimit = latencySamples() + static_cast<int>(spaceDrainSeconds(curSpace) * sr);
        if (envLevel_ < kSilentEnvelope && !anyGate) {
            if (silentTailSamples_ >= tailDrainLimit) {
                outL[i] = 0.0f;
                outR[i] = 0.0f;
                continue;
            }
            ++silentTailSamples_;
        } else {
            silentTailSamples_ = 0;
        }

        // Musical formant center stays in natural vocal range (1.6 .. 6.2x) instead of whistling at high filterHz!
        const double formantRatio = std::clamp(1.6 + std::log2(std::max(120.0, curFilterHz) / 120.0) * 0.68, 1.6, 6.2);
        const double foldAmt = 0.2 + curDrive * 0.8;
        const double pw = 0.5 + 0.28 * std::sin(hba::dsp::kTwoPi * curLane2Phase);

        double sumBodyL = 0.0;
        double sumBodyR = 0.0;
        double sumSub = 0.0;

        const ModeRenderArgs modeArgs{
            sr,
            invSr,
            rootMidi,
            curSpiralPos,
            ratioSemi,
            spiralChord,
            curBellCenter,
            curBellWidth,
            detuneRatio,
            effWidth,
            voicePitchCoef,
            timbre,
            formantRatio,
            foldAmt,
            pw,
            &targetVoicePitches,
            i == numSamples - 1,
        };
        if (mode == Mode::spiral)
            renderSpiralSample(modeArgs, sumBodyL, sumBodyR, sumSub, lastEffectiveBellHz);
        else if (mode == Mode::copedent)
            renderCopedentSample(modeArgs, sumBodyL, sumBodyR, sumSub);
        else
            renderMobiusSample(modeArgs, sumBodyL, sumBodyR, sumSub);

        // Split stereo body (> subCutoffHz) from mono Sub-Anchor (< subCutoffHz) cleanly via LR4!
        double bodyL = bodyHpL_.process(subSplitCoefs, sumBodyL);
        double bodyR = bodyHpR_.process(subSplitCoefs, sumBodyR);
        double subMono = subLp_.process(subSplitCoefs, sumSub) * subAnchor;
        if (curSubWarmth > 0.005) {
            const double warmed =
                subMono + curSubWarmth * 0.28 * (std::tanh(2.4 * subMono) + 0.30 * (subMono * subMono - 0.12));
            subMono = subWarmthLp_.lowpass(subWarmthCoefs, warmed);
        }

        // HIIR Oversampled ADAA2 Nonlinear Waveshaper on stereo upper body (constant latency!)
        {
            const int osN = oversamplerL_.factor();
            const double driveGain = 1.0 + curDrive * 6.5;
            const double wetMix = std::clamp(curDrive * 1.25, 0.0, 1.0);
            const auto runChannel = [&](double inVal, hba::dsp::Oversampler& os,
                                        hba::dsp::MultiShaperAdaa2& shaper) noexcept HBA_NONBLOCKING {
                std::array<float, 8> upBuf{};
                os.up(static_cast<float>(inVal), upBuf.data());
                for (int s = 0; s < osN; ++s) {
                    const double x = static_cast<double>(upBuf[static_cast<std::size_t>(s)]);
                    if (curDrive > 0.005) {
                        double dryAligned = 0.0;
                        const double wet = shaper.process(x, driveGain, 0.0, shaperModel, shaperModel, 1.0, dryAligned);
                        upBuf[static_cast<std::size_t>(s)] =
                            static_cast<float>(dryAligned + wetMix * (wet * 0.78 - dryAligned));
                    } else {
                        upBuf[static_cast<std::size_t>(s)] = static_cast<float>(shaper.bypass(x));
                    }
                }
                return static_cast<double>(os.down(upBuf.data()));
            };
            bodyL = runChannel(bodyL, oversamplerL_, shaperL_);
            bodyR = runChannel(bodyR, oversamplerR_, shaperR_);
        }

        // 4-Mode Dual-Stage Cascaded Cytomic TPT SVF (LP 12 / LP 24 / Bandpass / Neuro Notch!)
        switch (filterMode) {
        case FilterMode::lp12:
            bodyL = filterL_.lowpass(filterCoefs, bodyL);
            bodyR = filterR_.lowpass(filterCoefs, bodyR);
            break;
        case FilterMode::lp24:
            bodyL = filter2L_.lowpass(filterCoefs, filterL_.lowpass(filterCoefs, bodyL));
            bodyR = filter2R_.lowpass(filterCoefs, filterR_.lowpass(filterCoefs, bodyR));
            break;
        case FilterMode::bandpass:
            bodyL = filterL_.bandpass(filterCoefs, bodyL) * 1.45;
            bodyR = filterR_.bandpass(filterCoefs, bodyR) * 1.45;
            break;
        case FilterMode::neuroNotch: {
            const double bpL1 = filterL_.bandpass(filterCoefs, bodyL);
            const double bpR1 = filterR_.bandpass(filterCoefs, bodyR);
            const double bpL2 = filter2L_.bandpass(filter2Coefs, bodyL);
            const double bpR2 = filter2R_.bandpass(filter2Coefs, bodyR);
            bodyL = bodyL - 1.38 * bpL1 + 0.34 * bpL2;
            bodyR = bodyR - 1.38 * bpR1 + 0.34 * bpR2;
            break;
        }
        }

        // Optional Risset Eternal Accelerando rhythmic pulse gate coupled to bar spiral!
        // Placed BEFORE Manifold Space so Reverb Halo rings out naturally between gated stabs!
        double gateGain = 1.0;
        if (curRissetGate > 0.005) {
            constexpr double kInvLn2 = 1.4426950408889634;
            const double p = curBarPhase;
            const double pulseA = 0.5 + 0.5 * std::cos(hba::dsp::kTwoPi * 4.0 * (std::pow(2.0, p) - 1.0) * kInvLn2);
            const double pulseB = 0.5 + 0.5 * std::cos(hba::dsp::kTwoPi * 8.0 * (std::pow(2.0, p) - 1.0) * kInvLn2);
            const double wA = std::sin(hba::dsp::kPi * (0.5 * p + 0.5));
            const double wB = std::sin(hba::dsp::kPi * (0.5 * p));
            const double pulse = (wA * wA * pulseA + wB * wB * pulseB) / std::max(1e-6, wA * wA + wB * wB);
            gateGain = (1.0 - curRissetGate) + curRissetGate * (0.12 + 0.88 * pulse);
        }
        lastGateEnv = gateGain;

        const double gatedBodyL = bodyL * gateGain * envLevel_;
        const double gatedBodyR = bodyR * gateGain * envLevel_;

        // Upper-Band Stereo Dimension Chorus + 4-Tap Hadamard FDN Manifold Reverb (strictly > subCutoffHz!)
        double wetSpaceL = gatedBodyL;
        double wetSpaceR = gatedBodyR;
        {
            spaceBufL_[static_cast<std::size_t>(spaceWritePos_)] = gatedBodyL;
            spaceBufR_[static_cast<std::size_t>(spaceWritePos_)] = gatedBodyR;
            spaceLfo1_ = hba::dsp::frac(spaceLfo1_ + 0.29 * invSr);
            spaceLfo2_ = hba::dsp::frac(spaceLfo2_ + 0.47 * invSr);
            spaceLfo3_ = hba::dsp::frac(spaceLfo3_ + 0.73 * invSr);

            if (curSpace > 0.005) {
                const auto readDelay = [&](const std::array<double, kSpaceBufSize>& buf,
                                           double delaySamples) noexcept HBA_NONBLOCKING {
                    const double d = std::clamp(delaySamples, 2.0, static_cast<double>(kSpaceBufSize - 4));
                    const double readPos = static_cast<double>(spaceWritePos_) - d;
                    const double wrapped = readPos < 0.0 ? readPos + static_cast<double>(kSpaceBufSize) : readPos;
                    const int i0 = static_cast<int>(wrapped) & kSpaceBufMask;
                    const int i1 = (i0 + 1) & kSpaceBufMask;
                    const double frac = wrapped - std::floor(wrapped);
                    return buf[static_cast<std::size_t>(i0)] +
                           frac * (buf[static_cast<std::size_t>(i1)] - buf[static_cast<std::size_t>(i0)]);
                };
                const double ms = sr * 0.001;
                const double s1 = std::sin(hba::dsp::kTwoPi * spaceLfo1_);
                const double s2 = std::sin(hba::dsp::kTwoPi * spaceLfo2_);
                const double s3 = std::sin(hba::dsp::kTwoPi * spaceLfo3_);
                const double chL = 0.42 * readDelay(spaceBufL_, (7.5 + 3.2 * s1) * ms) +
                                   0.38 * readDelay(spaceBufR_, (14.2 + 4.1 * s2) * ms) +
                                   0.26 * readDelay(spaceBufL_, (21.5 + 2.8 * s3) * ms);
                const double chR = 0.42 * readDelay(spaceBufR_, (8.1 - 3.2 * s1) * ms) +
                                   0.38 * readDelay(spaceBufL_, (15.4 - 4.1 * s2) * ms) +
                                   0.26 * readDelay(spaceBufR_, (22.8 - 2.8 * s3) * ms);

                // Stage 2: 4-Tap Hadamard Orthogonal FDN Diffusion Halo
                constexpr std::array<double, kFdnLines> kLfoHz{0.19, 0.27, 0.34, 0.43};
                std::array<double, kFdnLines> tap{};
                for (int j = 0; j < kFdnLines; ++j) {
                    const auto ji = static_cast<std::size_t>(j);
                    const auto& buf = fdnBuf_[ji];
                    const int len = static_cast<int>(buf.size());
                    if (len == 0)
                        continue;
                    fdnLfo_[ji] = hba::dsp::frac(fdnLfo_[ji] + kLfoHz[ji] * invSr);
                    const double modSamp = kFdnModulationMs * ms * std::sin(hba::dsp::kTwoPi * fdnLfo_[ji]);
                    const double baseD = static_cast<double>(fdnDelaySamples_[ji]) + modSamp;
                    double rpos = static_cast<double>(fdnPos_[ji]) - baseD;
                    if (rpos < 0.0)
                        rpos += static_cast<double>(len);
                    const int i0 = static_cast<int>(rpos) % len;
                    const int i1 = (i0 + 1) % len;
                    const double fr = rpos - std::floor(rpos);
                    double v = buf[static_cast<std::size_t>(i0)] +
                               fr * (buf[static_cast<std::size_t>(i1)] - buf[static_cast<std::size_t>(i0)]);
                    // Warm loop damping (keeps tail silky, zero harshness, zero sub mud)
                    fdnLp_[ji] += fdnLpCoef * (v - fdnLp_[ji]);
                    fdnHp_[ji] += fdnHpCoef * (fdnLp_[ji] - fdnHp_[ji]);
                    tap[ji] = fdnLp_[ji] - fdnHp_[ji];
                }

                // Orthogonal 4x4 Hadamard matrix (0.5 * H4): norm-preserving diffusion
                const double h0 = 0.5 * (tap[0] + tap[1] + tap[2] + tap[3]);
                const double h1 = 0.5 * (tap[0] - tap[1] + tap[2] - tap[3]);
                const double h2 = 0.5 * (tap[0] + tap[1] - tap[2] - tap[3]);
                const double h3 = 0.5 * (tap[0] - tap[1] - tap[2] + tap[3]);

                const double fb = std::clamp(0.24 + 0.48 * curSpace + 0.10 * voidShimmerAmt, 0.24, 0.80);
                const std::array<double, kFdnLines> inject{
                    gatedBodyL * 0.55 + chL * 0.25,
                    gatedBodyR * 0.55 + chR * 0.25,
                    (gatedBodyL - gatedBodyR) * 0.35 + chR * 0.20,
                    (gatedBodyL + gatedBodyR) * 0.32 - chL * 0.20,
                };
                const std::array<double, kFdnLines> fbVec{h0, h1, h2, h3};
                for (int j = 0; j < kFdnLines; ++j) {
                    const auto ji = static_cast<std::size_t>(j);
                    const int len = static_cast<int>(fdnBuf_[ji].size());
                    if (len == 0)
                        continue;
                    fdnBuf_[ji][static_cast<std::size_t>(fdnPos_[ji])] = inject[ji] + fb * fbVec[ji];
                    fdnPos_[ji] = (fdnPos_[ji] + 1 == len) ? 0 : fdnPos_[ji] + 1;
                }

                const double revL = 0.58 * (tap[0] + tap[2]) + 0.24 * (tap[1] - tap[3]);
                const double revR = 0.58 * (tap[1] + tap[3]) + 0.24 * (tap[2] - tap[0]);
                const double chMix = curSpace * 0.42;
                const double revMix = std::pow(curSpace, 1.25) * (0.48 + 0.20 * voidShimmerAmt);
                wetSpaceL = gatedBodyL * (1.0 - chMix * 0.28) + chL * chMix + revL * revMix;
                wetSpaceR = gatedBodyR * (1.0 - chMix * 0.28) + chR * chMix + revR * revMix;
            }
            spaceWritePos_ = (spaceWritePos_ + 1) & kSpaceBufMask;
        }

        // Recombine gated + spatial stereo upper body + rock-solid mono Sub-Anchor Floor!
        const double subOut = subMono * (0.75 + 0.25 * gateGain) * envLevel_;
        double finalL = (wetSpaceL + subOut) * outGain;
        double finalR = (wetSpaceR + subOut) * outGain;

        finalL = dcL_.process(finalL);
        finalR = dcR_.process(finalR);

        if (!useLimiter)
            hba::dsp::softClipLinked(finalL, finalR, 0.88, 1.0);
        // Always run limiter lookahead delay line (amount=0 when bypassed) so PDC latency stays constant!
        limiter_.process(finalL, finalR, 0.944, useLimiter ? 1.0 : 0.0); // -0.5 dBTP ceiling

        outL[i] = static_cast<float>(finalL);
        outR[i] = static_cast<float>(finalR);

        blockPeakL = std::max(blockPeakL, std::abs(outL[i]));
        blockPeakR = std::max(blockPeakR, std::abs(outR[i]));
        blockSubPeak = std::max(blockSubPeak, static_cast<float>(std::abs(subOut * outGain)));
        blockRmsSum += 0.5 * (finalL * finalL + finalR * finalR);
    }

    // Publish lock-free telemetry snapshot for 60 fps Visage UI!
    telemetry_.mode = static_cast<int>(mode);
    telemetry_.barPhase = static_cast<float>(curBarPhase);
    telemetry_.spiralPos = static_cast<float>(curSpiralPos);
    telemetry_.morphPos = static_cast<float>(curMorph);
    telemetry_.lane1Value = static_cast<float>(curLane1Val);
    telemetry_.lane2Value = static_cast<float>(curLane2Val);
    telemetry_.lane1Phase = static_cast<float>(curLane1Phase);
    telemetry_.lane2Phase = static_cast<float>(curLane2Phase);
    telemetry_.activeScene = curActiveScene;
    const double curBeatTotal = curBarPhase * loopBeats;
    telemetry_.barNumber = static_cast<int>(std::floor(curBeatTotal / 4.0)) + 1;
    telemetry_.beatNumber = static_cast<int>(std::floor(std::fmod(curBeatTotal, 4.0))) + 1;
    telemetry_.bpm = static_cast<float>(bpm);
    telemetry_.gateLevel = static_cast<float>(lastGateEnv);
    telemetry_.sounding = envLevel_ > 1e-3;
    for (int v = 0; v < kVoiceCount; ++v)
        telemetry_.voicePitches[static_cast<std::size_t>(v)] =
            static_cast<float>(targetVoicePitches[static_cast<std::size_t>(v)]);
    telemetry_.peakL = blockPeakL;
    telemetry_.peakR = blockPeakR;
    telemetry_.subPeak = blockSubPeak;
    telemetry_.rmsLevel = static_cast<float>(std::sqrt(blockRmsSum / static_cast<double>(numSamples)));
    telemetry_.limiterGain = static_cast<float>(limiter_.gain());
    telemetry_.effectiveBellHz = static_cast<float>(lastEffectiveBellHz);
    telemetry_.effectiveFilterHz = static_cast<float>(curFilterHz);
    telemetry_.effectiveSpace = static_cast<float>(curSpace);
    telemetry_.effectiveDrive = static_cast<float>(curDrive);
    telemetry_.effectiveSubAnchor = static_cast<float>(targetSubAnchorEff);
    telemetry_.spiralVel = static_cast<float>(lastSpiralVel);
    telemetry_.padAmount = static_cast<float>(lastPadAmt);
    telemetry_.padActive = padEngaged;
    telemetry_.keySwitchScene = useKeySwitch ? keySwitchScene_ : -1;

    // Publish currently held MIDI notes (up to 5 lowest) for one-click Live Chord Capture!
    int heldCount = 0;
    for (int k = 0; k < 128 && heldCount < kVoiceCount; ++k) {
        if (keysDown_[static_cast<std::size_t>(k)])
            telemetry_.heldNotes[static_cast<std::size_t>(heldCount++)] = k;
    }
    telemetry_.heldNoteCount = heldCount;
}

} // namespace tg
