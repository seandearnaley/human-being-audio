// Turns audio into pictures. The audio thread pushes samples into a lock-free
// queue and never waits; the interface thread pops them and does the maths.
#pragma once

#include "tf/ui/model.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <vector>

namespace tf {

class Analysis {
public:
    Analysis();

    // Audio thread. Any pointer may be null (treated as silence). Drops the block
    // when the queue is full, which is what happens while the editor is closed.
    void push(const float* master, const float* growl, const float* left, const float* right, int numSamples) noexcept;

    // Audio thread: state of the engine after the block.
    struct Status {
        float hz = 0.0f;
        int note = -1;
        bool gate = false;
        float gainReductionDb = 0.0f;
        double ppq = 0.0;
        std::array<float, kLaneCount> lanePhase {};
        std::array<float, kLaneCount> laneValue {};
    };
    void setStatus(const Status& status) noexcept;
    void setSampleRate(double sampleRate) noexcept { sampleRate_.store(sampleRate, std::memory_order_relaxed); }

    // Interface thread, once per frame.
    void update(ui::Visuals& visuals, float secondsSinceLastUpdate);

private:
    enum Channel { master, growl, left, right, numChannels };
    static constexpr int kQueueSize = 1 << 15;
    static constexpr int kHistorySize = 1 << 14;
    static constexpr int kFftOrder = 13;
    static constexpr int kFftSize = 1 << kFftOrder;

    void drainQueue(float& peakLeft, float& peakRight);
    float history(Channel channel, int samplesAgo) const;
    void updateScope(ui::Visuals& visuals, double sampleRate, float hz) const;
    void updateStereo(ui::Visuals& visuals) const;
    void updateSpectrum(ui::Visuals& visuals, double sampleRate, float seconds);

    juce::AbstractFifo queue_ { kQueueSize };
    std::array<std::vector<float>, numChannels> queued_;

    std::array<std::vector<float>, numChannels> history_;
    int historyWrite_ = 0;

    juce::dsp::FFT fft_ { kFftOrder };
    juce::dsp::WindowingFunction<float> window_ { static_cast<size_t>(kFftSize),
                                                  juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> fftBuffer_;
    std::array<float, ui::Visuals::kSpectrumBands> spectrum_;
    float peakLeftDb_ = -100.0f;
    float peakRightDb_ = -100.0f;

    std::atomic<double> sampleRate_ { 48000.0 };
    std::atomic<float> hz_ { 0.0f };
    std::atomic<int> note_ { -1 };
    std::atomic<bool> gate_ { false };
    std::atomic<float> gainReductionDb_ { 0.0f };
    std::atomic<double> ppq_ { 0.0 };
    std::array<std::atomic<float>, kLaneCount> lanePhase_ {};
    std::array<std::atomic<float>, kLaneCount> laneValue_ {};
};

}
