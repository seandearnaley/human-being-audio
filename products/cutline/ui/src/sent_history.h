// SPDX-License-Identifier: AGPL-3.0-or-later
// What the PLAY surface remembers: the last bar of notes the engine sent, and where each drum lane's row is.
#pragma once

#include "cl/pattern.h"
#include "cl/sent_notes.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace cl::ui {

// Drum rows, top to bottom: high to low, so Roll's Y picks the lane under the finger (bottom third kick, middle
// snare, top closed hat) and Rate's "Kick to Tops" reads the same way.
inline constexpr std::array<int, kDrumLaneCount> kSurfaceLaneOrder{4, 5, 7, 3, 1, 2, 6, 0};

constexpr int surfaceRowOfLane(int lane) {
    for (int row = 0; row < kDrumLaneCount; ++row)
        if (kSurfaceLaneOrder[static_cast<std::size_t>(row)] == lane)
            return row;
    return 0;
}

// The last bar of sent notes. Fed from Visuals::sent every UI tick; a note is taken once, by its sequence number.
class SentHistory {
  public:
    static constexpr int kCapacity = 512;

    void ingest(std::span<const SentNote> notes) {
        for (const SentNote& note : notes) {
            if (note.seq != 0 && note.seq <= lastSeq_)
                continue;
            lastSeq_ = std::max(lastSeq_, note.seq);
            SentNote& slot = marks_[static_cast<std::size_t>(next_)];
            slot = note;
            slot.seq = std::max(1u, note.seq); // 0 marks an empty slot
            next_ = (next_ + 1) % kCapacity;
        }
    }

    // The transport jumped: every mark moves with the clock, so it keeps its age.
    void shift(double hostBeats) {
        for (auto& m : marks_)
            m.ppq += hostBeats;
    }

    void clear() {
        marks_ = {};
        next_ = 0;
    }

    // 1 when the note was sent, 0 one bar of its own clock later.
    static float lifeOf(const SentNote& mark, double nowPpq) {
        const double age = std::max(0.0, nowPpq - mark.ppq);
        return 1.0f - static_cast<float>(age * static_cast<double>(std::max(0.05f, mark.rate)) / 4.0);
    }

    // Calls fn(mark, life) for every mark still visible at host beat `nowPpq`, oldest first.
    template <typename Fn> void forEachLive(double nowPpq, Fn&& fn) const {
        for (int i = 0; i < kCapacity; ++i) {
            const SentNote& m = marks_[static_cast<std::size_t>((next_ + i) % kCapacity)];
            if (m.seq == 0)
                continue;
            const float life = lifeOf(m, nowPpq);
            if (life > 0.0f)
                fn(m, life);
        }
    }

  private:
    std::array<SentNote, kCapacity> marks_{};
    int next_ = 0;
    std::uint32_t lastSeq_ = 0;
};

} // namespace cl::ui
