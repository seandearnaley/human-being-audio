// SPDX-License-Identifier: AGPL-3.0-or-later
// The MIDI Slicer compiler: held notes and the literal gate strip in, one phrase of notes out.
// The phrase is as long as the active scene's pattern. Nothing in it is random.
#pragma once

#include "cl/groove_state.h"
#include "cl/held_notes.h"
#include "cl/nonblocking.h"
#include "cl/params.h"

#include <array>
#include <cstdint>

namespace cl {

struct SlicerNote {
    double beat = 0.0; // start, in pattern beats from the start of the phrase
    double durBeats = 0.2;
    std::int8_t note = 60;
    std::int8_t voice = 0; // which held note produced it, lowest first (capped at 7)
    std::int16_t step = 0; // the gate step that produced it
    std::uint8_t velocity = 100;
};

inline constexpr int kMaxSlicerNotes = 768;

struct SlicerPlan {
    double phraseBeats = 4.0;
    int noteCount = 0;
    std::array<SlicerNote, kMaxSlicerNotes> notes{};
};

// Compiles the slicer phrase for `scene`, sorted by start time. Stab Chop, Strum / Flam, Break Arp and
// Bass / Stab produce notes. Clip Slice and an empty `held` produce none: Clip Slice replays captured
// notes inside the engine.
void compileSlicer(const Params& params, const GrooveState& groove, int scene, const HeldNotes& held, double bpm,
                   SlicerPlan& out) noexcept CL_NONBLOCKING;

// Everything a compiled phrase depends on besides the bank's contents, which callers track with a
// revision counter. The engine and the UI model both keep the inputs of their last compile and
// recompile only when sameAs() says something changed. Compared field by field; nothing is hashed.
struct SlicerInputs {
    bool valid = false; // false until the first compile
    std::uint32_t grooveRevision = 0;
    int scene = 0;
    int style = 0;
    int rateIdx = 0;
    float density = 0.0f;
    float dynamics = 0.0f;
    float swing = 0.0f;
    float gateLen = 0.0f;
    float strumMs = 0.0f;
    double bpm = 0.0;
    HeldNotes held{};

    bool sameAs(const SlicerInputs& other) const noexcept CL_NONBLOCKING;
};

SlicerInputs slicerInputs(const Params& params, int scene, const HeldNotes& held, double bpm,
                          std::uint32_t grooveRevision) noexcept CL_NONBLOCKING;

} // namespace cl
