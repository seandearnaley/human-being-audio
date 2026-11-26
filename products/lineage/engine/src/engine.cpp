// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ln/engine.h"

#include <hba/core/note_name.h>
#include <hba/dsp/adaa.h>
#include <hba/dsp/allpass.h>
#include <hba/dsp/control.h>
#include <hba/dsp/crossover.h>
#include <hba/dsp/denormals.h>
#include <hba/dsp/gain.h>
#include <hba/dsp/limiter.h>
#include <hba/dsp/oscillator.h>
#include <hba/dsp/oversampling.h>
#include <hba/dsp/pitch.h>
#include <hba/dsp/svf.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace ln {

void PrintBuffer::computePeaks() {
    peaks.fill(0.0f);
    if (!valid())
        return;
    for (int i = 0; i < kWaveformPoints; ++i) {
        const int a = (i * length) / kWaveformPoints;
        const int b = std::max(a + 1, ((i + 1) * length) / kWaveformPoints);
        float pk = 0.0f;
        for (int s = a; s < b && s < length; ++s) {
            const auto si = static_cast<std::size_t>(s);
            pk = std::max(pk, std::max(std::abs(left[si]), std::abs(right[si])));
        }
        peaks[static_cast<std::size_t>(i)] = std::clamp(pk, 0.0f, 1.0f);
    }
}

namespace {

constexpr int kNumVoices = 7;
constexpr int kMaxHeldNotes = 16;
constexpr int kCombMaxDelay = 4096;
constexpr int kDisperseStages = 6;
constexpr int kPhaserStages = 4;
constexpr double kControlSec = 0.005;
constexpr double kPrintXfadeSec = 0.015;
constexpr double kPi = hba::dsp::kPi;
constexpr double kTwoPi = hba::dsp::kTwoPi;

constexpr std::array<double, kNumVoices> kDetuneOffsets{-1.0, -0.618, -0.275, 0.0, 0.275, 0.618, 1.0};
constexpr std::array<double, kNumVoices> kVoicePan{-0.92, 0.76, -0.48, 0.0, 0.48, -0.76, 0.92};
constexpr std::array<double, kNumVoices> kInitPhases{0.0, 0.371, 0.719, 0.143, 0.583, 0.867, 0.241};

// 5 vowel formant tables (a, ae, i, o, u) for Filter 1 Formant mode
struct FormantVowel {
    std::array<double, 3> hz;
    std::array<double, 3> q;
    std::array<double, 3> gain;
};
constexpr std::array<FormantVowel, 5> kVowels{{
    {{730.0, 1090.0, 2440.0}, {6.0, 8.0, 10.0}, {1.0, 0.68, 0.32}}, // a
    {{660.0, 1720.0, 2410.0}, {6.0, 8.5, 10.0}, {1.0, 0.74, 0.36}}, // ae
    {{270.0, 2290.0, 3010.0}, {6.5, 9.5, 11.0}, {0.95, 0.72, 0.38}},// i
    {{570.0, 840.0, 2410.0}, {6.0, 8.0, 10.0}, {1.0, 0.72, 0.28}},  // o
    {{300.0, 870.0, 2240.0}, {6.5, 8.0, 10.0}, {1.0, 0.62, 0.24}},  // u
}};

// Smooth 4-point Catmull-Rom / Hermite interpolation for click-free pitch-tracked Gen-2 resampling
inline float hermiteRead(const float* buf, int length, double pos) noexcept LN_NONBLOCKING {
    if (length <= 4)
        return 0.0f;
    const double wrapped = std::clamp(pos, 0.0, static_cast<double>(length - 1));
    const int i1 = std::clamp(static_cast<int>(wrapped), 0, length - 1);
    const double frac = wrapped - static_cast<double>(i1);
    const int i0 = std::max(0, i1 - 1);
    const int i2 = std::min(length - 1, i1 + 1);
    const int i3 = std::min(length - 1, i1 + 2);
    const double y0 = static_cast<double>(buf[i0]);
    const double y1 = static_cast<double>(buf[i1]);
    const double y2 = static_cast<double>(buf[i2]);
    const double y3 = static_cast<double>(buf[i3]);
    const double c0 = y1;
    const double c1 = 0.5 * (y2 - y0);
    const double c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
    const double c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
    return static_cast<float>(((c3 * frac + c2) * frac + c1) * frac + c0);
}

inline hba::dsp::ShaperModel toShaperModel(DriveType dt) noexcept LN_NONBLOCKING {
    switch (dt) {
    case DriveType::tube:
        return hba::dsp::ShaperModel::tube;
    case DriveType::fold:
        return hba::dsp::ShaperModel::fold;
    case DriveType::buchla:
        return hba::dsp::ShaperModel::buchla;
    case DriveType::sine:
        return hba::dsp::ShaperModel::sine;
    case DriveType::clip:
        return hba::dsp::ShaperModel::clip;
    }
    return hba::dsp::ShaperModel::tube;
}

struct CombUnit {
    std::array<double, kCombMaxDelay> delay{};
    int writePos = 0;
    double lpState = 0.0;

    void reset() noexcept LN_NONBLOCKING {
        delay.fill(0.0);
        writePos = 0;
        lpState = 0.0;
    }

    double process(double x, double hz, double res01, double sr) noexcept LN_NONBLOCKING {
        const double safeHz = std::clamp(hz, 32.0, std::min(8000.0, 0.35 * sr));
        const double dSamp = std::clamp(sr / safeHz, 2.0, static_cast<double>(kCombMaxDelay - 4));
        double readPos = static_cast<double>(writePos) - dSamp;
        if (readPos < 0.0)
            readPos += static_cast<double>(kCombMaxDelay);
        const int i0 = static_cast<int>(readPos) & (kCombMaxDelay - 1);
        const int i1 = (i0 + 1) & (kCombMaxDelay - 1);
        const double f = readPos - std::floor(readPos);
        const double delayed = delay[static_cast<std::size_t>(i0)] * (1.0 - f) + delay[static_cast<std::size_t>(i1)] * f;
        // Gentle high-frequency damping inside comb loop for organic metallic singing
        lpState += 0.35 * (delayed - lpState);
        const double fb = std::clamp(0.15 + 0.78 * res01, 0.0, 0.93);
        const double toWrite = hba::dsp::softGuard(x + fb * lpState, 1.8, 3.0);
        delay[static_cast<std::size_t>(writePos)] = toWrite;
        writePos = (writePos + 1) & (kCombMaxDelay - 1);
        return (x + 0.85 * delayed) * (1.0 / (1.0 + 0.75 * fb));
    }
};

struct PhaserUnit {
    std::array<hba::dsp::AllpassPair, kPhaserStages> stages{};
    double fbState = 0.0;

    void reset() noexcept LN_NONBLOCKING {
        for (auto& st : stages)
            st.reset();
        fbState = 0.0;
    }

    void setFreq(double hz, double sr) noexcept LN_NONBLOCKING {
        constexpr std::array<double, kPhaserStages> kMult{0.42, 0.85, 1.65, 3.10};
        for (std::size_t i = 0; i < kPhaserStages; ++i)
            stages[i].setFreq(std::clamp(hz * kMult[i], 40.0, 0.44 * sr), sr);
    }

    double process(double x, double res01) noexcept LN_NONBLOCKING {
        const double fb = std::clamp(res01 * 0.74, 0.0, 0.76);
        double y = hba::dsp::softGuard(x - fb * fbState, 2.0, 3.5);
        for (auto& st : stages)
            y = st.process(y);
        fbState = y;
        return 0.55 * x + 0.55 * y;
    }
};

struct PrintSlot {
    std::array<float, PrintBuffer::kMaxPrintSamples> left{};
    std::array<float, PrintBuffer::kMaxPrintSamples> right{};
    int length = 0;
    double sampleRate = 48000.0;
    int rootMidiNote = 29;
    int generation = 0;
};

} // namespace

struct Engine::Impl {
    double sampleRate = 48000.0;
    int osFactor = 4;
    double osRate = 192000.0;
    double controlCoef = 0.01;
    double xfadeStep = 0.001;

    // Triple-buffered pre-allocated print storage for lock-free zero-allocation Gen-2 swaps
    std::array<PrintSlot, 3> printSlots{};
    std::atomic<int> activeSlot{-1};
    std::atomic<int> readingSlot{-1};
    std::atomic<int> pendingSlot{-1};
    int prevSlot = -1;
    double printXfade = 1.0;

    // Oversamplers (stereo mid-top Gen-1 path)
    std::array<hba::dsp::Oversampler, 2> osDown{};

