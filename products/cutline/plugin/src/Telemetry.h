// SPDX-License-Identifier: AGPL-3.0-or-later
// Non-blocking telemetry bridge from the audio callback to the 60 Hz Visage UI.
#pragma once

#include "cl/engine.h"
#include "cl/ui/model.h"

#include <juce_core/juce_core.h>

#include <atomic>

namespace cl {

class Telemetry {
  public:
    Telemetry() {
        Params defaults;
        HeldNotes held;
        compilePhrase(defaults, 0, held, snapshot_.plan);
    }

    // Audio thread: never blocks; skips update if UI is mid-copy.
    void publish(const Engine& engine, double bpm) noexcept {
        lastBpm_.store(bpm, std::memory_order_relaxed);
        const juce::SpinLock::ScopedTryLockType scoped(lock_);
        if (!scoped.isLocked())
            return;
        snapshot_.plan = engine.currentPlan();
        snapshot_.playheadBeats = engine.playheadPhraseBeats();
        snapshot_.ppq = engine.lanePpq();
        snapshot_.lanePhase = engine.lanePhases();
        snapshot_.laneValue = engine.laneValues();
        snapshot_.activeNotesLow = engine.activeNotesLowMask();
        snapshot_.activeNotesHigh = engine.activeNotesHighMask();
        snapshot_.lastBlockEvents = engine.lastBlockEventCount();
        snapshot_.playing = engine.isPlaying();
    }

    // Message thread: copies latest snapshot for rendering.
    void read(ui::Visuals& out) const {
        const juce::SpinLock::ScopedLockType scoped(lock_);
        out = snapshot_;
    }

    double lastBpm() const noexcept { return lastBpm_.load(std::memory_order_relaxed); }

  private:
    mutable juce::SpinLock lock_;
    ui::Visuals snapshot_{};
    std::atomic<double> lastBpm_{172.0};
};

} // namespace cl
