// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "sb/export.h"
#include "sb/ui/model.h"

#include <juce_core/juce_core.h>

#include <atomic>

namespace sb {

class Bouncer final : private juce::Thread {
  public:
    Bouncer();
    ~Bouncer() override;

    static juce::File directory();

    ui::BounceState state() const noexcept { return state_.load(std::memory_order_acquire); }
    void start(const OneShotRequest& request, const juce::String& presetName);
    void invalidate() noexcept;
    juce::File file() const;

  private:
    void run() override;

    mutable juce::CriticalSection lock_;
    OneShotRequest request_{};
    juce::File target_{};
    std::atomic<std::uint64_t> generation_{0};
    std::atomic<std::uint64_t> activeGen_{0};
    std::atomic<ui::BounceState> state_{ui::BounceState::idle};
};

} // namespace sb
