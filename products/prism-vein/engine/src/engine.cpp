// SPDX-License-Identifier: AGPL-3.0-or-later
#include "pv/engine.h"

#include <hba/dsp/denormals.h>
#include <hba/dsp/gain.h>

#include <algorithm>
#include <cmath>

namespace pv {

namespace {

inline double midiToHzEqual(double midiNote) noexcept PV_NONBLOCKING {
    return 440.0 * std::pow(2.0, (midiNote - 69.0) / 12.0);
}

inline hba::dsp::ShaperModel mapGritType(int choice) noexcept PV_NONBLOCKING {
    switch (std::clamp(choice, 0, 4)) {
    case 0:
        return hba::dsp::ShaperModel::tube;
    case 1:
        return hba::dsp::ShaperModel::sine;
    case 2:
        return hba::dsp::ShaperModel::fold;
    case 3:
        return hba::dsp::ShaperModel::buchla;
    case 4:
        return hba::dsp::ShaperModel::clip;
    default:
        return hba::dsp::ShaperModel::tube;
    }
}

inline float magToUiNorm(float mag) noexcept PV_NONBLOCKING {
    const float db = 20.0f * std::log10(std::max(mag, 1.0e-5f));
    return std::clamp((db + 68.0f) / 72.0f, 0.0f, 1.0f);
}

inline constexpr std::array<double, 12> kJustRatios{
    1.0,         // 0: unison
    16.0 / 15.0, // 1: m2
    9.0 / 8.0,   // 2: M2
    6.0 / 5.0,   // 3: m3
    5.0 / 4.0,   // 4: M3
    4.0 / 3.0,   // 5: P4
    45.0 / 32.0, // 6: tritone
    3.0 / 2.0,   // 7: P5
    8.0 / 5.0,   // 8: m6
    5.0 / 3.0,   // 9: M6
    9.0 / 5.0,   // 10: m7
    15.0 / 8.0,  // 11: M7
};

inline void wrapPhase(float& phase) noexcept PV_NONBLOCKING {
    constexpr float kTwoPiF = 6.2831853071795864769f;
    if (phase >= kTwoPiF || phase < 0.0f)
        phase -= kTwoPiF * std::floor(phase / kTwoPiF);
}

inline void rotateQuadPhase(int k, float inRe, float inIm, float& outRe, float& outIm) noexcept PV_NONBLOCKING {
    switch (k & 3) {
    case 1:
        outRe = -inIm;
        outIm = inRe;
        break;
    case 2:
        outRe = -inRe;
        outIm = -inIm;
        break;
    case 3:
        outRe = inIm;
        outIm = -inRe;
        break;
    default:
        outRe = inRe;
        outIm = inIm;
        break;
    }
}

} // namespace

Engine::Engine() : buf_(std::make_unique<Buffers>()) {}
Engine::~Engine() = default;
Engine::Engine(Engine&&) noexcept = default;
Engine& Engine::operator=(Engine&&) noexcept = default;

double Engine::justIntervalRatio(int semitonesFromRoot) noexcept PV_NONBLOCKING {
    int oct = 0;
    int st = semitonesFromRoot;
    while (st < 0) {
        st += 12;
        --oct;
    }
    while (st >= 12) {
        st -= 12;
        ++oct;
    }
    return kJustRatios[static_cast<std::size_t>(st)] * std::pow(2.0, static_cast<double>(oct));
}

void Engine::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = std::max(sampleRate, 8000.0);
    invSr_ = 1.0 / sampleRate_;
    maxBlockSize_ = std::max(1, maxBlockSize);

    buf_->stft.prepare(kFftSize);
    buf_->telFft.prepare(kFftSize);
    for (int i = 0; i < kFftSize; ++i) {
        buf_->telWin[static_cast<std::size_t>(i)] = static_cast<float>(
            0.5 * (1.0 - std::cos(hba::dsp::kTwoPi * static_cast<double>(i) / static_cast<double>(kFftSize))));
    }

    transientSplit_.prepare(sampleRate_);
    subDelayL_.prepare(kFftSize);
    subDelayR_.prepare(kFftSize);
    transDelayL_.prepare(kFftSize);
    transDelayR_.prepare(kFftSize);
    dryBodyDelayL_.prepare(kFftSize);
    dryBodyDelayR_.prepare(kFftSize);
    combDelayL_.prepare(kFftSize);
    combDelayR_.prepare(kFftSize);

    const int qIdx = std::clamp(targetParams_.choice(ParamId::quality), 0, 2);
    preparedOsFactor_ = kQualityFactors[static_cast<std::size_t>(qIdx)];
    osL_.prepare(preparedOsFactor_);
    osR_.prepare(preparedOsFactor_);

    dcL_.prepare(12.0, sampleRate_);
    dcR_.prepare(12.0, sampleRate_);

    demoSnareCoefs_ = hba::dsp::SvfCoefs::make(1850.0, 1.4, sampleRate_);
    demoKickDecay_ = std::exp(-invSr_ / 0.055);
    demoSnareDecay_ = std::exp(-invSr_ / 0.068);
    demoHatDecay_ = std::exp(-invSr_ / 0.018);

    // Precompute log-frequency bin ranges (32 Hz .. 18.5 kHz) for the 96-column stage visualizer.
    const double minHz = 32.0;
    const double maxHz = std::min(18500.0, 0.47 * sampleRate_);
    const double ratio = maxHz / minHz;
    const double binPerHz = static_cast<double>(kFftSize) / sampleRate_;
    for (int i = 0; i < kSpectrumBins; ++i) {
        const double f0 = minHz * std::pow(ratio, static_cast<double>(i) / static_cast<double>(kSpectrumBins));
        const double f1 = minHz * std::pow(ratio, static_cast<double>(i + 1) / static_cast<double>(kSpectrumBins));
        specBinLo_[static_cast<std::size_t>(i)] =
            static_cast<float>(std::clamp(f0 * binPerHz, 1.0, static_cast<double>(kNumBins - 1)));
        specBinHi_[static_cast<std::size_t>(i)] =
            static_cast<float>(std::clamp(f1 * binPerHz, 1.0, static_cast<double>(kNumBins - 1)));
    }

    reset();
}

void Engine::reset() noexcept PV_NONBLOCKING {
    buf_->stft.reset();
    subSplitL_.reset();
    subSplitR_.reset();
    lastSubHz_ = -1.0;
    cachedXoverCoefs_ = hba::dsp::butterworth(105.0, sampleRate_);
    transientSplit_.reset();
    subDelayL_.clear();
    subDelayR_.clear();
    transDelayL_.clear();
    transDelayR_.clear();
    dryBodyDelayL_.clear();
    dryBodyDelayR_.clear();
    combDelayL_.clear();
    combDelayR_.clear();

    buf_->tailReL.fill(0.0f);
    buf_->tailImL.fill(0.0f);
    buf_->tailReR.fill(0.0f);
    buf_->tailImR.fill(0.0f);
    buf_->freezeMagL.fill(0.0f);
    buf_->freezeMagR.fill(0.0f);
    for (int k = 0; k < kNumBins; ++k) {
        const float kf = static_cast<float>(k);
        buf_->freezePhaseL[static_cast<std::size_t>(k)] = kf * 1.6180339f;
        buf_->freezePhaseR[static_cast<std::size_t>(k)] = kf * 2.4142135f;
        buf_->internalCarrPhase[static_cast<std::size_t>(k)] = kf * 0.7853981f;
    }
    buf_->maskL.fill(0.0f);
    buf_->maskR.fill(0.0f);
    buf_->workInMag.fill(0.0f);
    buf_->workPreTameMag.fill(0.0f);
    buf_->workOutMag.fill(0.0f);
    buf_->tameBinSmooth.fill(1.0f);
    buf_->workTameGain.fill(1.0f);
    compGainSmoothed_ = 1.0f;
    freezeFade_ = 0.0f;
    freezeCaptured_ = false;
    prevFreezeParam_ = false;

    buf_->telInRing.fill(0.0f);
    buf_->telOutRing.fill(0.0f);
    buf_->telTameGain.fill(1.0f);
    telPos_ = 0;
    telHopCount_ = 0;

    for (auto& v : buf_->voices) {
        v.bufL.fill(0.0f);
        v.bufR.fill(0.0f);
        v.writePos = 0;
        v.lpL = v.lpR = 0.0;
        v.dcInL = v.dcOutL = 0.0;
        v.dcInR = v.dcOutR = 0.0;
        v.cachedFbFreq = -1.0;
        v.cachedFbT60 = -1.0;
        v.weight = 0.0f;
    }

    liveTameL_.reset();
    liveTameR_.reset();
    liveTameEnv_ = 0.0;

    osL_.reset();
    osR_.reset();
    shaperL_.reset();
    shaperR_.reset();
    airL_.reset();
    airR_.reset();
    dcL_.reset();
    dcR_.reset();
    clipL_.reset();
    clipR_.reset();
    grEnv_ = 1.0;

    smInputGain_.reset();
    smOutputGain_.reset();
    smSubGain_.reset();
    smColor_.reset();
    smShield_.reset();
    smGrit_.reset();
    smSubHz_.reset();
    smAirDb_.reset();
    smInHarm_.reset();
    smCombBlend_.reset();
    smMidiGate_.reset();
    smPitchBend_.reset();
    smModWheel_.reset();

    demoPhase1_ = 0.0;
    demoPhase2_ = 0.37;
    demoPhaseSub_ = 0.0;
    demoStepPhase_ = 0.0;
    demoStep_ = 0;
    demoKickEnv_ = 0.0;
    demoKickPhase_ = 0.0;
    demoSnareEnv_ = 0.0;
    demoHatEnv_ = 0.0;
    demoReeseFilterL_.reset();
    demoReeseFilterR_.reset();
    demoSnareFilter_.reset();
    demoRng_.seed(0x50524953u);

    scRms_ = 0.0;
    telemetry_ = {};
    telemetry_.activePitchMask = chordMask_;
    telemetry_.sampleRate = static_cast<float>(sampleRate_);
    telemetry_.latencySamples = latencySamples();
}

