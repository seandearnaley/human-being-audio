// Turns audio into pictures. The audio thread pushes samples into a lock-free
// queue and never waits; the interface thread pops them and does the maths.
#pragma once

#include "tf/engine.h"
#include "tf/ui/model.h"

#include <hba/host/stereo_analysis.h>

#include <array>
#include <atomic>

namespace tf {

class Analysis {
  public:
    Analysis() = default;

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
        double notePpq = 0.0;
        double freeSec = 0.0;
        double noteSec = 0.0;
        std::array<float, kLaneCount> lanePhase{};
        std::array<float, kLaneCount> laneValue{};
        Engine::EffectiveState effective{};
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

    void updateScope(ui::Visuals& visuals, double sampleRate, float hz) const;

    hba::host::StereoAnalyzerBase<numChannels, kQueueSize, kHistorySize, kFftOrder, ui::Visuals::kSpectrumBands> core_;

    std::atomic<double> sampleRate_{48000.0};
    std::atomic<float> hz_{0.0f};
    std::atomic<int> note_{-1};
    std::atomic<bool> gate_{false};
    std::atomic<float> gainReductionDb_{0.0f};
    std::atomic<double> ppq_{0.0};
    std::atomic<double> notePpq_{0.0};
    std::atomic<double> freeSec_{0.0};
    std::atomic<double> noteSec_{0.0};
    std::array<std::atomic<float>, kLaneCount> lanePhase_{};
    std::array<std::atomic<float>, kLaneCount> laneValue_{};
    std::atomic<float> effCoupling_{0.72f};
    std::atomic<float> effBeat_{0.38f};
    std::atomic<float> effReach_{96.0f};
    std::atomic<float> effMorph_{0.63f};
    std::atomic<float> effShift_{0.0f};
    std::atomic<float> effDrive_{0.42f};
    std::atomic<float> effSculpt_{6.0f};
    std::atomic<float> effWidth_{0.64f};
    std::atomic<bool> effActive_{false};
};

} // namespace tf
