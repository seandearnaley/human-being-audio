// SPDX-License-Identifier: AGPL-3.0-or-later
// Datum DSP engine: Nord-philosophy pure kick synthesis (Sine, Twin-T, Schroeder
// Harmonic, Berger Membrane + Helmholtz Cavity), phase-native 3-band sculpting,
// Orchard PhaseRotator, Sub-Link live sidechain bass alignment, and A/B/C/D composer.
// Engine::process is strictly [[clang::nonblocking]] and zero-allocation.
#pragma once

#include "dt/nonblocking.h"
#include "dt/params.h"
#include "dt/pattern.h"

#include <hba/dsp/envelope_curve.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dt {

struct Transport {
    double bpm = 172.0;
    double ppq = 0.0;
    bool playing = false;
};

struct ClickSample {
    static constexpr int kMaxSamples = 48000;
    std::vector<float> mono;
    double sampleRate = 48000.0;
    int offsetSamples = 0; // auto-aligned onset offset
    float polarity = 1.0f; // +1 or -1
    std::string name;

    bool valid() const noexcept { return !mono.empty(); }
};

struct PreviewSnapshot {
    std::array<float, kScopePoints> subWave{};
    std::array<float, kScopePoints> bodyWave{};
    std::array<float, kScopePoints> clickWave{};
    std::array<float, kScopePoints> sumWave{};
    std::array<float, kScopePoints> pitchNorm{};
    float effectiveLengthMs = 145.0f;
    float truePeakDb = -60.0f;
    float rmsDb = -60.0f;
    float crestDb = 0.0f;
    float dcPercent = 0.0f;
    float zeroCrossNorm = 1.0f;
};

struct TelemetrySnapshot {
    float peakL = 0.0f;
    float peakR = 0.0f;
    float truePeakDb = -60.0f;
    float rmsDb = -60.0f;
    float crestDb = 0.0f;
    float dcPercent = 0.0f;
    float limiterGrDb = 0.0f;

    // Live Sub-Link sidechain bass alignment telemetry
    bool scActive = false;
    float scCorrelation = 0.0f;
    float scPeakDeltaDb = 0.0f;
    float scRootHz = 0.0f;

    int activeVoices = 0;
    int lastSlot = 0;
    int lastMidiNote = 36;
    int currentSeqStep = -1;
    bool sounding = false;

    std::array<float, kScopePoints> kickScope{};
    std::array<float, kScopePoints> scScope{};
    std::array<float, kScopePoints> sumScope{};
};

class Engine {
  public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void prepare(double sampleRate, int maxBlockSize, int oversamplingFactor = 4);
    void reset() noexcept DT_NONBLOCKING;

    void triggerMidi(int midiNote, float velocity = 0.95f) noexcept DT_NONBLOCKING;
    void triggerSlot(int slotIndex, float velocity = 0.95f) noexcept DT_NONBLOCKING;
    void noteOff(int midiNote) noexcept DT_NONBLOCKING;
    void allNotesOff() noexcept DT_NONBLOCKING;

    void setCurves(const hba::dsp::EnvelopeCurve& pitchCurve,
                   const hba::dsp::EnvelopeCurve& ampCurve) noexcept DT_NONBLOCKING;
    void setPattern(const ComposePattern& pattern) noexcept DT_NONBLOCKING;

    // Called from message/worker thread to publish or clear an optional drop-in click WAV.
    void publishClickSample(const ClickSample& sample);
    void clearClickSample() noexcept DT_NONBLOCKING;

    // Real-time audio path: zero allocations, lock-free, syscall-free.
    void process(const Params& params, const Transport& transport, float* left, float* right, int numSamples,
                 const float* sidechainL = nullptr, const float* sidechainR = nullptr) noexcept DT_NONBLOCKING;

    int latencySamples() const noexcept DT_NONBLOCKING;
    int oversamplingFactor() const noexcept DT_NONBLOCKING;
    const TelemetrySnapshot& telemetry() const noexcept DT_NONBLOCKING;

    // Deterministic single-shot preview renderer for instant 60 FPS UI metrology.
    static PreviewSnapshot renderPreview(const Params& params, const hba::dsp::EnvelopeCurve& pitchCurve,
                                         const hba::dsp::EnvelopeCurve& ampCurve, const ComposePattern& pattern,
                                         const ClickSample* click = nullptr, int slotIndex = 0,
                                         double sampleRate = 48000.0);

    // Sub-Link solvers: find optimal startPhase (0..360 deg) aligning kick to captured sidechain wave.
    static float solveSubLinkPhase(const Params& params, const hba::dsp::EnvelopeCurve& pitchCurve,
                                   const hba::dsp::EnvelopeCurve& ampCurve, const ComposePattern& pattern,
                                   const std::array<float, kScopePoints>& sidechainScope, float* outBestCorr = nullptr);

    // Auto-align an external click sample against the core's first peak cycle.
    static void autoAlignClick(const Params& params, const hba::dsp::EnvelopeCurve& pitchCurve,
                               const hba::dsp::EnvelopeCurve& ampCurve, ClickSample& sample);

    // Offline high-precision WAV renderer (one-shot at sample 0 OR full context pattern loop).
    static void renderOffline(const Params& params, const hba::dsp::EnvelopeCurve& pitchCurve,
                              const hba::dsp::EnvelopeCurve& ampCurve, const ComposePattern& pattern,
                              const ClickSample* click, double sampleRate, double bpm, bool renderPatternLoop,
                              int slotIndex, std::vector<float>& outLeft, std::vector<float>& outRight);

    // Export current ComposePattern as 960-PPQ Type-0 Standard MIDI File bytes.
    static std::vector<std::uint8_t> encodePatternMidi(const Params& params, const ComposePattern& pattern, double bpm);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace dt
