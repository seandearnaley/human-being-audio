// Renders the current patch offline at 16x into a float WAV that can be dragged
// into the host. Runs on its own thread; the audio thread is not involved.
#pragma once

#include "tf/render.h"
#include "tf/ui/model.h"

#include <juce_core/juce_core.h>

#include <atomic>

namespace tf {

class Resampler final : private juce::Thread {
public:
    Resampler();
    ~Resampler() override;

    // Ignored while a render is running.
    void start(const RenderRequest& request, const juce::String& presetName);

    ui::ResampleState state() const noexcept { return state_.load(); }
    // The finished file. Empty unless the state is `ready`.
    juce::File file() const;

    static juce::File directory();

private:
    void run() override;

    std::atomic<ui::ResampleState> state_ { ui::ResampleState::idle };
    RenderRequest request_;
    juce::File target_;
    juce::CriticalSection lock_;
};

}
