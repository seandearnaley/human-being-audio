// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "sb/micro_room.h"
#include "sb/modal_models.h"
#include "sb/params.h"

#include <hba/dsp/adaa.h>
#include <hba/dsp/allpass.h>
#include <hba/dsp/control.h>
#include <hba/dsp/crossover.h>
#include <hba/dsp/limiter.h>
#include <hba/dsp/nonblocking.h>
#include <hba/dsp/oversampling.h>
#include <hba/dsp/svf.h>

#include <array>
#include <cstdint>

namespace sb {

inline constexpr int kMaxVoices = 4;
inline constexpr int kDisperserPairs = 8; // 8 matched pairs = 16 flat-magnitude 1st-order allpass stages!

struct TelemetrySnapshot {
    // 40 ms Latency-Compensated Transient Microscope curves (256 samples across 0..40 ms)
    std::array<float, kScopeSamples> bodyWave{};
    std::array<float, kScopeSamples> shellWave{};
    std::array<float, kScopeSamples> wireWave{};
    std::array<float, kScopeSamples> sumWave{};
    float scopePeak = 1.0f;

    // Live 24-mode instantaneous amplitudes & normalized ratios for Chladni-ring visualizer
    std::array<float, kModeCount> modeEnergy{};
    std::array<float, kModeCount> modeRadial{};
    std::array<int, kModeCount> modeAngular{};

    float peakL = 0.0f;
    float peakR = 0.0f;
    float peakDb = -60.0f;
    float crestDb = 0.0f;
    float phaseLockPct = 100.0f; // 0..100% constructive transient summation lock
    float gainReductionDb = 0.0f;
    float lastFundamentalHz = 185.0f;
    float lastVelocity = 1.0f;
    int lastArticulation = 0;
    std::uint32_t triggerCount = 0;
    bool active = false;
};

struct AlignResult {
    float bodyPhaseDeg = 90.0f;
    float shellOffsetMs = 0.0f;
    bool shellInvert = false;
    float wireOffsetMs = 0.0f;
    bool wireInvert = false;
    float beforeLockPct = 0.0f;
    float afterLockPct = 0.0f;
};

class Engine {
  public:
    Engine() = default;

    // Allocates / prepares buffers. Call off the audio thread.
    void prepare(double sampleRate, int maxBlockSize, int oversamplingFactor = 4);
    void reset() noexcept HBA_NONBLOCKING;
    int latencySamples() const noexcept HBA_NONBLOCKING;

    // Trigger via explicit articulation (from UI pads or tests) or MIDI note.
    void trigger(Articulation art, float velocity = 1.0f, float pitchRatio = 1.0f) noexcept HBA_NONBLOCKING;
    void noteOn(int midiNote, float velocity, const Params& params) noexcept HBA_NONBLOCKING;
    void noteOff(int midiNote) noexcept HBA_NONBLOCKING;
    void allNotesOff() noexcept HBA_NONBLOCKING;

    void process(const Params& params, float* outL, float* outR, int numSamples) noexcept HBA_NONBLOCKING;

    const TelemetrySnapshot& telemetry() const noexcept HBA_NONBLOCKING { return telemetry_; }

  private:
    struct ModeOsc {
        double re = 0.0;
        double im = 0.0;
        double baseFreqHz = 200.0;
        double cosW = 1.0;
        double sinW = 0.0;
        double decayCoef = 0.999;
        double gainL = 0.0;
        double gainR = 0.0;
        double excWeight = 0.0;
    };

    using AllpassPair = hba::dsp::AllpassPair;

    struct Voice {
        bool active = false;
        int midiNote = -1;
        Articulation art = Articulation::main;
        double velocity = 1.0;
        double pitchRatio = 1.0;
        double ageSamples = 0.0;
        double fadeOutGain = 1.0;
        double bodyFadeGain = 1.0;
        bool stealing = false;
        bool bodyStealing = false;

        // Layer 1: Fundamental Body
        double bodyPhase = 0.0;
        double fmPhase = 0.0;
        double bodyEnv = 0.0;
        double pitchEnv = 0.0;

        // Layer 2: 24-Mode Nonlinear Tensioned Modal Shell
        std::array<ModeOsc, kModeCount> modes{};
        double shellEnergy = 0.0;
        double malletChirpPhase = 0.0;
        MalletType malletType = MalletType::woodStick;