int Engine::latencySamples() const noexcept PV_NONBLOCKING {
    const auto latMode = static_cast<LatencyMode>(targetParams_.choice(ParamId::latencyMode));
    return latMode == LatencyMode::live0ms ? 0 : kFftSize;
}

void Engine::noteOn(int midiNote, float velocity) noexcept PV_NONBLOCKING {
    if (midiNote < 0 || midiNote >= 128)
        return;
    const auto idx = static_cast<std::size_t>(midiNote);
    const bool hold = targetParams_.on(ParamId::midiHold);
    if (midiHeldCount_ == 0) {
        notePpq_ = 0.0;
        noteSec_ = 0.0;
        if (hold) {
            midiLatched_.fill(0);
            midiLatchedCount_ = 0;
        }
    }
    if (midiHeld_[idx] == 0u) {
        midiHeld_[idx] = 1u;
        ++midiHeldCount_;
    }
    if (midiSustained_[idx] != 0u) {
        midiSustained_[idx] = 0u;
        midiSustainedCount_ = std::max(0, midiSustainedCount_ - 1);
    }
    if (hold && midiLatched_[idx] == 0u) {
        midiLatched_[idx] = 1u;
        ++midiLatchedCount_;
    }
    lastMidiVelocity_ = std::clamp(static_cast<double>(velocity), 0.05, 1.0);
}

void Engine::noteOff(int midiNote) noexcept PV_NONBLOCKING {
    if (midiNote < 0 || midiNote >= 128)
        return;
    const auto idx = static_cast<std::size_t>(midiNote);
    if (midiHeld_[idx] != 0u) {
        midiHeld_[idx] = 0u;
        midiHeldCount_ = std::max(0, midiHeldCount_ - 1);
        if (sustainDown_ && midiSustained_[idx] == 0u) {
            midiSustained_[idx] = 1u;
            ++midiSustainedCount_;
        }
    }
}

void Engine::setSustainPedal(bool down) noexcept PV_NONBLOCKING {
    sustainDown_ = down;
    if (!down && midiSustainedCount_ > 0) {
        midiSustained_.fill(0);
        midiSustainedCount_ = 0;
    }
}

void Engine::setPitchBend(float semitones) noexcept PV_NONBLOCKING {
    pitchBendTargetSt_ = std::isfinite(semitones) ? std::clamp(static_cast<double>(semitones), -48.0, 48.0) : 0.0;
}

void Engine::setModWheel(float norm01) noexcept PV_NONBLOCKING {
    modWheelTarget_ = std::isfinite(norm01) ? std::clamp(static_cast<double>(norm01), 0.0, 1.0) : 0.0;
}

void Engine::resetControllers() noexcept PV_NONBLOCKING {
    setSustainPedal(false);
    pitchBendTargetSt_ = 0.0;
    modWheelTarget_ = 0.0;
}

void Engine::allNotesOff() noexcept PV_NONBLOCKING {
    midiHeld_.fill(0);
    midiSustained_.fill(0);
    midiLatched_.fill(0);
    midiHeldCount_ = 0;
    midiSustainedCount_ = 0;
    midiLatchedCount_ = 0;
    sustainDown_ = false;
}

void Engine::updateActiveChordTargets(const Params& p, double totalShiftSt,
                                      bool& midiWaitingOut) noexcept PV_NONBLOCKING {
    const bool hold = p.on(ParamId::midiHold);
    if (!hold && prevHoldParam_) {
        midiLatched_.fill(0);
        midiLatchedCount_ = 0;
    } else if (hold && !prevHoldParam_ && (midiHeldCount_ + midiSustainedCount_) > 0) {
        midiLatched_.fill(0);
        midiLatchedCount_ = 0;
        for (std::size_t n = 0; n < 128; ++n) {
            if (midiHeld_[n] != 0u || midiSustained_[n] != 0u) {
                midiLatched_[n] = 1u;
                ++midiLatchedCount_;
            }
        }
    }
    prevHoldParam_ = hold;

    const auto pitchSrc = static_cast<PitchSource>(p.choice(ParamId::pitchSource));
    const auto tuning = static_cast<TuningMode>(p.choice(ParamId::tuningMode));
    const int octOffset = (p.choice(ParamId::octaveShift) - 2) * 12;

    std::array<int, kMaxChordVoices> notes{};
    int count = 0;
    std::uint16_t liveMask = 0u;
    bool usingMidi = false;
    midiWaitingOut = false;

    const bool useLatched = hold && midiLatchedCount_ > 0;
    const int liveMidiCount = useLatched ? midiLatchedCount_ : (midiHeldCount_ + midiSustainedCount_);

    if (pitchSrc == PitchSource::midi || (pitchSrc == PitchSource::midiPlusKeys && liveMidiCount > 0)) {
        if (liveMidiCount > 0) {
            usingMidi = true;
            for (int n = 0; n < 128 && count < kMaxChordVoices; ++n) {
                const auto nu = static_cast<std::size_t>(n);
                const bool active =
                    useLatched ? (midiLatched_[nu] != 0u) : (midiHeld_[nu] != 0u || midiSustained_[nu] != 0u);
                if (active) {
                    notes[static_cast<std::size_t>(count++)] = n + octOffset;
                    liveMask = static_cast<std::uint16_t>(liveMask | (1u << (n % 12)));
                }
            }
        } else if (pitchSrc == PitchSource::midi) {
            // Dedicated MIDI-only mode: keep previous voice pitches smoothly while gating wet off.
            midiWaitingOut = true;
            telemetry_.activePitchMask = 0u;
            telemetry_.midiActive = false;
            telemetry_.midiWaiting = true;
            return;
        }
    }

    if (count == 0) {
        const std::uint16_t m = chordMask_ != 0u ? chordMask_ : kDefaultChordMask;
        liveMask = m;
        for (int pc = 0; pc < 12 && count < kMaxChordVoices; ++pc) {
            if ((m & (1u << pc)) != 0u)
                notes[static_cast<std::size_t>(count++)] = 48 + pc + octOffset;
        }
    }

    if (count == 0) {
        notes[0] = 53 + octOffset;
        count = 1;
        liveMask = static_cast<std::uint16_t>(1u << 5);
    }

    const int rootNote = notes[0];
    const double rootHz = midiToHzEqual(static_cast<double>(rootNote) + totalShiftSt);

    activeVoiceCount_ = count;
    const double invSqrtN = 1.0 / std::sqrt(static_cast<double>(count));
    const double detuneSt = static_cast<double>(p.real(ParamId::detune)) * 0.01;
    const float fan = p.real(ParamId::stereoFan);

    for (int i = 0; i < kMaxChordVoices; ++i) {
        auto& v = buf_->voices[static_cast<std::size_t>(i)];
        if (i < count) {
            const int n = notes[static_cast<std::size_t>(i)];
            const double microSign = (i % 2 == 0) ? 1.0 : -1.0;
            const double microDetune = (count > 1 ? microSign * detuneSt * 0.5 : 0.0);
            double hz = 174.61;
            if (tuning == TuningMode::just) {
                hz = rootHz * justIntervalRatio(n - rootNote) * std::pow(2.0, microDetune / 12.0);
            } else if (tuning == TuningMode::neuro) {
                const double rel = static_cast<double>(n - rootNote) + microDetune * 1.45;
                hz = rootHz * std::pow(2.0045, rel / 12.0);
            } else {
                hz = midiToHzEqual(static_cast<double>(n) + totalShiftSt + microDetune);
            }
            v.targetHz = std::clamp(hz, 24.0, 0.42 * sampleRate_);
            if (v.weight <= 0.001f)
                v.freqHz = v.targetHz;
            v.weight = static_cast<float>(invSqrtN);
            const float panPos = (i == 0) ? 0.0f : (static_cast<float>((i % 2 == 0) ? -1 : 1) * fan * 0.55f);
            v.panL = std::sqrt(0.5f * (1.0f - panPos));
            v.panR = std::sqrt(0.5f * (1.0f + panPos));
        } else {
            v.weight = 0.0f;
        }
    }

    telemetry_.activePitchMask = liveMask;
    telemetry_.midiActive = usingMidi;
    telemetry_.midiWaiting = false;
}

