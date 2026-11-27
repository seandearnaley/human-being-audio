// SPDX-License-Identifier: AGPL-3.0-or-later
// Deterministic 8-lane polymetric breakbeat & live MIDI slicer phrase compiler,
// strictly monotonic MPC swing / gravitational microtiming field.
// Pure C++20, zero heap allocations, [[clang::nonblocking]].
#pragma once

#include "cl/held_notes.h"
#include "cl/nonblocking.h"
#include "cl/params.h"
#include "cl/pattern.h"

#include <array>
#include <cstdint>

namespace cl {

struct CompiledNote {
    double startBeats = 0.0;    // Quantized grid position in [0, phraseBeats)
    double warpedBeats = 0.0;   // After swing/gravity microtiming + per-lane pocket + step nudge
    double durationBeats = 0.2; // Clamped cleanly inside phrase
    std::int8_t noteNumber = 36;
    std::int8_t lane = 0;       // 0..7 drum lane, or chord voice index in Slicer mode
    std::int16_t stepIndex = 0; // 0..31 step cell in active lane
    std::uint8_t velocity = 100;
    std::uint8_t channel = 1;
    bool isGhost = false;
    bool isRatchet = false;
    bool isFill = false;
    bool isChop = false;
    bool isFlam = false;
    bool muted = false;
};

inline constexpr int kMaxPhraseNotes = 768;

struct PhrasePlan {
    std::int64_t phraseIndex = -1;
    double phraseBeats = 16.0;
    std::uint32_t signature = 0;
    int activeScene = 0;
    double fillStartBeat = 16.0; // Beat where bar-end turnaround fill begins (phraseBeats if none)
    int noteCount = 0;
    std::array<CompiledNote, kMaxPhraseNotes> notes{};

    // Resolved visual grid snapshot for the 8-lane polymetric editor & Slicer gate timeline:
    std::array<std::uint8_t, kDrumLaneCount> laneLengths{};
    std::array<std::int8_t, kDrumLaneCount> laneMidiNotes{};
    std::array<std::array<std::uint8_t, kMaxLaneSteps>, kDrumLaneCount> gridVel{};
    std::array<std::array<std::uint8_t, kMaxLaneSteps>, kDrumLaneCount> gridRatchet{};
    std::array<std::array<bool, kMaxLaneSteps>, kDrumLaneCount> gridFlam{};

    std::uint8_t slicerLength = 16;
    std::array<std::uint8_t, kMaxLaneSteps> slicerVel{};
    std::array<std::uint8_t, kMaxLaneSteps> slicerRatchet{};
    std::array<bool, kMaxLaneSteps> slicerFlam{};
};

// Strictly monotonic microtiming field (MPC Swing or sinusoidal gravitational pocket).
// Guaranteed: for any beatA < beatB, warpBeat(beatA, ...) < warpBeat(beatB, ...).
double warpBeat(double beat, double grooveNorm, Attractor pocket) noexcept CL_NONBLOCKING;

// Deterministic signature hash of all compilation inputs so UI & engine only recompile on change.
std::uint32_t phraseSignature(const Params& params, int sceneIdx, const HeldNotes& held, const PatternBank& bank = {},
                              double bpm = 172.0) noexcept CL_NONBLOCKING;

// Compiles one complete 2/4/8-bar drum or MIDI slicer phrase into `out` without heap allocation.
void compilePhrase(const Params& params, int sceneIdx, std::int64_t phraseIndex, const HeldNotes& held, PhrasePlan& out,
                   const PatternBank& bank = {}, double bpm = 172.0) noexcept CL_NONBLOCKING;

// Utility: emit one bar of a selected Fill archetype into `out` starting at [fillStart, fillEnd).
void appendDrumFill(PhrasePlan& out, double fillStart, double fillEnd, double totalBeats, FillType fillType,
                    double groove, Attractor pocket, float dynamics, float ratchetAmt, int variation, NoteMap noteMap,
                    const SceneData& scene, std::uint32_t salt) noexcept CL_NONBLOCKING;

} // namespace cl
