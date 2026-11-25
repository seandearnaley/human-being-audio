// SPDX-License-Identifier: AGPL-3.0-or-later
#include "tg/engine.h"

#include <hba/dsp/denormals.h>
#include <hba/dsp/gain.h>

#include <algorithm>
#include <cmath>

namespace tg {

namespace {
inline double midiToHz(double midi) noexcept HBA_NONBLOCKING { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); }

inline double polyBlep(double t, double dt) noexcept HBA_NONBLOCKING {
    if (dt <= 0.0)
        return 0.0;
    if (t < dt) {
        const double x = t / dt;
        return x + x - x * x - 1.0;
    }
    if (t > 1.0 - dt) {
        const double x = (t - 1.0) / dt;
        return x * x + x + x + 1.0;
    }
    return 0.0;
}

// Strict zero-boundary Hann^2 log-frequency bell shaped by center & width.
inline double spiralBellWeight(double u, double octSpan, double centerOct, double widthNorm) noexcept HBA_NONBLOCKING {
    const double edgeNorm = std::clamp(u / octSpan, 0.0, 1.0);
    const double boundary = std::sin(hba::dsp::kPi * edgeNorm);
    const double boundarySq = boundary * boundary;
    // Gaussian tilt inside the boundary so users can sweep Bell Center & Bell Width dramatically
    // while keeping strict zero at u = 0 and u = octSpan!
    const double sigma = 0.65 + std::clamp(widthNorm, 0.15, 1.0) * 2.25;
    const double dist = (u - centerOct) / sigma;
    const double tilt = std::exp(-0.5 * dist * dist);
    return boundarySq * tilt;
}

// Focused club Sub-Anchor Floor envelope centered around 52 Hz (30..92 Hz).
inline double subAnchorWeight(double hz) noexcept HBA_NONBLOCKING {
    if (hz < 18.0 || hz > 180.0)
        return 0.0;
    const double logDist = std::log2(hz / 52.0);
    return std::exp(-0.5 * (logDist / 0.44) * (logDist / 0.44));
}

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
    mobiusCycleOffset_ = 0;