    // Gen-0 voice oscillator phases & Soft-Sync directions
    double masterPhase = 0.0;
    std::array<double, kNumVoices> voicePhase = kInitPhases;
    std::array<double, kNumVoices> slavePhase = kInitPhases;
    std::array<double, kNumVoices> slaveDir{1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
    std::array<double, kNumVoices> fmModPhase = kInitPhases;
    double subWarmthLpState = 0.0;
    hba::dsp::PhaseRotator subRotator{};
    std::array<hba::dsp::Lr4Highpass, 2> preSculptHp{};
    hba::dsp::Lr4Highpass postSideHp{};

    // Gen-1 Filters, Shaper, Dispersion & 3-band Density (stereo)
    std::array<hba::dsp::SvfState, 2> f1A{}, f1B{};
    std::array<std::array<hba::dsp::SvfState, 3>, 2> f1Formant{};
    std::array<CombUnit, 2> f1Comb{};
    std::array<hba::dsp::MultiShaperAdaa2, 2> shaper{};
    DriveType activeDriveType = DriveType::tube;
    DriveType prevDriveType = DriveType::tube;
    double shaperXfade = 1.0;

    std::array<hba::dsp::SvfState, 2> f2A{}, f2B{};
    std::array<PhaserUnit, 2> f2Phaser{};
    std::array<std::array<hba::dsp::AllpassPair, kDisperseStages>, 2> disperseAp{};
    std::array<hba::dsp::ThreeBandSplit, 2> densitySplit{};
    std::array<double, 3> bandEnv{0.15, 0.15, 0.15};

    // Gen-2 Strata playback & Post-sculpt
    double strataCursor = 0.0;
    double strataPingDir = 1.0;
    double scrubPos = 0.0;
    bool scrubInit = false;
    std::array<hba::dsp::Adaa1<hba::dsp::TanhShaper>, 2> postShaper{};
    std::array<hba::dsp::SvfState, 2> postTiltLp{};
    std::array<hba::dsp::Lr4Highpass, 2> strataHp{};
    std::array<hba::dsp::Lr4Split, 2> g1BandSplit{};
    std::array<hba::dsp::Lr4Split, 2> g2BandSplit{};
    hba::dsp::Limiter limiter{};

    // Note / envelope / expression state
    std::array<int, kMaxHeldNotes> noteStack{};
    int noteCount = 0;
    int activeNote = 29; // F1 default
    int lastDroneChoice = 5;
    bool hasReceivedMidi = false;
    bool gate = false;
    bool sustainDown = false;
    double velocityGain = 0.9;
    double ampEnv = 0.0;
    double pitchBendSt = 0.0;
    double pitchBendRange = 2.0;
    double modWheel = 0.0;
    double timbreCtrl = 0.5;
    double aftertouch = 0.0;
    bool padNoteActive = false;

    // Smoothers
    hba::dsp::Smoother smNoteMidi{}, smBend{}, smFilt1{}, smFilt1Res{}, smDrive{}, smAsym{}, smTone{}, smFilt2{},
        smFilt2Res{}, smDisperse{}, smDensity{}, smWidth{}, smUnison{}, smDetune{}, smSubLevel{}, smSubWarmth{},
        smSubSplit{}, smSubCos{}, smSubSin{}, smGenMix{}, smStrataPos{}, smStrataSpan{}, smStrataPitch{},
        smStrataFade{}, smPostDrive{}, smPostTone{}, smOutGain{}, smLimit{}, smCeiling{}, smPadX{}, smPadY{};

    // Clocks & lane state
    LaneCurves laneCurves = defaultLaneCurves();
    double freePpq = 0.0;
    std::uint64_t freeSamples = 0;
    std::uint64_t noteSamples = 0;
    std::uint64_t sampleIndex = 0;
    int scopeWrite = 0;

    // Persistent filter coefficients for exact bit-identical block-size invariance
    hba::dsp::SvfCoefs cF1{}, cF2L{}, cF2R{}, cSubSplit{}, cBandLo{}, cBandHi{}, cPostTilt{}, cStrataSplit{};
    std::array<hba::dsp::SvfCoefs, 3> cFormant{};
    std::array<double, 3> formGain{1.0, 0.7, 0.3};

    TelemetrySnapshot tel{};

    void prepare(double sr, int /*maxBlock*/, int factor) {
        sampleRate = std::max(sr, 22050.0);
        osFactor = (factor == 2 || factor == 4 || factor == 8) ? factor : 4;
        osRate = sampleRate * static_cast<double>(osFactor);
        controlCoef = hba::dsp::onePoleCoef(kControlSec, 1.0 / sampleRate);
        xfadeStep = 1.0 / std::max(1.0, kPrintXfadeSec * sampleRate);

        for (auto& os : osDown)
            os.prepare(osFactor);
        subRotator.prepare(sampleRate);
        limiter.prepare(sampleRate);
        cBandLo = hba::dsp::butterworth(260.0, sampleRate);
        cBandHi = hba::dsp::butterworth(2800.0, sampleRate);
        cPostTilt = hba::dsp::butterworth(900.0, sampleRate);
        cStrataSplit = hba::dsp::butterworth(460.0, sampleRate);
        reset();
    }

    void reset() noexcept LN_NONBLOCKING {
        for (auto& os : osDown)
            os.reset();
        subRotator.reset();
        for (auto& hp : preSculptHp)
            hp.reset();
        postSideHp.reset();
        for (int c = 0; c < 2; ++c) {
            const auto ci = static_cast<std::size_t>(c);
            f1A[ci].reset();
            f1B[ci].reset();
            for (auto& fm : f1Formant[ci])
                fm.reset();
            f1Comb[ci].reset();
            shaper[ci].reset();
            f2A[ci].reset();
            f2B[ci].reset();
            f2Phaser[ci].reset();
            for (auto& ap : disperseAp[ci])
                ap.reset();
            densitySplit[ci].reset();
            postShaper[ci].reset();
            postTiltLp[ci].reset();
            strataHp[ci].reset();
            g1BandSplit[ci].reset();
            g2BandSplit[ci].reset();
        }
        bandEnv = {0.15, 0.15, 0.15};
        limiter.reset();

        masterPhase = 0.0;
        voicePhase = kInitPhases;
        slavePhase = kInitPhases;
        slaveDir.fill(1.0);
        fmModPhase = kInitPhases;
        subWarmthLpState = 0.0;
        strataCursor = 0.0;
        strataPingDir = 1.0;
        scrubPos = 0.0;
        scrubInit = false;

        noteCount = 0;
        activeNote = 29;
        lastDroneChoice = 5;
        hasReceivedMidi = false;
        gate = false;
        sustainDown = false;
        ampEnv = 0.0;
        pitchBendSt = 0.0;
        modWheel = 0.0;
        timbreCtrl = 0.5;
        aftertouch = 0.0;
        padNoteActive = false;

        smNoteMidi.reset();
        smBend.reset();
        smFilt1.reset();
        smFilt1Res.reset();
        smDrive.reset();
        smAsym.reset();
        smTone.reset();
        smFilt2.reset();
        smFilt2Res.reset();
        smDisperse.reset();
        smDensity.reset();
        smWidth.reset();
        smUnison.reset();
        smDetune.reset();
        smSubLevel.reset();
        smSubWarmth.reset();
        smSubSplit.reset();
        smSubCos.reset();
        smSubSin.reset();
        smGenMix.reset();
        smStrataPos.reset();
        smStrataSpan.reset();
        smStrataPitch.reset();
        smStrataFade.reset();
        smPostDrive.reset();
        smPostTone.reset();
        smOutGain.reset();
        smLimit.reset();
        smCeiling.reset();
        smPadX.reset();
        smPadY.reset();

        freePpq = 0.0;
        freeSamples = 0;
        noteSamples = 0;
        sampleIndex = 0;
        scopeWrite = 0;
        shaperXfade = 1.0;
        printXfade = 1.0;
        tel = {};
    }

    void triggerNote(int note, float vel, bool resetClock) noexcept LN_NONBLOCKING {
        activeNote = std::clamp(note, 12, 96);
        velocityGain = std::clamp(0.45 + 0.55 * static_cast<double>(vel), 0.25, 1.0);
        const bool wasSilent = (!gate && ampEnv < 0.02);
        gate = true;
        if (wasSilent) {
            masterPhase = 0.0;
            voicePhase = kInitPhases;
            slavePhase = kInitPhases;
            slaveDir.fill(1.0);
            fmModPhase = kInitPhases;
            strataCursor = 0.0;
            strataPingDir = 1.0;
            smNoteMidi.reset();
        }
        if (resetClock)
            noteSamples = 0;
    }

    void noteOn(int note, float vel) noexcept LN_NONBLOCKING {
        hasReceivedMidi = true;
        noteOffFromStack(note);
        if (noteCount < kMaxHeldNotes)
            noteStack[static_cast<std::size_t>(noteCount++)] = note;
        else
            noteStack[kMaxHeldNotes - 1] = note;
        triggerNote(note, vel, true);
    }

    void noteOffFromStack(int note) noexcept LN_NONBLOCKING {
        for (int i = 0; i < noteCount; ++i) {
            if (noteStack[static_cast<std::size_t>(i)] == note) {
                for (int j = i; j < noteCount - 1; ++j)
                    noteStack[static_cast<std::size_t>(j)] = noteStack[static_cast<std::size_t>(j + 1)];
                --noteCount;
                break;
            }
        }
    }

    void noteOff(int note) noexcept LN_NONBLOCKING {
        noteOffFromStack(note);
        if (noteCount > 0) {
            activeNote = noteStack[static_cast<std::size_t>(noteCount - 1)];
        } else if (!sustainDown && !padNoteActive) {
            gate = false;
        }
    }

    void sustain(bool down) noexcept LN_NONBLOCKING {
        sustainDown = down;
        if (!down && noteCount == 0 && !padNoteActive)
            gate = false;
    }

    void allNotesOff() noexcept LN_NONBLOCKING {
        noteCount = 0;
        sustainDown = false;
        padNoteActive = false;
        gate = false;
    }

    void publishPrint(const PrintBuffer& buf) {
        if (!buf.valid())
            return;
        const int curActive = activeSlot.load(std::memory_order_acquire);
        const int curReading = readingSlot.load(std::memory_order_acquire);
        int target = 0;
        for (int s = 0; s < 3; ++s) {
            if (s != curActive && s != curReading) {
                target = s;
                break;
            }
        }
        auto& slot = printSlots[static_cast<std::size_t>(target)];
        const int len = std::min(buf.length, PrintBuffer::kMaxPrintSamples);
        for (int i = 0; i < len; ++i) {
            const auto ii = static_cast<std::size_t>(i);
            slot.left[ii] = buf.left[ii];
            slot.right[ii] = buf.right[ii];
        }
        slot.length = len;
        slot.sampleRate = buf.sampleRate > 8000.0 ? buf.sampleRate : 48000.0;
        slot.rootMidiNote = buf.rootMidiNote;
        slot.generation = std::max(1, buf.generation);
        pendingSlot.store(target, std::memory_order_release);
    }

    void clearPrint() noexcept LN_NONBLOCKING {
        pendingSlot.store(-2, std::memory_order_release); // -2 sentinel = clear
    }

    // Evaluates one sample of the Gen-2 printed buffer with key-tracked pitch, smooth equal-power
    // loop crossfades (strataFade), ping-pong wall softening, and tape-head scrub inertia.
    void readStrataSlot(int slotIdx, double pitchRatio, double pos01, double span01, StrataDir dir, double fadeMs,
                        float& outL, float& outR) noexcept LN_NONBLOCKING {
        if (slotIdx < 0 || slotIdx >= 3) {
            outL = outR = 0.0f;
            return;
        }
        const auto& slot = printSlots[static_cast<std::size_t>(slotIdx)];
        if (slot.length <= 64) {
            outL = outR = 0.0f;
            return;
        }
        const double totalLen = static_cast<double>(slot.length);
        const double winLen = std::max(256.0, totalLen * std::clamp(span01, 0.05, 1.0));
        const double maxStart = std::max(0.0, totalLen - winLen);
        const double winStart = std::clamp(pos01, 0.0, 1.0) * maxStart;
        const double srScale = slot.sampleRate / sampleRate;
        const double step = std::clamp(pitchRatio * srScale, 0.125, 8.0);
        const double fadeSamples = std::clamp(fadeMs, 2.0, 80.0) * 0.001 * sampleRate;

        double readPos = winStart;
        if (dir == StrataDir::scrub) {
            // Silky tape-head scrub with inertia scaled by Loop Xfade (eliminates zipper jumps)
            const double targetPos = std::clamp(pos01, 0.0, 0.9999) * (totalLen - 2.0);
            if (!scrubInit) {
                scrubPos = targetPos;
                scrubInit = true;
            } else {
                const double scrubSec = std::clamp(fadeMs * 0.001 * 1.4, 0.004, 0.12);
                const double scrubCoef = hba::dsp::onePoleCoef(scrubSec, 1.0 / sampleRate);
                scrubPos += scrubCoef * (targetPos - scrubPos);
            }
            readPos = std::clamp(scrubPos, 0.0, totalLen - 2.0);
            strataCursor = readPos - winStart;
        } else if (dir == StrataDir::pingPong) {
            scrubInit = false;
            strataCursor += step * strataPingDir;
            if (strataCursor >= winLen - 2.0) {
                strataCursor = std::max(0.0, (winLen - 2.0) - (strataCursor - (winLen - 2.0)));
                strataPingDir = -1.0;
            } else if (strataCursor < 0.0) {
                strataCursor = std::min(winLen - 2.0, -strataCursor);
                strataPingDir = 1.0;
            }
            readPos = winStart + strataCursor;
        } else if (dir == StrataDir::reverse) {
            scrubInit = false;
            strataCursor -= step;
            while (strataCursor < 0.0)
                strataCursor += winLen;
            while (strataCursor >= winLen)
                strataCursor -= winLen;
            readPos = winStart + strataCursor;
        } else {
            scrubInit = false;
            strataCursor += step;
            while (strataCursor >= winLen)
                strataCursor -= winLen;
            while (strataCursor < 0.0)
                strataCursor += winLen;
            readPos = winStart + strataCursor;
        }

        float sL = hermiteRead(slot.left.data(), slot.length, readPos);
        float sR = hermiteRead(slot.right.data(), slot.length, readPos);

        // Equal-power loop-edge crossfade for Forward/Reverse (prevents level dips & clicks across 2..80ms!)
        if (dir == StrataDir::forward || dir == StrataDir::reverse) {
            const double fadeLen = std::min(winLen * 0.44, fadeSamples);
            if (fadeLen > 4.0) {
                const double local = std::clamp(readPos - winStart, 0.0, winLen);
                if (local < fadeLen) {
                    const double u = local / fadeLen;
                    const double gA = std::sin(u * (kPi * 0.5));
                    const double gB = std::cos(u * (kPi * 0.5));
                    const double wrapPos = winStart + std::clamp(winLen - fadeLen + local, 0.0, winLen - 1.0);
                    sL = static_cast<float>(gA * sL + gB * hermiteRead(slot.left.data(), slot.length, wrapPos));
                    sR = static_cast<float>(gA * sR + gB * hermiteRead(slot.right.data(), slot.length, wrapPos));
                }
            }
        } else if (dir == StrataDir::pingPong) {
            // Subtle mirror softening near turnaround walls for click-free short ping-pong spans
            const double wallFade = std::min(winLen * 0.12, fadeSamples * 0.35);
            if (wallFade > 4.0) {
                const double local = std::clamp(readPos - winStart, 0.0, winLen);
                if (local < wallFade) {
                    const double u = local / wallFade;
                    const double w = 0.5 - 0.5 * std::cos(u * kPi);
                    const double mirror = winStart + std::clamp(2.0 * wallFade - local, 0.0, winLen - 1.0);
                    sL = static_cast<float>((0.7 + 0.3 * w) * sL + (0.3 * (1.0 - w)) * hermiteRead(slot.left.data(), slot.length, mirror));
                    sR = static_cast<float>((0.7 + 0.3 * w) * sR + (0.3 * (1.0 - w)) * hermiteRead(slot.right.data(), slot.length, mirror));
                } else if (local > winLen - wallFade) {
                    const double u = (winLen - local) / wallFade;
                    const double w = 0.5 - 0.5 * std::cos(u * kPi);
                    const double mirror = winStart + std::clamp(winLen - 2.0 * wallFade + (winLen - local), 0.0, winLen - 1.0);
                    sL = static_cast<float>((0.7 + 0.3 * w) * sL + (0.3 * (1.0 - w)) * hermiteRead(slot.left.data(), slot.length, mirror));
                    sR = static_cast<float>((0.7 + 0.3 * w) * sR + (0.3 * (1.0 - w)) * hermiteRead(slot.right.data(), slot.length, mirror));
                }
            }
        }
        outL = sL;
        outR = sR;
    }

    void process(const Params& params, const Transport& transport, float* left, float* right,
                 int numSamples) noexcept LN_NONBLOCKING {
        hba::dsp::ScopedFlushDenormals flush;
        if (numSamples <= 0 || left == nullptr || right == nullptr)
            return;

        // Pick up any newly printed Gen-2 buffer lock-free
        const int incoming = pendingSlot.exchange(-1, std::memory_order_acq_rel);
        if (incoming == -2) {
            prevSlot = -1;
            activeSlot.store(-1, std::memory_order_release);
            readingSlot.store(-1, std::memory_order_release);
            printXfade = 1.0;
        } else if (incoming >= 0 && incoming < 3) {
            prevSlot = activeSlot.load(std::memory_order_acquire);
            activeSlot.store(incoming, std::memory_order_release);
            printXfade = (prevSlot >= 0) ? 0.0 : 1.0;
            strataCursor = 0.0;
            strataPingDir = 1.0;
        }

        // Pad touch / Drone triggers live note cleanly even without DAW MIDI, honoring Drone Key!
        const int droneChoice = std::clamp(params.choice(ParamId::droneNote), 0, 11);
        const int desiredDroneMidi = 24 + droneChoice; // C1..B1
        if (droneChoice != lastDroneChoice) {
            lastDroneChoice = droneChoice;
            if (noteCount == 0)
                activeNote = desiredDroneMidi;
        } else if (!hasReceivedMidi && noteCount == 0) {
            activeNote = desiredDroneMidi;
        }

        const bool wantPadNote = params.on(ParamId::padTouch) || params.on(ParamId::padHold);
        if (wantPadNote && !padNoteActive) {
            padNoteActive = true;
            if (noteCount == 0)
                triggerNote(activeNote, 0.92f, true);
        } else if (!wantPadNote && padNoteActive) {
            padNoteActive = false;
            if (noteCount == 0 && !sustainDown)
                gate = false;
        }

        const double bpm = std::clamp(transport.bpm, 20.0, 400.0);
        const double quartersPerSample = bpm / (60.0 * sampleRate);

        const auto coreMode = static_cast<CoreMode>(std::clamp(params.choice(ParamId::coreMode), 0, 5));
        const auto filt1Mode = static_cast<Filt1Mode>(std::clamp(params.choice(ParamId::filt1Mode), 0, 4));
        const auto driveType = static_cast<DriveType>(std::clamp(params.choice(ParamId::driveType), 0, 4));
        const auto filt2Mode = static_cast<Filt2Mode>(std::clamp(params.choice(ParamId::filt2Mode), 0, 4));
        const auto strataDir = static_cast<StrataDir>(std::clamp(params.choice(ParamId::strataDir), 0, 3));
        const auto strataBlend = static_cast<StrataBlend>(std::clamp(params.choice(ParamId::strataBlend), 0, 2));
        const bool strataKeytrack = params.on(ParamId::strataKeytrack);
        const auto padProg = static_cast<PadProgram>(std::clamp(params.choice(ParamId::padProgram), 0, 4));
        const int octSt = kOctaveSemitones[static_cast<std::size_t>(std::clamp(params.choice(ParamId::octave), 0, 3))];

        if (driveType != activeDriveType) {
            prevDriveType = activeDriveType;
            activeDriveType = driveType;
            shaperXfade = 0.0;
        }
        const double shaperStep = 1.0 / std::max(1.0, 0.008 * osRate);

        const double glideSec = static_cast<double>(params[ParamId::glide]) * 0.001;
        const double glideCoef = glideSec > 1.0e-4 ? hba::dsp::onePoleCoef(glideSec, 1.0 / sampleRate) : 1.0;
        const double atkCoef =
            hba::dsp::onePoleCoef(std::max(0.0005, static_cast<double>(params[ParamId::attack]) * 0.001), 1.0 / sampleRate);
        const double relCoef =
            hba::dsp::onePoleCoef(std::max(0.005, static_cast<double>(params[ParamId::release]) * 0.001), 1.0 / sampleRate);
        const double densityAtk = hba::dsp::onePoleCoef(0.006, 1.0 / sampleRate);
        const double densityRel = hba::dsp::onePoleCoef(0.075, 1.0 / sampleRate);

        const double targetOutGain = hba::dsp::dbToGain(static_cast<double>(params[ParamId::outputGain]));
        const double targetCeiling = hba::dsp::dbToGain(static_cast<double>(params[ParamId::ceiling]));
        const double targetLimit = params.on(ParamId::limit) ? 1.0 : 0.0;

        float blockPeakL = 0.0f, blockPeakR = 0.0f, blockSubPk = 0.0f;
        double rmsSum = 0.0;
        double minLimGain = 1.0;

        const int curSlot = activeSlot.load(std::memory_order_acquire);
        readingSlot.store(curSlot, std::memory_order_release);
        const bool hasValidPrint = (curSlot >= 0 && printSlots[static_cast<std::size_t>(curSlot)].length > 64);

        const auto laneCfgs = extractLaneConfigs(params);
        const double motionAmt = static_cast<double>(params[ParamId::motion]);

        for (int n = 0; n < numSamples; ++n) {
            const double ppq = transport.playing ? transport.ppq + quartersPerSample * n : freePpq;
            if (!transport.playing)
                freePpq += quartersPerSample;
            else
                freePpq = ppq;
            const double freeSec = static_cast<double>(freeSamples) / sampleRate;
            const double noteSec = static_cast<double>(noteSamples) / sampleRate;
            const double notePpq = static_cast<double>(noteSamples) * quartersPerSample;
            ++freeSamples;
            ++noteSamples;

            // Smooth amp envelope
            const double envTarget = gate ? 1.0 : 0.0;
            ampEnv += (envTarget > ampEnv ? atkCoef : relCoef) * (envTarget - ampEnv);
            if (!gate && ampEnv < 1.0e-6)
                ampEnv = 0.0;

            // Evaluate 4 unified Free-Hz / Sync lanes
            std::array<float, kLaneCount> sends{};
            const hba::dsp::LaneClock clk{ppq, notePpq, freeSec, noteSec};
            for (int l = 0; l < kLaneCount; ++l) {
                sends[static_cast<std::size_t>(l)] =
                    static_cast<float>(hba::dsp::evaluateLaneSend(laneCfgs, laneCurves, l, clk, motionAmt));
            }

            // Smooth X/Y pad offsets (centered at 0.5 -> 0 offset!)
            const double px = smPadX.step(static_cast<double>(params[ParamId::padX]), controlCoef);
            const double py = smPadY.step(static_cast<double>(params[ParamId::padY]), controlCoef);
            const double dx = (px - 0.5) * 2.0; // -1..+1
            const double dy = (py - 0.5) * 2.0; // -1..+1

            double padFilt1Oct = 0.0, padFilt2Oct = 0.0, padDrive = 0.0, padRes1 = 0.0, padRes2 = 0.0;
            double padTone = 0.0, padDetune = 0.0, padWidth = 0.0, padDisperse = 0.0, padDensity = 0.0;
            double padSubWarm = 0.0, padSubPhase = 0.0, padGenMix = 0.0, padStrataPos = 0.0, padStrataSpan = 0.0;

            switch (padProg) {
            case PadProgram::sculpt:
                padFilt1Oct = dx * 2.6;
                padDrive = dy * 0.45;
                padRes1 = std::max(0.0, dy) * 0.35;
                padFilt2Oct = dx * 1.1;
                break;
            case PadProgram::reese:
                padTone = dx * 0.48;
                padDetune = dx * 24.0;
                padFilt2Oct = dy * 2.4;
                padRes2 = std::max(0.0, dy) * 0.32;
                padWidth = dy * 0.35;
                break;
            case PadProgram::strata:
                padStrataPos = dx * 0.50;
                padGenMix = dy * 0.50;
                padStrataSpan = -std::max(0.0, dy) * 0.45;
                break;
            case PadProgram::disperse:
                padFilt1Oct = dx * 1.8;
                padDisperse = dy * 0.55;
                padDensity = std::max(0.0, dy) * 0.42;
                break;
            case PadProgram::abyss:
                padSubWarm = dx * 0.55;
                padSubPhase = dx * 120.0;
                padFilt1Oct = -dy * 2.2;
                break;
            }

            // Combine base params + 4 Movement Lanes + MIDI Expression + X/Y Pad
            const double rawF1 =
                static_cast<double>(laneApply(LaneTarget::filt1Hz, params[ParamId::filt1Hz],
                                              laneSum(params, sends, LaneTarget::filt1Hz))) *
                std::exp2(padFilt1Oct + (timbreCtrl - 0.5) * 2.4);
            const double rawF2 =
                static_cast<double>(laneApply(LaneTarget::filt2Hz, params[ParamId::filt2Hz],
                                              laneSum(params, sends, LaneTarget::filt2Hz))) *
                std::exp2(padFilt2Oct + modWheel * 1.8);
            const double tF1 = std::clamp(rawF1, 24.0, 18000.0);
            const double tF2 = std::clamp(rawF2, 55.0, 16000.0);
            const double tF1Res = std::clamp(static_cast<double>(params[ParamId::filt1Res]) + padRes1, 0.0, 0.96);
            const double tF2Res = std::clamp(static_cast<double>(params[ParamId::filt2Res]) + padRes2, 0.0, 0.95);
            const double tTone = std::clamp(
                static_cast<double>(laneApply(LaneTarget::tone, params[ParamId::tone], laneSum(params, sends, LaneTarget::tone))) +
                    padTone + 0.35 * modWheel,
                0.0, 1.0);
            const double tDrive = std::clamp(
                static_cast<double>(laneApply(LaneTarget::drive, params[ParamId::drive], laneSum(params, sends, LaneTarget::drive))) +
                    padDrive + 0.30 * aftertouch,
                0.0, 1.0);
            const double tDisperse = std::clamp(
                static_cast<double>(laneApply(LaneTarget::disperse, params[ParamId::disperse],
                                              laneSum(params, sends, LaneTarget::disperse))) +
                    padDisperse,
                0.0, 1.0);
            const double tDensity = std::clamp(
                static_cast<double>(laneApply(LaneTarget::density, params[ParamId::density],
                                              laneSum(params, sends, LaneTarget::density))) +
                    padDensity,
                0.0, 1.0);
            const double tWidth = std::clamp(
                static_cast<double>(laneApply(LaneTarget::width, params[ParamId::width], laneSum(params, sends, LaneTarget::width))) +
                    padWidth,
                0.0, 1.0);
            const double tGenMix = std::clamp(
                static_cast<double>(laneApply(LaneTarget::genMix, params[ParamId::genMix], laneSum(params, sends, LaneTarget::genMix))) +
                    padGenMix,
                0.0, 1.0);
            const double tStrataPos = hba::dsp::frac(
                static_cast<double>(laneApply(LaneTarget::strataPos, params[ParamId::strataPos],
                                              laneSum(params, sends, LaneTarget::strataPos))) +
                padStrataPos);
            const double tStrataSpan = std::clamp(
                static_cast<double>(laneApply(LaneTarget::strataSpan, params[ParamId::strataSpan],
                                              laneSum(params, sends, LaneTarget::strataSpan))) +
                    padStrataSpan,
                0.05, 1.0);
            const double tSubWarm = std::clamp(static_cast<double>(params[ParamId::subWarmth]) + padSubWarm, 0.0, 1.0);
            const double rawSubPhase =
                static_cast<double>(laneApply(LaneTarget::subPhase, params[ParamId::subPhase],
                                              laneSum(params, sends, LaneTarget::subPhase))) +
                padSubPhase;

            // Smooth control signals
            const double sNote = smNoteMidi.step(static_cast<double>(activeNote + octSt), glideCoef);
            const double sBend = smBend.step(pitchBendSt, controlCoef);
            const double totalMidi = sNote + sBend;
            const double baseHz = std::clamp(hba::dsp::midiToHz(totalMidi), 18.0, 4200.0);

            const double sF1 = smFilt1.step(tF1, controlCoef);
            const double sF1Res = smFilt1Res.step(tF1Res, controlCoef);
            const double sDrive = smDrive.step(tDrive, controlCoef);
            const double sAsym = smAsym.step(static_cast<double>(params[ParamId::asym]), controlCoef);
            const double sTone = smTone.step(tTone, controlCoef);
            const double sF2 = smFilt2.step(tF2, controlCoef);
            const double sF2Res = smFilt2Res.step(tF2Res, controlCoef);
            const double sDisperse = smDisperse.step(tDisperse, controlCoef);
            const double sDensity = smDensity.step(tDensity, controlCoef);
            const double sWidth = smWidth.step(tWidth, controlCoef);
            const double sUnison = smUnison.step(static_cast<double>(params[ParamId::unison]), controlCoef);
            const double sDetune = smDetune.step(std::clamp(static_cast<double>(params[ParamId::detune]) + padDetune, 0.0, 80.0),
                                                 controlCoef);
            const double sSubLvl = smSubLevel.step(static_cast<double>(params[ParamId::subLevel]), controlCoef);
            const double sSubWarm = smSubWarmth.step(tSubWarm, controlCoef);
            const double sSubSplit = smSubSplit.step(static_cast<double>(params[ParamId::subSplit]), controlCoef);
            const double sGenMix = hasValidPrint ? smGenMix.step(tGenMix, controlCoef) : smGenMix.step(0.0, controlCoef);
            const double sStrataPos = smStrataPos.step(tStrataPos, controlCoef);
            const double sStrataSpan = smStrataSpan.step(tStrataSpan, controlCoef);
            const double sStrataPitch = smStrataPitch.step(static_cast<double>(params[ParamId::strataPitch]), controlCoef);
            const double sStrataFade = smStrataFade.step(static_cast<double>(params[ParamId::strataFade]), controlCoef);
            const double sPostDrive = smPostDrive.step(static_cast<double>(params[ParamId::postDrive]), controlCoef);
            const double sPostTone = smPostTone.step(static_cast<double>(params[ParamId::postTone]), controlCoef);
            const double sOutGain = smOutGain.step(targetOutGain, controlCoef);
            const double sLimit = smLimit.step(targetLimit, controlCoef);
            const double sCeiling = smCeiling.step(targetCeiling, controlCoef);

            // Vector-smoothed Sub Phase (zero 360->0 wrap click)
            const double subPhaseRad = hba::dsp::frac(rawSubPhase / 360.0) * kTwoPi;
            const double scSub = smSubCos.step(std::cos(subPhaseRad), controlCoef);
            const double ssSub = smSubSin.step(std::sin(subPhaseRad), controlCoef);
            const double subNorm = std::hypot(scSub, ssSub);
            const double cSub = subNorm > 1.0e-5 ? (scSub / subNorm) : 1.0;
            const double sSub = subNorm > 1.0e-5 ? (ssSub / subNorm) : 0.0;

            // Update filter coefficients every 4 samples (bit-exact & fast)
            if ((sampleIndex & 3u) == 0u) {
                const double q1 = 0.65 + std::pow(sF1Res, 1.55) * 9.5;
                cF1 = hba::dsp::SvfCoefs::make(sF1, q1, osRate);
                if (filt1Mode == Filt1Mode::formant) {
                    // Map log(filt1Hz) smoothly across 5 vowels
                    const double vPos = std::clamp(std::log2(sF1 / 80.0) / 7.0, 0.0, 1.0) * 4.0;
                    const int v0 = std::clamp(static_cast<int>(vPos), 0, 3);
                    const double vf = vPos - static_cast<double>(v0);
                    const auto& a = kVowels[static_cast<std::size_t>(v0)];
                    const auto& b = kVowels[static_cast<std::size_t>(v0 + 1)];
                    const double qBoost = 0.75 + 1.35 * sF1Res;
                    for (std::size_t k = 0; k < 3; ++k) {
                        const double hz = a.hz[k] * std::pow(b.hz[k] / a.hz[k], vf);
                        const double q = (a.q[k] + (b.q[k] - a.q[k]) * vf) * qBoost;
                        cFormant[k] = hba::dsp::SvfCoefs::make(hz, q, osRate);
                        formGain[k] = a.gain[k] + (b.gain[k] - a.gain[k]) * vf;
                    }
                }
                const double q2 = 0.75 + std::pow(sF2Res, 1.4) * 11.0;
                const double stereoOff = 1.0 + 0.14 * sWidth;
                cF2L = hba::dsp::SvfCoefs::make(std::clamp(sF2 / stereoOff, 40.0, 0.44 * osRate), q2, osRate);
                cF2R = hba::dsp::SvfCoefs::make(std::clamp(sF2 * stereoOff, 40.0, 0.44 * osRate), q2, osRate);
                if (filt2Mode == Filt2Mode::phaser) {
                    f2Phaser[0].setFreq(sF2 / stereoOff, osRate);
                    f2Phaser[1].setFreq(sF2 * stereoOff, osRate);
                }
                cSubSplit = hba::dsp::butterworth(sSubSplit, sampleRate);
                const double dispHz = std::clamp(sF1 * 0.65, 90.0, 2800.0);
                for (int s = 0; s < kDisperseStages; ++s) {
                    const double stHz = dispHz * (0.75 + 0.12 * static_cast<double>(s));
                    disperseAp[0][static_cast<std::size_t>(s)].setFreq(stHz, sampleRate);
                    disperseAp[1][static_cast<std::size_t>(s)].setFreq(stHz * (1.0 + 0.04 * sWidth), sampleRate);
                }
            }
            ++sampleIndex;

            if (ampEnv <= 1.0e-6) {
                left[n] = 0.0f;
                right[n] = 0.0f;
                if (n == numSamples - 1)
                    publishTelemetry(ppq, notePpq, freeSec, noteSec, params, sends, sF1, sF2, sDrive, sTone, sGenMix,
                                     sStrataPos, sStrataSpan, sDisperse, sDensity, sWidth, sSubWarm, curSlot, false,
                                     wantPadNote, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
                continue;
            }

            // 1. Clean Phase-Locked Mono Sub + Small-Speaker Harmonic Translation
            const double masterInc = baseHz / sampleRate;
            const double subTheta = kTwoPi * masterPhase;
            const double subFund = std::sin(subTheta);
            // Phase-locked 2nd/3rd harmonics softly rolled off above 380 Hz for small-speaker authority
            const double warmRaw = 0.34 * std::sin(2.0 * subTheta) + 0.14 * std::sin(3.0 * subTheta);
            const double warmCoef = hba::dsp::onePoleCoef(1.0 / (kTwoPi * 360.0), 1.0 / sampleRate);
            subWarmthLpState += warmCoef * (warmRaw - subWarmthLpState);
            const double subPre = (subFund + sSubWarm * subWarmthLpState) * (1.0 / (1.0 + 0.25 * sSubWarm));
            const double subClean = subRotator.processCosSin(subPre, cSub, sSub) * sSubLvl * 0.58;
            masterPhase = hba::dsp::frac(masterPhase + masterInc);

            // 2. Gen 0 (Oversampled 7-Voice Reese/Sync/TZFM Core) + Gen 1 (Filter 1 -> ADAA2 -> Filter 2)
            std::array<float, hba::dsp::kMaxOversampling> bufL{}, bufR{};
            const double driveGain = 1.0 + std::pow(sDrive, 1.45) * 14.0;
            const double driveMakeup = 1.0 / std::pow(driveGain, 0.42);
            const double asymBias = sAsym * 0.45 * sDrive;
            const auto fromShaper = toShaperModel(prevDriveType);
            const auto toShaper = toShaperModel(activeDriveType);

            // Voice amplitude weights based on Unison (0 = tight 3-voice core, 1 = lush 7-voice Reese)
            std::array<double, kNumVoices> vWeight{};
            double weightNorm = 0.0;
            for (int v = 0; v < kNumVoices; ++v) {
                const auto vi = static_cast<std::size_t>(v);
                const int distFromCenter = std::abs(v - 3);
                double w = 1.0;
                if (distFromCenter == 1)
                    w = 0.45 + 0.55 * sUnison;
                else if (distFromCenter == 2)
                    w = std::clamp((sUnison - 0.15) * 1.15, 0.0, 0.88);
                else if (distFromCenter == 3)
                    w = std::clamp((sUnison - 0.35) * 1.35, 0.0, 0.78);
                vWeight[vi] = w;
                weightNorm += w * w;
            }
            const double voiceScale = 0.42 / std::sqrt(std::max(1.0, weightNorm));

            // Precompute per-voice base increments at osRate
            std::array<double, kNumVoices> vInc{};
            for (int v = 0; v < kNumVoices; ++v) {
                const auto vi = static_cast<std::size_t>(v);
                const double cents = kDetuneOffsets[vi] * sDetune * (0.28 + 0.72 * sUnison);
                vInc[vi] = std::clamp((baseHz * std::exp2(cents / 1200.0)) / osRate, 1.0e-6, 0.45);
            }

            const double syncRatio = 1.0 + 6.8 * std::pow(sTone, 1.35);
            const double fmRatio = (sTone < 0.33) ? 1.0 : ((sTone < 0.66) ? 2.0 : 3.0);
            const double fmIndex = sTone * 4.5;
            const double foldDrive = 1.0 + sTone * 5.2;

            for (int k = 0; k < osFactor; ++k) {
                double sumL = 0.0, sumR = 0.0;
                for (int v = 0; v < kNumVoices; ++v) {
                    const auto vi = static_cast<std::size_t>(v);
                    if (vWeight[vi] <= 1.0e-4)
                        continue;

                    const double inc = vInc[vi];
                    double p = voicePhase[vi] + inc;
                    bool wrapped = false;
                    double wrapFrac = 0.0;
                    if (p >= 1.0) {
                        p -= 1.0;
                        wrapped = true;
                        wrapFrac = p / inc;
                    }
                    voicePhase[vi] = p;

                    double sig = 0.0;
                    switch (coreMode) {
                    case CoreMode::analogReese: {
                        const double saw = hba::dsp::polyBlepSaw(p, inc);
                        // Warm triangle + subtle bright saw-octave blend driven by Tone
                        const double tri = 1.0 - 4.0 * std::abs(p - 0.5);
                        const double octP = hba::dsp::frac(p * 2.0);
                        const double octSaw = hba::dsp::polyBlepSaw(octP, std::min(0.45, inc * 2.0));
                        sig = (1.0 - 0.45 * sTone) * (0.72 * saw + 0.28 * tri) + 0.45 * sTone * (0.75 * saw + 0.25 * octSaw);
                        break;
                    }
                    case CoreMode::softSync: {
                        // Continuous phase-reversing soft sync: zero hard discontinuities!
                        const double sInc = std::min(0.42, inc * syncRatio);
                        if (wrapped)
                            slaveDir[vi] = -slaveDir[vi];
                        double sp = slavePhase[vi] + slaveDir[vi] * sInc;
                        if (sp >= 1.0) {
                            sp = 2.0 - sp;
                            slaveDir[vi] = -1.0;
                        } else if (sp <= 0.0) {
                            sp = -sp;
                            slaveDir[vi] = 1.0;
                        }
                        slavePhase[vi] = std::clamp(sp, 0.0, 1.0);
                        // Rich continuous soft-sync waveshape: muscular analog sync overtones, zero hard jump
                        const double tri = 2.0 * slavePhase[vi] - 1.0;
                        const double shaped = std::tanh(tri * (2.1 + 1.6 * sTone));
                        const double edge = std::sin(kPi * tri * (1.0 + 0.85 * sTone));
                        sig = 0.74 * shaped + 0.26 * edge;
                        break;
                    }
                    case CoreMode::hardSync: {
                        const double sInc = std::min(0.45, inc * syncRatio);
                        double sp = slavePhase[vi] + sInc;
                        double blep = 0.0;
                        if (sp >= 1.0) {
                            sp -= 1.0;
                            blep -= hba::dsp::polyBlep(sp, sInc);
                        }
                        if (wrapped) {
                            // Exact subsample-corrected master-forced reset
                            const double preVal = 2.0 * sp - 1.0;
                            sp = wrapFrac * sInc;
                            const double postVal = 2.0 * sp - 1.0;
                            const double jump = (postVal - preVal) * 0.5;
                            blep += jump * hba::dsp::polyBlep(p, inc);
                        }
                        slavePhase[vi] = sp;
                        sig = (2.0 * sp - 1.0) + blep;
                        break;
                    }
                    case CoreMode::tzfm: {
                        const double mInc = std::min(0.45, inc * fmRatio * (1.0 + 0.002 * kDetuneOffsets[vi]));
                        fmModPhase[vi] = hba::dsp::frac(fmModPhase[vi] + mInc);
                        const double mod = std::sin(kTwoPi * fmModPhase[vi]);
                        sig = std::sin(kTwoPi * p + fmIndex * mod);
                        break;
                    }
                    case CoreMode::pulseReese: {
                        const double pw = std::clamp(0.50 - 0.38 * sTone + 0.04 * kDetuneOffsets[vi], 0.07, 0.93);
                        const double pulse = hba::dsp::polyBlepPulse(p, inc, pw);
                        const double saw = hba::dsp::polyBlepSaw(p, inc);
                        sig = 0.70 * pulse + 0.30 * saw;
                        break;
                    }
                    case CoreMode::subFold: {
                        const double baseWave = 0.72 * std::sin(kTwoPi * p) + 0.28 * (1.0 - 4.0 * std::abs(p - 0.5));
                        sig = 0.65 * hba::dsp::Buchla259Shaper::eval(baseWave * foldDrive) +
                              0.35 * std::sin(baseWave * foldDrive * 1.4);
                        break;
                    }
                    }

                    const double pan = kVoicePan[vi] * (0.25 + 0.75 * sWidth);
                    const double gL = vWeight[vi] * std::sqrt(0.5 * (1.0 - pan));
                    const double gR = vWeight[vi] * std::sqrt(0.5 * (1.0 + pan));
                    sumL += sig * gL;
                    sumR += sig * gR;
                }

                double xL = sumL * voiceScale;
                double xR = sumR * voiceScale;

                // Filter 1 (oversampled)
                const auto applyF1 = [&](int ch, double x) noexcept -> double {
                    const auto ci = static_cast<std::size_t>(ch);
                    switch (filt1Mode) {
                    case Filt1Mode::lp24: {
                        const double s1 = f1A[ci].lowpass(cF1, std::tanh(x * 1.15) * 0.92);
                        return f1B[ci].lowpass(cF1, s1);
                    }
                    case Filt1Mode::bp12:
                        return f1A[ci].bandpass(cF1, x) * 1.35;
                    case Filt1Mode::notch: {
                        double v1 = 0.0, v2 = 0.0;
                        f1A[ci].tick(cF1, x, v1, v2);
                        return x - cF1.k * v1;
                    }
                    case Filt1Mode::comb:
                        return f1Comb[ci].process(x, sF1, sF1Res, osRate);
                    case Filt1Mode::formant: {
                        double out = 0.0;
                        for (std::size_t fi = 0; fi < 3; ++fi)
                            out += f1Formant[ci][fi].bandpass(cFormant[fi], x) * formGain[fi];
                        return out * 1.85 + x * 0.16;
                    }
                    }
                    return x;
                };
                xL = applyF1(0, xL);
                xR = applyF1(1, xR);

                // ADAA2 Inter-stage Nonlinear Shaper (with click-free model crossfade & gain match!)
                double dryL = 0.0, dryR = 0.0;
                const double wetL = shaper[0].process(xL, driveGain, asymBias, fromShaper, toShaper, shaperXfade, dryL);
                const double wetR = shaper[1].process(xR, driveGain, asymBias, fromShaper, toShaper, shaperXfade, dryR);
                if (shaperXfade < 1.0)
                    shaperXfade = std::min(1.0, shaperXfade + shaperStep);
                const double dBlend = std::clamp(sDrive * 1.25, 0.0, 1.0);
                xL = dryL * (1.0 - dBlend) + (wetL * driveMakeup * 1.18) * dBlend;
                xR = dryR * (1.0 - dBlend) + (wetR * driveMakeup * 1.18) * dBlend;

                // Filter 2 (post-distortion liquid Reese canyons / phaser!)
                const auto applyF2 = [&](int ch, double x, const hba::dsp::SvfCoefs& c) noexcept -> double {
                    const auto ci = static_cast<std::size_t>(ch);
                    switch (filt2Mode) {
                    case Filt2Mode::notch: {
                        double v1 = 0.0, v2 = 0.0;
                        f2A[ci].tick(c, x, v1, v2);
                        return x - c.k * v1;
                    }
                    case Filt2Mode::dualNotch: {
                        double v1 = 0.0, v2 = 0.0;
                        f2A[ci].tick(c, x, v1, v2);
                        const double n1 = x - c.k * v1;
                        const auto cHi = hba::dsp::SvfCoefs::make(
                            std::clamp(sF2 * (ch == 0 ? 1.52 : 1.78), 80.0, 0.44 * osRate),
                            0.9 + sF2Res * 9.0, osRate);
                        f2B[ci].tick(cHi, n1, v1, v2);
                        return n1 - cHi.k * v1;
                    }
                    case Filt2Mode::lp12:
                        return f2A[ci].lowpass(c, x);
                    case Filt2Mode::bp12:
                        return f2A[ci].bandpass(c, x) * 1.30;
                    case Filt2Mode::phaser:
                        return f2Phaser[ci].process(x, sF2Res);
                    }
                    return x;
                };
                xL = applyF2(0, xL, cF2L);
                xR = applyF2(1, xR, cF2R);

                bufL[static_cast<std::size_t>(k)] = static_cast<float>(hba::dsp::softGuard(xL, 2.2, 3.8));
                bufR[static_cast<std::size_t>(k)] = static_cast<float>(hba::dsp::softGuard(xR, 2.2, 3.8));
            }

            // Downsample Gen-1 sculpted stereo signal & LR4 high-pass above subSplit!
            double g1L = preSculptHp[0].process(cSubSplit, static_cast<double>(osDown[0].down(bufL.data())));
            double g1R = preSculptHp[1].process(cSubSplit, static_cast<double>(osDown[1].down(bufR.data())));

            // Elastic 6-stage allpass Dispersion (flat magnitude, liquid smear)
            if (sDisperse > 1.0e-4) {
                double dL = g1L, dR = g1R;
                for (int s = 0; s < kDisperseStages; ++s) {
                    dL = disperseAp[0][static_cast<std::size_t>(s)].process(dL);
                    dR = disperseAp[1][static_cast<std::size_t>(s)].process(dR);
                }
                g1L = g1L * (1.0 - sDisperse) + dL * sDisperse;
                g1R = g1R * (1.0 - sDisperse) + dR * sDisperse;
            }

            // 3-Band Controlled Density (phase-coherent LR4 upward/downward glue without OTT harshness!)
            if (sDensity > 1.0e-4) {
                double loL = 0.0, midL = 0.0, hiL = 0.0;
                double loR = 0.0, midR = 0.0, hiR = 0.0;
                densitySplit[0].process(cBandLo, cBandHi, g1L, loL, midL, hiL);
                densitySplit[1].process(cBandLo, cBandHi, g1R, loR, midR, hiR);

                const std::array<double, 3> mag{
                    std::max(std::abs(loL), std::abs(loR)),
                    std::max(std::abs(midL), std::abs(midR)),
                    std::max(std::abs(hiL), std::abs(hiR)),
                };
                constexpr std::array<double, 3> kTarget{0.24, 0.28, 0.20};
                std::array<double, 3> gain{1.0, 1.0, 1.0};
                for (std::size_t b = 0; b < 3; ++b) {
                    const double coef = mag[b] > bandEnv[b] ? densityAtk : densityRel;
                    bandEnv[b] += coef * (mag[b] - bandEnv[b]);
                    const double ratio = kTarget[b] / std::max(bandEnv[b], 0.035);
                    // Gentle upward (+5 dB max) and downward (-8 dB max) musical control
                    const double shaped = std::clamp(std::pow(ratio, 0.45), 0.38, 1.85);
                    gain[b] = 1.0 + sDensity * (shaped - 1.0);
                }
                g1L = std::tanh((loL * gain[0] + midL * gain[1] + hiL * gain[2]) * 1.08) * 0.96;
                g1R = std::tanh((loR * gain[0] + midR * gain[1] + hiR * gain[2]) * 1.08) * 0.96;
            }

            // 3. Gen 2 · Live Non-Destructive Resampled Strata Playback (Optional Key-Track + Sub-Split Protected!)
            double g2L = 0.0, g2R = 0.0;
            if (hasValidPrint && sGenMix > 1.0e-4) {
                const auto& slot = printSlots[static_cast<std::size_t>(curSlot)];
                const double trackOffset = strataKeytrack ? (totalMidi - static_cast<double>(slot.rootMidiNote)) : 0.0;
                const double pitchRatio = std::exp2((trackOffset + sStrataPitch) / 12.0);
                float curL = 0.0f, curR = 0.0f;
                readStrataSlot(curSlot, pitchRatio, sStrataPos, sStrataSpan, strataDir, sStrataFade, curL, curR);
                if (printXfade < 1.0 && prevSlot >= 0) {
                    float oldL = 0.0f, oldR = 0.0f;
                    readStrataSlot(prevSlot, pitchRatio, sStrataPos, sStrataSpan, strataDir, sStrataFade, oldL, oldR);
                    const double ang = printXfade * (kPi * 0.5);
                    g2L = std::sin(ang) * static_cast<double>(curL) + std::cos(ang) * static_cast<double>(oldL);
                    g2R = std::sin(ang) * static_cast<double>(curR) + std::cos(ang) * static_cast<double>(oldR);
                    printXfade = std::min(1.0, printXfade + xfadeStep);
                } else {
                    g2L = static_cast<double>(curL);
                    g2R = static_cast<double>(curR);
                }

                // Gen-2 Post-Sculpt: warm ADAA1 character + tilt shelf
                const double pGain = 1.0 + sPostDrive * 4.5;
                g2L = postShaper[0].process(g2L * pGain) / std::pow(pGain, 0.45);
                g2R = postShaper[1].process(g2R * pGain) / std::pow(pGain, 0.45);
                if (std::abs(sPostTone) > 1.0e-4) {
                    const double lpL = postTiltLp[0].lowpass(cPostTilt, g2L);
                    const double lpR = postTiltLp[1].lowpass(cPostTilt, g2R);
                    const double hiL = g2L - lpL;
                    const double hiR = g2R - lpR;
                    g2L = lpL * (1.0 - 0.35 * sPostTone) + hiL * (1.0 + 0.55 * sPostTone);
                    g2R = lpR * (1.0 - 0.35 * sPostTone) + hiR * (1.0 + 0.55 * sPostTone);
                }
            }
            // Dedicated LR4 Sub-Split high-pass on Strata so pitched-down loops, Post Drive DC,
            // and full-range external WAVs NEVER fight or phase-cancel the clean mono sub!
            g2L = strataHp[0].process(cSubSplit, g2L);
            g2R = strataHp[1].process(cSubSplit, g2R);

            // Gen 1 <-> Gen 2 Crossover / Blend (Full Blend, Hi-Strata >460 Hz, or Lo-Strata <460 Hz)
            double g1LoL = 0.0, g1HiL = 0.0, g1LoR = 0.0, g1HiR = 0.0;
            double g2LoL = 0.0, g2HiL = 0.0, g2LoR = 0.0, g2HiR = 0.0;
            g1BandSplit[0].process(cStrataSplit, g1L, g1LoL, g1HiL);
            g1BandSplit[1].process(cStrataSplit, g1R, g1LoR, g1HiR);
            g2BandSplit[0].process(cStrataSplit, g2L, g2LoL, g2HiL);
            g2BandSplit[1].process(cStrataSplit, g2R, g2LoR, g2HiR);

            const double mixAng = sGenMix * (kPi * 0.5);
            const double cMix = std::cos(mixAng);
            const double sMix = std::sin(mixAng);
            double midTopL = 0.0, midTopR = 0.0;
            if (strataBlend == StrataBlend::hiStrata) {
                midTopL = g1LoL + (cMix * g1HiL + sMix * g2HiL);
                midTopR = g1LoR + (cMix * g1HiR + sMix * g2HiR);
            } else if (strataBlend == StrataBlend::loStrata) {
                midTopL = (cMix * g1LoL + sMix * g2LoL) + g1HiL;
                midTopR = (cMix * g1LoR + sMix * g2LoR) + g1HiR;
            } else {
                midTopL = cMix * g1L + sMix * g2L;
                midTopR = cMix * g1R + sMix * g2R;
            }

            // Stereo width strictly above subSplit (sub band stays 100% mono!)
            const double midSig = 0.5 * (midTopL + midTopR);
            const double sideClean = postSideHp.process(cSubSplit, 0.5 * (midTopL - midTopR)) * (sWidth * 1.45);
            double outL = (midSig + sideClean + subClean) * ampEnv * velocityGain * sOutGain;
            double outR = (midSig - sideClean + subClean) * ampEnv * velocityGain * sOutGain;

            limiter.process(outL, outR, sCeiling, sLimit);
            minLimGain = std::min(minLimGain, limiter.gain());

            const double clipKnee = sLimit * std::min(sCeiling, 0.98) + (1.0 - sLimit) * 2.0;
            const double clipCeil = sLimit * std::min(std::max(sCeiling * 1.02, 0.99), 1.0) + (1.0 - sLimit) * 4.0;
            hba::dsp::softClipLinked(outL, outR, clipKnee, clipCeil);

            left[n] = static_cast<float>(outL);
            right[n] = static_cast<float>(outR);

            blockPeakL = std::max(blockPeakL, std::abs(left[n]));
            blockPeakR = std::max(blockPeakR, std::abs(right[n]));
            blockSubPk = std::max(blockSubPk, static_cast<float>(std::abs(subClean * ampEnv)));
            rmsSum += 0.5 * (outL * outL + outR * outR);

            // Downsampled live oscilloscope capture for 60fps UI waveforms
            if ((sampleIndex % 8u) == 0u) {
                tel.scope[static_cast<std::size_t>(scopeWrite)] = 0.5f * (left[n] + right[n]);
                scopeWrite = (scopeWrite + 1) % TelemetrySnapshot::kScopePoints;
            }

            if (n == numSamples - 1) {
                const float rmsVal = static_cast<float>(std::sqrt(rmsSum / static_cast<double>(numSamples)));
                publishTelemetry(ppq, notePpq, freeSec, noteSec, params, sends, sF1, sF2, sDrive, sTone, sGenMix,
                                 sStrataPos, sStrataSpan, sDisperse, sDensity, sWidth, sSubWarm, curSlot,
                                 ampEnv > 1.0e-3, wantPadNote, blockPeakL, blockPeakR, rmsVal, blockSubPk,
                                 static_cast<float>(minLimGain));
            }
        }
    }

    void publishTelemetry(double ppq, double notePpq, double freeSec, double noteSec, const Params& params,
                          const std::array<float, kLaneCount>& sends, double sF1, double sF2, double sDrive,
                          double sTone, double sGenMix, double sStrataPos, double sStrataSpan, double sDisperse,
                          double sDensity, double sWidth, double sSubWarm, int curSlot, bool isSounding, bool isPadOn,
                          float pkL, float pkR, float rmsVal, float subPk, float limGain) noexcept LN_NONBLOCKING {
        tel.peakL = pkL;
        tel.peakR = pkR;
        tel.rms = rmsVal;
        tel.subPeak = subPk;
        tel.limiterGain = limGain;
        tel.effFilt1Hz = static_cast<float>(sF1);
        tel.effFilt2Hz = static_cast<float>(sF2);
        tel.effDrive = static_cast<float>(sDrive);
        tel.effTone = static_cast<float>(sTone);
        tel.effGenMix = static_cast<float>(sGenMix);
        tel.effStrataPos = static_cast<float>(sStrataPos);
        tel.effStrataSpan = static_cast<float>(sStrataSpan);
        tel.effDisperse = static_cast<float>(sDisperse);
        tel.effDensity = static_cast<float>(sDensity);
        tel.effWidth = static_cast<float>(sWidth);
        tel.effSubWarmth = static_cast<float>(sSubWarm);

        tel.activeNote = activeNote;
        tel.sounding = isSounding;
        tel.padActive = isPadOn;
        tel.ppq = ppq;
        tel.notePpq = notePpq;
        tel.freeSec = freeSec;
        tel.noteSec = noteSec;

        for (int l = 0; l < kLaneCount; ++l) {
            const auto li = static_cast<std::size_t>(l);
            tel.lanePhase[li] = static_cast<float>(lanePhaseFor(params, l, ppq, notePpq, freeSec, noteSec));
            tel.laneValue[li] = sends[li];
        }
        for (int v = 0; v < kNumVoices; ++v)
            tel.voicePhases[static_cast<std::size_t>(v)] = static_cast<float>(voicePhase[static_cast<std::size_t>(v)]);

        if (curSlot >= 0 && printSlots[static_cast<std::size_t>(curSlot)].length > 64) {
            const auto& slot = printSlots[static_cast<std::size_t>(curSlot)];
            tel.hasPrint = true;
            tel.generation = slot.generation;
            const double totalLen = static_cast<double>(slot.length);
            const double winLen = std::max(256.0, totalLen * sStrataSpan);
            const double maxStart = std::max(0.0, totalLen - winLen);
            const double winStart = sStrataPos * maxStart;
            tel.loopStartNorm = static_cast<float>(winStart / totalLen);
            tel.loopEndNorm = static_cast<float>(std::min(1.0, (winStart + winLen) / totalLen));
            tel.playheadNorm = static_cast<float>(std::clamp((winStart + strataCursor) / totalLen, 0.0, 1.0));
        } else {
            tel.hasPrint = false;
            tel.generation = 0;
            tel.loopStartNorm = 0.0f;
            tel.loopEndNorm = 1.0f;
            tel.playheadNorm = 0.0f;
        }
    }
};

Engine::Engine() : impl_(std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::prepare(double sampleRate, int maxBlockSize, int oversamplingFactor) {
    impl_->prepare(sampleRate, maxBlockSize, oversamplingFactor);
}
void Engine::reset() noexcept LN_NONBLOCKING { impl_->reset(); }
void Engine::noteOn(int midiNote, float velocity) noexcept LN_NONBLOCKING { impl_->noteOn(midiNote, velocity); }
void Engine::noteOff(int midiNote) noexcept LN_NONBLOCKING { impl_->noteOff(midiNote); }
void Engine::pitchBend(float semitones) noexcept LN_NONBLOCKING { impl_->pitchBendSt = semitones; }
void Engine::setPitchBendRange(float semitones) noexcept LN_NONBLOCKING {
    impl_->pitchBendRange = std::clamp(static_cast<double>(semitones), 1.0, 48.0);
}
void Engine::setModWheel(float norm01) noexcept LN_NONBLOCKING {
    impl_->modWheel = std::clamp(static_cast<double>(norm01), 0.0, 1.0);
}
void Engine::setTimbre(float norm01) noexcept LN_NONBLOCKING {
    impl_->timbreCtrl = std::clamp(static_cast<double>(norm01), 0.0, 1.0);
}
void Engine::setAftertouch(float norm01) noexcept LN_NONBLOCKING {
    impl_->aftertouch = std::clamp(static_cast<double>(norm01), 0.0, 1.0);
}
void Engine::sustain(bool down) noexcept LN_NONBLOCKING { impl_->sustain(down); }
void Engine::allNotesOff() noexcept LN_NONBLOCKING { impl_->allNotesOff(); }
void Engine::setLaneCurves(const LaneCurves& curves) noexcept LN_NONBLOCKING { impl_->laneCurves = curves; }
void Engine::publishPrint(const PrintBuffer& buffer) { impl_->publishPrint(buffer); }
void Engine::clearPrint() noexcept LN_NONBLOCKING { impl_->clearPrint(); }

void Engine::process(const Params& params, const Transport& transport, float* left, float* right,
                     int numSamples) noexcept LN_NONBLOCKING {
    impl_->process(params, transport, left, right, numSamples);
}

int Engine::latencySamples() const noexcept LN_NONBLOCKING {
    return impl_->limiter.latency() + impl_->osDown[0].latencySamples();
}
int Engine::oversamplingFactor() const noexcept LN_NONBLOCKING { return impl_->osFactor; }
const TelemetrySnapshot& Engine::telemetry() const noexcept LN_NONBLOCKING { return impl_->tel; }

PrintBuffer Engine::renderPrint(const Params& params, const LaneCurves& curves, double sampleRate, double bpm,
                                int rootMidiNote, const PrintBuffer* prevPrint, bool stackGeneration) {
    PrintBuffer out;
    const double sr = std::clamp(sampleRate, 22050.0, 96000.0);
    const double safeBpm = std::clamp(bpm, 40.0, 300.0);
    const int barIdx = std::clamp(params.choice(ParamId::printBars), 0, 2);
    const int bars = kPrintBarCounts[static_cast<std::size_t>(barIdx)];
    const double quarters = static_cast<double>(bars) * 4.0;
    const double seconds = quarters * (60.0 / safeBpm);
    const int targetSamples = std::clamp(static_cast<int>(std::lround(seconds * sr)), 4096, PrintBuffer::kMaxPrintSamples);

    Engine baker;
    baker.prepare(sr, 512, 8); // High-precision 8x oversampled bake!
    baker.setLaneCurves(curves);

    Params bakeParams = params;
    // Strip clean mono sub from the baked strata so live playback always re-injects true key-tracked sub!
    bakeParams[ParamId::subLevel] = 0.0f;
    bakeParams[ParamId::outputGain] = 0.0f;
    bakeParams[ParamId::padTouch] = 0.0f;
    bakeParams[ParamId::padHold] = 0.0f;

    int nextGen = 2;
    if (stackGeneration && prevPrint != nullptr && prevPrint->valid()) {
        baker.publishPrint(*prevPrint);
        bakeParams[ParamId::genMix] = 1.0f;
        nextGen = prevPrint->generation + 1;
    } else {
        bakeParams[ParamId::genMix] = 0.0f;
        nextGen = 2;
    }

    // Settle envelope & smoothers briefly before capturing
    baker.noteOn(rootMidiNote, 0.95f);
    std::vector<float> warmL(512, 0.0f), warmR(512, 0.0f);
    const int warmBlocks = std::max(2, static_cast<int>((0.04 * sr) / 512.0));
    for (int b = 0; b < warmBlocks; ++b)
        baker.process(bakeParams, {safeBpm, 0.0, false}, warmL.data(), warmR.data(), 512);

    out.left.assign(static_cast<std::size_t>(targetSamples), 0.0f);
    out.right.assign(static_cast<std::size_t>(targetSamples), 0.0f);
    const double qPerSample = safeBpm / (60.0 * sr);
    int pos = 0;
    while (pos < targetSamples) {
        const int chunk = std::min(512, targetSamples - pos);
        const double ppq = static_cast<double>(pos) * qPerSample;
        baker.process(bakeParams, {safeBpm, ppq, true}, out.left.data() + pos, out.right.data() + pos, chunk);
        pos += chunk;
    }

    // Peak-normalize to -2.0 dBFS (~0.794) so Gen-2 matches Live Gen-1 loudness smoothly
    float pk = 0.0f;
    for (int i = 0; i < targetSamples; ++i) {
        const auto ii = static_cast<std::size_t>(i);
        pk = std::max(pk, std::max(std::abs(out.left[ii]), std::abs(out.right[ii])));
    }
    if (pk > 1.0e-4f) {
        const float normGain = std::min(6.0f, 0.76f / pk);
        for (int i = 0; i < targetSamples; ++i) {
            const auto ii = static_cast<std::size_t>(i);
            out.left[ii] *= normGain;
            out.right[ii] *= normGain;
        }
    }

    out.length = targetSamples;
    out.sampleRate = sr;
    out.rootMidiNote = rootMidiNote;
    out.generation = nextGen;
    out.label = "GEN " + std::to_string(nextGen) + " \xc2\xb7 " + hba::core::midiNoteName(rootMidiNote, false) +
                " \xc2\xb7 " + std::to_string(bars) + "b";
    out.computePeaks();
    return out;
}

void Engine::renderOffline(const Params& params, const LaneCurves& curves, double sampleRate, double bpm, int midiNote,
                           int bars, const PrintBuffer* printBuf, std::vector<float>& outLeft,
                           std::vector<float>& outRight) {
    const double sr = std::clamp(sampleRate, 22050.0, 96000.0);
    const double safeBpm = std::clamp(bpm, 40.0, 300.0);
    const double seconds = static_cast<double>(std::clamp(bars, 1, 8)) * 4.0 * (60.0 / safeBpm);
    const int total = std::max(512, static_cast<int>(std::lround(seconds * sr)));

    Engine eng;
    eng.prepare(sr, 512, 8);
    eng.setLaneCurves(curves);
    if (printBuf != nullptr && printBuf->valid())
        eng.publishPrint(*printBuf);

    Params p = params;
    p[ParamId::padTouch] = 0.0f;
    p[ParamId::padHold] = 0.0f;

    outLeft.assign(static_cast<std::size_t>(total), 0.0f);
    outRight.assign(static_cast<std::size_t>(total), 0.0f);
    eng.noteOn(midiNote, 0.95f);
    const int releaseAt = std::max(512, total - static_cast<int>(0.08 * sr));
    const double qPerSample = safeBpm / (60.0 * sr);
    bool released = false;

    int pos = 0;
    while (pos < total) {
        const int chunk = std::min(512, total - pos);
        if (!released && pos >= releaseAt) {
            eng.noteOff(midiNote);
            released = true;
        }
        eng.process(p, {safeBpm, static_cast<double>(pos) * qPerSample, true}, outLeft.data() + pos,
                    outRight.data() + pos, chunk);
        pos += chunk;
    }
}

} // namespace ln
