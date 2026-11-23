// SPDX-License-Identifier: AGPL-3.0-or-later
// Deterministic phrase-grammar compiler and strictly monotonic gravitational
// microtiming field. Pure C++20, zero heap allocations, [[clang::nonblocking]].
#pragma once

#include "cl/nonblocking.h"
#include "cl/overrides.h"
#include "cl/params.h"

#include <array>
#include <cstdint>

namespace cl {

struct RoleSpan {
    double startBeats = 0.0;
    double endBeats = 4.0;
    Role role = Role::statement;
    bool locked = false;
};

struct CompiledNote {
    double startBeats = 0.0;    // Quantized grid position in [0, phraseBeats)
    double warpedBeats = 0.0;   // After gravitational microtiming warp
    double durationBeats = 0.5; // Clamped cleanly inside phrase
    std::int16_t slotIndex = -1; // Deterministic pre-override slot index for non-destructive edits
    std::int8_t noteNumber = 41;
    std::int8_t drumRow = 0; // 0..5 in Drum Kit mode
    std::uint8_t velocity = 100;
    float bendSemitones = 0.0f; // Per-note pitch dive / scoop / slide target
    Role role = Role::statement;
    std::uint8_t channel = 1; // 1 in Mono/Drum; 2..15 in MPE Lower Zone
    bool isRatchet = false;
    bool muted = false;
};

inline constexpr int kMaxPhraseNotes = 256;
inline constexpr int kMaxRoleSpans = 32;
inline constexpr int kMaxHeldInputNotes = 16;

struct HeldNotes {
    std::array<std::uint8_t, kMaxHeldInputNotes> notes{};
    int count = 0;
    std::uint8_t lastPressed = 0;

    void clear() noexcept CL_NONBLOCKING {
        count = 0;
        lastPressed = 0;
    }
    void push(int note) noexcept CL_NONBLOCKING {
        const auto n = static_cast<std::uint8_t>(note < 0 ? 0 : (note > 127 ? 127 : note));
        lastPressed = n;
        for (int i = 0; i < count; ++i) {
            if (notes[static_cast<std::size_t>(i)] == n)
                return;
        }
        if (count >= kMaxHeldInputNotes) {
            // Evict lowest note if full and new note is higher, else keep existing highest notes.
            if (n < notes[0])
                return;
            for (int i = 0; i < kMaxHeldInputNotes - 1; ++i)
                notes[static_cast<std::size_t>(i)] = notes[static_cast<std::size_t>(i + 1)];
            --count;
        }
        int insertAt = count;
        while (insertAt > 0 && notes[static_cast<std::size_t>(insertAt - 1)] > n) {
            notes[static_cast<std::size_t>(insertAt)] = notes[static_cast<std::size_t>(insertAt - 1)];
            --insertAt;
        }
        notes[static_cast<std::size_t>(insertAt)] = n;
        ++count;
    }
    void remove(int note) noexcept CL_NONBLOCKING {
        const auto n = static_cast<std::uint8_t>(note < 0 ? 0 : (note > 127 ? 127 : note));
        for (int i = 0; i < count; ++i) {
            if (notes[static_cast<std::size_t>(i)] == n) {
                for (int j = i; j < count - 1; ++j)
                    notes[static_cast<std::size_t>(j)] = notes[static_cast<std::size_t>(j + 1)];
                --count;
                if (count == 0)
                    lastPressed = 0;
                else if (lastPressed == n)
                    lastPressed = notes[static_cast<std::size_t>(count - 1)];
                return;
            }
        }
    }
};

struct PhrasePlan {
    std::int64_t phraseIndex = -1;
    double phraseBeats = 16.0;
    std::uint32_t signature = 0;
    int noteCount = 0;
    int spanCount = 0;
    std::array<CompiledNote, kMaxPhraseNotes> notes{};
    std::array<RoleSpan, kMaxRoleSpans> spans{};
};

// Drum Kit GM/Drum-Rack note map: Kick, Snare, Ghost Snare, Closed Hat, Open Hat, Ratchet Perc.
inline constexpr std::array<std::int8_t, 6> kDrumNotes{36, 38, 39, 42, 46, 43};

// Scale query & snapping helpers (shared by phrase compiler and interactive piano roll).
bool isNoteInScale(int midiNote, int root, Scale scale) noexcept CL_NONBLOCKING;
int snapNoteToScale(int midiNote, int root, Scale scale) noexcept CL_NONBLOCKING;

// Smooth, strictly monotonic gravitational microtiming warp over bar phase.
// Guaranteed: for any beatA < beatB, warpBeat(beatA, ...) < warpBeat(beatB, ...).
double warpBeat(double beat, double gravityNorm, Attractor attractor) noexcept CL_NONBLOCKING;

// Deterministic signature hash of all phrase-shaping parameters, overrides, and held notes,
// allowing the engine to recompile instantly whenever a relevant control moves.
std::uint32_t phraseSignature(const Params& params, std::int64_t phraseIndex, const HeldNotes& held,
                              const PhraseOverrides& overrides = {}) noexcept CL_NONBLOCKING;

// Compiles one complete 2/4/8-bar phrase into `out` without heap allocation.
void compilePhrase(const Params& params, std::int64_t phraseIndex, const HeldNotes& held, PhrasePlan& out,
                   const PhraseOverrides& overrides = {}) noexcept CL_NONBLOCKING;

} // namespace cl
