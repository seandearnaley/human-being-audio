// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <hba/dsp/nonblocking.h>

#include <array>
#include <atomic>
#include <cstddef>

namespace hba::dsp {

// A serialized producer prepares samples in its private slot. The audio thread
// owns current and previous until its next successful pull, including across
// callbacks. Only the middle slot changes hands. Replacing an unread publish
// returns that slot to the producer. No sample or ownership object moves on pull.
template <typename T> class SampleExchange {
  public:
    T& writable() noexcept HBA_NONBLOCKING { return slots_[writer_]; }
    void publish() noexcept HBA_NONBLOCKING {
        writer_ = middle_.exchange(writer_ | dirty, std::memory_order_acq_rel) & mask;
    }
    bool pull() noexcept HBA_NONBLOCKING {
        if ((middle_.load(std::memory_order_acquire) & dirty) == 0)
            return false;
        const auto incoming = middle_.exchange(previous_, std::memory_order_acq_rel) & mask;
        previous_ = current_;
        current_ = incoming;
        return true;
    }
    const T& current() const noexcept HBA_NONBLOCKING { return slots_[current_]; }
    const T& previous() const noexcept HBA_NONBLOCKING { return slots_[previous_]; }

  private:
    static constexpr unsigned dirty = 4, mask = 3;
    static_assert(std::atomic<unsigned>::is_always_lock_free);
    std::array<T, 4> slots_{};
    unsigned writer_ = 0, current_ = 2, previous_ = 3;
    std::atomic<unsigned> middle_{1};
};
} // namespace hba::dsp
