// SPDX-License-Identifier: AGPL-3.0-or-later
// Fixed-capacity, allocation-free phrase overrides (span locks, span transforms,
// divider drags, role overrides including Rest, per-bar chord progression offsets,
// and musical-coordinate per-note edits). Safe on audio thread.
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

// Stable musical coordinates for per-note overrides so edits survive Density/Mutate tweaks.
struct NoteKey {
    std::int8_t spanIdx = -1;
    std::int16_t stepTick = -1; // Quantized 1/24-beat grid position: round(startBeats * 24)
    std::int8_t voiceIdx = 0;   // 0 = primary melodic, 1 = harmony, 0..5 = drum row

    bool valid() const noexcept CL_NONBLOCKING { return spanIdx >= 0 && stepTick >= 0; }
    bool operator==(const NoteKey&) const noexcept = default;
};

struct SpanOverride {
    bool locked = false;
    std::uint16_t lockedSeed = 0;  // Captured seed when locked (1..999)
    std::int8_t roleOverride = -1; // -1 = auto from grammar, 0..5 = explicit Role
    std::int8_t octaveShift = 0;   // -2..+2 octaves
    bool invert = false;           // Mirror melodic contour around root
    std::int8_t timeScale = 0;     // -1 = Half-time (0.5x), 0 = Normal (1x), +1 = Double-time (2x)

    bool active() const noexcept CL_NONBLOCKING {
        return locked || roleOverride >= 0 || octaveShift != 0 || invert || timeScale != 0;
    }
    bool operator==(const SpanOverride&) const noexcept = default;
};

struct NoteOverride {
    NoteKey key{};
    std::int16_t slotIndex = -1;      // Legacy fallback for v0.2 JSON overrides without musical key
    std::int8_t pitchDelta = 0;       // Semitones (-24..+24)
    std::int8_t velocityDelta = 0;    // Velocity offset (-64..+64)
    std::int8_t lengthSixteenths = 0; // Duration offset in 1/16 beats (-16..+32)
    std::int8_t microNudgeTicks = 0;  // Onset micro-nudge in 1/96 beats (-24..+24 => +-1/4 beat)
    std::uint8_t ratchetCount = 0;    // 0 = auto, or explicit 2, 3, 4, 6 sub-ratchet burst
    bool muted = false;
    bool flipBend = false;

    bool hasEdits() const noexcept CL_NONBLOCKING {
        return pitchDelta != 0 || velocityDelta != 0 || lengthSixteenths != 0 || microNudgeTicks != 0 ||
               ratchetCount != 0 || muted || flipBend;
    }
    bool active() const noexcept CL_NONBLOCKING { return (key.valid() || slotIndex >= 0) && hasEdits(); }
    bool operator==(const NoteOverride&) const noexcept = default;
};

struct PhraseOverrides {
    static constexpr int kMaxSpanOverrides = 6;
    static constexpr int kMaxNoteOverrides = 128;
    static constexpr int kMaxChordBars = 8;

    std::array<SpanOverride, kMaxSpanOverrides> spans{};
    std::array<std::int8_t, kMaxChordBars> barChordOffsets{}; // Per-bar scale degree root offset (-7..+7)
    float fillStartFrac = -1.0f;                              // < 0 = auto, else [0.50, 0.90]
    float turnStartFrac = -1.0f;                              // < 0 = auto, else [0.75, 0.96]
    std::array<NoteOverride, kMaxNoteOverrides> notes{};

    bool hasChordProgression() const noexcept CL_NONBLOCKING {
        for (std::int8_t c : barChordOffsets) {
            if (c != 0)
                return true;
        }
        return false;
    }

    bool hasAny() const noexcept CL_NONBLOCKING {
        if (fillStartFrac >= 0.0f || turnStartFrac >= 0.0f || hasChordProgression())
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
        barChordOffsets = {};
        fillStartFrac = -1.0f;
        turnStartFrac = -1.0f;
        notes = {};
    }

    // Musical-coordinate lookup (preferred 1.0 API)
    const NoteOverride* findNote(NoteKey key) const noexcept CL_NONBLOCKING {
        if (!key.valid())
            return nullptr;
        for (const auto& n : notes) {
            if (n.key == key && n.active())
                return &n;
        }
        return nullptr;
    }

    NoteOverride* findOrCreateNote(NoteKey key) noexcept CL_NONBLOCKING {
        if (!key.valid())
            return nullptr;
        NoteOverride* freeSlot = nullptr;
        for (auto& n : notes) {
            if (n.key == key)
                return &n;
            if (freeSlot == nullptr && !n.active())
                freeSlot = &n;
        }
        if (freeSlot != nullptr) {
            *freeSlot = {};
            freeSlot->key = key;
            return freeSlot;
        }
        return nullptr;
    }

    void clearNote(NoteKey key) noexcept CL_NONBLOCKING {
        if (!key.valid())
            return;
        for (auto& n : notes) {
            if (n.key == key)
                n = {};
        }
    }

    // Legacy slot-index lookup (preserves v0.2 preset & test compatibility)
    const NoteOverride* findNote(int slotIndex) const noexcept CL_NONBLOCKING {
        if (slotIndex < 0)
            return nullptr;
        for (const auto& n : notes) {
            if (!n.key.valid() && n.slotIndex == slotIndex && n.active())
                return &n;
        }
        return nullptr;
    }

    NoteOverride* findOrCreateNote(int slotIndex) noexcept CL_NONBLOCKING {
        if (slotIndex < 0)
            return nullptr;
        NoteOverride* freeSlot = nullptr;
        for (auto& n : notes) {
            if (!n.key.valid() && n.slotIndex == slotIndex)
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
            if (!n.key.valid() && n.slotIndex == slotIndex)
                n = {};
        }
    }

    // Combined lookup used by phrase compiler: musical key first, legacy slot fallback second.
    const NoteOverride* findNoteFor(NoteKey key, int slotIndex) const noexcept CL_NONBLOCKING {
        if (const auto* byKey = findNote(key))
            return byKey;
        return findNote(slotIndex);
    }

    bool operator==(const PhraseOverrides& o) const noexcept {
        return spans == o.spans && barChordOffsets == o.barChordOffsets &&
               std::abs(fillStartFrac - o.fillStartFrac) < 1e-5f && std::abs(turnStartFrac - o.turnStartFrac) < 1e-5f &&
               notes == o.notes;
    }
};

} // namespace cl
