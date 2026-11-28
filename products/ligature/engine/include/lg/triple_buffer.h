// SPDX-License-Identifier: AGPL-3.0-or-later
// Lock-free single-producer, single-consumer triple buffer. The writer never waits for the reader and the reader
// never copies: publishing swaps an index. Allocates its three buffers once, at construction.
#pragma once

#include "lg/nonblocking.h"

#include <atomic>
#include <cstdint>
#include <memory>

namespace lg {

template <typename T> class TripleBuffer {
  public:
    TripleBuffer() : buffers_(std::make_unique<T[]>(3)) {}

    // Writer (message thread): fill write(), then publish().
    T& write() noexcept { return buffers_[back_]; }
    void publish() noexcept { back_ = middle_.exchange(static_cast<std::uint8_t>(back_ | kDirty)) & kIndex; }

    // Reader (audio thread): take the newest published buffer, if any. Returns true when it changed.
    bool acquire() noexcept LG_NONBLOCKING {
        if ((middle_.load(std::memory_order_acquire) & kDirty) == 0)
            return false;
        front_ = middle_.exchange(front_, std::memory_order_acq_rel) & kIndex;
        return true;
    }
    const T& read() const noexcept LG_NONBLOCKING { return buffers_[front_]; }

  private:
    static constexpr std::uint8_t kDirty = 0x4;
    static constexpr std::uint8_t kIndex = 0x3;
    std::unique_ptr<T[]> buffers_;
    std::uint8_t back_ = 0;
    std::atomic<std::uint8_t> middle_{1};
    std::uint8_t front_ = 2;
};

} // namespace lg
