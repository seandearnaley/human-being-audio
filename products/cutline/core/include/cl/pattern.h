// SPDX-License-Identifier: AGPL-3.0-or-later
// Fixed-capacity, allocation-free 8-lane polymetric drum pattern, 4-scene bank,
// and live MIDI slicer step store. Safe on the real-time audio thread.
#pragma once

#include "cl/nonblocking.h"
#include "cl/params.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace cl {

inline constexpr int kDrumLaneCount = 8;
inline constexpr int kMaxLaneSteps = 32;

inline constexpr std::array<std::string_view, kDrumLaneCount> kDrumLaneNames{"KICK",   "SNARE",  "GHOST",  "CLAP",
                                                                             "CL HAT", "OP HAT", "PERC A", "PERC B"};

inline constexpr std::array<std::string_view, kPerfPadCount> kPerfPadLabels{"Slice", "Roll",    "Bounce", "Swing",
                                                                            "Poly",  "Stutter", "Morph",  "Drop"};

inline constexpr std::array<std::string_view, kPerfPadCount> kPerfPadXLabels{
    "Chop", "Rate", "Gravity", "Pocket", "Polymeter", "Window", "Time Warp", "Build"};

inline constexpr std::array<std::string_view, kPerfPadCount> kPerfPadYLabels{
    "Density", "Escalate", "Swarm", "Ghosts", "Rotate", "Gate Pump", "Sub \u2194 Tops", "Tension"};

inline constexpr std::array<std::string_view, kPerfPadCount> kPerfPadHints{"C3", "C#3", "D3",  "D#3",
                                                                           "E3", "F3",  "F#3", "G3"};

// Ten built-in MIDI output maps for the 8 drum lanes (preserving indices 0..2 for preset compatibility):
//   0 = Live Rack / GM:     Kick 36, Snare 38, Sidestick 37, Clap 39, CH 42, OH 46, Floor Tom 43, Crash 49
//   1 = Strike Body:        Native articulation map (Main 36, Ghost 37, Rim 38, Roll 39/40)
//   2 = Chromatic (C1..G1): 36..43 for Simpler Slice / Atlas 2 / 8-pad Drum Racks
//   3 = Addictive Drums 2:  XLN AD2 native map (Kick 36, Snare 38, Shallow Ghost 43, Rimshot 37, CH 49, OH 55, Tom 67,
//   Crash 77) 4 = Toontrack (SD3/EZD): Superior Drummer 3 / EZdrummer 2-3 (Kick 36, Snare 38, Ghost 38, Rimshot 40, CH
//   42, OH 46, FlTom 43, Crash 49) 5 = BFD3:               BFD3 native keymap (C0 Kick 24, D0 Snare 26, E0 SnAlt 28, F0
//   Rim 29, CH 42, OH 46, G0 FlTom 31, Crash 49) 6 = GetGood Drums:      GGD Invasion / Modern & Massive (C0 Kick 24,
//   D0 Snare 26, Ghost 26, Sidestick 30, CH 43, OH 46, FlTom 35, Crash 52) 7 = Slate SSD5:         Steven Slate Drums 5
//   (Kick 36, Snare 38, Ghost 38, Rimshot 40, CH 42, OH 46, MidTom 45, Crash 49) 8 = NI Studio / Abbey:  Kontakt Studio
//   Drummer / Abbey Road (Kick 36, Snare 38, Ghost 38, Rimshot 40, CH 42, OH 46, HiTom 48, Ride 51) 9 = XLN XO:
//   Hardcoded 8-slot XO layout (Kick 36, Snare 38, Ghost 38, Clap 39, CH 40, OH 41, Flex1 42, Flex2 43)
inline constexpr std::array<std::array<std::int8_t, kDrumLaneCount>, kNoteMapCount> kKitNoteMaps{{
    {36, 38, 37, 39, 42, 46, 43, 49}, // 0: Live Rack / GM
    {36, 36, 37, 38, 37, 38, 39, 40}, // 1: Strike Body
    {36, 37, 38, 39, 40, 41, 42, 43}, // 2: Chromatic
    {36, 38, 43, 37, 49, 55, 67, 77}, // 3: Addictive Drums 2
    {36, 38, 38, 40, 42, 46, 43, 49}, // 4: Toontrack (SD3/EZD)
    {24, 26, 28, 29, 42, 46, 31, 49}, // 5: BFD3
    {24, 26, 26, 30, 43, 46, 35, 52}, // 6: GetGood Drums
    {36, 38, 38, 40, 42, 46, 45, 49}, // 7: Slate SSD5
    {36, 38, 38, 40, 42, 46, 48, 51}, // 8: NI Studio / Abbey
    {36, 38, 38, 39, 40, 41, 42, 43}, // 9: XLN XO
}};

