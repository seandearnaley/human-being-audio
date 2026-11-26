// SPDX-License-Identifier: GPL-3.0-or-later
// Non-blocking try-lock telemetry bridge from the audio callback to the UI thread.
#pragma once

#include <juce_core/juce_core.h>

namespace hba::host {

template <typename SnapshotT> class TelemetryChannel {
  public:
    // Audio thread: non-blocking try-lock write; drops frame if UI is mid-read.
    void publish(const SnapshotT& snap) noexcept {
        const juce::SpinLock::ScopedTryLockType scoped(lock_);
        if (scoped.isLocked())
            latest_ = snap;
    }

    // UI thread: copies latest snapshot.
    SnapshotT read() const {
        const juce::SpinLock::ScopedLockType scoped(lock_);
        return latest_;
    }

  private:
    mutable juce::SpinLock lock_;
    SnapshotT latest_{};
};

} // namespace hba::host
