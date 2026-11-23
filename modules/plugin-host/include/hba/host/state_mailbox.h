// SPDX-License-Identifier: GPL-3.0-or-later
// Thread-safe message-to-audio mailbox using a try-lock on the audio thread.
#pragma once

#include <juce_core/juce_core.h>

#include <atomic>

namespace hba::host {

template <typename T> class StateMailbox {
  public:
    StateMailbox() = default;
    explicit StateMailbox(const T& initial) : value_(initial) {}

    // Message thread.
    T get() const {
        const juce::SpinLock::ScopedLockType scoped(lock_);
        return value_;
    }

    void set(const T& next) {
        const juce::SpinLock::ScopedLockType scoped(lock_);
        value_ = next;
        dirty_.store(true, std::memory_order_release);
    }

    template <typename Fn> void modify(Fn&& fn) {
        const juce::SpinLock::ScopedLockType scoped(lock_);
        fn(value_);
        dirty_.store(true, std::memory_order_release);
    }

    // Audio thread: non-blocking try-lock pull. Returns true if new data was copied.
    bool pull(T& destination) noexcept {
        if (!dirty_.load(std::memory_order_acquire))
            return false;
        const juce::SpinLock::ScopedTryLockType scoped(lock_);
        if (!scoped.isLocked())
            return false;
        destination = value_;
        dirty_.store(false, std::memory_order_release);
        return true;
    }

  private:
    T value_{};
    mutable juce::SpinLock lock_;
    std::atomic<bool> dirty_{true};
};

} // namespace hba::host