void Engine::processStftFrame(hba::dsp::StereoStft<kFftSize>::FrameBuffers& fb, const Params& p, double modDecay,
                              double modTame, double modTilt, double modFan, double modMorph,
                              bool publishTelemetry) noexcept PV_NONBLOCKING {
    const int bins = fb.numBins;
    const float binHz = static_cast<float>(sampleRate_) / static_cast<float>(fb.fftSize);
    const float invBinHz = 1.0f / std::max(binHz, 1.0e-3f);

    const auto mode = static_cast<EngineMode>(p.choice(ParamId::engineMode));
    const float decay =
        static_cast<float>(std::clamp(static_cast<double>(p.real(ParamId::decay)) + modDecay * 0.5, 0.0, 0.995));
    const float tameAmt =
        static_cast<float>(std::clamp(static_cast<double>(p.real(ParamId::tame)) + modTame * 0.5, 0.0, 1.0));
    const float tilt = static_cast<float>(std::clamp(static_cast<double>(p.real(ParamId::tilt)) + modTilt, -1.0, 1.0));
    const float fan =
        static_cast<float>(std::clamp(static_cast<double>(p.real(ParamId::stereoFan)) + modFan * 0.5, 0.0, 1.0));
    const float morph =
        static_cast<float>(std::clamp(static_cast<double>(p.real(ParamId::morphBlend)) + modMorph * 0.5, 0.0, 1.0));
    const float qSharp = p.real(ParamId::qSharpness);
    const int numHarms = std::clamp(static_cast<int>(std::lround(p.real(ParamId::harmonics))), 1, 16);
    const float shimmer =
        std::clamp(p.real(ParamId::shimmer) + static_cast<float>(smModWheel_.value()) * 0.20f, 0.0f, 1.0f);
    const float subHz = p.real(ParamId::subLockHz);
    const float detuneRatio =
        static_cast<float>(std::pow(2.0, static_cast<double>(p.real(ParamId::detune)) / 1200.0) - 1.0);
    const bool freezeToggle = p.on(ParamId::freeze);
    const bool glassMode = (mode == EngineMode::glassFreeze);
    const bool wantFrozen = freezeToggle || glassMode;

    // 1. Step voice glide smoothly at hop rate and measure input magnitudes.
    const float glideHopCoef =
        static_cast<float>(hba::dsp::onePoleCoef(std::max(0.001, static_cast<double>(p.real(ParamId::glideMs)) * 0.001),
                                                 static_cast<double>(fb.hopSize) * invSr_));
    for (int v = 0; v < activeVoiceCount_; ++v) {
        auto& voice = buf_->voices[static_cast<std::size_t>(v)];
        voice.freqHz += static_cast<double>(glideHopCoef) * (voice.targetHz - voice.freqHz);
    }

    float inEnergy = 0.0f;
    const int subCutoffBin = std::clamp(static_cast<int>(subHz * invBinHz), 2, bins - 2);
    for (int k = 0; k < bins; ++k) {
        const auto ku = static_cast<std::size_t>(k);
        const float mL = std::hypot(fb.reL[k], fb.imL[k]);
        const float mR = std::hypot(fb.reR[k], fb.imR[k]);
        const float m = 0.5f * (mL + mR);
        buf_->workInMag[ku] = m;
        if (k >= subCutoffBin)
            inEnergy += m * m;
    }

    // 2. Build stereo Harmonic Chord Lattice mask (with odd/even stereo fanning & Q sharpness).
    const float floorGain = 0.002f + 0.11f * (1.0f - qSharp) * (1.0f - qSharp);
    buf_->maskL.fill(floorGain);
    buf_->maskR.fill(floorGain);

    const float sigmaBins = 0.58f + (1.0f - qSharp) * (1.0f - qSharp) * 1.95f;
    const float invTwoSigma2 = 0.5f / (sigmaBins * sigmaBins);
    const int splatRad = std::clamp(static_cast<int>(std::ceil(sigmaBins * 2.6f)), 2, 9);

    std::array<float, 12> pcGlow{};
    const float tiltExp = -0.58f + 0.76f * tilt;

    for (int v = 0; v < activeVoiceCount_; ++v) {
        const auto& voice = buf_->voices[static_cast<std::size_t>(v)];
        if (voice.weight <= 0.0f)
            continue;
        const float f0 = static_cast<float>(voice.freqHz);
        if (f0 < 24.0f)
            continue;
        const float midiEst = 69.0f + 12.0f * std::log2(std::max(f0, 20.0f) / 440.0f);
        int pc = static_cast<int>(std::lround(midiEst)) % 12;
        if (pc < 0)
            pc += 12;

        for (int h = 1; h <= numHarms; ++h) {
            const float fh = f0 * static_cast<float>(h);
            if (fh >= static_cast<float>(sampleRate_) * 0.46f)
                break;

            const float wTilt = std::pow(static_cast<float>(h), tiltExp) * voice.weight * 1.35f;
            const float detShift = (h > 1) ? (fh * detuneRatio * 0.52f) : 0.0f;
            const float bL = (fh - detShift) * invBinHz;
            const float bR = (fh + detShift) * invBinHz;

            const float stereoFactor = (fh > 220.0f) ? fan * 0.54f : 0.0f;
            const float sideSign = ((h + v) & 1) ? 1.0f : -1.0f;
            const float gainL = wTilt * (1.0f + sideSign * stereoFactor);
            const float gainR = wTilt * (1.0f - sideSign * stereoFactor);

            const int centerL = static_cast<int>(std::lround(bL));
            const int centerR = static_cast<int>(std::lround(bR));
            const int loL = std::max(1, centerL - splatRad);
            const int hiL = std::min(bins - 2, centerL + splatRad);
            for (int k = loL; k <= hiL; ++k) {
                const float d = static_cast<float>(k) - bL;
                const float bell = std::exp(-d * d * invTwoSigma2);
                const auto ku = static_cast<std::size_t>(k);
                buf_->maskL[ku] = std::min(3.4f, buf_->maskL[ku] + gainL * bell);
            }
            const int loR = std::max(1, centerR - splatRad);
            const int hiR = std::min(bins - 2, centerR + splatRad);
            for (int k = loR; k <= hiR; ++k) {
                const float d = static_cast<float>(k) - bR;
                const float bell = std::exp(-d * d * invTwoSigma2);
                const auto ku = static_cast<std::size_t>(k);
                buf_->maskR[ku] = std::min(3.4f, buf_->maskR[ku] + gainR * bell);
            }

            if (h <= 2 && centerL >= 1 && centerL < bins) {
                pcGlow[static_cast<std::size_t>(pc)] =
                    std::max(pcGlow[static_cast<std::size_t>(pc)],
                             magToUiNorm(buf_->workInMag[static_cast<std::size_t>(centerL)] * 1.8f));
            }
        }
    }

    // 3. Apply live spectral color engine (Chord Lattice or Carrier Morph) + click-free Freeze cloud blend.
    const float decayCoef = std::pow(decay, 0.42f) * 0.945f;
    const float inputDrive = 1.0f - 0.58f * decayCoef;
    const float hopPhaseStep =
        static_cast<float>(hba::dsp::kTwoPi * static_cast<double>(fb.hopSize) / static_cast<double>(fb.fftSize));

    // Smooth click-free freeze crossfade (~25 ms over 5 hops).
    const float freezeTarget = wantFrozen ? 1.0f : 0.0f;
    freezeFade_ += 0.25f * (freezeTarget - freezeFade_);
    if (!wantFrozen && freezeFade_ < 0.002f) {
        freezeFade_ = 0.0f;
        freezeCaptured_ = false;
    }

    // Capture or continuously refresh frozen magnitude cloud whenever freeze is active.
    const bool justLatched = freezeToggle && !prevFreezeParam_;
    const bool continuousCloud = !freezeToggle && glassMode;
    if (wantFrozen) {
        if (justLatched || !freezeCaptured_) {
            // Capture max(live input, ringing chord tail) so freezing mid-tail never grabs silence!
            for (int k = 0; k < bins; ++k) {
                const auto ku = static_cast<std::size_t>(k);
                const float liveL = std::hypot(fb.reL[k], fb.imL[k]);
                const float liveR = std::hypot(fb.reR[k], fb.imR[k]);
                const float tailL = std::hypot(buf_->tailReL[ku], buf_->tailImL[ku]);
                const float tailR = std::hypot(buf_->tailReR[ku], buf_->tailImR[ku]);
                buf_->freezeMagL[ku] = std::max(liveL, tailL * 0.85f);
                buf_->freezeMagR[ku] = std::max(liveR, tailR * 0.85f);
            }
            freezeCaptured_ = true;
        } else if (continuousCloud) {
            const float holdCoef = 0.80f + 0.185f * decay;
            for (int k = 0; k < bins; ++k) {
                const auto ku = static_cast<std::size_t>(k);
                const float mL = std::hypot(fb.reL[k], fb.imL[k]);
                const float mR = std::hypot(fb.reR[k], fb.imR[k]);
                buf_->freezeMagL[ku] = mL > buf_->freezeMagL[ku]
                                           ? (0.45f * mL + 0.55f * buf_->freezeMagL[ku])
                                           : (holdCoef * buf_->freezeMagL[ku] + (1.0f - holdCoef) * mL);
                buf_->freezeMagR[ku] = mR > buf_->freezeMagR[ku]
                                           ? (0.45f * mR + 0.55f * buf_->freezeMagR[ku])
                                           : (holdCoef * buf_->freezeMagR[ku] + (1.0f - holdCoef) * mR);
            }
        }
    }
    prevFreezeParam_ = freezeToggle;

    // Live (non-frozen) engine computation (skip only when 100% frozen).
    if (freezeFade_ < 0.998f) {
        if (mode == EngineMode::carrierMorph) {
            // Dimensionally exact Carrier Morph / Spectral Vocoder + coherent decay & shimmer.
            const bool useSc = fb.hasSidechain && scRms_ > 1.0e-4;
            const float cTilt = p.real(ParamId::carrierTilt);

            // First compute actual carrier complex spectrum and its exact bin magnitudes.
            buf_->workCarrMag[0] = buf_->workCarrMag[static_cast<std::size_t>(bins - 1)] = 0.0f;
            for (int k = 1; k < bins - 1; ++k) {
                const auto ku = static_cast<std::size_t>(k);
                if (useSc) {
                    buf_->workCarrReL[ku] = fb.scReL[k];
                    buf_->workCarrImL[ku] = fb.scImL[k];
                    buf_->workCarrReR[ku] = fb.scReR[k];
                    buf_->workCarrImR[ku] = fb.scImR[k];
                } else {
                    buf_->internalCarrPhase[ku] += hopPhaseStep * static_cast<float>(k);
                    wrapPhase(buf_->internalCarrPhase[ku]);
                    const float cs = std::cos(buf_->internalCarrPhase[ku]);
                    const float sn = std::sin(buf_->internalCarrPhase[ku]);
                    buf_->workCarrReL[ku] = buf_->maskL[ku] * cs;
                    buf_->workCarrImL[ku] = buf_->maskL[ku] * sn;
                    buf_->workCarrReR[ku] = buf_->maskR[ku] * cs;
                    buf_->workCarrImR[ku] = buf_->maskR[ku] * sn;
                }
                buf_->workCarrMag[ku] = 0.5f * (std::hypot(buf_->workCarrReL[ku], buf_->workCarrImL[ku]) +
                                                std::hypot(buf_->workCarrReR[ku], buf_->workCarrImR[ku]));
            }

            // Compute matched 11-bin smoothed envelopes of Main and Carrier on identical linear magnitude scale.
            for (int k = 1; k < bins - 1; ++k) {
                const auto ku = static_cast<std::size_t>(k);
                const int lo = std::max(1, k - 5);
                const int hi = std::min(bins - 2, k + 5);
                float sumM = 0.0f;
                float sumC = 0.0f;
                const float invW = 1.0f / static_cast<float>(hi - lo + 1);
                for (int j = lo; j <= hi; ++j) {
                    sumM += buf_->workInMag[static_cast<std::size_t>(j)];
                    sumC += buf_->workCarrMag[static_cast<std::size_t>(j)];
                }
                buf_->envMain[ku] = sumM * invW;
                buf_->envCarr[ku] = sumC * invW;
            }

            for (int k = 1; k < bins - 1; ++k) {
                const auto ku = static_cast<std::size_t>(k);
                const float freqNorm = static_cast<float>(k) / static_cast<float>(bins);
                const float tiltGain = std::pow(std::max(freqNorm, 0.01f), cTilt * 0.55f);

                const float mMagL = std::hypot(fb.reL[k], fb.imL[k]);
                const float mMagR = std::hypot(fb.reR[k], fb.imR[k]);
                const float cReL = buf_->workCarrReL[ku];
                const float cImL = buf_->workCarrImL[ku];
                const float cReR = buf_->workCarrReR[ku];
                const float cImR = buf_->workCarrImR[ku];
                const float cMagL = std::hypot(cReL, cImL);
                const float cMagR = std::hypot(cReR, cImR);

                // Linear Vocoder magnitude: Main envelope imprints onto Carrier fine structure.
                const float vocScale =
                    std::clamp(buf_->envMain[ku] / (buf_->envCarr[ku] + 1.0e-4f), 0.0f, 4.5f) * tiltGain;
                const float vocMagL = cMagL * vocScale;
                const float vocMagR = cMagR * vocScale;

                // Level-matched Geometric Cross-Morph: linear in signal amplitude!
                const float carrScaledL = cMagL * vocScale;
                const float carrScaledR = cMagR * vocScale;
                const float geomMagL = std::sqrt(mMagL * (carrScaledL + 1.0e-6f)) * (0.45f + 0.55f * buf_->maskL[ku]);
                const float geomMagR = std::sqrt(mMagR * (carrScaledR + 1.0e-6f)) * (0.45f + 0.55f * buf_->maskR[ku]);

                const float targetMagL = (1.0f - morph) * vocMagL + morph * geomMagL;
                const float targetMagR = (1.0f - morph) * vocMagR + morph * geomMagR;

                // Blend phase cleanly via unit phasors.
                const float cUnitReL = cReL / (cMagL + 1.0e-7f);
                const float cUnitImL = cImL / (cMagL + 1.0e-7f);
                const float mUnitReL = fb.reL[k] / (mMagL + 1.0e-7f);
                const float mUnitImL = fb.imL[k] / (mMagL + 1.0e-7f);
                float pReL = (1.0f - 0.5f * morph) * cUnitReL + (0.5f * morph) * mUnitReL;
                float pImL = (1.0f - 0.5f * morph) * cUnitImL + (0.5f * morph) * mUnitImL;
                const float pNormL = 1.0f / std::max(std::hypot(pReL, pImL), 1.0e-5f);
                pReL *= pNormL;
                pImL *= pNormL;

                const float cUnitReR = cReR / (cMagR + 1.0e-7f);
                const float cUnitImR = cImR / (cMagR + 1.0e-7f);
                const float mUnitReR = fb.reR[k] / (mMagR + 1.0e-7f);
                const float mUnitImR = fb.imR[k] / (mMagR + 1.0e-7f);
                float pReR = (1.0f - 0.5f * morph) * cUnitReR + (0.5f * morph) * mUnitReR;
                float pImR = (1.0f - 0.5f * morph) * cUnitImR + (0.5f * morph) * mUnitImR;
                const float pNormR = 1.0f / std::max(std::hypot(pReR, pImR), 1.0e-5f);
                pReR *= pNormR;
                pImR *= pNormR;

                float drvReL = targetMagL * pReL;
                float drvImL = targetMagL * pImL;
                float drvReR = targetMagR * pReR;
                float drvImR = targetMagR * pImR;

                if (shimmer > 0.001f && k >= 6) {
                    const auto halfK = static_cast<std::size_t>(k / 2);
                    const float shimW = shimmer * 0.45f;
                    drvReL += buf_->tailReL[halfK] * buf_->maskL[ku] * shimW;
                    drvImL += buf_->tailImL[halfK] * buf_->maskL[ku] * shimW;
                    drvReR += buf_->tailReR[halfK] * buf_->maskR[ku] * shimW;
                    drvImR += buf_->tailImR[halfK] * buf_->maskR[ku] * shimW;
                }

                float rotReL = 0.0f, rotImL = 0.0f, rotReR = 0.0f, rotImR = 0.0f;
                rotateQuadPhase(k, buf_->tailReL[ku], buf_->tailImL[ku], rotReL, rotImL);
                rotateQuadPhase(k, buf_->tailReR[ku], buf_->tailImR[ku], rotReR, rotImR);

                const float binDecay = decayCoef * 0.85f;
                const float outReL = binDecay * rotReL + (1.0f - 0.5f * binDecay) * drvReL;
                const float outImL = binDecay * rotImL + (1.0f - 0.5f * binDecay) * drvImL;
                const float outReR = binDecay * rotReR + (1.0f - 0.5f * binDecay) * drvReR;
                const float outImR = binDecay * rotImR + (1.0f - 0.5f * binDecay) * drvImR;

                buf_->tailReL[ku] = outReL;
                buf_->tailImL[ku] = outImL;
                buf_->tailReR[ku] = outReR;
                buf_->tailImR[ku] = outImR;

                fb.reL[k] = outReL;
                fb.imL[k] = outImL;
                fb.reR[k] = outReR;
                fb.imR[k] = outImR;
            }
        } else {
            // Chord Lattice (Spectral Pitch Pulling + phase-coherent 4x COLA tail + Shimmer).
            const float pullBlend = 0.28f + 0.52f * qSharp;
            for (int k = 1; k < bins - 1; ++k) {
                const auto ku = static_cast<std::size_t>(k);
                const int lo = std::max(1, k - 4);
                const int hi = std::min(bins - 2, k + 4);
                float localMag = 0.0f;
                for (int j = lo; j <= hi; ++j)
                    localMag = std::max(localMag, buf_->workInMag[static_cast<std::size_t>(j)]);
                buf_->envMain[ku] = localMag;
            }

            for (int k = 1; k < bins - 1; ++k) {
                const auto ku = static_cast<std::size_t>(k);
                float rotReL = 0.0f, rotImL = 0.0f, rotReR = 0.0f, rotImR = 0.0f;
                rotateQuadPhase(k, buf_->tailReL[ku], buf_->tailImL[ku], rotReL, rotImL);
                rotateQuadPhase(k, buf_->tailReR[ku], buf_->tailImR[ku], rotReR, rotImR);

                buf_->internalCarrPhase[ku] += hopPhaseStep * static_cast<float>(k);
                wrapPhase(buf_->internalCarrPhase[ku]);
                const float pullAmpL = buf_->envMain[ku] * buf_->maskL[ku] * pullBlend * 0.65f;
                const float pullAmpR = buf_->envMain[ku] * buf_->maskR[ku] * pullBlend * 0.65f;
                const float cs = std::cos(buf_->internalCarrPhase[ku]);
                const float sn = std::sin(buf_->internalCarrPhase[ku]);

                float drvReL = fb.reL[k] * buf_->maskL[ku] * (1.0f - 0.35f * pullBlend) + pullAmpL * cs;
                float drvImL = fb.imL[k] * buf_->maskL[ku] * (1.0f - 0.35f * pullBlend) + pullAmpL * sn;
                float drvReR = fb.reR[k] * buf_->maskR[ku] * (1.0f - 0.35f * pullBlend) + pullAmpR * cs;
                float drvImR = fb.imR[k] * buf_->maskR[ku] * (1.0f - 0.35f * pullBlend) + pullAmpR * sn;

                if (shimmer > 0.001f && k >= 6) {
                    const auto halfK = static_cast<std::size_t>(k / 2);
                    const float shimW = shimmer * 0.55f;
                    drvReL += buf_->tailReL[halfK] * buf_->maskL[ku] * shimW;
                    drvImL += buf_->tailImL[halfK] * buf_->maskL[ku] * shimW;
                    drvReR += buf_->tailReR[halfK] * buf_->maskR[ku] * shimW;
                    drvImR += buf_->tailImR[halfK] * buf_->maskR[ku] * shimW;
                }

                const float freqDamp =
                    (k > bins / 2) ? (1.0f - 0.14f * static_cast<float>(k - bins / 2) / static_cast<float>(bins / 2))
                                   : 1.0f;
                const float binDecay = decayCoef * freqDamp;

                const float outReL = binDecay * rotReL + inputDrive * drvReL;
                const float outImL = binDecay * rotImL + inputDrive * drvImL;
                const float outReR = binDecay * rotReR + inputDrive * drvReR;
                const float outImR = binDecay * rotImR + inputDrive * drvImR;

                buf_->tailReL[ku] = outReL;
                buf_->tailImL[ku] = outImL;
                buf_->tailReR[ku] = outReR;
                buf_->tailImR[ku] = outImR;

                fb.reL[k] = outReL;
                fb.imL[k] = outImL;
                fb.reR[k] = outReR;
                fb.imR[k] = outImR;
            }
        }
    }

    // Blend smoothly with Glass Freeze / Latched cloud when freezeFade_ > 0.
    if (freezeFade_ > 0.002f) {
        const float liveW = 1.0f - freezeFade_;
        for (int k = 1; k < bins - 1; ++k) {
            const auto ku = static_cast<std::size_t>(k);
            buf_->freezePhaseL[ku] +=
                hopPhaseStep * (static_cast<float>(k) + 0.03f * std::sin(static_cast<float>(k) * 0.37f));
            buf_->freezePhaseR[ku] +=
                hopPhaseStep * (static_cast<float>(k) - 0.03f * std::cos(static_cast<float>(k) * 0.41f));
            wrapPhase(buf_->freezePhaseL[ku]);
            wrapPhase(buf_->freezePhaseR[ku]);

            float mL = buf_->freezeMagL[ku] * buf_->maskL[ku];
            float mR = buf_->freezeMagR[ku] * buf_->maskR[ku];
            if (shimmer > 0.001f && k >= 4) {
                const auto halfK = static_cast<std::size_t>(k / 2);
                mL += shimmer * 0.55f * buf_->freezeMagL[halfK] * buf_->maskL[ku];
                mR += shimmer * 0.55f * buf_->freezeMagR[halfK] * buf_->maskR[ku];
            }
            const float frReL = mL * std::cos(buf_->freezePhaseL[ku]);
            const float frImL = mL * std::sin(buf_->freezePhaseL[ku]);
            const float frReR = mR * std::cos(buf_->freezePhaseR[ku]);
            const float frImR = mR * std::sin(buf_->freezePhaseR[ku]);

            if (liveW < 0.002f) {
                fb.reL[k] = frReL;
                fb.imL[k] = frImL;
                fb.reR[k] = frReR;
                fb.imR[k] = frImR;
            } else {
                fb.reL[k] = liveW * fb.reL[k] + freezeFade_ * frReL;
                fb.imL[k] = liveW * fb.imL[k] + freezeFade_ * frImL;
                fb.reR[k] = liveW * fb.reR[k] + freezeFade_ * frReR;
                fb.imR[k] = liveW * fb.imR[k] + freezeFade_ * frImR;
            }
        }
    }

    // 4. Cleanly attenuate sub bins inside STFT (sub bypass carries < subLockHz in exact phase).
    fb.reL[0] = fb.imL[0] = fb.reR[0] = fb.imR[0] = 0.0f;
    fb.reL[bins - 1] = fb.imL[bins - 1] = fb.reR[bins - 1] = fb.imR[bins - 1] = 0.0f;
    for (int k = 1; k < subCutoffBin; ++k) {
        const float ramp = static_cast<float>(k) / static_cast<float>(subCutoffBin);
        const float att = ramp * ramp * 0.25f;
        fb.reL[k] *= att;
        fb.imL[k] *= att;
        fb.reR[k] *= att;
        fb.imR[k] *= att;
    }

    // 5. Energy-preserving loudness compensation (freeze keeps steady loudness even when input stops).
    float coloredEnergy = 0.0f;
    for (int k = subCutoffBin; k < bins - 1; ++k) {
        const float mL = std::hypot(fb.reL[k], fb.imL[k]);
        const float mR = std::hypot(fb.reR[k], fb.imR[k]);
        const float m = 0.5f * (mL + mR);
        coloredEnergy += m * m;
    }
    const float targetComp = (!wantFrozen && inEnergy > 1.0e-7f && coloredEnergy > 1.0e-7f)
                                 ? std::clamp(std::sqrt(inEnergy / coloredEnergy) * 0.92f, 0.35f, 3.0f)
                                 : (wantFrozen ? compGainSmoothed_ : 1.0f);
    compGainSmoothed_ += 0.18f * (targetComp - compGainSmoothed_);

    // 6. Two-Pass Symmetric Bark Auto-Tame with per-bin attack/release smoothing!
    // Pass A: apply compGain and populate immutable pre-tame magnitudes across ALL bins.
    buf_->workPreTameMag[0] = buf_->workPreTameMag[static_cast<std::size_t>(bins - 1)] = 0.0f;
    for (int k = 1; k < bins - 1; ++k) {
        const auto ku = static_cast<std::size_t>(k);
        fb.reL[k] *= compGainSmoothed_;
        fb.imL[k] *= compGainSmoothed_;
        fb.reR[k] *= compGainSmoothed_;
        fb.imR[k] *= compGainSmoothed_;
        buf_->workPreTameMag[ku] = 0.5f * (std::hypot(fb.reL[k], fb.imL[k]) + std::hypot(fb.reR[k], fb.imR[k]));
    }

    // Pass B: compute true symmetric Bark prominence strictly from immutable buf_->workPreTameMag.
    const float tameFocusHz = p.real(ParamId::tameFocusHz);
    const float tameSelect = p.real(ParamId::tameSelect);
    const float spikeThresh = 1.85f - 0.85f * tameSelect;
    float maxTameCutDb = 0.0f;

    buf_->workTameGain[0] = buf_->workTameGain[static_cast<std::size_t>(bins - 1)] = 1.0f;
    buf_->workOutMag[0] = buf_->workOutMag[static_cast<std::size_t>(bins - 1)] = 0.0f;
    for (int k = 1; k < bins - 1; ++k) {
        const auto ku = static_cast<std::size_t>(k);
        const float mag = buf_->workPreTameMag[ku];
        float targetAtten = 1.0f;

        if (tameAmt > 0.005f && k >= subCutoffBin) {
            const float fHz = static_cast<float>(k) * binHz;
            const int rad = std::clamp(static_cast<int>(2.0f + fHz * 0.0018f), 3, 14);
            const int lo = std::max(subCutoffBin, k - rad);
            const int hi = std::min(bins - 2, k + rad);
            float localSum = 0.0f;
            int localCnt = 0;
            for (int j = lo; j <= hi; ++j) {
                if (std::abs(j - k) <= 1)
                    continue;
                localSum += buf_->workPreTameMag[static_cast<std::size_t>(j)];
                ++localCnt;
            }
            const float localAvg = localCnt > 0 ? (localSum / static_cast<float>(localCnt)) : mag;
            const float ratio = mag / (localAvg + 1.0e-5f);

            const float octDist = std::log2(std::max(fHz, 120.0f) / std::max(tameFocusHz, 300.0f));
            const float focusW = 0.28f + 0.72f * std::exp(-0.5f * (octDist * octDist) / (1.65f * 1.65f));

            if (ratio > spikeThresh) {
                const float excess = (ratio - spikeThresh) / std::max(ratio, 1.0f);
                targetAtten = 1.0f - std::clamp(excess * tameAmt * focusW * 1.15f, 0.0f, 0.86f);
            }
        }

        // Smooth per-bin gain with fast attack (catches spikes) and silky release (zero chatter).
        const float prevG = buf_->tameBinSmooth[ku];
        const float sm = (targetAtten < prevG) ? 0.45f : 0.16f;
        const float atten = prevG + sm * (targetAtten - prevG);
        buf_->tameBinSmooth[ku] = atten;

        fb.reL[k] *= atten;
        fb.imL[k] *= atten;
        fb.reR[k] *= atten;
        fb.imR[k] *= atten;
        buf_->workTameGain[ku] = atten;
        buf_->workOutMag[ku] = mag * atten;

        if (atten < 0.995f) {
            const float cutDb = -20.0f * std::log10(std::max(atten, 0.05f));
            maxTameCutDb = std::max(maxTameCutDb, cutDb);
        }
    }

    if (publishTelemetry) {
        updateTelemetrySpectrum(buf_->workInMag.data(), buf_->workOutMag.data(), buf_->workTameGain.data());
        telemetry_.tamePeakDb = 0.72f * telemetry_.tamePeakDb + 0.28f * maxTameCutDb;
        telemetry_.frozen = wantFrozen;
        for (std::size_t pc = 0; pc < 12; ++pc)
            telemetry_.pitchEnergy[pc] = 0.65f * telemetry_.pitchEnergy[pc] + 0.35f * pcGlow[pc];
    }
}