        // Layer 3: Transient-Ducked Wire & Click
        std::uint32_t rngL = 0x12345678u;
        std::uint32_t rngR = 0x9e3779b9u;
        double wireEnv = 0.0;
        double clickEnv = 0.0;
        double duckEnv = 0.0;
        double crushHoldL = 0.0;
        double crushHoldR = 0.0;
        double crushPhase = 0.0;
        hba::dsp::SvfState wireBpL{}, wireBpR{}, wireHpL{}, wireHpR{}, clickBp{}, malletLp{};

        // Per-voice deterministic variation offsets (0 when humanize == 0)
        double humPitchMult = 1.0;
        double humDecayMult = 1.0;
        double humStrikeOff = 0.0;
    };

    void startVoice(Voice& v, Articulation art, double vel, double pitchRatio, int midiNote,
                    const Params& params) noexcept HBA_NONBLOCKING;
    double nextNoise(std::uint32_t& state) noexcept HBA_NONBLOCKING;

    double sampleRate_ = 48000.0;
    int maxBlock_ = 512;
    int oversamplingFactor_ = 4;
    std::uint32_t triggerSeq_ = 0;

    std::array<Voice, kMaxVoices> voices_{};
    bool havePendingParams_ = false;
    Params cachedParams_ = Params::defaults();

    // Alignment micro-delays (stereo L/R for Body, Shell, Wire) + smoothers
    DelayBuf delBodyL_{}, delBodyR_{};
    DelayBuf delShellL_{}, delShellR_{};
    DelayBuf delWireL_{}, delWireR_{};
    hba::dsp::Smoother bodyDelSmooth_{};
    hba::dsp::Smoother shellDelSmooth_{};
    hba::dsp::Smoother wireDelSmooth_{};

    // Latency-matching scope delay lines so BODY/SHELL/WIRE align sample-for-sample with SUM!
    DelayBuf scopeLatBody_{}, scopeLatShell_{}, scopeLatWire_{};

    // 16-stage (8 matched pairs) 100% Series Flat-Magnitude Allpass Disperser (L/R)
    std::array<AllpassPair, kDisperserPairs> dispL_{};
    std::array<AllpassPair, kDisperserPairs> dispR_{};

    // Tone Scoop (380 Hz bell + 6.2 kHz shelf) & Stereo Micro-Room
    Biquad scoopBellL_{}, scoopBellR_{};
    Biquad scoopShelfL_{}, scoopShelfR_{};
    MicroRoom room_{};

    // Shared Studio Oversampler + ADAA2 + Sub-Mono LR4 + DC + Limiter
    hba::dsp::Oversampler oversamplerL_{};
    hba::dsp::Oversampler oversamplerR_{};
    hba::dsp::MultiShaperAdaa2 shaperL_{};
    hba::dsp::MultiShaperAdaa2 shaperR_{};
    hba::dsp::Lr4Split subSplitL_{};
    hba::dsp::Lr4Split subSplitR_{};
    hba::dsp::DcBlocker dcL_{};
    hba::dsp::DcBlocker dcR_{};
    hba::dsp::Limiter limiter_{};

    hba::dsp::Smoother outGainSmooth_{};
    hba::dsp::Smoother driveSmooth_{};

    // 40 ms Transient Microscope capture state
    bool capturingScope_ = false;
    int scopeCaptureSample_ = 0;
    int scopeTotalSamples_ = 1920; // 40 ms at 48 kHz
    double scopeRmsSum_ = 0.0;
    double scopePeakAbs_ = 0.0;
    double lockBandBodySq_ = 0.0;
    double lockBandOtherSq_ = 0.0;
    double lockBandSumSq_ = 0.0;
    double lockLpBody_ = 0.0;
    double lockLpOther_ = 0.0;
    int lockWindowSamples_ = 672; // 14 ms transient lock window
    std::array<float, kScopeSamples> capBody_{};
    std::array<float, kScopeSamples> capShell_{};
    std::array<float, kScopeSamples> capWire_{};
    std::array<float, kScopeSamples> capSum_{};

    TelemetrySnapshot telemetry_{};
};

// Offline Auto Phase-Align solver: searches bodyPhase (22.5 deg steps) x grid-locked [0..+3.2 ms] shell/wire offsets
// x polarity to maximize 0-14 ms low/mid constructive transient summation without delaying Body off the DAW grid!
AlignResult solveAutoAlign(const Params& baseParams, double sampleRate = 48000.0);

} // namespace sb