inline constexpr std::array<std::array<std::string_view, kDrumLaneCount>, kNoteMapCount> kKitLaneNames{{
    {"KICK", "SNARE", "GHOST", "CLAP", "CL HAT", "OP HAT", "PERC A", "PERC B"},      // 0: Live Rack / GM
    {"MAIN K", "MAIN S", "GHOST", "RIM", "GHOST2", "RIM 2", "ROLL A", "ROLL B"},     // 1: Strike Body
    {"PAD 1", "PAD 2", "PAD 3", "PAD 4", "PAD 5", "PAD 6", "PAD 7", "PAD 8"},        // 2: Chromatic
    {"KICK", "SNARE", "SHALLOW", "RIMSHOT", "HH TIP", "HH OPEN", "TOM", "CRASH"},    // 3: Addictive Drums 2
    {"KICK", "SNARE", "GHOST", "RIMSHOT", "HH TIP", "HH OPEN", "FL TOM", "CRASH"},   // 4: Toontrack (SD3/EZD)
    {"KICK", "SNARE", "SN ALT", "RIMSHOT", "HH TIP", "HH OPEN", "FL TOM", "CRASH"},  // 5: BFD3
    {"KICK", "SNARE", "GHOST", "X-STICK", "HH TIP", "HH OPEN", "FL TOM", "CRASH"},   // 6: GetGood Drums
    {"KICK", "SNARE", "GHOST", "RIMSHOT", "HH CLSD", "HH OPEN", "MID TOM", "CRASH"}, // 7: Slate SSD5
    {"KICK", "SNARE", "GHOST", "RIMSHOT", "HH CLSD", "HH OPEN", "HI TOM", "RIDE"},   // 8: NI Studio / Abbey
    {"KICK", "SNARE", "GHOST", "CLAP", "CL HAT", "OP HAT", "FLEX 1", "FLEX 2"},      // 9: XLN XO
}};

constexpr std::string_view kitLaneName(NoteMap map, int lane) noexcept {
    const auto mIdx = static_cast<std::size_t>(std::clamp(static_cast<int>(map), 0, kNoteMapCount - 1));
    const auto lIdx = static_cast<std::size_t>(std::clamp(lane, 0, kDrumLaneCount - 1));
    return kKitLaneNames[mIdx][lIdx];
}

enum class StepState : std::int8_t {
    autoGen = 0,   // Follow active style / Euclidean generator
    forceOn = 1,   // Explicitly painted ON
    forceOff = -1, // Explicitly muted / cleared OFF
};

struct StepEdit {
    StepState state = StepState::autoGen;
    std::int8_t velOffset = 0;  // -64..+64 velocity sculpt
    std::uint8_t ratchet = 0;   // 0 = auto, 2/3/4 = explicit sub-hit ratchet burst
    std::int8_t microNudge = 0; // -24..+24 (1/96 beat => +-1/4 beat)
    bool flam = false;          // Pre-hit grace flam (-16 ms)

    bool hasAny() const noexcept CL_NONBLOCKING {
        return state != StepState::autoGen || velOffset != 0 || ratchet != 0 || microNudge != 0 || flam;
    }
    bool operator==(const StepEdit&) const noexcept = default;
};

