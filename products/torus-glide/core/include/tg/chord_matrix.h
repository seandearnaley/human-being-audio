// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "tg/params.h"

#include <hba/dsp/nonblocking.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace tg {

// 4 programmable chord scenes × 5 voice semitone offsets relative to root MIDI note.
// Offsets can range from -24 to +48 semitones (supports 1->5 unison blooms & wide stacks).
struct SceneMatrix {
    std::array<std::array<float, kVoiceCount>, kSceneCount> offsets{};
    bool isCustom = false;

    friend bool operator==(const SceneMatrix&, const SceneMatrix&) = default;
};

inline constexpr int kVoicingPackCount = 8;

struct VoicingPackInfo {
    std::string_view name;
    std::array<std::string_view, kSceneCount> sceneLabels;
    std::array<std::array<float, kVoiceCount>, kSceneCount> offsets;
    bool mobiusLoop = false; // If true, Scene D wraps continuously into Scene A + 12 st
};

const std::array<VoicingPackInfo, kVoicingPackCount>& voicingPacks();
SceneMatrix matrixForPack(int packIndex) noexcept HBA_NONBLOCKING;

// Chord intervals (in semitones) for Spiral Mode's `spiralStack` choice.
struct SpiralChord {
    std::array<double, kVoiceCount> intervals{};
    int count = 1;
};
SpiralChord spiralChordForStack(int stackIndex) noexcept HBA_NONBLOCKING;

// Scale pitch-class masks (bit pc set if pitch class pc in 0..11 relative to root is in scale).
inline constexpr int kScaleCount = 9;
std::uint16_t scaleMask(int scaleIndex) noexcept HBA_NONBLOCKING;

// Tymoczko minimal-distance voice-leading solver:
// Reorders voices in scenes 1..3 so total L1 voice movement from scene s-1 -> s is minimized,
// eliminating awkward crossings while preserving Voice 0 when `keepBass` is true.
void optimizeVoiceLeading(SceneMatrix& matrix, bool keepBass = false) noexcept HBA_NONBLOCKING;

// Snapshots 1..5 live held MIDI notes directly into Scene A..D relative to `rootMidi`.
// Pads cleanly when fewer than 5 notes are held, marks `matrix.isCustom = true`, and
// optionally runs `optimizeVoiceLeading` so the captured chord voice-leads smoothly.
bool captureChordIntoScene(SceneMatrix& matrix, int sceneIndex, const int* midiNotes, int count, int rootMidi,
                           bool autoLead = true, bool keepBass = false) noexcept HBA_NONBLOCKING;

// Continuous Harmonic Gravity Potential Well ("bending through a tune"):
// Smoothly attracts a continuous MIDI pitch toward adjacent scale degrees and/or
// pure Just Intonation ratios relative to `anchorMidi`, with zero stepping artifacts.
double applyGravity(double midi, int rootPc, int scaleIndex, GravityMode mode, double strength,
                    double anchorMidi) noexcept HBA_NONBLOCKING;

// Evaluate all 5 polyphonic voice pitches (in absolute MIDI notes) at morph position m in [0, 1],
// applying Pedal-Steel per-voice stagger, Voice-1 sub drone anchor, and Harmonic Gravity Wells.
struct MorphEval {
    std::array<double, kVoiceCount> pitches{};
    int activeScene = 0;
};

MorphEval evaluateMorphPitches(const SceneMatrix& matrix, double rootMidi, int rootPc, int scaleIndex, double morph01,
                               double gravity, GravityMode gravMode, double stagger, StaggerOrder order, bool anchorSub,
                               bool mobiusWrap) noexcept HBA_NONBLOCKING;

} // namespace tg
