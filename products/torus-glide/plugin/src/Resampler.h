// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "tg/export.h"
#include "tg/ui/model.h"

#include <juce_core/juce_core.h>

#include <atomic>

namespace tg {

class Resampler final : private juce::Thread {
  public:
    Resampler();
    ~Resampler() override;

    static juce::File directory();

    ui::ResampleState state() const noexcept { return state_.load(std::memory_order_acquire); }
    void start(const RenderRequest& request, const juce::String& presetName);
    void invalidate() noexcept;
    juce::File file() const;

  private:
    void run() override;

    mutable juce::CriticalSection lock_;
    RenderRequest request_{};
    juce::File target_{};
    std::atomic<std::uint64_t> generation_{0};
    std::atomic<std::uint64_t> activeGen_{0};
    std::atomic<ui::ResampleState> state_{ui::ResampleState::idle};
};

} // namespace tg
