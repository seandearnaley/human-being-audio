#include "pf/engine.h"

#include "pole_zero.h"

#include <hba/dsp/control.h>
#include <hba/dsp/crossover.h>
#include <hba/dsp/denormals.h>
#include <hba/dsp/limiter.h>
#include <hba/dsp/oversampling.h>
#include <hba/dsp/svf.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace pf {

namespace {
constexpr double kControlRateSec = 0.004;   // 4 ms parameter smoothing
constexpr double kStageMorphSec = 0.006;    // 6 ms log-polar Z-plane cube morphing
constexpr double kStageEnableSec = 0.008;   // 8 ms 4P/8P/12P/14P stage fade
constexpr double kModeXfadeSec = 0.010;     // 10 ms topology/drivePos/routing crossfade
constexpr int kMaxHeldNotes = 16;
constexpr int kRootMidiNote = 29;           // F1 (43.65 Hz) = 0 semitones when pitch-tracking
constexpr double kRootHz = 43.653528929125486;
} // namespace

struct Engine::Impl {
    double sampleRate = 48000.0;
    int osFactor = 4;
    double osRate = 192000.0;
    double controlCoef = 0.01;
    double stageMorphCoef = 0.02;
    double stageEnableCoef = 0.005;
    double xfadeStep = 0.002;

    // Exact 0.00 dB flat LR4 Sub Lock crossover + Dome Hilbert reference alignment:
    //   Sub = subFit(LR4_LP(inMono))
    //   Pre-lattice high = LR4_HP(in)  (-39 dB at f0/3 -> prevents drive IM in sub band)
    //   Post-lattice Mid passes unfiltered at subSplit -> Sub + WetMid == exact 0.00 dB LR4_AP!
    //   Post-lattice Side runs LR4_HP(subSplit) so sub band stays < -65 dB strictly mono!
    std::array<hba::dsp::Lr4Split, 2> subSplit{};
    hba::dsp::Lr4Highpass postSideHp{};
    std::array<hba::dsp::Lr4Allpass, 2> dryAp{};
    hba::dsp::PhaseRotator subFit{};
    std::array<hba::dsp::PhaseRotator, 2> wetFitRef{};
    std::array<hba::dsp::PhaseRotator, 2> dryFitRef{};

    // Matched HIIR upsamplers/decimators for wet L/R, dry L/R, mono sub, and audio-rate FM.
    std::array<hba::dsp::Oversampler, 2> wetOs{};
    std::array<hba::dsp::Oversampler, 2> dryOs{};
    hba::dsp::Oversampler subOs{};
    hba::dsp::Oversampler fmOs{};

    // Primary lattice + standby crossfade lattice (only active during 10 ms mode switches).
    std::array<dsp::PoleZeroLattice, 2> lattice {};
    std::array<dsp::PoleZeroLattice, 2> xfadeLattice{};
    Topology activeTopology = Topology::serial;
    DrivePos activeDrivePos = DrivePos::inter;
    StereoMode activeStereoMode = StereoMode::stereo;
    Topology prevTopology = Topology::serial;
    DrivePos prevDrivePos = DrivePos::inter;
    StereoMode prevStereoMode = StereoMode::stereo;
    double xfadeMix = 1.0;
    bool modeInitialised = false;

    // 2-oversampled-sample alignment shift registers for dry L/R, sub, and M/S pass-through.
    std::array<std::array<double, 2>, 2> dryOsDelay{};
    std::array<double, 2> subOsDelay{};
    std::array<double, 2> msPassDelay{};
    std::array<double, 2> xfadeMsPassDelay{};

    hba::dsp::Limiter limiter{};

    // Smoothers
    hba::dsp::Smoother morphX{}, morphY{}, morphZ{}, freq{}, res{}, transform{}, drive{}, asym{}, spread{}, mix{},
        subLock{}, subSplitHz{}, fitCos{}, fitSin{}, inputGain{}, outputGain{}, limitAmt{}, ceiling{}, midiPitch{},
        pitchBendSmooth{}, fmAmount{}, envSensGain{};

    // Per-stage log-polar smoothers (L & R) for click-free Cube morphing.
    struct SmoothStage {
        hba::dsp::Smoother logPoleHz{}, logPoleQ{}, logZeroHz{}, logZeroQ{};
        void reset() noexcept PF_NONBLOCKING {
            logPoleHz.reset();
            logPoleQ.reset();
            logZeroHz.reset();
            logZeroQ.reset();
        }
    };
    std::array<SmoothStage, kNumStages> smStagesL{};
    std::array<SmoothStage, kNumStages> smStagesR{};
    hba::dsp::Smoother smMakeupL{}, smMakeupR{};
    std::array<hba::dsp::Smoother, kNumStages> smStageEnable{};

    // Input/sidechain Envelope Follower state
    double envState = 0.0;
    // Audio-rate FM pitch-tracked sine oscillator phase
    double fmSinePhase = 0.0;