void Engine::processCombSample(double inL, double inR, const CombBlockParams& cb, double& outL,
                               double& outR) noexcept PV_NONBLOCKING {
    const double monoIn = 0.5 * (inL + inR);
    const double sideIn = 0.5 * (inL - inR);
    const double t60 = cb.t60;
    const double dampCoef = cb.dampCoef;
    const double fanAmt = cb.fanAmt;
    const double dcR = 0.9965;
    const double excGain = cb.excGain;
    constexpr double kLog001 = -6.907755278982137;

    double sumL = 0.0;
    double sumR = 0.0;
    for (int v = 0; v < activeVoiceCount_; ++v) {
        auto& voice = buf_->voices[static_cast<std::size_t>(v)];
        voice.freqHz += voiceGlideCoef_ * (voice.targetHz - voice.freqHz);
        const double w = static_cast<double>(voice.weight);
        if (w <= 1.0e-5)
            continue;

        const double f =
            std::clamp(voice.freqHz, sampleRate_ / static_cast<double>(kCombBufSize - 8), 0.35 * sampleRate_);
        const double stereoDet = 1.0 + fanAmt * 0.0028 * ((v & 1) ? 1.0 : -1.0);
        const double delayL = std::clamp(sampleRate_ / (f * stereoDet), 2.0, static_cast<double>(kCombBufSize - 4));
        const double delayR = std::clamp(sampleRate_ / (f / stereoDet), 2.0, static_cast<double>(kCombBufSize - 4));

        if (std::abs(f - voice.cachedFbFreq) > 1.0e-6 || std::abs(t60 - voice.cachedFbT60) > 1.0e-6) {
            voice.cachedFb = std::clamp(std::exp(kLog001 / std::max(f * t60, 0.25)), 0.35, 0.965);
            voice.cachedFbFreq = f;
            voice.cachedFbT60 = t60;
        }
        const double fb = voice.cachedFb;
        const double polR = (fanAmt > 0.35 && (v & 1) != 0) ? (1.0 - 0.55 * (fanAmt - 0.35)) : 1.0;

        const auto readInterp = [&](const std::array<float, kCombBufSize>& buf, double dSamples) noexcept {
            const double readPos = static_cast<double>(voice.writePos) - dSamples;
            const double floorPos = std::floor(readPos);
            const double frac = readPos - floorPos;
            const int i0 = static_cast<int>(floorPos) & kCombBufMask;
            const int i1 = (i0 + 1) & kCombBufMask;
            return static_cast<double>(buf[static_cast<std::size_t>(i0)]) * (1.0 - frac) +
                   static_cast<double>(buf[static_cast<std::size_t>(i1)]) * frac;
        };

        const double tapL = readInterp(voice.bufL, delayL);
        const double tapR = readInterp(voice.bufR, delayR);

        voice.lpL += dampCoef * (tapL - voice.lpL);
        voice.lpR += dampCoef * (tapR - voice.lpR);
        const double dcL = voice.lpL - voice.dcInL + dcR * voice.dcOutL;
        voice.dcInL = voice.lpL;
        voice.dcOutL = dcL;
        const double dcRVal = voice.lpR - voice.dcInR + dcR * voice.dcOutR;
        voice.dcInR = voice.lpR;
        voice.dcOutR = dcRVal;

        const double driveL = (monoIn + sideIn * 0.45) * excGain;
        const double driveR = (monoIn - sideIn * 0.45) * excGain;
        const double fbL = hba::dsp::softGuard(driveL + fb * dcL, 1.2, 2.2);
        const double fbR = hba::dsp::softGuard(driveR + fb * polR * dcRVal, 1.2, 2.2);

        voice.bufL[static_cast<std::size_t>(voice.writePos)] = static_cast<float>(fbL);
        voice.bufR[static_cast<std::size_t>(voice.writePos)] = static_cast<float>(fbR);
        voice.writePos = (voice.writePos + 1) & kCombBufMask;

        sumL += dcL * w * static_cast<double>(voice.panL);
        sumR += dcRVal * w * static_cast<double>(voice.panR);
    }

    outL = hba::dsp::softGuard(sumL * 1.15, 0.95, 1.85);
    outR = hba::dsp::softGuard(sumR * 1.15, 0.95, 1.85);
}

