// SPDX-License-Identifier: AGPL-3.0-or-later
// The note-ons Cutline actually sent, for the PLAY surface. The engine lists each block's note-ons; SentNoteRing
// carries them from the audio thread to the UI: one producer, one consumer, no lock and no allocation.
#pragma once

#include "cl/nonblocking.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace cl {

struct SentNote {
    double ppq = 0.0;       // host beat the note-on was sent at (the transport, before Shift)
    float beatInBar = 0.0f; // where it sounds in its bar on the pattern's clock, 0..4: its real, swung, nudged time
    float rate = 1.0f;      // pattern beats per host beat when it was sent: its bar lasts 4 / rate host beats
    std::uint32_t seq = 0;  // 1, 2, 3... stamped by SentNoteRing; 0 until then
    int sampleOffset = 0;   // in the block that sent it
    std::int8_t note = 0;
    std::int8_t lane = -1; // drum lane 0..7, or -1 for a MIDI Slicer note
    std::uint8_t velocity = 0;
};

// Single-producer, single-consumer ring of sent notes. push() runs on the audio thread, drain() on the UI thread.
// When the UI does not drain (the editor is closed), the ring fills and push() drops what does not fit.
class SentNoteRing {
  public:
    static constexpr std::uint32_t kCapacity = 1024; // a power of two

    bool push(const SentNote& note) noexcept CL_NONBLOCKING {
        const std::uint32_t w = write_.load(std::memory_order_relaxed);
        const std::uint32_t r = read_.load(std::memory_order_acquire);
        if (w - r >= kCapacity)
            return false;
        SentNote& slot = slots_[static_cast<std::size_t>(w & (kCapacity - 1u))];
        slot = note;
        slot.seq = w + 1u;
        write_.store(w + 1u, std::memory_order_release);
        return true;
    }

    // Copies up to `max` notes into `out`, oldest first, and returns how many.
    int drain(SentNote* out, int max) noexcept {
        const std::uint32_t r = read_.load(std::memory_order_relaxed);
        const std::uint32_t w = write_.load(std::memory_order_acquire);
        const std::uint32_t available = w - r;
        const std::uint32_t n = max > 0 ? std::min(available, static_cast<std::uint32_t>(max)) : 0u;
        for (std::uint32_t i = 0; i < n; ++i)
            out[i] = slots_[static_cast<std::size_t>((r + i) & (kCapacity - 1u))];
        read_.store(r + n, std::memory_order_release);
        return static_cast<int>(n);
    }

    // Forgets everything not yet drained.
    void discard() noexcept { read_.store(write_.load(std::memory_order_acquire), std::memory_order_release); }

  private:
    std::array<SentNote, kCapacity> slots_{};
    std::atomic<std::uint32_t> write_{0};
    std::atomic<std::uint32_t> read_{0};
};

} // namespace cl