struct LaneConfig {
    std::uint8_t stepLength = 0; // 0 = auto from style (16 or polymetric default), else 1..32
    std::uint8_t midiNote = 0;   // 0 = use active KitMap note, else 1..127 custom MIDI note
    bool muted = false;
    bool solo = false;
    bool locked = false;
    std::uint8_t euclidHits = 0; // 0 = auto, 1..32 explicit Euclidean hits
    std::uint8_t euclidRot = 0;  // 0..31 rotation
    std::uint8_t lockedVar = 0;  // 0 = follow global, 1..64 captured variation when locked
    std::array<StepEdit, kMaxLaneSteps> steps{};

    bool hasAny() const noexcept CL_NONBLOCKING {
        if (stepLength != 0 || midiNote != 0 || muted || solo || locked || euclidHits != 0 || euclidRot != 0 ||
            lockedVar != 0)
            return true;
        for (const auto& s : steps) {
            if (s.hasAny())
                return true;
        }
        return false;
    }
    bool operator==(const LaneConfig&) const noexcept = default;
};

struct SceneData {
    bool customized = false;
    std::int8_t styleOverride = -1;    // -1 = inherit global param, else 0..11
    std::int8_t fillTypeOverride = -1; // -1 = inherit global param, else 0..5
    std::int8_t variationOffset = 0;   // -32..+32 relative variation shift
    float complexityBias = 0.0f;       // -1..+1 relative complexity bias
    float chopBias = 0.0f;             // -1..+1 relative chop bias
    float ghostBias = 0.0f;            // -1..+1 relative ghost bias
    std::array<LaneConfig, kDrumLaneCount> lanes{};

    bool hasAny() const noexcept CL_NONBLOCKING {
        if (customized || styleOverride >= 0 || fillTypeOverride >= 0 || variationOffset != 0 ||
            std::abs(complexityBias) > 1e-4f || std::abs(chopBias) > 1e-4f || std::abs(ghostBias) > 1e-4f)
            return true;
        for (const auto& l : lanes) {
            if (l.hasAny())
                return true;
        }
        return false;
    }
    bool operator==(const SceneData&) const noexcept = default;
};

struct PatternBank {
    std::array<SceneData, kSceneCount> scenes{};
    std::uint8_t slicerLength = 16; // 1..32 polymetric slicer gate length
    std::array<StepEdit, kMaxLaneSteps> slicerSteps{};

    bool hasAny() const noexcept CL_NONBLOCKING {
        if (slicerLength != 16)
            return true;
        for (const auto& sc : scenes) {
            if (sc.hasAny())
                return true;
        }
        for (const auto& st : slicerSteps) {
            if (st.hasAny())
                return true;
        }
        return false;
    }

    void clear() noexcept CL_NONBLOCKING {
        scenes = {};
        slicerLength = 16;
        slicerSteps = {};
    }

    bool operator==(const PatternBank&) const noexcept = default;
};

// Resolve effective MIDI note number for a drum lane under a given KitMap.
std::int8_t effectiveLaneMidiNote(NoteMap map, int lane, const LaneConfig& cfg) noexcept CL_NONBLOCKING;

// Returns the built-in scene character biases when a scene hasn't been manually customized yet:
//   Scene A (0): Main dialed groove
//   Scene B (1): Elevated chop + higher complexity variation
//   Scene C (2): Stripped tight intro/verse groove
//   Scene D (3): Polyrhythmic / syncopated contrast switch
SceneData effectiveSceneData(const PatternBank& bank, int sceneIdx) noexcept CL_NONBLOCKING;

// Tactile UI action helpers (respect locked lanes!).
void mutateUnlockedLanes(SceneData& scene, std::uint32_t seed) noexcept;
void applyPolymeterToUnlocked(SceneData& scene, std::uint32_t seed) noexcept;
void cloneSceneSlot(PatternBank& bank, int fromIdx, int toIdx, bool mutateClone, std::uint32_t seed) noexcept;
void clearSceneEdits(SceneData& scene) noexcept;

std::string midiNoteLabel(int midiNote);

} // namespace cl
