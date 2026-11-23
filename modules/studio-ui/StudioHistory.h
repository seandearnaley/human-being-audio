// SPDX-License-Identifier: AGPL-3.0-or-later
// Shared studio Undo/Redo + A/B compare state manager for Visage plugin models.
// Header-only, dependency-free C++20.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace hba::studio {

template <typename Snapshot, std::size_t kMaxDepth = 64> class History {
  public:
    void push(const Snapshot& beforeChange) {
        lastCoalesceTag_ = 0;
        if (!undo_.empty() && undo_.back() == beforeChange)
            return;
        if (undo_.size() >= kMaxDepth)
            undo_.erase(undo_.begin());
        undo_.push_back(beforeChange);
        redo_.clear();
    }

    // Coalesces rapid continuous edits (e.g. curve point drags, note drags, divider drags)
    // sharing the same non-zero `tag` within `windowMs` into a single undo entry so one
    // Undo reverts the entire drag gesture rather than a single pixel step.
    void pushCoalesced(const Snapshot& beforeChange, std::uint64_t tag, std::int64_t nowMs,
                       std::int64_t windowMs = 450) {
        if (tag != 0 && tag == lastCoalesceTag_ && (nowMs - lastCoalesceMs_) <= windowMs) {
            lastCoalesceMs_ = nowMs;
            return;
        }
        push(beforeChange);
        lastCoalesceTag_ = tag;
        lastCoalesceMs_ = nowMs;
    }

    std::optional<Snapshot> undo(const Snapshot& current) {
        lastCoalesceTag_ = 0;
        if (undo_.empty())
            return std::nullopt;
        Snapshot target = std::move(undo_.back());
        undo_.pop_back();
        if (redo_.size() >= kMaxDepth)
            redo_.erase(redo_.begin());
        redo_.push_back(current);
        return target;
    }

    std::optional<Snapshot> redo(const Snapshot& current) {
        lastCoalesceTag_ = 0;
        if (redo_.empty())
            return std::nullopt;
        Snapshot target = std::move(redo_.back());
        redo_.pop_back();
        if (undo_.size() >= kMaxDepth)
            undo_.erase(undo_.begin());
        undo_.push_back(current);
        return target;
    }

    // Swaps between A and B slots. The first switch into B initializes B from A
    // so the user starts tweaking from their current sound instead of defaults.
    Snapshot toggleAB(const Snapshot& current) {
        lastCoalesceTag_ = 0;
        if (!slotBActive_) {
            slotA_ = current;
            slotBActive_ = true;
            if (!slotB_.has_value())
                slotB_ = current;
            return *slotB_;
        }
        slotB_ = current;
        slotBActive_ = false;
        if (!slotA_.has_value())
            slotA_ = current;
        return *slotA_;
    }

    void copyActiveToOther(const Snapshot& current) {
        if (slotBActive_) {
            slotB_ = current;
            slotA_ = current;
        } else {
            slotA_ = current;
            slotB_ = current;
        }
    }

    bool canUndo() const noexcept { return !undo_.empty(); }
    bool canRedo() const noexcept { return !redo_.empty(); }
    bool isSlotB() const noexcept { return slotBActive_; }

    void clearHistory() noexcept {
        lastCoalesceTag_ = 0;
        undo_.clear();
        redo_.clear();
    }

  private:
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    std::optional<Snapshot> slotA_;
    std::optional<Snapshot> slotB_;
    std::uint64_t lastCoalesceTag_ = 0;
    std::int64_t lastCoalesceMs_ = 0;
    bool slotBActive_ = false;
};

} // namespace hba::studio