    rootMidiSmooth_.reset();
    morphSmooth_.reset();
    filterSmooth_.reset();
    driveSmooth_.reset();
    spaceSmooth_.reset();
    subAnchorSmooth_.reset();
    bellCenterSmooth_.reset();
    outputGainSmooth_.reset();
    for (auto& s : voicePitchSmooth_)
        s.reset();

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

void Engine::noteOn(int note, float velocity, bool allowKeySwitch) noexcept HBA_NONBLOCKING {
    const int n = std::clamp(note, 0, 127);
    if (allowKeySwitch && n >= 12 && n <= 15) {
        // Low-octave C0..D#0 keyswitches glide straight to Scene A, B, C, or D without transposing root!
        keySwitchScene_ = n - 12;
        return;
    }
    const bool wasClosed = !gateOpen_;
    keysDown_[static_cast<std::size_t>(n)] = true;
    currentNote_ = n;
    velocity_ = std::clamp(velocity, 0.05f, 1.0f);
    gateOpen_ = true;
    silentTailSamples_ = 0;
    if (wasClosed)
        retrigPpqOffset_ = lastTransportPpq_;
}

void Engine::noteOff(int note) noexcept HBA_NONBLOCKING {
    const int n = std::clamp(note, 0, 127);
    if (n >= 12 && n <= 15 && !keysDown_[static_cast<std::size_t>(n)])
        return;
    keysDown_[static_cast<std::size_t>(n)] = false;
    if (sustainDown_)
        return;

    // Monophonic last/highest-key fallback so legato playing feels buttery smooth!
    int fallback = -1;
    for (int k = 127; k >= 0; --k) {
        if (keysDown_[static_cast<std::size_t>(k)]) {
            fallback = k;
            break;
        }
    }
    if (fallback >= 0) {
        currentNote_ = fallback;
    } else {
        gateOpen_ = false;
    }
}

void Engine::sustain(bool down) noexcept HBA_NONBLOCKING {
    sustainDown_ = down;
    if (!down) {
        bool any = false;
        for (int k = 127; k >= 0; --k) {
            if (keysDown_[static_cast<std::size_t>(k)]) {
                currentNote_ = k;
                any = true;
                break;
            }
        }
        if (!any)
            gateOpen_ = false;
    }
}

void Engine::allNotesOff() noexcept HBA_NONBLOCKING {
    keysDown_.fill(false);
    sustainDown_ = false;
    gateOpen_ = false;
}

double Engine::oscSample(TimbreKind kind, double phase, double inc, double formantRatio, double foldAmt,
                         double pw) const noexcept HBA_NONBLOCKING {
    switch (kind) {
    case TimbreKind::reese: {
        double saw = 2.0 * phase - 1.0;
        saw -= polyBlep(phase, inc);
        return saw;
    }
    case TimbreKind::sine:
        return std::sin(hba::dsp::kTwoPi * phase);
    case TimbreKind::formant: {
        // Windowed vocal formant harmonic crossfade (zero aliasing below Nyquist!)
        const double window = 0.5 * (1.0 - std::cos(hba::dsp::kTwoPi * phase));
        const double maxH = std::max(1.0, std::min(formantRatio, 0.42 / std::max(inc, 1e-6)));
        const double hLo = std::floor(maxH);
        const double hHi = hLo + 1.0;
        const double fracH = maxH - hLo;
        const double cLo = std::cos(hba::dsp::kTwoPi * hLo * phase);
        const double cHi = std::cos(hba::dsp::kTwoPi * hHi * phase);
        return 1.35 * window * (cLo + fracH * (cHi - cLo));
    }
    case TimbreKind::wavefold: {
        // Phase-modulated self-folding harmonic tone
        const double mod = (0.35 + foldAmt * 1.45) * std::sin(hba::dsp::kTwoPi * 2.0 * phase);
        return std::sin(hba::dsp::kTwoPi * phase + mod);
    }
    case TimbreKind::pulse: {
        double p = phase < pw ? 1.0 : -1.0;
        p += polyBlep(phase, inc);
        p -= polyBlep(hba::dsp::frac(phase + 1.0 - pw), inc);
        return 0.85 * p;
    }
    }
    return 0.0;
}

void Engine::emitMpeConfig(OutMidiBuffer& out, int offset, int rangeSt) noexcept HBA_NONBLOCKING {
    // MPE Lower Zone: Master ch 1 (0), 5 member channels (ch 2..6 -> 1..5).
    out.push(offset, 0xB0, 101, 0);
    out.push(offset, 0xB0, 100, 6);
    out.push(offset, 0xB0, 6, static_cast<std::uint8_t>(kVoiceCount));
    // Set pitch bend sensitivity RPN 00 00 on member channels 2..6
    const auto clampedRange = static_cast<std::uint8_t>(std::clamp(rangeSt, 1, 96));
    for (int v = 0; v < kVoiceCount; ++v) {
        const auto ch = static_cast<std::uint8_t>(v + 1);
        out.push(offset, static_cast<std::uint8_t>(0xB0 | ch), 101, 0);
        out.push(offset, static_cast<std::uint8_t>(0xB0 | ch), 100, 0);
        out.push(offset, static_cast<std::uint8_t>(0xB0 | ch), 6, clampedRange);
        out.push(offset, static_cast<std::uint8_t>(0xB0 | ch), 38, 0);
    }
    mpeConfigured_ = true;
    lastMpeRangeSt_ = rangeSt;
}

void Engine::emitMpeAllNotesOff(OutMidiBuffer& out, int offset) noexcept HBA_NONBLOCKING {
    for (int v = 0; v < kVoiceCount; ++v) {
        auto& mv = mpeVoices_[static_cast<std::size_t>(v)];
        if (mv.active) {
            const auto ch = static_cast<std::uint8_t>(v + 1);
            out.push(offset, static_cast<std::uint8_t>(0x80 | ch), static_cast<std::uint8_t>(mv.baseNote), 0);
            mv.active = false;
        }
    }
}

void Engine::updateMpeVoices(OutMidiBuffer& out, int offset, const std::array<double, kVoiceCount>& pitches,
                             bool sounding, int rangeSt, double filterNorm) noexcept HBA_NONBLOCKING {
    if (!mpeConfigured_ || lastMpeRangeSt_ != rangeSt)
        emitMpeConfig(out, offset, rangeSt);

    if (!sounding) {
        emitMpeAllNotesOff(out, offset);
        return;
    }

    const int vel7 = std::clamp(static_cast<int>(std::lround(velocity_ * 127.0f)), 1, 127);
    const int timbre7 = std::clamp(static_cast<int>(std::lround(filterNorm * 127.0)), 0, 127);

    for (int v = 0; v < kVoiceCount; ++v) {
        auto& mv = mpeVoices_[static_cast<std::size_t>(v)];
        const auto ch = static_cast<std::uint8_t>(v + 1);
        const double targetPitch = std::clamp(pitches[static_cast<std::size_t>(v)], 12.0, 116.0);

        if (!mv.active) {
            mv.baseNote = std::clamp(static_cast<int>(std::lround(targetPitch)), 12, 116);
            mv.lastBend14 = 8192;
            mv.lastTimbre7 = timbre7;
            // Center pitch bend before Note-On (MPE specification best practice!)
            out.push(offset, static_cast<std::uint8_t>(0xE0 | ch), 0, 64);
            out.push(offset, static_cast<std::uint8_t>(0xB0 | ch), 74, static_cast<std::uint8_t>(timbre7));
            out.push(offset, static_cast<std::uint8_t>(0x90 | ch), static_cast<std::uint8_t>(mv.baseNote),
                     static_cast<std::uint8_t>(vel7));
            mv.active = true;
        }

        const double bendSt = std::clamp(targetPitch - static_cast<double>(mv.baseNote), -static_cast<double>(rangeSt),
                                         static_cast<double>(rangeSt));
        const double bendNorm = bendSt / std::max(1.0, static_cast<double>(rangeSt));
        const int bend14 = std::clamp(static_cast<int>(std::lround(8192.0 + bendNorm * 8191.0)), 0, 16383);

        if (std::abs(bend14 - mv.lastBend14) >= 4) {
            out.push(offset, static_cast<std::uint8_t>(0xE0 | ch), static_cast<std::uint8_t>(bend14 & 0x7F),
                     static_cast<std::uint8_t>((bend14 >> 7) & 0x7F));
            mv.lastBend14 = bend14;
        }
        if (std::abs(timbre7 - mv.lastTimbre7) >= 2) {
            out.push(offset, static_cast<std::uint8_t>(0xB0 | ch), 74, static_cast<std::uint8_t>(timbre7));
            mv.lastTimbre7 = timbre7;
        }
    }
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
    } else {
        if (wasPlaying_) {
            wasPlaying_ = false;
        }
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
    const double bellWidth = std::clamp(static_cast<double>(params.real(ParamId::bellWidth)) * 0.01, 0.25, 1.0);
    const double phaseOffset = std::clamp(static_cast<double>(params.real(ParamId::phaseOffset)) * 0.01, 0.0, 1.0);

    const bool morphAuto = params.flag(ParamId::morphAuto);
    const double baseMorph = std::clamp(static_cast<double>(params.real(ParamId::morph)) * 0.01, 0.0, 1.0);
    const double baseGravity = std::clamp(static_cast<double>(params.real(ParamId::gravity)) * 0.01, 0.0, 1.0);
    const auto gravMode = static_cast<GravityMode>(std::clamp(params.choice(ParamId::gravityMode), 0, 2));
    const double stagger = std::clamp(static_cast<double>(params.real(ParamId::stagger)) * 0.01, 0.0, 1.0);
    const auto staggerOrder = static_cast<StaggerOrder>(std::clamp(params.choice(ParamId::staggerOrder), 0, 3));
    const bool anchorSub = params.flag(ParamId::anchorSub);

    const auto timbre = static_cast<TimbreKind>(std::clamp(params.choice(ParamId::timbre), 0, 4));
    const double detuneRatio = std::pow(2.0, (static_cast<double>(params.real(ParamId::detune)) * 0.5) / 1200.0);
    const double width = std::clamp(static_cast<double>(params.real(ParamId::width)) * 0.01, 0.0, 1.0);
    const double targetSpaceBase = std::clamp(static_cast<double>(params.real(ParamId::space)) * 0.01, 0.0, 1.0);
    const double subWarmth = std::clamp(static_cast<double>(params.real(ParamId::subWarmth)) * 0.01, 0.0, 1.0);
    const double subCutoffHz = std::clamp(static_cast<double>(params.real(ParamId::subCutoff)), 45.0, 120.0);
    const auto shaperModel = shaperFromChoice(params.choice(ParamId::driveModel));
    const auto filterMode = static_cast<FilterMode>(std::clamp(params.choice(ParamId::filterMode), 0, 3));
    const double filterRes = std::clamp(static_cast<double>(params.real(ParamId::filterRes)) * 0.01, 0.0, 0.92);
    const double filterQ = 0.707 + filterRes * filterRes * 7.5;
    const double baseRissetGate = std::clamp(static_cast<double>(params.real(ParamId::rissetGate)) * 0.01, 0.0, 1.0);

    const double glideTau = std::max(0.001, static_cast<double>(params.real(ParamId::glideMs)) * 0.001);
    const double glideCoef = hba::dsp::onePoleCoef(glideTau, invSr);
    const double smoothFast = hba::dsp::onePoleCoef(0.008, invSr);
    const double voicePitchCoef = hba::dsp::onePoleCoef(0.0035, invSr);
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
    const double targetSubAnchor = std::clamp(static_cast<double>(params.real(ParamId::subAnchor)) * 0.01, 0.0, 1.0);
    const double targetBellCenter = std::clamp(static_cast<double>(params.real(ParamId::bellCenter)), 80.0, 2400.0);
    const double targetFilterBase = std::clamp(static_cast<double>(params.real(ParamId::filterHz)), 120.0, 16000.0);
    const double targetDriveBase = std::clamp(static_cast<double>(params.real(ParamId::drive)) * 0.01, 0.0, 1.0);

    const hba::dsp::SvfCoefs subSplitCoefs = hba::dsp::butterworth(subCutoffHz, sr);
    const hba::dsp::SvfCoefs subWarmthCoefs = hba::dsp::butterworth(std::min(280.0, subCutoffHz * 2.6), sr);

    // Sub-block control cadence (32 samples ≈ 0.67 ms at 48 kHz) keeps CPU low while remaining ultra-smooth.
    constexpr int kControlInterval = 32;
    const int mpeThrottleSamples = std::max(64, static_cast<int>(sr * 0.0025));

    // Control variables cached per control sub-block
    double curSpiralPos = 0.0;
    double curBarPhase = 0.0;
    double curMorph = baseMorph;
    double curGravity = baseGravity;
    double curDrive = targetDriveBase;
    double curSpace = targetSpaceBase;
    double curFilterHz = targetFilterBase;
    double curBellCenter = targetBellCenter;
    double curRissetGate = baseRissetGate;
    double curLane1Val = 0.0;
    double curLane2Val = 0.0;
    double curLane1Phase = 0.0;
    double curLane2Phase = 0.0;
    int curActiveScene = 0;
    std::array<double, kVoiceCount> targetVoicePitches{};
    hba::dsp::SvfCoefs filterCoefs = hba::dsp::SvfCoefs::make(curFilterHz, filterQ, sr);
    hba::dsp::SvfCoefs filter2Coefs =
        hba::dsp::SvfCoefs::make(std::min(16000.0, curFilterHz * 1.85), filterQ * 0.85, sr);

    float blockPeakL = 0.0f;
    float blockPeakR = 0.0f;
    float blockSubPeak = 0.0f;
    double blockRmsSum = 0.0;
    double lastGateEnv = 1.0;
    double lastEffectiveBellHz = targetBellCenter;

    // Precompute stereo pan gains for 5 voices
    constexpr std::array<double, kVoiceCount> kBasePans{0.0, -0.78, 0.78, -0.42, 0.42};

    for (int i = 0; i < numSamples; ++i) {
        const double ppqNow = transport.playing ? (transport.ppq + static_cast<double>(i) * beatsPerSample) : freePpq_;
        if (!transport.playing)
            freePpq_ += beatsPerSample;
        lastTransportPpq_ = ppqNow;

        // Envelope
        const double envTarget = gateOpen_ ? static_cast<double>(velocity_) : 0.0;
        envLevel_ += (gateOpen_ ? attackCoef : releaseCoef) * (envTarget - envLevel_);

        // Root MIDI note with smooth portamento + live pitch bend
        const double targetRootMidi = static_cast<double>(currentNote_) + static_cast<double>(pitchBendSt_);
        const double rootMidi = rootMidiSmooth_.step(targetRootMidi, glideCoef);
        const double subAnchor = subAnchorSmooth_.step(targetSubAnchor, smoothFast);
        const double outGain = outputGainSmooth_.step(targetOutGain, smoothFast);

        // Update modulation lanes, pitch manifold, and filter coefficients every 32 samples (and sample 0)
        if ((i & (kControlInterval - 1)) == 0) {
            const double relPpq = retrig ? std::max(0.0, ppqNow - retrigPpqOffset_) : ppqNow;
            const double cycleCount = (relPpq / loopBeats) + phaseOffset;
            curBarPhase = hba::dsp::frac(cycleCount);

            curLane1Phase = hba::dsp::frac(relPpq / lane1Beats);
            curLane2Phase = hba::dsp::frac(relPpq / lane2Beats);
            curLane1Val = static_cast<double>(hba::dsp::evaluate(curves_[0], curLane1Phase));
            curLane2Val = static_cast<double>(hba::dsp::evaluate(curves_[1], curLane2Phase));

            // Apply lane modulation routing
            double modMorph = 0.0;
            double modBell = 0.0;
            double modFilter = 0.0;
            double modDrive = 0.0;
            double modGrav = 0.0;
            double modGate = 0.0;
            double modSpace = 0.0;

            const auto applyLane = [&](LaneTarget tgt, double val, double depth) noexcept HBA_NONBLOCKING {
                const double uni = 0.5 * (val + 1.0); // 0..1 for unipolar Morph sweeps
                const double bi = val;                // -1..1 for bipolar offsets
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

            // Spiral unwrapped continuous cycle position (ONLY 1 silent boundary partial wraps at a time!)
            if (spiralDir == SpiralDir::rise)
                curSpiralPos = cycleCount;
            else if (spiralDir == SpiralDir::fall)
                curSpiralPos = -cycleCount;
            else
                curSpiralPos = 0.5 - 0.5 * std::cos(hba::dsp::kTwoPi * curBarPhase);

            // Morph position (0..1) — honors low-octave KeySwitch C0..D#0 when active!
            const double rawMorph = baseMorph + static_cast<double>(modWheel_) + modMorph;
            if (useKeySwitch && keySwitchScene_ >= 0 && mode != Mode::spiral) {
                const double targetKS = static_cast<double>(keySwitchScene_) /
                                        static_cast<double>(mode == Mode::mobius ? kSceneCount : (kSceneCount - 1));
                curMorph = morphSmooth_.step(targetKS, hba::dsp::onePoleCoef(0.055, invSr * kControlInterval));
            } else if (mode == Mode::mobius) {
                const double mobCycle =
                    morphAuto ? (cycleCount + baseMorph + static_cast<double>(modWheel_)) : rawMorph;
                const double dirMob = (spiralDir == SpiralDir::fall) ? -mobCycle : mobCycle;
                curMorph = hba::dsp::frac(dirMob);
                morphSmooth_.snap(curMorph);
                mobiusCycleOffset_ = static_cast<int>(std::floor(dirMob));
                prevMorphCycle_ = dirMob;
            } else {
                curMorph = morphSmooth_.step(std::clamp(rawMorph, 0.0, 1.0),
                                             hba::dsp::onePoleCoef(0.010, invSr * kControlInterval));
            }

            curGravity = std::clamp(baseGravity + modGrav * 0.5, 0.0, 1.0);
            curDrive = driveSmooth_.step(
                std::clamp(targetDriveBase + modDrive * 0.5 + static_cast<double>(aftertouch_) * 0.25, 0.0, 1.0),
                hba::dsp::onePoleCoef(0.012, invSr * kControlInterval));
            curSpace = spaceSmooth_.step(std::clamp(targetSpaceBase + modSpace * 0.5, 0.0, 1.0),
                                         hba::dsp::onePoleCoef(0.015, invSr * kControlInterval));
            curBellCenter =
                bellCenterSmooth_.step(std::clamp(targetBellCenter * std::pow(2.0, modBell * 2.0), 80.0, 3200.0),
                                       hba::dsp::onePoleCoef(0.012, invSr * kControlInterval));
            const double filterModOct = modFilter * 3.0 + (static_cast<double>(timbreCc_) - 0.5) * 2.0;
            curFilterHz = filterSmooth_.step(std::clamp(targetFilterBase * std::pow(2.0, filterModOct), 100.0, 18000.0),
                                             hba::dsp::onePoleCoef(0.010, invSr * kControlInterval));
            curRissetGate = std::clamp(baseRissetGate + modGate * 0.65, 0.0, 1.0);

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
                    evaluateMorphPitches(matrix_, rootMidi, rootPc, scaleIdx, curMorph, curGravity, gravMode, stagger,
                                         staggerOrder, anchorSub, mode == Mode::mobius);
                curActiveScene = eval.activeScene;
                targetVoicePitches = eval.pitches;
            }

            // Update MPE Lower Zone MIDI output cleanly at throttled intervals
            if (mpeOut != nullptr && mpeEnabled) {
                mpeThrottleCounter_ -= kControlInterval;
                if (mpeThrottleCounter_ <= 0 || !gateOpen_) {
                    updateMpeVoices(*mpeOut, i, targetVoicePitches, envLevel_ > 1e-4 && gateOpen_, mpeRangeSt,
                                    std::clamp(std::log2(curFilterHz / 120.0) / std::log2(16000.0 / 120.0), 0.0, 1.0));
                    mpeThrottleCounter_ = mpeThrottleSamples;
                }
            } else if (mpeOut != nullptr && !mpeEnabled) {
                emitMpeAllNotesOff(*mpeOut, i);
            }
        }

        // Drain chorus + oversampler + limiter lookahead buffers before going completely silent
        const int tailDrainLimit = latencySamples() + static_cast<int>(0.035 * sr);
        if (envLevel_ < 1e-6 && !gateOpen_) {
            if (silentTailSamples_ >= tailDrainLimit) {
                outL[i] = 0.0f;
                outR[i] = 0.0f;
                continue;
            }
            ++silentTailSamples_;
        } else {
            silentTailSamples_ = 0;
        }

        const double formantRatio = std::clamp(curFilterHz / 220.0, 1.5, 14.0);
        const double foldAmt = 0.2 + curDrive * 0.8;
        const double pw = 0.5 + 0.28 * std::sin(hba::dsp::kTwoPi * curLane2Phase);

        double sumBodyL = 0.0;
        double sumBodyR = 0.0;
        double sumSub = 0.0;

        if (mode == Mode::spiral) {
            // 7-Octave (or custom-ratio) Phase-Coherent Shepard-Risset Spiral!
            // Anchor F0..C1 floor to played pitch class, AND let played MIDI octave shift Bell Center naturally!
            const double playedPc = std::fmod(rootMidi, 12.0);
            const double normPc = playedPc < 0.0 ? playedPc + 12.0 : playedPc;
            const double baseOctaveMidi = 12.0 + normPc; // ~16..31 Hz floor
            const double octaveShift = std::clamp((rootMidi - (36.0 + normPc)) / 12.0, -2.0, 2.5);
            const double effBellHz = std::clamp(curBellCenter * std::pow(2.0, octaveShift * 0.75), 65.0, 4800.0);
            lastEffectiveBellHz = effBellHz;
            const double bellCenterU = std::clamp(
                (12.0 * std::log2(effBellHz / std::max(12.0, midiToHz(baseOctaveMidi)))) / ratioSemi, 0.75, 6.25);

            // Constant-RMS bell power normalization eliminates subtle loudness breathing across octave wraps!
            std::array<double, kOctaveCount> bellW{};
            std::array<double, kOctaveCount> partialU{};
            double bellPow = 0.0;
            for (int k = 0; k < kOctaveCount; ++k) {
                const auto ki = static_cast<std::size_t>(k);
                const double u =
                    hba::dsp::frac((static_cast<double>(k) + curSpiralPos) / static_cast<double>(kOctaveCount)) *
                    static_cast<double>(kOctaveCount);
                partialU[ki] = u;
                const double b = spiralBellWeight(u, static_cast<double>(kOctaveCount), bellCenterU, bellWidth);
                bellW[ki] = b;
                bellPow += b * b;
            }
            const double rmsNorm = 1.0 / std::sqrt(std::max(0.35, bellPow));

            const int chordVoices = spiralChord.count;
            const double chordNorm = 1.0 / std::sqrt(static_cast<double>(chordVoices));

            for (int v = 0; v < chordVoices; ++v) {
                const auto vi = static_cast<std::size_t>(v);
                const double chordSemi = spiralChord.intervals[vi];
                const double pan = (chordVoices == 1) ? 0.0 : (kBasePans[vi] * width);
                const double panL = std::cos((pan + 1.0) * 0.25 * hba::dsp::kPi);
                const double panR = std::sin((pan + 1.0) * 0.25 * hba::dsp::kPi);

                for (int k = 0; k < kOctaveCount; ++k) {
                    const auto ki = static_cast<std::size_t>(k);
                    const double u = partialU[ki];
                    const double midi = baseOctaveMidi + u * ratioSemi + chordSemi;
                    const double hz = std::clamp(midiToHz(midi), 16.0, 0.44 * sr);
                    const double bell = bellW[ki] * rmsNorm;

                    if (v == 0 && i == numSamples - 1) {
                        telemetry_.spiralPartialU[ki] = static_cast<float>(u);
                        telemetry_.spiralPartialGain[ki] = static_cast<float>(bellW[ki]);
                    }

                    auto& ps = partials_[vi][ki];
                    const double incA = (hz / detuneRatio) * invSr;
                    const double incB = (hz * detuneRatio) * invSr;
                    ps.phaseA = hba::dsp::frac(ps.phaseA + incA);
                    ps.phaseB = hba::dsp::frac(ps.phaseB + incB);

                    if (bell > 1e-5) {
                        const double sA = oscSample(timbre, ps.phaseA, incA, formantRatio, foldAmt, pw);
                        const double sB = oscSample(timbre, ps.phaseB, incB, formantRatio, foldAmt, pw);
                        const double w = bell * 0.34 * chordNorm;
                        const double mid = 0.5 * (sA + sB);
                        const double side = 0.5 * (sA - sB) * width;
                        sumBodyL += w * (mid * panL + side);
                        sumBodyR += w * (mid * panR - side);
                    }

                    // Dedicated pure mono Sub-Anchor fundamental on chord root (v == 0)
                    if (v == 0) {
                        const double subW = subAnchorWeight(hz);
                        ps.phaseSub = hba::dsp::frac(ps.phaseSub + hz * invSr);
                        if (subW > 1e-5)
                            sumSub += subW * std::sin(hba::dsp::kTwoPi * ps.phaseSub) * 0.62;
                    }
                }
            }
        } else if (mode == Mode::copedent) {
            // 5-Voice Programmable Polyphonic Chord-Bend Instrument!
            constexpr double voiceNorm = 0.30;
            for (int v = 0; v < kVoiceCount; ++v) {
                const auto vi = static_cast<std::size_t>(v);
                const double smoothedPitch = voicePitchSmooth_[vi].step(targetVoicePitches[vi], voicePitchCoef);
                const double hz = std::clamp(midiToHz(smoothedPitch), 20.0, 0.44 * sr);

                auto& ps = partials_[vi][0];
                const double incA = (hz / detuneRatio) * invSr;
                const double incB = (hz * detuneRatio) * invSr;
                ps.phaseA = hba::dsp::frac(ps.phaseA + incA);
                ps.phaseB = hba::dsp::frac(ps.phaseB + incB);

                const double sA = oscSample(timbre, ps.phaseA, incA, formantRatio, foldAmt, pw);
                const double sB = oscSample(timbre, ps.phaseB, incB, formantRatio, foldAmt, pw);
                const double pan = kBasePans[vi] * width;
                const double panL = std::cos((pan + 1.0) * 0.25 * hba::dsp::kPi);
                const double panR = std::sin((pan + 1.0) * 0.25 * hba::dsp::kPi);
                const double mid = 0.5 * (sA + sB);
                const double side = 0.5 * (sA - sB) * width * 0.65;
                const double gainV = (v == 0 ? 1.15 : 0.92) * voiceNorm;

                sumBodyL += gainV * (mid * panL + side);
                sumBodyR += gainV * (mid * panR - side);

                // Mono Sub-Anchor tracks Voice 0 (fundamental bass root)
                if (v == 0) {
                    ps.phaseSub = hba::dsp::frac(ps.phaseSub + hz * invSr);
                    sumSub += std::sin(hba::dsp::kTwoPi * ps.phaseSub) * 0.55;
                }
            }
        } else {
            // MÖBIUS HYBRID: 5-Voice Polyphonic Chord Voice-Leading on an Infinite 4-Octave Pitch Torus!
            constexpr int kMobiusCopies = 4;
            constexpr double kMobiusSpanSemi = 48.0; // 4 octaves
            const double floorMidi = std::clamp(rootMidi - 12.0, 20.0, 48.0);
            const double bellCenterU =
                std::clamp((12.0 * std::log2(curBellCenter / std::max(16.0, midiToHz(floorMidi)))) / 12.0, 0.7, 3.3);
            constexpr double voiceNorm = 0.20;

            for (int v = 0; v < kVoiceCount; ++v) {
                const auto vi = static_cast<std::size_t>(v);
                const double unwrapped = targetVoicePitches[vi] + static_cast<double>(mobiusCycleOffset_) * 12.0;
                const double smoothedPitch = voicePitchSmooth_[vi].step(unwrapped, voicePitchCoef);
                const double pan = kBasePans[vi] * width;
                const double panL = std::cos((pan + 1.0) * 0.25 * hba::dsp::kPi);
                const double panR = std::sin((pan + 1.0) * 0.25 * hba::dsp::kPi);

                for (int c = 0; c < kMobiusCopies; ++c) {
                    const auto ci = static_cast<std::size_t>(c);
                    const double relSemi = smoothedPitch - floorMidi + static_cast<double>(c) * 12.0;
                    const double wrappedSemi = hba::dsp::frac(relSemi / kMobiusSpanSemi) * kMobiusSpanSemi;
                    const double uOct = wrappedSemi / 12.0;
                    const double midi = floorMidi + wrappedSemi;
                    const double hz = std::clamp(midiToHz(midi), 20.0, 0.44 * sr);
                    const double bell =
                        spiralBellWeight(uOct, static_cast<double>(kMobiusCopies), bellCenterU, bellWidth);

                    if (v == 0 && i == numSamples - 1) {
                        telemetry_.spiralPartialU[ci] = static_cast<float>(uOct * (7.0 / 4.0));
                        telemetry_.spiralPartialGain[ci] = static_cast<float>(bell);
                    }

                    auto& ps = partials_[vi][ci];
                    const double incA = (hz / detuneRatio) * invSr;
                    const double incB = (hz * detuneRatio) * invSr;
                    ps.phaseA = hba::dsp::frac(ps.phaseA + incA);
                    ps.phaseB = hba::dsp::frac(ps.phaseB + incB);

                    if (bell > 1e-5) {
                        const double sA = oscSample(timbre, ps.phaseA, incA, formantRatio, foldAmt, pw);
                        const double sB = oscSample(timbre, ps.phaseB, incB, formantRatio, foldAmt, pw);
                        const double w = bell * voiceNorm;
                        const double mid = 0.5 * (sA + sB);
                        const double side = 0.5 * (sA - sB) * width * 0.55;
                        sumBodyL += w * (mid * panL + side);
                        sumBodyR += w * (mid * panR - side);
                    }
                    if (v == 0) {
                        const double subW = subAnchorWeight(hz);
                        ps.phaseSub = hba::dsp::frac(ps.phaseSub + hz * invSr);
                        if (subW > 1e-5)
                            sumSub += subW * std::sin(hba::dsp::kTwoPi * ps.phaseSub) * 0.48;
                    }
                }
            }
        }

        // Split stereo body (> subCutoffHz) from mono Sub-Anchor (< subCutoffHz) cleanly via LR4!
        double bodyL = bodyHpL_.process(subSplitCoefs, sumBodyL);
        double bodyR = bodyHpR_.process(subSplitCoefs, sumBodyR);
        double subMono = subLp_.process(subSplitCoefs, sumSub) * subAnchor;
        if (subWarmth > 0.005) {
            // Subtle 2nd/3rd harmonic sub warmth so bass punches through headphones & laptops while staying 100% mono!
            const double warmed =
                subMono + subWarmth * 0.28 * (std::tanh(2.4 * subMono) + 0.30 * (subMono * subMono - 0.12));
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
            // Twin-notch Neurofunk phaser scoop: deep primary notch + upper harmonic formant resonance!
            const double bpL1 = filterL_.bandpass(filterCoefs, bodyL);
            const double bpR1 = filterR_.bandpass(filterCoefs, bodyR);
            const double bpL2 = filter2L_.bandpass(filter2Coefs, bodyL);
            const double bpR2 = filter2R_.bandpass(filter2Coefs, bodyR);
            bodyL = bodyL - 1.38 * bpL1 + 0.34 * bpL2;
            bodyR = bodyR - 1.38 * bpR1 + 0.34 * bpR2;
            break;
        }
        }

        // Upper-Band Zero-Allocation Stereo Ensemble / Dimension (strictly > subCutoffHz — Sub-Anchor stays 100% mono!)
        {
            spaceBufL_[static_cast<std::size_t>(spaceWritePos_)] = bodyL;
            spaceBufR_[static_cast<std::size_t>(spaceWritePos_)] = bodyR;
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
                const double wetL = 0.42 * readDelay(spaceBufL_, (7.5 + 3.2 * s1) * ms) +
                                    0.38 * readDelay(spaceBufR_, (14.2 + 4.1 * s2) * ms) +
                                    0.26 * readDelay(spaceBufL_, (21.5 + 2.8 * s3) * ms);
                const double wetR = 0.42 * readDelay(spaceBufR_, (8.1 - 3.2 * s1) * ms) +
                                    0.38 * readDelay(spaceBufL_, (15.4 - 4.1 * s2) * ms) +
                                    0.26 * readDelay(spaceBufR_, (22.8 - 2.8 * s3) * ms);
                const double mix = curSpace * 0.58;
                bodyL = bodyL * (1.0 - mix * 0.35) + wetL * mix;
                bodyR = bodyR * (1.0 - mix * 0.35) + wetR * mix;
            }
            spaceWritePos_ = (spaceWritePos_ + 1) & kSpaceBufMask;
        }

        // Optional Risset Eternal Accelerando rhythmic pulse gate coupled to bar spiral!
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

        // Recombine gated stereo upper body + rock-solid mono Sub-Anchor Floor!
        double finalL = (bodyL * gateGain + subMono * (0.75 + 0.25 * gateGain)) * envLevel_ * outGain;
        double finalR = (bodyR * gateGain + subMono * (0.75 + 0.25 * gateGain)) * envLevel_ * outGain;

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
        blockSubPeak = std::max(blockSubPeak, static_cast<float>(std::abs(subMono * envLevel_ * outGain)));
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
