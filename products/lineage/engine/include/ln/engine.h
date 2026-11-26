// SPDX-License-Identifier: AGPL-3.0-or-later
// Lineage DSP engine: zero-aliasing Reese/Sync/TZFM core + serial nonlinear sculpting bench +
// lock-free triple-buffered non-destructive multi-generation resampling (Strata).
// Engine::process is strictly [[clang::nonblocking]] and zero-allocation.
#pragma once

#include "ln/lanes.h"
#include "ln/nonblocking.h"
#include "ln/params.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace ln {

struct Transport {
    double bpm = 172.0;
    double ppq = 0.0;
    bool playing = false;
};

struct PrintBuffer {
    static constexpr int kMaxPrintSamples = 192000; // 4.0 s @ 48 kHz
    static constexpr int kWaveformPoints = 192;

    std::vector<float> left;
    std::vector<float> right;
    int length = 0;
    double sampleRate = 48000.0;
    int rootMidiNote = 29; // F1
    int generation = 0;
    std::array<float, kWaveformPoints> peaks{};
    std::string label;

    bool valid() const noexcept {
        return length > 64 && static_cast<int>(left.size()) >= length && static_cast<int>(right.size()) >= length;
    }

    void computePeaks();
};

struct TelemetrySnapshot {
    static constexpr int kScopePoints = 64;

    float peakL = 0.0f;
    float peakR = 0.0f;
    float rms = 0.0f;
    float subPeak = 0.0f;
    float limiterGain = 1.0f;

    float effFilt1Hz = 920.0f;
    float effFilt2Hz = 1850.0f;
    float effDrive = 0.38f;
    float effTone = 0.42f;
    float effGenMix = 0.0f;
    float effStrataPos = 0.0f;
    float effStrataSpan = 1.0f;
    float effDisperse = 0.24f;
    float effDensity = 0.36f;
    float effWidth = 0.68f;
    float effSubWarmth = 0.26f;

    float playheadNorm = 0.0f;
    float loopStartNorm = 0.0f;
    float loopEndNorm = 1.0f;
    int activeNote = -1;
    int activeVoices = 0;
    int generation = 0;
    bool sounding = false;
    bool padActive = false;
    bool hasPrint = false;

    double ppq = 0.0;
    double notePpq = 0.0;
    double freeSec = 0.0;
    double noteSec = 0.0;

    std::array<float, kLaneCount> lanePhase{};
    std::array<float, kLaneCount> laneValue{};
    std::array<float, kScopePoints> scope{};
    std::array<float, 7> voicePhases{};
};

class Engine {
  public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Allocates oversampling/print buffers outside the audio thread.
    void prepare(double sampleRate, int maxBlockSize, int oversamplingFactor = 4);
    void reset() noexcept LN_NONBLOCKING;

    // Real-time safe MIDI & expression hooks (monophonic last-note priority).
    void noteOn(int midiNote, float velocity = 0.9f) noexcept LN_NONBLOCKING;
    void noteOff(int midiNote) noexcept LN_NONBLOCKING;
    void pitchBend(float semitones) noexcept LN_NONBLOCKING;
    void setPitchBendRange(float semitones) noexcept LN_NONBLOCKING;
    void setModWheel(float norm01) noexcept LN_NONBLOCKING;
    void setTimbre(float norm01) noexcept LN_NONBLOCKING;
    void setAftertouch(float norm01) noexcept LN_NONBLOCKING;
    void sustain(bool down) noexcept LN_NONBLOCKING;
    void allNotesOff() noexcept LN_NONBLOCKING;

    void setLaneCurves(const LaneCurves& curves) noexcept LN_NONBLOCKING;

    // Called from a worker/message thread to publish a newly printed or loaded Gen-2 buffer.
    // Swaps into the audio engine lock-free with a click-free crossfade.
    void publishPrint(const PrintBuffer& buffer);
    void clearPrint() noexcept LN_NONBLOCKING;

    void process(const Params& params, const Transport& transport, float* left, float* right,
                 int numSamples) noexcept LN_NONBLOCKING;

    int latencySamples() const noexcept LN_NONBLOCKING;
    int oversamplingFactor() const noexcept LN_NONBLOCKING;
    const TelemetrySnapshot& telemetry() const noexcept LN_NONBLOCKING;

    // Offline non-destructive Gen-2 / Gen-N print baker (called on worker thread or in tests).
    static PrintBuffer renderPrint(const Params& params, const LaneCurves& curves, double sampleRate, double bpm,
                                   int rootMidiNote = 29, const PrintBuffer* prevPrint = nullptr,
                                   bool stackGeneration = false);

    // Offline high-quality WAV export (renders full sub + sculpted/strata output).
    static void renderOffline(const Params& params, const LaneCurves& curves, double sampleRate, double bpm,
                              int midiNote, int bars, const PrintBuffer* printBuf, std::vector<float>& outLeft,
                              std::vector<float>& outRight);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ln
