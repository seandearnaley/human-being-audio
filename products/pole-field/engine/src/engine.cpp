#include "pf/engine.h"

#include "control.h"
#include "crossover.h"
#include "decimator.h"
#include "denormals.h"
#include "limiter.h"
#include "pole_zero.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace pf {

namespace {
constexpr double kControlRateSec = 0.004;   // 4 ms parameter smoothing
constexpr int kMaxHeldNotes = 16;
constexpr int kRootMidiNote = 29;           // F1 (43.65 Hz) = 0 semitones when pitch-tracking
}

struct Engine::Impl {
    double sampleRate = 48000.0;
    int osFactor = 4;
    double osRate = 192000.0;
    double controlCoef = 0.01;

    std::array<dsp::Lr4Split, 2> subSplit {};
    std::array<dsp::Lr4Highpass, 2> postWetHp {};
    dsp::PhaseRotator subFit {};

    // Matched HIIR upsamplers/decimators for wet L/R, dry L/R, and mono sub bypass
    // so wet, dry, and sub are phase- and sample-aligned by construction.
    std::array<dsp::Oversampler, 2> wetOs {};
    std::array<dsp::Oversampler, 2> dryOs {};
    dsp::Oversampler subOs {};

    std::array<dsp::PoleZeroLattice, 2> lattice {};
    dsp::Limiter limiter {};

    // Smoothers
    dsp::Smoother morphX {}, morphY {}, morphZ {}, freq {}, res {}, transform {}, drive {}, asym {}, spread {}, mix {},
        subLock {}, subSplitHz {}, fit {}, inputGain {}, outputGain {}, limitAmt {}, ceiling {}, midiPitch {};

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

    // Published telemetry for UI
    double lastPpq = 0.0;
    std::array<float, kLaneCount> lastLanePhase {};
    std::array<float, kLaneCount> lastLaneValue {};
    float lastGrDb = 0.0f;
    EffectiveState effective {};

    // Persistent lattice coefficient state for exact block-size invariance
    std::uint64_t sampleIndex = 0;
    std::array<dsp::PoleZeroCoefs, kNumStages> coefsL {};
    std::array<dsp::PoleZeroCoefs, kNumStages> coefsR {};
    double makeupL = 1.0;
    double makeupR = 1.0;

    void prepare(double sr, int /*maxBlock*/, int factor) {
        sampleRate = std::max(sr, 22050.0);
        osFactor = (factor == 2 || factor == 4 || factor == 8) ? factor : 4;
        osRate = sampleRate * static_cast<double>(osFactor);
        controlCoef = dsp::onePoleCoef(kControlRateSec, 1.0 / sampleRate);

        for (auto& os : wetOs) os.prepare(osFactor);
        for (auto& os : dryOs) os.prepare(osFactor);
        subOs.prepare(osFactor);
        for (auto& lat : lattice) lat.prepare(osRate);
        limiter.prepare(sampleRate);
        reset();
    }

    void reset() noexcept PF_NONBLOCKING {
        for (auto& sp : subSplit) sp.reset();
        for (auto& hp : postWetHp) hp.reset();
        subFit.reset();
        for (auto& os : wetOs) os.reset();
        for (auto& os : dryOs) os.reset();
        subOs.reset();
        for (auto& lat : lattice) lat.reset();
        limiter.reset();
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
        fit.reset();
        inputGain.reset();
        outputGain.reset();
        limitAmt.reset();
        ceiling.reset();
        midiPitch.reset();
        noteCount = 0;
        activeNote = -1;
        pitchBendSt = 0.0f;
        modWheelTarget = 0.0;
        timbreOffsetTarget = 0.0;
        aftertouchTarget = 0.0;
        freePpq = 0.0;
        lastGrDb = 0.0f;
        sampleIndex = 0;
    }

