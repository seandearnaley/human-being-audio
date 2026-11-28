// SPDX-License-Identifier: GPL-3.0-or-later
// What the audio thread hands to the rest of the plugin: fixed-size events in a lock-free
// single-producer, single-consumer ring. Nothing here allocates, locks or makes a system call.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace stage {

inline constexpr int macroCount = 8;

struct Event {
    enum class Type : std::uint8_t { block, noteOn, noteOff };

    Type type{Type::block};
    std::uint8_t note{0};
    std::uint8_t channel{0};
    std::uint8_t sigNumerator{4};
    std::uint8_t sigDenominator{4};
    bool playing{false};
    float velocity{0.0F};
    float level{0.0F};
    double ppq{0.0}; // quarter notes at this event
    double bpm{120.0};
    std::int64_t micros{0}; // wall clock at this event, microseconds since the Unix epoch
    std::array<float, macroCount> macros{};
    std::int64_t frame{-1}; // index of this block's first sample in the sound stream, or -1 with no input
};

class Capture {
  public:
    // Audio thread. Drops the event if the consumer has fallen a whole ring behind.
    bool push(const Event& event) noexcept [[clang::nonblocking]] {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto next = (head + 1) % capacity;
        if (next == tail_.load(std::memory_order_acquire)) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        ring_[head] = event;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Pump thread.
    bool pop(Event& event) noexcept {
        const auto tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
            return false;
        event = ring_[tail];
        tail_.store((tail + 1) % capacity, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::uint64_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

  private:
    static constexpr std::size_t capacity = 4096;
    std::array<Event, capacity> ring_{};
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
    std::atomic<std::uint64_t> dropped_{0};
};

// The sound at the plugin's input, on its way to the Stage: interleaved stereo in a lock-free
// single-producer, single-consumer ring. About 2.7 seconds at 48 kHz.
class SampleRing {
  public:
    // Audio thread. Takes the whole block or none of it.
    bool push(const float* left, const float* right, int frames) noexcept [[clang::nonblocking]] {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto count = static_cast<std::uint64_t>(frames) * 2;
        if (capacity - (head - tail_.load(std::memory_order_acquire)) < count)
            return false;
        for (std::uint64_t i = 0; i < count; i += 2) {
            ring_[(head + i) & mask] = left[i / 2];
            ring_[(head + i + 1) & mask] = right[i / 2];
        }
        head_.store(head + count, std::memory_order_release);
        return true;
    }

    // Pump thread. Copies up to `max` samples out and returns how many.
    std::size_t pop(float* out, std::size_t max) noexcept {
        const auto tail = tail_.load(std::memory_order_relaxed);
        const auto count = std::min<std::uint64_t>(max, head_.load(std::memory_order_acquire) - tail);
        for (std::uint64_t i = 0; i < count; ++i)
            out[i] = ring_[(tail + i) & mask];
        tail_.store(tail + count, std::memory_order_release);
        return static_cast<std::size_t>(count);
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

  private:
    static constexpr std::uint64_t capacity = 1U << 18;
    static constexpr std::uint64_t mask = capacity - 1;
    std::array<float, capacity> ring_{};
    std::atomic<std::uint64_t> head_{0};
    std::atomic<std::uint64_t> tail_{0};
};

} // namespace stage
