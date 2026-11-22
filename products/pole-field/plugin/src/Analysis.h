#pragma once

#include "pf/engine.h"
#include "pf/ui/model.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <vector>

namespace pf {

class Analysis {
public:
    Analysis();

    void push(const float* left, const float* right, int numSamples) noexcept;

    struct Status {
        float gainReductionDb = 0.0f;
        double ppq = 0.0;
        std::array<float, kLaneCount> lanePhase {};
        std::array<float, kLaneCount> laneValue {};
        Engine::EffectiveState effective {};
    };
    void setStatus(const Status& status) noexcept;
    void setSampleRate(double sampleRate) noexcept { sampleRate_.store(sampleRate, std::memory_order_relaxed); }

    void update(ui::Visuals& visuals, float secondsSinceLastUpdate);

private:
    enum Channel { left, right, numChannels };
    static constexpr int kQueueSize = 1 << 15;
    static constexpr int kHistorySize = 1 << 14;
    static constexpr int kFftOrder = 12;
    static constexpr int kFftSize = 1 << kFftOrder;

    void drainQueue(float& peakLeft, float& peakRight);
    float history(Channel channel, int samplesAgo) const;
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
    std::atomic<float> gainReductionDb_ { 0.0f };
    std::atomic<double> ppq_ { 0.0 };
    std::array<std::atomic<float>, kLaneCount> lanePhase_ {};
    std::array<std::atomic<float>, kLaneCount> laneValue_ {};

    std::atomic<float> effMorphX_ { 0.36f };
    std::atomic<float> effMorphY_ { 0.28f };
    std::atomic<float> effMorphZ_ { 0.18f };
    std::atomic<float> effFreq_ { 0.0f };
    std::atomic<float> effRes_ { 0.62f };
    std::atomic<float> effTransform_ { 0.0f };
    std::atomic<float> effSpread_ { 0.22f };
    std::atomic<float> midiShiftSt_ { 0.0f };
    std::atomic<int> activeNote_ { -1 };
};

}