void Engine::pushCombTelemetrySample(float inMono, float outMono, double liveTameCutDb,
                                     double tameFocusHz) noexcept PV_NONBLOCKING {
    constexpr int kMask = kFftSize - 1;
    buf_->telInRing[static_cast<std::size_t>(telPos_)] = inMono;
    buf_->telOutRing[static_cast<std::size_t>(telPos_)] = outMono;
    telPos_ = (telPos_ + 1) & kMask;
    if (++telHopCount_ < 512)
        return;
    telHopCount_ = 0;

    for (int i = 0; i < kFftSize; ++i) {
        const auto idx = static_cast<std::size_t>((telPos_ + i) & kMask);
        const float w = buf_->telWin[static_cast<std::size_t>(i)];
        buf_->telTimeIn[static_cast<std::size_t>(i)] = buf_->telInRing[idx] * w;
        buf_->telTimeOut[static_cast<std::size_t>(i)] = buf_->telOutRing[idx] * w;
    }

    // 2-for-1 FFT: analyze mono input (L slot) and mono comb output (R slot) in ONE real FFT!
    buf_->telFft.forwardStereo(buf_->telTimeIn.data(), buf_->telTimeOut.data(), buf_->telInRe.data(),
                               buf_->telInIm.data(), buf_->telOutRe.data(), buf_->telOutIm.data(),
                               buf_->telScratchRe.data(), buf_->telScratchIm.data());

    const float binHz = static_cast<float>(sampleRate_) / static_cast<float>(kFftSize);
    const float cutLinear = static_cast<float>(hba::dsp::dbToGain(-std::max(0.0, liveTameCutDb)));
    for (int k = 0; k < kNumBins; ++k) {
        const auto ku = static_cast<std::size_t>(k);
        buf_->telInMag[ku] = std::hypot(buf_->telInRe[ku], buf_->telInIm[ku]);
        buf_->telOutMag[ku] = std::hypot(buf_->telOutRe[ku], buf_->telOutIm[ku]);
        if (liveTameCutDb > 0.1 && k > 0) {
            const float fHz = static_cast<float>(k) * binHz;
            const float oct = std::log2(std::max(fHz, 120.0f) / std::max(static_cast<float>(tameFocusHz), 300.0f));
            const float bell = std::exp(-0.5f * oct * oct / (0.85f * 0.85f));
            buf_->telTameGain[ku] = 1.0f - bell * (1.0f - cutLinear);
        } else {
            buf_->telTameGain[ku] = 1.0f;
        }
    }

    // Compute per-pitch-class glow from real comb spectrum too!
    std::array<float, 12> pcGlow{};
    const float invBinHz = 1.0f / std::max(binHz, 1.0e-3f);
    for (int v = 0; v < activeVoiceCount_; ++v) {
        const auto& voice = buf_->voices[static_cast<std::size_t>(v)];
        if (voice.weight <= 0.0f)
            continue;
        const float f0 = static_cast<float>(voice.freqHz);
        const int b = std::clamp(static_cast<int>(std::lround(f0 * invBinHz)), 1, kNumBins - 1);
        const float midiEst = 69.0f + 12.0f * std::log2(std::max(f0, 20.0f) / 440.0f);
        int pc = static_cast<int>(std::lround(midiEst)) % 12;
        if (pc < 0)
            pc += 12;
        pcGlow[static_cast<std::size_t>(pc)] = std::max(
            pcGlow[static_cast<std::size_t>(pc)], magToUiNorm(buf_->telOutMag[static_cast<std::size_t>(b)] * 1.8f));
    }

    updateTelemetrySpectrum(buf_->telInMag.data(), buf_->telOutMag.data(), buf_->telTameGain.data());
    telemetry_.tamePeakDb = 0.72f * telemetry_.tamePeakDb + 0.28f * static_cast<float>(liveTameCutDb);
    telemetry_.frozen = false;
    for (std::size_t pc = 0; pc < 12; ++pc)
        telemetry_.pitchEnergy[pc] = 0.65f * telemetry_.pitchEnergy[pc] + 0.35f * pcGlow[pc];
}

