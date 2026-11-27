#pragma once

#include "pf/engine.h"
#include "pf/ui/model.h"

#include <hba/host/stereo_analysis.h>

#include <array>
#include <atomic>
#include <vector>

namespace pf {

class Analysis {
  public:
    Analysis();

    void push(const float* leftOut, const float* rightOut, int numSamples) noexcept;
    void push(const float* preInMono, const float* leftOut, const float* rightOut, int numSamples) noexcept;

    struct Status {
        float gainReductionDb = 0.0f;
        double ppq = 0.0;
        double notePpq = 0.0;
        double freeSec = 0.0;
        double noteSec = 0.0;
        int osFactor = 4;
        std::array<float, kLaneCount> lanePhase{};
        std::array<float, kLaneCount> laneValue{};
        Engine::EffectiveState effective{};
    };
    void setStatus(const Status& status) noexcept;
    void setSampleRate(double sampleRate) noexcept { sampleRate_.store(sampleRate, std::memory_order_relaxed); }

    void update(ui::Visuals& visuals, float secondsSinceLastUpdate);

    // Copies the most recent ~0.68 s of pre-filter mono input audio for 1-click Live Capture Imprint.
    void snapshotInput(std::vector<float>& destination, double& outSampleRate);

  private:
    enum Channel { left, right, preIn, numChannels };
    static constexpr int kQueueSize = 1 << 15;
    static constexpr int kHistorySize = 1 << 15;
    static constexpr int kFftOrder = 12;

    hba::host::StereoAnalyzerBase<numChannels, kQueueSize, kHistorySize, kFftOrder, ui::Visuals::kSpectrumBands> core_;
    std::array<float, ui::Visuals::kEnvHistoryPoints> envHistory_{};

    std::atomic<double> sampleRate_{48000.0};
    std::atomic<int> osFactor_{4};
    std::atomic<float> gainReductionDb_{0.0f};
    std::atomic<double> ppq_{0.0};
    std::atomic<double> notePpq_{0.0};
    std::atomic<double> freeSec_{0.0};
    std::atomic<double> noteSec_{0.0};
    std::array<std::atomic<float>, kLaneCount> lanePhase_{};
    std::array<std::atomic<float>, kLaneCount> laneValue_{};

    std::atomic<float> effMorphX_{0.36f};
    std::atomic<float> effMorphY_{0.28f};
    std::atomic<float> effMorphZ_{0.18f};
    std::atomic<float> effFreq_{0.0f};
    std::atomic<float> effRes_{0.62f};
    std::atomic<float> effDrive_{0.34f};
    std::atomic<float> effTransform_{0.0f};
    std::atomic<float> effSpread_{0.22f};
    std::atomic<float> effFmAmount_{0.0f};
    std::atomic<float> envLevel_{0.0f};
    std::atomic<float> midiShiftSt_{0.0f};
    std::atomic<int> activeNote_{-1};
};

} // namespace pf
