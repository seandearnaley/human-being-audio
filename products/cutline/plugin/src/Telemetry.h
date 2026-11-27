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
    // Audio thread: never blocks; skips the update if the UI is mid-copy.
    void publish(const Engine& engine, double bpm, bool slicerMode) noexcept {
        lastBpm_.store(bpm, std::memory_order_relaxed);
        const juce::SpinLock::ScopedTryLockType scoped(lock_);
        if (!scoped.isLocked())
            return;
        snapshot_.position = engine.position();
        if (slicerMode)
            snapshot_.slicerPlan = engine.slicerPlan(); // the slicer keeps its plan snapshot
        snapshot_.heldNotes = engine.activeHeldNotes();
        snapshot_.activeScene = engine.activeScene();
        snapshot_.pendingScene = engine.pendingScene();
        snapshot_.activePadProgram = engine.activePadProgram();
        snapshot_.pendingPadProgram = engine.pendingPadProgram();
        snapshot_.padEngaged = engine.padEngaged();
        snapshot_.midiPadMask = engine.midiPadMask();
        snapshot_.capturedClipCount = engine.capturedClipCount();
        snapshot_.bpm = bpm;
        snapshot_.ppq = engine.lanePpq();
        snapshot_.lanePhase = engine.lanePhases();
        snapshot_.laneValue = engine.laneValues();
        snapshot_.laneFlash = engine.laneHitFlashes();
        snapshot_.activeNotesLow = engine.activeNotesLowMask();
        snapshot_.activeNotesHigh = engine.activeNotesHighMask();
        snapshot_.lastBlockEvents = engine.lastBlockEventCount();
        snapshot_.activityMask |= engine.lastActivityMask();
        snapshot_.playing = engine.isPlaying();
    }

    // Message thread: copies latest snapshot for rendering and resets latched activity bits.
    void read(ui::Visuals& out) const {
        const juce::SpinLock::ScopedLockType scoped(lock_);
        out = snapshot_;
        snapshot_.activityMask = 0;
    }

    // Message thread peek without clearing activity bits (used by MIDI exporter).
    void peek(ui::Visuals& out) const {
        const juce::SpinLock::ScopedLockType scoped(lock_);
        out = snapshot_;
    }

    double lastBpm() const noexcept { return lastBpm_.load(std::memory_order_relaxed); }

  private:
    mutable juce::SpinLock lock_;
    mutable ui::Visuals snapshot_{};
    std::atomic<double> lastBpm_{172.0};
};

} // namespace cl