void Engine::synthesizeDemoSample(double& outL, double& outR) noexcept PV_NONBLOCKING {
    const double bpm = std::clamp(transport_.bpm, 60.0, 220.0);
    const double stepsPerSec = (bpm / 60.0) * 4.0;
    demoStepPhase_ += stepsPerSec * invSr_;
    if (demoStepPhase_ >= 1.0) {
        demoStepPhase_ -= 1.0;
        demoStep_ = (demoStep_ + 1) & 15;
        if (demoStep_ == 0 || demoStep_ == 10) {
            demoKickEnv_ = 1.0;
            demoKickPhase_ = 0.0;
        }
        if (demoStep_ == 4 || demoStep_ == 12)
            demoSnareEnv_ = 1.0;
        if ((demoStep_ & 1) == 0 || demoStep_ == 15)
            demoHatEnv_ = (demoStep_ & 1) ? 0.55 : 0.85;
    }

    const double baseHz = (demoStep_ >= 12) ? 48.999 : 43.654;
    demoPhase1_ += baseHz * 1.007 * invSr_;
    demoPhase2_ += baseHz * 0.993 * invSr_;
    demoPhaseSub_ += baseHz * invSr_;
    if (demoPhase1_ >= 1.0)
        demoPhase1_ -= 1.0;
    if (demoPhase2_ >= 1.0)
        demoPhase2_ -= 1.0;
    if (demoPhaseSub_ >= 1.0)
        demoPhaseSub_ -= 1.0;

    const double saw1 = 2.0 * demoPhase1_ - 1.0;
    const double saw2 = 2.0 * demoPhase2_ - 1.0;
    const double reeseL = 0.55 * saw1 + 0.45 * saw2;
    const double reeseR = 0.45 * saw1 + 0.55 * saw2;

    const double sweepHz = 520.0 + 1650.0 * (0.5 + 0.5 * std::sin(freeSec_ * 2.2));
    const auto reeseCoefs = hba::dsp::SvfCoefs::make(sweepHz, 1.35, sampleRate_);
    const double fL = std::tanh(demoReeseFilterL_.lowpass(reeseCoefs, reeseL) * 2.1);
    const double fR = std::tanh(demoReeseFilterR_.lowpass(reeseCoefs, reeseR) * 2.1);
    const double sub = std::sin(hba::dsp::kTwoPi * demoPhaseSub_) * 0.42;

    double kick = 0.0;
    if (demoKickEnv_ > 1.0e-4) {
        const double kHz = 46.0 + 210.0 * demoKickEnv_ * demoKickEnv_;
        demoKickPhase_ += hba::dsp::kTwoPi * kHz * invSr_;
        kick = std::sin(demoKickPhase_) * demoKickEnv_ * 0.58;
        demoKickEnv_ *= demoKickDecay_;
    }
    const double wn = demoRng_.nextBipolar();
    double snare = 0.0;
    if (demoSnareEnv_ > 1.0e-4) {
        snare = demoSnareFilter_.bandpass(demoSnareCoefs_, wn) * demoSnareEnv_ * 0.72;
        demoSnareEnv_ *= demoSnareDecay_;
    }
    double hat = 0.0;
    if (demoHatEnv_ > 1.0e-4) {
        hat = wn * demoHatEnv_ * 0.16;
        demoHatEnv_ *= demoHatDecay_;
    }

    outL = (fL * 0.34 + sub * 0.35 + kick + snare + hat) * 0.58;
    outR = (fR * 0.34 + sub * 0.35 + kick + snare * 0.92 - hat * 0.85) * 0.58;
}

void Engine::updateTelemetrySpectrum(const float* inMag, const float* outMag,
                                     const float* tameGain) noexcept PV_NONBLOCKING {
    for (int i = 0; i < kSpectrumBins; ++i) {
        const auto iu = static_cast<std::size_t>(i);
        const float loF = specBinLo_[iu];
        const float hiF = specBinHi_[iu];
        float peakIn = 0.0f;
        float peakOut = 0.0f;
        float minTame = 1.0f;

        if (hiF - loF < 1.25f) {
            const float center = 0.5f * (loF + hiF);
            const int k0 = std::clamp(static_cast<int>(std::floor(center)), 0, kNumBins - 2);
            const int k1 = k0 + 1;
            const float frac = std::clamp(center - static_cast<float>(k0), 0.0f, 1.0f);
            peakIn = inMag[k0] * (1.0f - frac) + inMag[k1] * frac;
            peakOut = outMag[k0] * (1.0f - frac) + outMag[k1] * frac;
            minTame = tameGain[k0] * (1.0f - frac) + tameGain[k1] * frac;
        } else {
            const int b0 = std::max(0, static_cast<int>(std::floor(loF)));
            const int b1 = std::min(kNumBins - 1, std::max(b0, static_cast<int>(std::ceil(hiF))));
            for (int k = b0; k <= b1; ++k) {
                const auto ku = static_cast<std::size_t>(k);
                peakIn = std::max(peakIn, inMag[ku]);
                peakOut = std::max(peakOut, outMag[ku]);
                minTame = std::min(minTame, tameGain[ku]);
            }
        }
        const float normIn = magToUiNorm(peakIn * (4.0f / static_cast<float>(kFftSize)));
        const float normOut = magToUiNorm(peakOut * (4.0f / static_cast<float>(kFftSize)));
        const float tameAmtUi = std::clamp(1.0f - minTame, 0.0f, 1.0f);

        telemetry_.inSpectrum[iu] = 0.62f * telemetry_.inSpectrum[iu] + 0.38f * normIn;
        telemetry_.outSpectrum[iu] = 0.62f * telemetry_.outSpectrum[iu] + 0.38f * normOut;
        telemetry_.tameReduction[iu] = 0.65f * telemetry_.tameReduction[iu] + 0.35f * tameAmtUi;
    }
}