    void noteOn(int note) noexcept PF_NONBLOCKING {
        // Remove existing occurrence before pushing to top (last-note priority)
        noteOff(note);
        if (noteCount < kMaxHeldNotes)
            noteStack[static_cast<std::size_t>(noteCount++)] = note;
        else
            noteStack[kMaxHeldNotes - 1] = note;
        activeNote = note;
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

    void process(const Params& params, const Transport& transport, float* left, float* right,
                 int numSamples) noexcept PF_NONBLOCKING {
        dsp::ScopedFlushDenormals flush;
        if (numSamples <= 0 || left == nullptr || right == nullptr) return;

        const double bpm = std::clamp(transport.bpm, 20.0, 400.0);
        const double quartersPerSample = bpm / (60.0 * sampleRate);

        const int cubeIdx = params.choice(ParamId::cube);
        const auto topology = static_cast<Topology>(std::clamp(params.choice(ParamId::topology), 0, 2));
        const int activeStages = kStageCounts[static_cast<std::size_t>(std::clamp(params.choice(ParamId::stages), 0, 3))];
        const auto foldType = static_cast<FoldType>(
            std::clamp(params.choice(ParamId::foldType), 0, static_cast<int>(choices::foldType.size()) - 1));
        const auto drivePos = static_cast<DrivePos>(std::clamp(params.choice(ParamId::drivePos), 0, 2));
        const bool pitchTrackOn = params.on(ParamId::pitchTrack);
        const double glideSec = static_cast<double>(params[ParamId::glide]) * 0.001;
        const double glideCoef = glideSec > 1.0e-4 ? dsp::onePoleCoef(glideSec, 1.0 / sampleRate) : 1.0;

        const double targetMidiShift = (pitchTrackOn && activeNote >= 0)
            ? static_cast<double>(activeNote - kRootMidiNote) + static_cast<double>(pitchBendSt)
            : 0.0;

        const double targetInGain = dsp::dbToGain(static_cast<double>(params[ParamId::inputGain]));
        const double targetOutGain = dsp::dbToGain(static_cast<double>(params[ParamId::outputGain]));
        const double targetCeiling = dsp::dbToGain(static_cast<double>(params[ParamId::ceiling]));
        const double targetLimit = params.on(ParamId::limit) ? 1.0 : 0.0;
        const double targetSubLock = params.on(ParamId::subLock) ? 1.0 : 0.0;

        // Update lattice coefficients every 8 base-rate samples (6 kHz control rate at 48 kHz)
        // indexed by persistent sampleIndex so output is bit-identical across any block size.
        constexpr std::uint64_t kCoefStride = 8;

        double maxGrDb = 0.0;

        for (int n = 0; n < numSamples; ++n) {
            const double ppq = transport.playing ? transport.ppq + quartersPerSample * n : freePpq;
            if (!transport.playing) freePpq += quartersPerSample;
            else freePpq = ppq;

            // Evaluate lanes every sample so step/square/custom curves are block-size invariant.
            std::array<float, kLaneCount> sends {};
            for (int l = 0; l < kLaneCount; ++l)
                sends[static_cast<std::size_t>(l)] = static_cast<float>(laneSend(params, laneCurves, l, ppq));

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
            const double tFreq = std::clamp(static_cast<double>(laneApply(LaneTarget::freq, params[ParamId::freq], laneSum(params, sends, LaneTarget::freq))), -48.0, 48.0);
            const double tRes = std::clamp(static_cast<double>(laneApply(LaneTarget::res, params[ParamId::res], laneSum(params, sends, LaneTarget::res))), 0.0, 1.0);
            const double tXform = std::clamp(static_cast<double>(laneApply(LaneTarget::transform, params[ParamId::transform], laneSum(params, sends, LaneTarget::transform))), -1.0, 1.0);
            const double tDrive = std::clamp(static_cast<double>(laneApply(LaneTarget::drive, params[ParamId::drive],
                                                                           laneSum(params, sends, LaneTarget::drive))) +
                                                 0.25 * aftertouchTarget,
                                             0.0, 1.0);
            const double tSpread = std::clamp(static_cast<double>(laneApply(LaneTarget::spread, params[ParamId::spread], laneSum(params, sends, LaneTarget::spread))), 0.0, 1.0);
            const double tMix = std::clamp(static_cast<double>(laneApply(LaneTarget::mix, params[ParamId::mix], laneSum(params, sends, LaneTarget::mix))), 0.0, 1.0);
            const double rawFit = static_cast<double>(laneApply(LaneTarget::fit, params[ParamId::fit], laneSum(params, sends, LaneTarget::fit)));
            const double tFit = dsp::frac(rawFit / 360.0) * 360.0;

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
            const double sFit = fit.step(tFit, controlCoef);
            const double sInGain = inputGain.step(targetInGain, controlCoef);
            const double sOutGain = outputGain.step(targetOutGain, controlCoef);
            const double sLimit = limitAmt.step(targetLimit, controlCoef);
            const double sCeiling = ceiling.step(targetCeiling, controlCoef);
            const double sMidi = midiPitch.step(targetMidiShift, glideCoef);

            if ((sampleIndex % kCoefStride) == 0) {
                const float totalFreq = static_cast<float>(sFreq + sMidi);
                const float stereoSt = static_cast<float>(sSpread * 5.0);  // up to ±5 semitones L/R divergence
                const auto latL = resolveLattice(cubeIdx, static_cast<float>(sX), static_cast<float>(sY),
                                                 static_cast<float>(sZ), totalFreq, static_cast<float>(sRes),
                                                 static_cast<float>(sXform), -stereoSt, &imprint);
                const auto latR = resolveLattice(cubeIdx, static_cast<float>(sX), static_cast<float>(sY),
                                                 static_cast<float>(sZ), totalFreq, static_cast<float>(sRes),
                                                 static_cast<float>(sXform), +stereoSt, &imprint);
                makeupL = static_cast<double>(latL.makeupLinear);
                makeupR = static_cast<double>(latR.makeupLinear);
                for (std::size_t s = 0; s < kNumStages; ++s) {
                    coefsL[s] = dsp::PoleZeroCoefs::make(latL.stages[s], osRate);
                    coefsR[s] = dsp::PoleZeroCoefs::make(latR.stages[s], osRate);
                }
            }
            ++sampleIndex;

            const double inL = static_cast<double>(left[n]) * sInGain;
            const double inR = static_cast<double>(right[n]) * sInGain;

            // Split clean mono sub below subSplit when Sub Lock is active.
            const dsp::SvfCoefs splitCoefs = dsp::butterworth(sSubHz, sampleRate);
            double loL = 0.0, hiL = 0.0, loR = 0.0, hiR = 0.0;
            subSplit[0].process(splitCoefs, inL, loL, hiL);
            subSplit[1].process(splitCoefs, inR, loR, hiR);

            // Blend smoothly between full-band wet input and high-passed growl input.
            const double feedL = (1.0 - sSubLock) * inL + sSubLock * hiL;
            const double feedR = (1.0 - sSubLock) * inR + sSubLock * hiR;
            const double subMono = sSubLock * subFit.process(0.5 * (loL + loR), sFit);

            // Matched HIIR upsampling of wet L/R, dry L/R, and mono sub bypass.
            std::array<float, dsp::kMaxOversampling> bufWL {}, bufWR {}, bufDL {}, bufDR {}, bufSub {};
            wetOs[0].up(static_cast<float>(feedL), bufWL.data());
            wetOs[1].up(static_cast<float>(feedR), bufWR.data());
            dryOs[0].up(static_cast<float>(left[n]), bufDL.data());
            dryOs[1].up(static_cast<float>(right[n]), bufDR.data());
            subOs.up(static_cast<float>(subMono), bufSub.data());

            for (int k = 0; k < osFactor; ++k) {
                const auto idx = static_cast<std::size_t>(k);
                bufWL[idx] = static_cast<float>(lattice[0].process(
                    static_cast<double>(bufWL[idx]), coefsL, activeStages, topology, makeupL,
                    sDrive, sAsym, foldType, drivePos));
                bufWR[idx] = static_cast<float>(lattice[1].process(
                    static_cast<double>(bufWR[idx]), coefsR, activeStages, topology, makeupR,
                    sDrive, sAsym, foldType, drivePos));
            }

            const double rawWetL = static_cast<double>(wetOs[0].down(bufWL.data()));
            const double rawWetR = static_cast<double>(wetOs[1].down(bufWR.data()));
            // Strip any low-frequency energy regenerated by inter-stage drive/asymmetry
            // when Sub Lock is active, keeping the sub band strictly clean and mono.
            const double cleanWetL = postWetHp[0].process(splitCoefs, rawWetL);
            const double cleanWetR = postWetHp[1].process(splitCoefs, rawWetR);
            const double wetL = (1.0 - sSubLock) * rawWetL + sSubLock * cleanWetL;
            const double wetR = (1.0 - sSubLock) * rawWetR + sSubLock * cleanWetR;
            const double dryL = static_cast<double>(dryOs[0].down(bufDL.data()));
            const double dryR = static_cast<double>(dryOs[1].down(bufDR.data()));
            const double alignedSub = static_cast<double>(subOs.down(bufSub.data()));

            // Recombine clean mono sub + processed growl, apply output gain, then blend dry/wet.
            double outL = ((wetL + alignedSub) * sOutGain) * sMix + dryL * (1.0 - sMix);
            double outR = ((wetR + alignedSub) * sOutGain) * sMix + dryR * (1.0 - sMix);

            limiter.process(outL, outR, sCeiling, sLimit);
            maxGrDb = std::max(maxGrDb, -dsp::gainToDb(limiter.gain()));

            // Transparent linked safety clip at 0 dBFS so extreme settings never emit NaN/Inf.
            dsp::softClipLinked(outL, outR, std::min(sCeiling, 0.97), 1.0);

            left[n] = static_cast<float>(outL);
            right[n] = static_cast<float>(outR);

            if (n == numSamples - 1) {
                lastPpq = ppq;
                for (int l = 0; l < kLaneCount; ++l) {
                    const auto li = static_cast<std::size_t>(l);
                    lastLanePhase[li] = static_cast<float>(
                        lanePhase(ppq, laneLengthQuarters(params, l), params[laneParam(l, LaneField::phase)]));
                    lastLaneValue[li] = sends[li];
                }
                effective.morphX = static_cast<float>(sX);
                effective.morphY = static_cast<float>(sY);
                effective.morphZ = static_cast<float>(sZ);
                effective.freq = static_cast<float>(sFreq);
                effective.res = static_cast<float>(sRes);
                effective.transform = static_cast<float>(sXform);
                effective.spread = static_cast<float>(sSpread);
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
void Engine::allNotesOff() noexcept PF_NONBLOCKING { impl_->noteCount = 0; impl_->activeNote = -1; }
void Engine::setLaneCurves(const LaneCurves& curves) noexcept PF_NONBLOCKING { impl_->laneCurves = curves; }
void Engine::setImprint(const ImprintData& imprint) noexcept PF_NONBLOCKING { impl_->imprint = imprint; }

void Engine::process(const Params& params, const Transport& transport, float* left, float* right,
                     int numSamples) noexcept PF_NONBLOCKING {
    impl_->process(params, transport, left, right, numSamples);
}

int Engine::latencySamples() const noexcept PF_NONBLOCKING { return impl_->limiter.latency(); }
float Engine::gainReductionDb() const noexcept PF_NONBLOCKING { return impl_->lastGrDb; }
double Engine::lanePpq() const noexcept PF_NONBLOCKING { return impl_->lastPpq; }
std::array<float, kLaneCount> Engine::lanePhases() const noexcept PF_NONBLOCKING { return impl_->lastLanePhase; }
std::array<float, kLaneCount> Engine::laneValues() const noexcept PF_NONBLOCKING { return impl_->lastLaneValue; }
Engine::EffectiveState Engine::effectiveState() const noexcept PF_NONBLOCKING { return impl_->effective; }

}
