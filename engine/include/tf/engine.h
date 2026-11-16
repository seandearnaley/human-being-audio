// The Tract Form engine: one monophonic voice, its oversampled growl path, the
// phase-locked sub and the output stage. Plain C++, no JUCE.
//
// Threading: prepare() allocates and must not run concurrently with anything
// else. Every other member is real-time safe and is meant to be called from the
// audio thread only; process() and the note methods are compiler-checked
// non-blocking (see tf/nonblocking.h).
#pragma once

#include "tf/nonblocking.h"
#include "tf/params.h"

#include <array>
#include <memory>

namespace tf {

struct Transport {
    double bpm = 174.0;
    double ppq = 0.0;
    bool playing = false;
};

// Optional per-block taps for the interface, base rate, time-aligned with the output.
// Either pointer may be null. Both are taken before the amp gate.
struct Taps {
    float* master = nullptr;  // the sub sine before its low-pass
    float* growl = nullptr;   // the mono growl path just before it is summed with the sub
};

class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Allocates. Not real-time safe. `oversampling` is 2, 4, 8 or 16 (1 is
    // accepted too). At host rates >= 88.2 kHz the factor is halved, and halved
    // again at >= 176.4 kHz, but never below 2 when 2 or more was requested.
    void prepare(double sampleRate, int maxBlockSize, int oversampling);
    void reset() noexcept TF_NONBLOCKING;
    int latencySamples() const noexcept TF_NONBLOCKING;
    int oversamplingFactor() const noexcept TF_NONBLOCKING;  // the factor in use

    void noteOn(int midiNote) noexcept TF_NONBLOCKING;
    void noteOff(int midiNote) noexcept TF_NONBLOCKING;
    void allNotesOff() noexcept TF_NONBLOCKING;
    void pitchBend(float semitones) noexcept TF_NONBLOCKING;

    // `transport.ppq` is the position of the block's first sample.
    void process(const Params& params, const Transport& transport,
                 float* left, float* right, int numSamples, Taps taps = {}) noexcept TF_NONBLOCKING;

    // State of the last processed sample, for the interface.
    float currentHz() const noexcept TF_NONBLOCKING;
    bool gateOpen() const noexcept TF_NONBLOCKING;
    // Look-ahead limiter gain reduction, >= 0. It acts on the growl (the sub is
    // only limited if it alone exceeds the ceiling); this is the larger of the two.
    float gainReductionDb() const noexcept TF_NONBLOCKING;
    // How hard the growl clipper worked over the last block: largest and mean
    // gain reduction in dB (>= 0).
    float growlClipDb() const noexcept TF_NONBLOCKING;
    float growlClipMeanDb() const noexcept TF_NONBLOCKING;
    double lanePpq() const noexcept TF_NONBLOCKING;  // the lane clock in quarter notes
    std::array<float, kLaneCount> lanePhases() const noexcept TF_NONBLOCKING;  // 0..1
    std::array<float, kLaneCount> laneValues() const noexcept TF_NONBLOCKING;  // -1..1, after depth and motion

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
