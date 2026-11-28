// SPDX-License-Identifier: AGPL-3.0-or-later
// Links Ligature instances in one host process: the leader publishes its song and its performance; followers play
// one part each from them, so each part can sit on its own track without channel routing (which Ableton Live lacks).
#pragma once

#include "lg/engine.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace lg {

class LinkHub {
  public:
    static LinkHub& instance() {
        static LinkHub hub;
        return hub;
    }

    // Message thread. The first lead instance to publish becomes the leader until it leaves or stops leading.
    bool claimLeader(const void* who) {
        const std::scoped_lock lock(songLock_);
        if (leader_ == nullptr) {
            leader_ = who;
            // A new leader counts its performance generations from its own start: the epoch keeps them apart from
            // the old leader's, so a follower never mistakes the new leader's first frames for ones it has seen.
            epoch_.fetch_add(1, std::memory_order_acq_rel);
        }
        leaderAudio_.store(leader_, std::memory_order_release);
        return leader_ == who;
    }

    void releaseLeader(const void* who) {
        const std::scoped_lock lock(songLock_);
        if (leader_ == who) {
            leader_ = nullptr;
            leaderAudio_.store(nullptr, std::memory_order_release);
            hasPerformance_.store(false, std::memory_order_release);
        }
    }

    bool isLeader(const void* who) const {
        const std::scoped_lock lock(songLock_);
        return leader_ == who;
    }

    bool hasLeader() const {
        const std::scoped_lock lock(songLock_);
        return leader_ != nullptr;
    }

    void publishSong(const void* who, const std::string& json, std::uint64_t revision) {
        const std::scoped_lock lock(songLock_);
        if (leader_ != who)
            return;
        songJson_ = json;
        songRevision_ = revision;
        ++songGeneration_;
    }

    // The leader's song when it changed since `seenGeneration`.
    std::optional<std::string> songIfNewer(std::uint64_t& seenGeneration) const {
        const std::scoped_lock lock(songLock_);
        if (leader_ == nullptr || songGeneration_ == seenGeneration || songJson_.empty())
            return std::nullopt;
        seenGeneration = songGeneration_;
        return songJson_;
    }

    // Audio threads: non-blocking. A frame is dropped when the other side holds the lock; the next block catches up.
    void publishPerformance(const void* who, const Performance& p) noexcept {
        if (who != leaderAudio_.load(std::memory_order_acquire))
            return;
        const juce::SpinLock::ScopedTryLockType scoped(perfLock_);
        if (scoped.isLocked()) {
            performance_ = p;
            constexpr std::uint64_t kLow = (std::uint64_t{1} << 40) - 1;
            performance_.generation = (epoch_.load(std::memory_order_acquire) << 40) | (p.generation & kLow);
            hasPerformance_.store(true, std::memory_order_release);
        }
    }

    bool pullPerformance(Performance& out) const noexcept {
        if (!hasPerformance_.load(std::memory_order_acquire))
            return false;
        const juce::SpinLock::ScopedTryLockType scoped(perfLock_);
        if (!scoped.isLocked())
            return false;
        out = performance_;
        return true;
    }

  private:
    mutable std::mutex songLock_;
    const void* leader_ = nullptr;
    std::string songJson_;
    std::uint64_t songRevision_ = 0;
    std::uint64_t songGeneration_ = 0;

    std::atomic<const void*> leaderAudio_{nullptr};
    std::atomic<std::uint64_t> epoch_{0};
    mutable juce::SpinLock perfLock_;
    Performance performance_{};
    std::atomic<bool> hasPerformance_{false};
};

} // namespace lg
