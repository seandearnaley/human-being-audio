// SPDX-License-Identifier: AGPL-3.0-or-later
// Fixed-capacity, allocation-free phrase overrides (span locks, divider drags,
// role overrides including Rest, and sparse per-note edits). Safe on audio thread.
#pragma once

#include "cl/nonblocking.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cl {

enum class Role : std::uint8_t {
    statement = 0,
    answer = 1,
    mutation = 2,
    ratchet = 3,
    turnaround = 4,
    rest = 5,
};

struct SpanOverride {
    bool locked = false;
    std::uint16_t lockedSeed = 0;  // Captured seed when locked (1..999)
    std::int8_t roleOverride = -1; // -1 = auto from grammar, 0..5 = explicit Role

    bool active() const noexcept CL_NONBLOCKING { return locked || roleOverride >= 0; }
    bool operator==(const SpanOverride&) const noexcept = default;
};

struct NoteOverride {
    std::int16_t slotIndex = -1;
    std::int8_t pitchDelta = 0;    // Semitones (-24..+24)
    std::int8_t velocityDelta = 0; // Velocity offset (-64..+64)
    bool muted = false;
    bool flipBend = false;

    bool active() const noexcept CL_NONBLOCKING {
        return slotIndex >= 0 && (pitchDelta != 0 || velocityDelta != 0 || muted || flipBend);
    }
    bool operator==(const NoteOverride&) const noexcept = default;
};

struct PhraseOverrides {
    static constexpr int kMaxSpanOverrides = 6;
    static constexpr int kMaxNoteOverrides = 32;

    std::array<SpanOverride, kMaxSpanOverrides> spans{};
    float fillStartFrac = -1.0f; // < 0 = auto, else [0.50, 0.90]
    float turnStartFrac = -1.0f; // < 0 = auto, else [0.75, 0.96]
    std::array<NoteOverride, kMaxNoteOverrides> notes{};

    bool hasAny() const noexcept CL_NONBLOCKING {
        if (fillStartFrac >= 0.0f || turnStartFrac >= 0.0f)
            return true;
        for (const auto& s : spans) {
            if (s.active())
                return true;
        }
        for (const auto& n : notes) {
            if (n.active())
                return true;
        }
        return false;
    }

    void clear() noexcept CL_NONBLOCKING {
        spans = {};
        fillStartFrac = -1.0f;
        turnStartFrac = -1.0f;
        notes = {};
    }

    const NoteOverride* findNote(int slotIndex) const noexcept CL_NONBLOCKING {
        if (slotIndex < 0)
            return nullptr;
        for (const auto& n : notes) {
            if (n.slotIndex == slotIndex && n.active())
                return &n;
        }
        return nullptr;
    }

    NoteOverride* findOrCreateNote(int slotIndex) noexcept CL_NONBLOCKING {
        if (slotIndex < 0)
            return nullptr;
        NoteOverride* freeSlot = nullptr;
        for (auto& n : notes) {
            if (n.slotIndex == slotIndex)
                return &n;
            if (freeSlot == nullptr && !n.active())
                freeSlot = &n;
        }
        if (freeSlot != nullptr) {
            *freeSlot = {};
            freeSlot->slotIndex = static_cast<std::int16_t>(slotIndex);
            return freeSlot;
        }
        return nullptr;
    }

    void clearNote(int slotIndex) noexcept CL_NONBLOCKING {
        for (auto& n : notes) {
            if (n.slotIndex == slotIndex)
                n = {};
        }
    }

    bool operator==(const PhraseOverrides& o) const noexcept {
        return spans == o.spans && std::abs(fillStartFrac - o.fillStartFrac) < 1e-5f &&
               std::abs(turnStartFrac - o.turnStartFrac) < 1e-5f && notes == o.notes;
    }
};

} // namespace cl