void Engine::process(const float* inL, const float* inR, const float* sidechainL, const float* sidechainR, float* outL,
                     float* outR, int numSamples) noexcept PV_NONBLOCKING {
    hba::dsp::ScopedFlushDenormals flush;
    if (numSamples <= 0 || outL == nullptr || outR == nullptr)
        return;

    const Params p = targetParams_;
    const auto engineMode = static_cast<EngineMode>(p.choice(ParamId::engineMode));
    const auto latMode = static_cast<LatencyMode>(p.choice(ParamId::latencyMode));
    const bool liveZeroLat = (latMode == LatencyMode::live0ms);
    const bool wantCombAudio = liveZeroLat || (engineMode == EngineMode::harmonicComb);
    const bool subMono = p.on(ParamId::subMono);
    const bool demoOn = p.on(ParamId::demoAudition);
    const bool limitOn = p.on(ParamId::limitOn);
    const auto shaperModel = mapGritType(p.choice(ParamId::gritType));

    const double bpm = std::clamp(transport_.bpm, 40.0, 260.0);
    const double ppqPerSample = (bpm / 60.0) * invSr_;
    if (transport_.playing)
        hostPpq_ = transport_.ppq;

    // Gather 4-lane modulation configs once per block (zero allocation).
    std::array<hba::dsp::LaneConfig, kLaneCount> laneCfgs{};
    std::array<LaneTarget, kLaneCount> laneTargets{};
    for (int l = 0; l < kLaneCount; ++l) {
        auto& cfg = laneCfgs[static_cast<std::size_t>(l)];
        cfg.on = p.on(laneParam(l, LaneField::on));
        cfg.rateIndex = p.choice(laneParam(l, LaneField::rate));
        cfg.phaseDeg = static_cast<double>(p.real(laneParam(l, LaneField::phase)));
        cfg.depth = static_cast<double>(p.real(laneParam(l, LaneField::depth)));
        cfg.shape = p.choice(laneParam(l, LaneField::shape));
        cfg.follow = p.choice(laneParam(l, LaneField::follow));
        cfg.ratioIndex = p.choice(laneParam(l, LaneField::ratio));
        cfg.mode = static_cast<hba::dsp::LaneMode>(p.choice(laneParam(l, LaneField::mode)));
        cfg.hz = static_cast<double>(p.real(laneParam(l, LaneField::hz)));
        laneTargets[static_cast<std::size_t>(l)] = static_cast<LaneTarget>(p.choice(laneParam(l, LaneField::target)));
    }

    const double motion = static_cast<double>(p.real(ParamId::motion));
    const hba::dsp::LaneClock clk{hostPpq_, notePpq_, freeSec_, noteSec_};
    std::array<double, kLaneTargetCount> mod{};
    for (int l = 0; l < kLaneCount; ++l) {
        const double send = hba::dsp::evaluateLaneSend(laneCfgs, curves_, l, clk, motion);
        mod[static_cast<std::size_t>(laneTargets[static_cast<std::size_t>(l)])] += send;
    }

    const double modColor = mod[static_cast<std::size_t>(LaneTarget::color)];
    const double modDecay = mod[static_cast<std::size_t>(LaneTarget::decay)];
    const double modTame = mod[static_cast<std::size_t>(LaneTarget::tame)];
    const double modShift = mod[static_cast<std::size_t>(LaneTarget::shift)] * 12.0;
    const double modGrit = mod[static_cast<std::size_t>(LaneTarget::grit)];
    const double modTilt = mod[static_cast<std::size_t>(LaneTarget::tilt)];
    const double modFan = mod[static_cast<std::size_t>(LaneTarget::fan)];
    const double modMorph = mod[static_cast<std::size_t>(LaneTarget::morph)];

    const double blkSmCoef = hba::dsp::onePoleCoef(0.015, static_cast<double>(numSamples) * invSr_);
    const double pitchBendSt = smPitchBend_.step(pitchBendTargetSt_, blkSmCoef);
    const double modWheel = smModWheel_.step(modWheelTarget_, blkSmCoef);

    // Unified chord target update: Shift + Lane Shift + MIDI Pitch Bend apply to ALL 4 engines!
    bool midiWaiting = false;
    updateActiveChordTargets(p, static_cast<double>(p.real(ParamId::shift)) + modShift + pitchBendSt, midiWaiting);
    voiceGlideCoef_ =
        hba::dsp::onePoleCoef(std::max(0.001, static_cast<double>(p.real(ParamId::glideMs)) * 0.001), invSr_);

    const double smCoef = hba::dsp::onePoleCoef(0.008, invSr_);
    const double combFadeCoef = hba::dsp::onePoleCoef(0.012, invSr_);
    const double midiGateCoef = hba::dsp::onePoleCoef(0.020, invSr_);

    const double targetInGain = hba::dsp::dbToGain(static_cast<double>(p.real(ParamId::inputDb)));
    const double targetOutGain = hba::dsp::dbToGain(static_cast<double>(p.real(ParamId::outputDb)));
    const double targetSubGain = hba::dsp::dbToGain(static_cast<double>(p.real(ParamId::subLevelDb)));
    const double velScale = 1.0 - static_cast<double>(p.real(ParamId::velToColor)) * (1.0 - lastMidiVelocity_);
    const double targetColor = std::clamp(
        (static_cast<double>(p.real(ParamId::color)) + modColor * 0.5 + modWheel * 0.25) * velScale, 0.0, 1.0);
    const double targetShield = static_cast<double>(p.real(ParamId::transientShield));
    const double targetGrit = std::clamp(static_cast<double>(p.real(ParamId::grit)) + modGrit * 0.5, 0.0, 1.0);
    const double targetSubHz = static_cast<double>(p.real(ParamId::subLockHz));
    const double targetAirDb = static_cast<double>(p.real(ParamId::airDb));
    const double targetInHarm = static_cast<double>(p.real(ParamId::inHarmMix));
    const double shieldSens = static_cast<double>(p.real(ParamId::shieldSens));
    const double ceilingGain = hba::dsp::dbToGain(static_cast<double>(p.real(ParamId::ceilingDb)));
    const double decayComb = std::clamp(static_cast<double>(p.real(ParamId::decay)) + modDecay * 0.5, 0.0, 1.0);
    const double tiltComb = std::clamp(static_cast<double>(p.real(ParamId::tilt)) + modTilt, -1.0, 1.0);
    const double fanComb = std::clamp(static_cast<double>(p.real(ParamId::stereoFan)) + modFan * 0.5, 0.0, 1.0);
    const double qSharp = static_cast<double>(p.real(ParamId::qSharpness));
    const double combDampCutoff = 1400.0 * std::pow(10.0, tiltComb * 0.58 + 0.42);
    const CombBlockParams combParams{
        0.04 + std::pow(decayComb, 1.5) * 1.60,
        hba::dsp::onePoleCoef(1.0 / (hba::dsp::kTwoPi * std::clamp(combDampCutoff, 450.0, 18000.0)), invSr_),
        fanComb,
        0.46 + 0.36 * qSharp,
    };
    const double liveTameAmt = std::clamp(static_cast<double>(p.real(ParamId::tame)) + modTame * 0.5, 0.0, 1.0);
    const double tameFocusHz = static_cast<double>(p.real(ParamId::tameFocusHz));
    const double liveTameCutDb = 10.0 * liveTameAmt * std::clamp(liveTameEnv_ * 2.2, 0.0, 1.0);

    const bool hasScBus = (sidechainL != nullptr && sidechainR != nullptr);
    const bool analyzeSc = !liveZeroLat && engineMode == EngineMode::carrierMorph && hasScBus;
    const double scEnvCoef = hba::dsp::onePoleCoef(0.05, invSr_);
    const double grRelCoef = hba::dsp::onePoleCoef(0.065, invSr_);

    float blockPeakL = 0.0f;
    float blockPeakR = 0.0f;
    float blockMaxTrans = 0.0f;
    double blockMinGr = 1.0;

    // Smoothly updated block-rate Air Shelf & Live Auto-Tame biquads.
    const double smoothAirDb = smAirDb_.step(targetAirDb, blkSmCoef);
    const auto airCoefs = hba::dsp::BiquadCoefs::highShelf(7600.0, 0.72, smoothAirDb, sampleRate_);
    const auto liveTameCoefs = hba::dsp::BiquadCoefs::peak(
        tameFocusHz, 1.2 + 2.4 * static_cast<double>(p.real(ParamId::tameSelect)), -liveTameCutDb, sampleRate_);

    std::array<float, hba::dsp::kMaxOversampling> osBufL{};
    std::array<float, hba::dsp::kMaxOversampling> osBufR{};

    for (int i = 0; i < numSamples; ++i) {
        hostPpq_ += ppqPerSample;
        notePpq_ += ppqPerSample;
        freeSec_ += invSr_;
        noteSec_ += invSr_;

        const double inGain = smInputGain_.step(targetInGain, smCoef);
        const double outGain = smOutputGain_.step(targetOutGain, smCoef);
        const double subGain = smSubGain_.step(targetSubGain, smCoef);
        const double midiGate = smMidiGate_.step(midiWaiting ? 0.0 : 1.0, midiGateCoef);
        const double colorMix = smColor_.step(targetColor, smCoef) * midiGate;
        const double shieldAmt = smShield_.step(targetShield, smCoef);
        const double gritAmt = smGrit_.step(targetGrit, smCoef);
        const double subHz = smSubHz_.step(targetSubHz, smCoef);
        const double inHarm = smInHarm_.step(targetInHarm, smCoef);
        const double combBlend = smCombBlend_.step(wantCombAudio ? 1.0 : 0.0, combFadeCoef);

        double xL = (inL != nullptr && std::isfinite(inL[i])) ? static_cast<double>(inL[i]) * inGain : 0.0;
        double xR = (inR != nullptr && std::isfinite(inR[i])) ? static_cast<double>(inR[i]) * inGain : 0.0;
        if (demoOn) {
            double dL = 0.0, dR = 0.0;
            synthesizeDemoSample(dL, dR);
            xL += dL;
            xR += dR;
        }

        double scL = 0.0, scR = 0.0;
        if (hasScBus) {
            scL = std::isfinite(sidechainL[i]) ? static_cast<double>(sidechainL[i]) : 0.0;
            scR = std::isfinite(sidechainR[i]) ? static_cast<double>(sidechainR[i]) : 0.0;
            const double scPow = 0.5 * (scL * scL + scR * scR);
            scRms_ += scEnvCoef * (std::sqrt(scPow) - scRms_);
        } else {
            scRms_ *= 0.999;
        }

        // 1. Phase-locked LR4 Sub Crossover with cached coefs (avoids per-sample tan() when static).
        if (std::abs(subHz - lastSubHz_) > 1.0e-4) {
            cachedXoverCoefs_ = hba::dsp::butterworth(subHz, sampleRate_);
            lastSubHz_ = subHz;
        }
        double subL = 0.0, highL = 0.0, subR = 0.0, highR = 0.0;
        subSplitL_.process(cachedXoverCoefs_, xL, subL, highL);
        subSplitR_.process(cachedXoverCoefs_, xR, subR, highR);
        if (subMono) {
            const double m = 0.5 * (subL + subR);
            subL = subR = m;
        }
        subL *= subGain;
        subR *= subGain;

        // 2. Exact Unity-Sum Transient Shield (trans + body == high).
        double transL = 0.0, transR = 0.0, bodyL = 0.0, bodyR = 0.0;
        const double transMask =
            transientSplit_.process(highL, highR, shieldAmt, shieldSens, transL, transR, bodyL, bodyR);
        blockMaxTrans = std::max(blockMaxTrans, static_cast<float>(transMask));

        // 3. Spectral STFT vs Zero-Latency Harmonic Comb with click-free crossfading + real FFT telemetry!
        double wetBodyL = 0.0;
        double wetBodyR = 0.0;
        double alignedSubL = subL;
        double alignedSubR = subR;
        double alignedTransL = transL;
        double alignedTransR = transR;
        double alignedDryBodyL = bodyL;
        double alignedDryBodyR = bodyR;

        if (!liveZeroLat) {
            alignedSubL = subDelayL_.process(subL);
            alignedSubR = subDelayR_.process(subR);
            alignedTransL = transDelayL_.process(transL);
            alignedTransR = transDelayR_.process(transR);
            alignedDryBodyL = dryBodyDelayL_.process(bodyL);
            alignedDryBodyR = dryBodyDelayR_.process(bodyR);

            float stftOutL = 0.0f;
            float stftOutR = 0.0f;
            const bool runStft = (combBlend < 0.999);
            const bool runComb = (combBlend > 0.001);

            if (runStft) {
                buf_->stft.processSample(static_cast<float>(bodyL), static_cast<float>(bodyR), static_cast<float>(scL),
                                         static_cast<float>(scR), analyzeSc, stftOutL, stftOutR,
                                         [&](hba::dsp::StereoStft<kFftSize>::FrameBuffers& fb) noexcept {
                                             processStftFrame(fb, p, modDecay, modTame, modTilt, modFan, modMorph,
                                                              !wantCombAudio);
                                         });
            }

            double combOutL = 0.0;
            double combOutR = 0.0;
            if (runComb) {
                double cL = 0.0, cR = 0.0;
                processCombSample(bodyL, bodyR, combParams, cL, cR);
                const double pk = std::max(std::abs(cL), std::abs(cR));
                liveTameEnv_ += 0.002 * (pk - liveTameEnv_);
                cL = liveTameL_.process(liveTameCoefs, cL);
                cR = liveTameR_.process(liveTameCoefs, cR);
                combOutL = combDelayL_.process(cL);
                combOutR = combDelayR_.process(cR);
                if (wantCombAudio) {
                    pushCombTelemetrySample(static_cast<float>(0.5 * (bodyL + bodyR)),
                                            static_cast<float>(0.5 * (cL + cR)), liveTameCutDb, tameFocusHz);
                }
            } else {
                // Keep comb delay line quiet when inactive so switching into Comb is silent.
                (void)combDelayL_.process(0.0);
                (void)combDelayR_.process(0.0);
            }

            wetBodyL = (1.0 - combBlend) * static_cast<double>(stftOutL) + combBlend * combOutL;
            wetBodyR = (1.0 - combBlend) * static_cast<double>(stftOutR) + combBlend * combOutR;
        } else {
            // Exact 0-sample latency Harmonic Comb mode with zero-latency real FFT telemetry!
            double cL = 0.0, cR = 0.0;
            processCombSample(bodyL, bodyR, combParams, cL, cR);
            const double pk = std::max(std::abs(cL), std::abs(cR));
            liveTameEnv_ += 0.002 * (pk - liveTameEnv_);
            wetBodyL = liveTameL_.process(liveTameCoefs, cL);
            wetBodyR = liveTameR_.process(liveTameCoefs, cR);
            pushCombTelemetrySample(static_cast<float>(0.5 * (bodyL + bodyR)),
                                    static_cast<float>(0.5 * (wetBodyL + wetBodyR)), liveTameCutDb, tameFocusHz);
        }

        // 4. Blend colored body + optional unquantized source grit + recombine phase-locked Sub & Transients!
        double midHighL = (1.0 - colorMix) * alignedDryBodyL + colorMix * (wetBodyL + inHarm * 0.45 * alignedDryBodyL);
        double midHighR = (1.0 - colorMix) * alignedDryBodyR + colorMix * (wetBodyR + inHarm * 0.45 * alignedDryBodyR);

        // 5. Post-Color ADAA2 Grit (oversampled on mid/high; click-free bypass through 0%).
        if (gritAmt > 1.0e-4) {
            const double driveGain = 1.0 + gritAmt * 5.2;
            const double invComp = 1.0 / (1.0 + gritAmt * 1.15);
            osL_.up(static_cast<float>(midHighL), osBufL.data());
            osR_.up(static_cast<float>(midHighR), osBufR.data());
            for (int os = 0; os < preparedOsFactor_; ++os) {
                double dryL = 0.0;
                double dryR = 0.0;
                const double sL = shaperL_.process(static_cast<double>(osBufL[static_cast<std::size_t>(os)]), driveGain,
                                                   0.06 * gritAmt, shaperModel, shaperModel, 1.0, dryL);
                const double sR = shaperR_.process(static_cast<double>(osBufR[static_cast<std::size_t>(os)]), driveGain,
                                                   0.06 * gritAmt, shaperModel, shaperModel, 1.0, dryR);
                osBufL[static_cast<std::size_t>(os)] =
                    static_cast<float>(dryL * (1.0 - gritAmt) + sL * invComp * gritAmt);
                osBufR[static_cast<std::size_t>(os)] =
                    static_cast<float>(dryR * (1.0 - gritAmt) + sR * invComp * gritAmt);
            }
            midHighL = static_cast<double>(osL_.down(osBufL.data()));
            midHighR = static_cast<double>(osR_.down(osBufR.data()));
        } else {
            (void)shaperL_.bypass(midHighL);
            (void)shaperR_.bypass(midHighR);
        }

        // 6. Air high-shelf on mid/high + exact phase-aligned recombination with Sub + Transients!
        midHighL = airL_.process(airCoefs, midHighL);
        midHighR = airR_.process(airCoefs, midHighR);

        double yL = dcL_.process((alignedSubL + alignedTransL + midHighL) * outGain);
        double yR = dcR_.process((alignedSubR + alignedTransR + midHighR) * outGain);

        // 7. Zero-extra-latency ADAA1 C^2 safety ceiling limiter with accurate GR metering.
        if (limitOn) {
            const double rawPeak = std::max(std::abs(yL), std::abs(yR));
            yL = clipL_.process(yL, 0.78, ceilingGain);
            yR = clipR_.process(yR, 0.78, ceilingGain);
            const double kneeLevel = ceilingGain * 0.78;
            const double instGr = rawPeak > kneeLevel
                                      ? std::min(1.0, hba::dsp::softClip(rawPeak, kneeLevel, ceilingGain) / rawPeak)
                                      : 1.0;
            grEnv_ = instGr < grEnv_ ? instGr : grEnv_ + grRelCoef * (instGr - grEnv_);
            blockMinGr = std::min(blockMinGr, grEnv_);
        } else {
            yL = hba::dsp::softGuard(yL, 2.5, 4.0);
            yR = hba::dsp::softGuard(yR, 2.5, 4.0);
            grEnv_ += grRelCoef * (1.0 - grEnv_);
        }

        const float fOutL = std::isfinite(yL) ? static_cast<float>(yL) : 0.0f;
        const float fOutR = std::isfinite(yR) ? static_cast<float>(yR) : 0.0f;
        outL[i] = fOutL;
        outR[i] = fOutR;

        blockPeakL = std::max(blockPeakL, std::abs(fOutL));
        blockPeakR = std::max(blockPeakR, std::abs(fOutR));
    }

    telemetry_.peakL = blockPeakL;
    telemetry_.peakR = blockPeakR;
    telemetry_.grDb = static_cast<float>(hba::dsp::gainToDb(std::clamp(blockMinGr, 0.05, 1.0)));
    telemetry_.transientAmt = 0.65f * telemetry_.transientAmt + 0.35f * blockMaxTrans;
    telemetry_.pitchBendSt = static_cast<float>(pitchBendSt);
    telemetry_.sampleRate = static_cast<float>(sampleRate_);
    telemetry_.latencySamples = latencySamples();
    telemetry_.ppq = hostPpq_;
    telemetry_.notePpq = notePpq_;
    telemetry_.freeSec = freeSec_;
    telemetry_.noteSec = noteSec_;
    telemetry_.sidechainActive = hasScBus && scRms_ > 1.0e-4;
}

} // namespace pv
