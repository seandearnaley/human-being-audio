// Pole Field DSP engine. Pure C++20, no JUCE, no threads, no I/O.
// Engine::process is [[clang::nonblocking]] and verified at compile time.
#pragma once

#include "pf/cubes.h"
#include "pf/lanes.h"
#include "pf/nonblocking.h"
#include "pf/params.h"

#include <array>
#include <memory>

namespace pf {

struct Transport {
    double bpm = 172.0;
    double ppq = 0.0;
    bool playing = false;
};

class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Allocates all delay/oversampling buffers. Call outside the audio thread.
    void prepare(double sampleRate, int maxBlockSize, int oversamplingFactor = 4);
    void reset() noexcept PF_NONBLOCKING;

    // Real-time safe MIDI pitch tracking & expression hooks (last-note priority, F1 = 0 st).
    void noteOn(int midiNote) noexcept PF_NONBLOCKING;
    void noteOff(int midiNote) noexcept PF_NONBLOCKING;
    void pitchBend(float semitones) noexcept PF_NONBLOCKING;
    void setModWheel(float norm01) noexcept PF_NONBLOCKING;
    void setTimbre(float norm01) noexcept PF_NONBLOCKING;
    void setAftertouch(float norm01) noexcept PF_NONBLOCKING;
    void allNotesOff() noexcept PF_NONBLOCKING;

    void setLaneCurves(const LaneCurves& curves) noexcept PF_NONBLOCKING;
    void setImprint(const ImprintData& imprint) noexcept PF_NONBLOCKING;

    // Processes `numSamples` of stereo audio in-place, with optional stereo external sidechain.
    void process(const Params& params, const Transport& transport, float* left, float* right, int numSamples,
                 const float* scLeft = nullptr, const float* scRight = nullptr) noexcept PF_NONBLOCKING;

    int latencySamples() const noexcept PF_NONBLOCKING;
    int oversamplingFactor() const noexcept PF_NONBLOCKING;
    float gainReductionDb() const noexcept PF_NONBLOCKING;
    double lanePpq() const noexcept PF_NONBLOCKING;
    std::array<float, kLaneCount> lanePhases() const noexcept PF_NONBLOCKING;
    std::array<float, kLaneCount> laneValues() const noexcept PF_NONBLOCKING;

    // Effective modulated values at the end of the last processed block, so the
    // UI draws live moving poles, zeros, envelope follower, and transfer curve.
    struct EffectiveState {
        float morphX = 0.36f;
        float morphY = 0.28f;
        float morphZ = 0.18f;
        float freq = 0.0f;
        float res = 0.62f;
        float drive = 0.34f;
        float transform = 0.0f;
        float spread = 0.22f;
        float fmAmount = 0.0f;
        float envLevel = 0.0f;
        float midiShiftSt = 0.0f;
        int activeNote = -1;
    };
    EffectiveState effectiveState() const noexcept PF_NONBLOCKING;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