    LaneCurves laneCurves = defaultLaneCurves();
    ImprintData imprint {};

    // Monophonic MIDI note stack for harmonic pitch tracking & expression
    std::array<int, kMaxHeldNotes> noteStack {};
    int noteCount = 0;
    int activeNote = -1;
    float pitchBendSt = 0.0f;
    double modWheelTarget = 0.0;
    double timbreOffsetTarget = 0.0;
    double aftertouchTarget = 0.0;
    double freePpq = 0.0;
    std::uint64_t freeSamples = 0;
    std::uint64_t noteSamples = 0;

    // Published telemetry for UI
    double lastPpq = 0.0;
    double lastNotePpq = 0.0;
    double lastFreeSec = 0.0;
    double lastNoteSec = 0.0;
    std::array<float, kLaneCount> lastLanePhase {};
    std::array<float, kLaneCount> lastLaneValue {};
    float lastGrDb = 0.0f;
    EffectiveState effective {};

    // Persistent lattice coefficient state for exact block-size invariance
    std::uint64_t sampleIndex = 0;
    std::array<dsp::PoleZeroCoefs, kNumStages> coefsL {};
    std::array<dsp::PoleZeroCoefs, kNumStages> coefsR {};
    std::array<double, kNumStages> stageMix{1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
    double activeStageWeight = 7.0;
    hba::dsp::SvfCoefs splitCoefs{};
    double makeupL = 1.0;
    double makeupR = 1.0;
    double prevMakeupL = 1.0;
    double prevMakeupR = 1.0;
    bool snapNewMakeup = false;

    void prepare(double sr, int /*maxBlock*/, int factor) {
        sampleRate = std::max(sr, 22050.0);
        osFactor = (factor == 2 || factor == 4 || factor == 8) ? factor : 4;
        osRate = sampleRate * static_cast<double>(osFactor);
        controlCoef = hba::dsp::onePoleCoef(kControlRateSec, 1.0 / sampleRate);
        stageMorphCoef = hba::dsp::onePoleCoef(kStageMorphSec, 8.0 / sampleRate);
        stageEnableCoef = hba::dsp::onePoleCoef(kStageEnableSec, 1.0 / sampleRate);
        xfadeStep = 1.0 / std::max(1.0, kModeXfadeSec * osRate);

        for (auto& os : wetOs) os.prepare(osFactor);
        for (auto& os : dryOs) os.prepare(osFactor);
        subOs.prepare(osFactor);
        fmOs.prepare(osFactor);
        subFit.prepare(sampleRate);
        for (auto& r : wetFitRef)
            r.prepare(sampleRate);
        for (auto& r : dryFitRef)
            r.prepare(sampleRate);
        for (auto& lat : lattice) lat.prepare(osRate);
        for (auto& lat : xfadeLattice)
            lat.prepare(osRate);
        limiter.prepare(sampleRate);
        reset();
    }

    void reset() noexcept PF_NONBLOCKING {
        for (auto& sp : subSplit) sp.reset();
        postSideHp.reset();
        for (auto& ap : dryAp)
            ap.reset();
        subFit.reset();
        for (auto& r : wetFitRef)
            r.reset();
        for (auto& r : dryFitRef)
            r.reset();
        for (auto& os : wetOs) os.reset();
        for (auto& os : dryOs) os.reset();
        subOs.reset();
        fmOs.reset();
        for (auto& lat : lattice) lat.reset();
        for (auto& lat : xfadeLattice)
            lat.reset();
        limiter.reset();
        dryOsDelay = {};
        subOsDelay = {};
        msPassDelay = {};
        xfadeMsPassDelay = {};
        xfadeMix = 1.0;
        modeInitialised = false;
        prevMakeupL = 1.0;
        prevMakeupR = 1.0;
        snapNewMakeup = false;

        morphX.reset();
        morphY.reset();
        morphZ.reset();
        freq.reset();
        res.reset();
        transform.reset();
        drive.reset();
        asym.reset();
        spread.reset();
        mix.reset();
        subLock.reset();
        subSplitHz.reset();
        fitCos.reset();
        fitSin.reset();
        inputGain.reset();
        outputGain.reset();
        limitAmt.reset();
        ceiling.reset();
        midiPitch.reset();
        pitchBendSmooth.reset();
        fmAmount.reset();
        envSensGain.reset();
        for (auto& st : smStagesL)
            st.reset();
        for (auto& st : smStagesR)
            st.reset();
        smMakeupL.reset();
        smMakeupR.reset();
        for (auto& se : smStageEnable)
            se.reset();

        envState = 0.0;
        fmSinePhase = 0.0;
        splitCoefs = hba::dsp::butterworth(110.0, sampleRate);
        noteCount = 0;
        activeNote = -1;
        pitchBendSt = 0.0f;
        modWheelTarget = 0.0;
        timbreOffsetTarget = 0.0;
        aftertouchTarget = 0.0;
        freePpq = 0.0;
        freeSamples = 0;
        noteSamples = 0;
        lastPpq = 0.0;
        lastNotePpq = 0.0;
        lastFreeSec = 0.0;
        lastNoteSec = 0.0;
        lastGrDb = 0.0f;
        sampleIndex = 0;
    }

    void noteOn(int note) noexcept PF_NONBLOCKING {
        noteOff(note);
        if (noteCount < kMaxHeldNotes)
            noteStack[static_cast<std::size_t>(noteCount++)] = note;
        else
            noteStack[kMaxHeldNotes - 1] = note;
        activeNote = note;
        noteSamples = 0;
    }

    void noteOff(int note) noexcept PF_NONBLOCKING {
        for (int i = 0; i < noteCount; ++i) {
            if (noteStack[static_cast<std::size_t>(i)] == note) {
                for (int j = i; j < noteCount - 1; ++j)
                    noteStack[static_cast<std::size_t>(j)] = noteStack[static_cast<std::size_t>(j + 1)];
                --noteCount;
                break;
            }
        }
        if (noteCount > 0) activeNote = noteStack[static_cast<std::size_t>(noteCount - 1)];
    }

    static double pushDelay2(std::array<double, 2>& d, double x) noexcept PF_NONBLOCKING {
        const double out = d[1];
        d[1] = d[0];
        d[0] = x;
        return out;
    }

    void processRouted(std::array<dsp::PoleZeroLattice, 2>& lat, std::array<double, 2>& msDel, StereoMode mode,
                       Topology topo, DrivePos dPos, double inL, double inR, double mkL, double mkR, double drive01,
                       double asym01, FoldType foldType, double fmScale, double& outL,
                       double& outR) noexcept PF_NONBLOCKING {
        if (mode == StereoMode::stereo) {
            outL = lat[0].process(inL, coefsL, stageMix, activeStageWeight, topo, mkL, drive01, asym01, foldType, dPos,
                                  fmScale);
            outR = lat[1].process(inR, coefsR, stageMix, activeStageWeight, topo, mkR, drive01, asym01, foldType, dPos,
                                  fmScale);
        } else if (mode == StereoMode::mid) {
            const double mid = 0.5 * (inL + inR);
            const double side = 0.5 * (inL - inR);
            const double midWet = lat[0].process(mid, coefsL, stageMix, activeStageWeight, topo, mkL, drive01, asym01,
                                                 foldType, dPos, fmScale);
            const double sideDel = pushDelay2(msDel, side);
            outL = midWet + sideDel;
            outR = midWet - sideDel;
        } else {
            const double mid = 0.5 * (inL + inR);
            const double side = 0.5 * (inL - inR);
            const double midDel = pushDelay2(msDel, mid);
            const double sideWet = lat[1].process(side, coefsR, stageMix, activeStageWeight, topo, mkR, drive01, asym01,
                                                  foldType, dPos, fmScale);
            outL = midDel + sideWet;
            outR = midDel - sideWet;
        }
    }

    void process(const Params& params, const Transport& transport, float* left, float* right, int numSamples,
                 const float* scLeft, const float* scRight) noexcept PF_NONBLOCKING {
        hba::dsp::ScopedFlushDenormals flush;
        if (numSamples <= 0 || left == nullptr || right == nullptr) return;

        const double bpm = std::clamp(transport.bpm, 20.0, 400.0);
        const double quartersPerSample = bpm / (60.0 * sampleRate);

        const int cubeIdx = params.choice(ParamId::cube);
        const auto topology = static_cast<Topology>(std::clamp(params.choice(ParamId::topology), 0, 2));
        const int activeStages = kStageCounts[static_cast<std::size_t>(std::clamp(params.choice(ParamId::stages), 0, 3))];
        const auto stereoMode = static_cast<StereoMode>(std::clamp(params.choice(ParamId::stereoMode), 0, 2));
        const auto foldType = static_cast<FoldType>(
            std::clamp(params.choice(ParamId::foldType), 0, static_cast<int>(choices::foldType.size()) - 1));
        const auto drivePos = static_cast<DrivePos>(std::clamp(params.choice(ParamId::drivePos), 0, 2));
        const auto fmSource = static_cast<FmSource>(std::clamp(params.choice(ParamId::fmSource), 0, 2));
        const auto envSource = static_cast<EnvSource>(std::clamp(params.choice(ParamId::envSource), 0, 1));
        const bool pitchTrackOn = params.on(ParamId::pitchTrack);
        const double glideSec = static_cast<double>(params[ParamId::glide]) * 0.001;
        const double glideCoef = glideSec > 1.0e-4 ? hba::dsp::onePoleCoef(glideSec, 1.0 / sampleRate) : 1.0;

        // Trigger click-free 10 ms equal-power crossfade if Topology, DrivePos, or Routing changed.
        if (!modeInitialised) {
            activeTopology = prevTopology = topology;
            activeDrivePos = prevDrivePos = drivePos;
            activeStereoMode = prevStereoMode = stereoMode;
            xfadeMix = 1.0;
            modeInitialised = true;
        } else if (topology != activeTopology || drivePos != activeDrivePos || stereoMode != activeStereoMode) {
            prevTopology = activeTopology;
            prevDrivePos = activeDrivePos;
            prevStereoMode = activeStereoMode;
            prevMakeupL = makeupL;
            prevMakeupR = makeupR;
            // Hand current live states to xfadeLattice and reset new active lattice cleanly.
            xfadeLattice = lattice;
            xfadeMsPassDelay = msPassDelay;
            for (auto& lat : lattice)
                lat.reset();
            msPassDelay = {};
            activeTopology = topology;
            activeDrivePos = drivePos;
            activeStereoMode = stereoMode;
            xfadeMix = 0.0;
            snapNewMakeup = true;
        }

        const double targetNoteShift =
            (pitchTrackOn && activeNote >= 0) ? static_cast<double>(activeNote - kRootMidiNote) : 0.0;
        const double targetBendShift = static_cast<double>(pitchBendSt);

        const double targetInGain = hba::dsp::dbToGain(static_cast<double>(params[ParamId::inputGain]));
        const double targetOutGain = hba::dsp::dbToGain(static_cast<double>(params[ParamId::outputGain]));
        const double targetCeiling = hba::dsp::dbToGain(static_cast<double>(params[ParamId::ceiling]));
        const double targetLimit = params.on(ParamId::limit) ? 1.0 : 0.0;
        const double targetSubLock = params.on(ParamId::subLock) ? 1.0 : 0.0;
        const double targetEnvSens = hba::dsp::dbToGain(static_cast<double>(params[ParamId::envSens]));
        const double envAtkCoef = hba::dsp::onePoleCoef(
            std::max(0.001, static_cast<double>(params[ParamId::envAttack]) * 0.001), 1.0 / sampleRate);
        const double envRelCoef = hba::dsp::onePoleCoef(
            std::max(0.005, static_cast<double>(params[ParamId::envRelease]) * 0.001), 1.0 / sampleRate);

        constexpr std::uint64_t kCoefStride = 8;
        double maxGrDb = 0.0;

        for (int n = 0; n < numSamples; ++n) {
            const double ppq = transport.playing ? transport.ppq + quartersPerSample * n : freePpq;
            if (!transport.playing) freePpq += quartersPerSample;
            else freePpq = ppq;
            const double freeSec = static_cast<double>(freeSamples) / sampleRate;
            const double noteSec = static_cast<double>(noteSamples) / sampleRate;
            const double notePpq = static_cast<double>(noteSamples) * quartersPerSample;
            ++freeSamples;
            ++noteSamples;

            const double rawInL = static_cast<double>(left[n]);
            const double rawInR = static_cast<double>(right[n]);
            const double scL = (scLeft != nullptr) ? static_cast<double>(scLeft[n]) : rawInL;
            const double scR = (scRight != nullptr) ? static_cast<double>(scRight[n]) : rawInR;

            const double sInGain = inputGain.step(targetInGain, controlCoef);
            const double sEnvSens = envSensGain.step(targetEnvSens, controlCoef);

            // Envelope Follower detector (Input or External Sidechain).
            const double detL = (envSource == EnvSource::sidechain && scLeft != nullptr) ? scL : (rawInL * sInGain);
            const double detR = (envSource == EnvSource::sidechain && scRight != nullptr) ? scR : (rawInR * sInGain);
            const double detMag = std::max(std::abs(detL), std::abs(detR)) * sEnvSens;
            const double detSat = detMag / (1.0 + detMag * 0.65); // soft knee into [0..1.5]
            const double envTarget = std::clamp(detSat, 0.0, 1.0);
            envState += (envTarget > envState ? envAtkCoef : envRelCoef) * (envTarget - envState);

            // Evaluate lanes every sample (with live envState feeding any Shape == Env lane).
            std::array<float, kLaneCount> sends {};
            for (int l = 0; l < kLaneCount; ++l)
                sends[static_cast<std::size_t>(l)] =
                    static_cast<float>(laneSend(params, laneCurves, l, ppq, envState, notePpq, freeSec, noteSec));

            const double tX = std::clamp(static_cast<double>(laneApply(LaneTarget::morphX, params[ParamId::morphX],
                                                                       laneSum(params, sends, LaneTarget::morphX))) +
                                             timbreOffsetTarget,
                                         0.0, 1.0);
            const double tY = std::clamp(static_cast<double>(laneApply(LaneTarget::morphY, params[ParamId::morphY],
                                                                       laneSum(params, sends, LaneTarget::morphY))) +
                                             0.45 * modWheelTarget,
                                         0.0, 1.0);
            const double tZ = std::clamp(static_cast<double>(laneApply(LaneTarget::morphZ, params[ParamId::morphZ],
                                                                       laneSum(params, sends, LaneTarget::morphZ))) +
                                             0.40 * aftertouchTarget,
                                         0.0, 1.0);
            const double tFreq = std::clamp(static_cast<double>(laneApply(LaneTarget::freq, params[ParamId::freq],
                                                                          laneSum(params, sends, LaneTarget::freq))),
                                            -48.0, 48.0);
            const double tRes = std::clamp(static_cast<double>(laneApply(LaneTarget::res, params[ParamId::res],
                                                                         laneSum(params, sends, LaneTarget::res))),
                                           0.0, 1.0);
            const double tXform =
                std::clamp(static_cast<double>(laneApply(LaneTarget::transform, params[ParamId::transform],
                                                         laneSum(params, sends, LaneTarget::transform))),
                           -1.0, 1.0);
            const double tDrive = std::clamp(static_cast<double>(laneApply(LaneTarget::drive, params[ParamId::drive],
                                                                           laneSum(params, sends, LaneTarget::drive))) +
                                                 0.25 * aftertouchTarget,
                                             0.0, 1.0);
            const double tSpread =
                std::clamp(static_cast<double>(laneApply(LaneTarget::spread, params[ParamId::spread],
                                                         laneSum(params, sends, LaneTarget::spread))),
                           0.0, 1.0);
            const double tMix = std::clamp(static_cast<double>(laneApply(LaneTarget::mix, params[ParamId::mix],
                                                                         laneSum(params, sends, LaneTarget::mix))),
                                           0.0, 1.0);
            const double tFm = std::clamp(static_cast<double>(laneApply(LaneTarget::fm, params[ParamId::fmAmount],
                                                                        laneSum(params, sends, LaneTarget::fm))),
                                          0.0, 1.0);

            // Smooth Fit in (cos, sin) vector form so 359 -> 0 deg wraps continuously with zero glitch!
            const double rawFit = static_cast<double>(
                laneApply(LaneTarget::fit, params[ParamId::fit], laneSum(params, sends, LaneTarget::fit)));
            const double fitRad = hba::dsp::frac(rawFit / 360.0) * hba::dsp::kTwoPi;
            const double scFit = fitCos.step(std::cos(fitRad), controlCoef);
            const double ssFit = fitSin.step(std::sin(fitRad), controlCoef);
            const double fitNorm = std::hypot(scFit, ssFit);
            const double cFit = fitNorm > 1.0e-5 ? (scFit / fitNorm) : 1.0;
            const double sFit = fitNorm > 1.0e-5 ? (ssFit / fitNorm) : 0.0;

            const double sX = morphX.step(tX, controlCoef);
            const double sY = morphY.step(tY, controlCoef);
            const double sZ = morphZ.step(tZ, controlCoef);
            const double sFreq = freq.step(tFreq, controlCoef);
            const double sRes = res.step(tRes, controlCoef);
            const double sXform = transform.step(tXform, controlCoef);
            const double sDrive = drive.step(tDrive, controlCoef);
            const double sAsym = asym.step(static_cast<double>(params[ParamId::asym]), controlCoef);
            const double sSpread = spread.step(tSpread, controlCoef);
            const double sMix = mix.step(tMix, controlCoef);
            const double sSubLock = subLock.step(targetSubLock, controlCoef);
            const double sSubHz = subSplitHz.step(static_cast<double>(params[ParamId::subSplit]), controlCoef);
            const double sOutGain = outputGain.step(targetOutGain, controlCoef);
            const double sLimit = limitAmt.step(targetLimit, controlCoef);
            const double sCeiling = ceiling.step(targetCeiling, controlCoef);
            const double sNote = midiPitch.step(targetNoteShift, glideCoef);
            const double sBend = pitchBendSmooth.step(targetBendShift, controlCoef);
            const double sMidi = sNote + sBend;
            const double sFm = fmAmount.step(tFm, controlCoef);

            // Smooth per-stage enable masks for click-free 4P/8P/12P/14P switching.
            double weightSum = 0.0;
            for (int s = 0; s < kNumStages; ++s) {
                const auto idx = static_cast<std::size_t>(s);
                const double targetOn = (s < activeStages) ? 1.0 : 0.0;
                stageMix[idx] = smStageEnable[idx].step(targetOn, stageEnableCoef);
                weightSum += stageMix[idx];
            }
            activeStageWeight = std::max(weightSum, 1.0);

            if ((sampleIndex % kCoefStride) == 0 || snapNewMakeup) {
                const float totalFreq = static_cast<float>(sFreq + sMidi);
                const float stereoSt = static_cast<float>(sSpread * 5.0);
                const int topoIdx = static_cast<int>(activeTopology);
                const auto latL = resolveLattice(
                    cubeIdx, static_cast<float>(sX), static_cast<float>(sY), static_cast<float>(sZ), totalFreq,
                    static_cast<float>(sRes), static_cast<float>(sXform), -stereoSt, &imprint, activeStages, topoIdx);
                const auto latR = resolveLattice(
                    cubeIdx, static_cast<float>(sX), static_cast<float>(sY), static_cast<float>(sZ), totalFreq,
                    static_cast<float>(sRes), static_cast<float>(sXform), +stereoSt, &imprint, activeStages, topoIdx);

                if (snapNewMakeup) {
                    smMakeupL.reset();
                    smMakeupR.reset();
                    snapNewMakeup = false;
                }
                makeupL = smMakeupL.step(static_cast<double>(latL.makeupLinear), stageMorphCoef);
                makeupR = smMakeupR.step(static_cast<double>(latR.makeupLinear), stageMorphCoef);

                for (std::size_t s = 0; s < kNumStages; ++s) {
                    const SectionSpec smoothL{
                        static_cast<float>(std::exp(smStagesL[s].logPoleHz.step(
                            std::log(static_cast<double>(latL.stages[s].poleHz)), stageMorphCoef))),
                        static_cast<float>(std::exp(smStagesL[s].logPoleQ.step(
                            std::log(static_cast<double>(latL.stages[s].poleQ)), stageMorphCoef))),
                        static_cast<float>(std::exp(smStagesL[s].logZeroHz.step(
                            std::log(static_cast<double>(latL.stages[s].zeroHz)), stageMorphCoef))),
                        static_cast<float>(std::exp(smStagesL[s].logZeroQ.step(
                            std::log(static_cast<double>(latL.stages[s].zeroQ)), stageMorphCoef))),
                    };
                    const SectionSpec smoothR{
                        static_cast<float>(std::exp(smStagesR[s].logPoleHz.step(
                            std::log(static_cast<double>(latR.stages[s].poleHz)), stageMorphCoef))),
                        static_cast<float>(std::exp(smStagesR[s].logPoleQ.step(
                            std::log(static_cast<double>(latR.stages[s].poleQ)), stageMorphCoef))),
                        static_cast<float>(std::exp(smStagesR[s].logZeroHz.step(
                            std::log(static_cast<double>(latR.stages[s].zeroHz)), stageMorphCoef))),
                        static_cast<float>(std::exp(smStagesR[s].logZeroQ.step(
                            std::log(static_cast<double>(latR.stages[s].zeroQ)), stageMorphCoef))),
                    };
                    coefsL[s] = dsp::PoleZeroCoefs::make(smoothL, osRate);
                    coefsR[s] = dsp::PoleZeroCoefs::make(smoothR, osRate);
                }
                splitCoefs = hba::dsp::butterworth(sSubHz, sampleRate);
            }
            ++sampleIndex;

            const double inL = rawInL * sInGain;
            const double inR = rawInR * sInGain;

            // Exact LR4 Sub Lock split: LR4_LP goes to mono sub (+ Hilbert Fit),
            // LR4_HP feeds the oversampled lattice (suppressing sub-band drive IM by 24 dB/oct).
            double loL = 0.0, hiL = 0.0, loR = 0.0, hiR = 0.0;
            subSplit[0].process(splitCoefs, inL, loL, hiL);
            subSplit[1].process(splitCoefs, inR, loR, hiR);

            const double feedL = (1.0 - sSubLock) * inL + sSubLock * hiL;
            const double feedR = (1.0 - sSubLock) * inR + sSubLock * hiR;
            const double subMono = sSubLock * subFit.processCosSin(0.5 * (loL + loR), cFit, sFit);

            // Audio-rate FM input signal (mono mix of Input or Sidechain; Pitch Sine generated directly at osRate).
            const double fmBase =
                (fmSource == FmSource::sidechain && scLeft != nullptr) ? 0.5 * (scL + scR) : 0.5 * (inL + inR);

            // Matched HIIR upsampling of wet L/R, dry L/R, mono sub, and FM modulator.
            std::array<float, hba::dsp::kMaxOversampling> bufWL{}, bufWR{}, bufDL{}, bufDR{}, bufSub{}, bufFm{};
            wetOs[0].up(static_cast<float>(feedL), bufWL.data());
            wetOs[1].up(static_cast<float>(feedR), bufWR.data());
            dryOs[0].up(static_cast<float>(rawInL), bufDL.data());
            dryOs[1].up(static_cast<float>(rawInR), bufDR.data());
            subOs.up(static_cast<float>(subMono), bufSub.data());
            if (sFm > 1.0e-4 && fmSource != FmSource::pitchSine)
                fmOs.up(static_cast<float>(fmBase), bufFm.data());

            const double fmOctDepth = sFm * 2.2;
            const double fmOscHz = std::clamp(kRootHz * std::exp2((sMidi + sFreq) / 12.0), 20.0, 0.35 * osRate);
            const double fmOscInc = fmOscHz / osRate;

            for (int k = 0; k < osFactor; ++k) {
                const auto idx = static_cast<std::size_t>(k);
                double fmScale = 1.0;
                if (sFm > 1.0e-4) {
                    double fmSig = 0.0;
                    if (fmSource == FmSource::pitchSine) {
                        fmSig = std::sin(hba::dsp::kTwoPi * fmSinePhase);
                        fmSinePhase = hba::dsp::frac(fmSinePhase + fmOscInc);
                    } else {
                        fmSig = std::clamp(static_cast<double>(bufFm[idx]), -1.5, 1.5);
                    }
                    fmScale = std::exp2(std::clamp(fmSig * fmOctDepth, -2.5, 2.5));
                }

                const double osInL = static_cast<double>(bufWL[idx]);
                const double osInR = static_cast<double>(bufWR[idx]);
                double wL = 0.0, wR = 0.0;
                processRouted(lattice, msPassDelay, activeStereoMode, activeTopology, activeDrivePos, osInL, osInR,
                              makeupL, makeupR, sDrive, sAsym, foldType, fmScale, wL, wR);

                if (xfadeMix < 1.0) {
                    double oldL = 0.0, oldR = 0.0;
                    processRouted(xfadeLattice, xfadeMsPassDelay, prevStereoMode, prevTopology, prevDrivePos, osInL,
                                  osInR, prevMakeupL, prevMakeupR, sDrive, sAsym, foldType, fmScale, oldL, oldR);
                    const double ang = xfadeMix * (hba::dsp::kPi * 0.5);
                    const double gNew = std::sin(ang);
                    const double gOld = std::cos(ang);
                    wL = gNew * wL + gOld * oldL;
                    wR = gNew * wR + gOld * oldR;
                    xfadeMix = std::min(1.0, xfadeMix + xfadeStep);
                }

                bufWL[idx] = static_cast<float>(wL);
                bufWR[idx] = static_cast<float>(wR);
                // Align dry L/R and sub to exact 2-oversampled-sample lattice/ADAA2 latency.
                bufDL[idx] = static_cast<float>(pushDelay2(dryOsDelay[0], static_cast<double>(bufDL[idx])));
                bufDR[idx] = static_cast<float>(pushDelay2(dryOsDelay[1], static_cast<double>(bufDR[idx])));
                bufSub[idx] = static_cast<float>(pushDelay2(subOsDelay, static_cast<double>(bufSub[idx])));
            }

            const double rawWetL = static_cast<double>(wetOs[0].down(bufWL.data()));
            const double rawWetR = static_cast<double>(wetOs[1].down(bufWR.data()));
            // Keep Mid unfiltered at subSplit so Sub(LR4_LP) + WetMid(LR4_HP) sums to an exact
            // 0.00 dB flat LR4 allpass, while high-passing Side below subSplit for strict mono sub!
            const double wetMid = 0.5 * (rawWetL + rawWetR);
            const double wetSideClean = postSideHp.process(splitCoefs, 0.5 * (rawWetL - rawWetR));
            const double refWetL = wetFitRef[0].processReference(wetMid + wetSideClean);
            const double refWetR = wetFitRef[1].processReference(wetMid - wetSideClean);
            const double wetL = (1.0 - sSubLock) * rawWetL + sSubLock * refWetL;
            const double wetR = (1.0 - sSubLock) * rawWetR + sSubLock * refWetR;

            const double rawDryL = static_cast<double>(dryOs[0].down(bufDL.data()));
            const double rawDryR = static_cast<double>(dryOs[1].down(bufDR.data()));
            const double refDryL = dryFitRef[0].processReference(dryAp[0].process(splitCoefs, rawDryL));
            const double refDryR = dryFitRef[1].processReference(dryAp[1].process(splitCoefs, rawDryR));
            const double dryL = (1.0 - sSubLock) * rawDryL + sSubLock * refDryL;
            const double dryR = (1.0 - sSubLock) * rawDryR + sSubLock * refDryR;

            const double alignedSub = static_cast<double>(subOs.down(bufSub.data()));

            // Recombine clean mono sub + processed growl, apply output gain, then blend dry/wet.
            double outL = ((wetL + alignedSub) * sOutGain) * sMix + dryL * (1.0 - sMix);
            double outR = ((wetR + alignedSub) * sOutGain) * sMix + dryR * (1.0 - sMix);

            limiter.process(outL, outR, sCeiling, sLimit);
            maxGrDb = std::max(maxGrDb, -hba::dsp::gainToDb(limiter.gain()));

            // Decoupled safety clipper: respects Ceiling when Limit = ON, provides transparent
            // +6 dBFS linear headroom (hard cap +12 dBFS) when Limit = OFF.
            const double clipKnee = sLimit * std::min(sCeiling, 0.98) + (1.0 - sLimit) * 2.0;
            const double clipCeil = sLimit * std::min(std::max(sCeiling * 1.02, 0.99), 1.0) + (1.0 - sLimit) * 4.0;
            hba::dsp::softClipLinked(outL, outR, clipKnee, clipCeil);

            left[n] = static_cast<float>(outL);
            right[n] = static_cast<float>(outR);

            if (n == numSamples - 1) {
                lastPpq = ppq;
                lastNotePpq = notePpq;
                lastFreeSec = freeSec;
                lastNoteSec = noteSec;
                for (int l = 0; l < kLaneCount; ++l) {
                    const auto li = static_cast<std::size_t>(l);
                    lastLanePhase[li] = static_cast<float>(lanePhaseFor(params, l, ppq, notePpq, freeSec, noteSec));
                    lastLaneValue[li] = sends[li];
                }
                effective.morphX = static_cast<float>(sX);
                effective.morphY = static_cast<float>(sY);
                effective.morphZ = static_cast<float>(sZ);
                effective.freq = static_cast<float>(sFreq);
                effective.res = static_cast<float>(sRes);
                effective.drive = static_cast<float>(sDrive);
                effective.transform = static_cast<float>(sXform);
                effective.spread = static_cast<float>(sSpread);
                effective.fmAmount = static_cast<float>(sFm);
                effective.envLevel = static_cast<float>(envState);
                effective.midiShiftSt = static_cast<float>(sMidi);
                effective.activeNote = pitchTrackOn ? activeNote : -1;
            }
        }
        lastGrDb = static_cast<float>(maxGrDb);
    }
};

Engine::Engine() : impl_(std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::prepare(double sampleRate, int maxBlockSize, int oversamplingFactor) {
    impl_->prepare(sampleRate, maxBlockSize, oversamplingFactor);
}

void Engine::reset() noexcept PF_NONBLOCKING { impl_->reset(); }
void Engine::noteOn(int midiNote) noexcept PF_NONBLOCKING { impl_->noteOn(midiNote); }
void Engine::noteOff(int midiNote) noexcept PF_NONBLOCKING { impl_->noteOff(midiNote); }
void Engine::pitchBend(float semitones) noexcept PF_NONBLOCKING { impl_->pitchBendSt = semitones; }
void Engine::setModWheel(float norm01) noexcept PF_NONBLOCKING {
    impl_->modWheelTarget = std::clamp(static_cast<double>(norm01), 0.0, 1.0);
}
void Engine::setTimbre(float norm01) noexcept PF_NONBLOCKING {
    impl_->timbreOffsetTarget = 0.9 * (std::clamp(static_cast<double>(norm01), 0.0, 1.0) - 0.5);
}
void Engine::setAftertouch(float norm01) noexcept PF_NONBLOCKING {
    impl_->aftertouchTarget = std::clamp(static_cast<double>(norm01), 0.0, 1.0);
}
void Engine::allNotesOff() noexcept PF_NONBLOCKING {
    impl_->noteCount = 0;
    impl_->activeNote = -1;
}
void Engine::setLaneCurves(const LaneCurves& curves) noexcept PF_NONBLOCKING { impl_->laneCurves = curves; }
void Engine::setImprint(const ImprintData& imprint) noexcept PF_NONBLOCKING { impl_->imprint = imprint; }

void Engine::process(const Params& params, const Transport& transport, float* left, float* right, int numSamples,
                     const float* scLeft, const float* scRight) noexcept PF_NONBLOCKING {
    impl_->process(params, transport, left, right, numSamples, scLeft, scRight);
}

int Engine::latencySamples() const noexcept PF_NONBLOCKING {
    return impl_->limiter.latency() + impl_->wetOs[0].latencySamples();
}
int Engine::oversamplingFactor() const noexcept PF_NONBLOCKING { return impl_->osFactor; }
float Engine::gainReductionDb() const noexcept PF_NONBLOCKING { return impl_->lastGrDb; }
double Engine::lanePpq() const noexcept PF_NONBLOCKING { return impl_->lastPpq; }
double Engine::notePpq() const noexcept PF_NONBLOCKING { return impl_->lastNotePpq; }
double Engine::freeSeconds() const noexcept PF_NONBLOCKING { return impl_->lastFreeSec; }
double Engine::noteSeconds() const noexcept PF_NONBLOCKING { return impl_->lastNoteSec; }
std::array<float, kLaneCount> Engine::lanePhases() const noexcept PF_NONBLOCKING { return impl_->lastLanePhase; }
std::array<float, kLaneCount> Engine::laneValues() const noexcept PF_NONBLOCKING { return impl_->lastLaneValue; }
Engine::EffectiveState Engine::effectiveState() const noexcept PF_NONBLOCKING { return impl_->effective; }

}
