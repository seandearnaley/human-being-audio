// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "tg/engine.h"

#include <juce_core/juce_core.h>

namespace tg {

class TelemetryChannel {
  public:
    // Audio thread: non-blocking try-lock write.
    void publish(const TelemetrySnapshot& snap) noexcept {
        const juce::SpinLock::ScopedTryLockType scoped(lock_);
        if (scoped.isLocked())
            latest_ = snap;
    }

    // UI thread: read latest snapshot.
    TelemetrySnapshot read() const {
        const juce::SpinLock::ScopedLockType scoped(lock_);
        return latest_;
    }

  private:
    mutable juce::SpinLock lock_;
    TelemetrySnapshot latest_{};
};

} // namespace tg
