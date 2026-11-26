// SPDX-License-Identifier: AGPL-3.0-or-later
// Deterministic 8-lane polymetric breakbeat & live MIDI slicer phrase compiler,
// strictly monotonic MPC swing / gravitational microtiming field.
// Pure C++20, zero heap allocations, [[clang::nonblocking]].
#pragma once

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
inline constexpr int kMaxHeldInputNotes = 16;

struct HeldNotes {
    std::array<std::uint8_t, kMaxHeldInputNotes> notes{};
    std::array<std::uint8_t, kMaxHeldInputNotes> velocities{};
    int count = 0;
    std::uint8_t lastPressed = 0;

    void clear() noexcept CL_NONBLOCKING {
        count = 0;
        lastPressed = 0;
    }
    void push(int note, int vel = 100) noexcept CL_NONBLOCKING {
        const auto n = static_cast<std::uint8_t>(note < 0 ? 0 : (note > 127 ? 127 : note));
        const auto v = static_cast<std::uint8_t>(vel < 1 ? 1 : (vel > 127 ? 127 : vel));
        lastPressed = n;
        for (int i = 0; i < count; ++i) {
            if (notes[static_cast<std::size_t>(i)] == n) {
                velocities[static_cast<std::size_t>(i)] = v;
                return;
            }
        }
        if (count >= kMaxHeldInputNotes) {
            if (n < notes[0])
                return;
            for (int i = 0; i < kMaxHeldInputNotes - 1; ++i) {
                notes[static_cast<std::size_t>(i)] = notes[static_cast<std::size_t>(i + 1)];
                velocities[static_cast<std::size_t>(i)] = velocities[static_cast<std::size_t>(i + 1)];
            }
            --count;
        }
        int insertAt = count;
        while (insertAt > 0 && notes[static_cast<std::size_t>(insertAt - 1)] > n) {
            notes[static_cast<std::size_t>(insertAt)] = notes[static_cast<std::size_t>(insertAt - 1)];
            velocities[static_cast<std::size_t>(insertAt)] = velocities[static_cast<std::size_t>(insertAt - 1)];
            --insertAt;
        }
        notes[static_cast<std::size_t>(insertAt)] = n;
        velocities[static_cast<std::size_t>(insertAt)] = v;
        ++count;
    }
    void remove(int note) noexcept CL_NONBLOCKING {
        const auto n = static_cast<std::uint8_t>(note < 0 ? 0 : (note > 127 ? 127 : note));
        for (int i = 0; i < count; ++i) {
            if (notes[static_cast<std::size_t>(i)] == n) {
                for (int j = i; j < count - 1; ++j) {
                    notes[static_cast<std::size_t>(j)] = notes[static_cast<std::size_t>(j + 1)];
                    velocities[static_cast<std::size_t>(j)] = velocities[static_cast<std::size_t>(j + 1)];
                }
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
