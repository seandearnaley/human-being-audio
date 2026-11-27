// SPDX-License-Identifier: AGPL-3.0-or-later
// Drum lanes, kit note maps and pad labels. The pattern data itself lives in cl/groove.h.
#pragma once

#include "cl/nonblocking.h"
#include "cl/params.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace cl {

inline constexpr int kDrumLaneCount = 8;

inline constexpr std::array<std::string_view, kDrumLaneCount> kDrumLaneNames{"KICK",   "SNARE",  "GHOST",  "CLAP",
                                                                             "CL HAT", "OP HAT", "PERC A", "PERC B"};

inline constexpr std::array<std::string_view, kPerfPadCount> kPerfPadLabels{"Repeat", "Roll", "Rate", "Drop"};
inline constexpr std::array<std::string_view, kPerfPadCount> kPerfPadXLabels{"Window", "Rate", "Speed",
                                                                             "Cut \u2194 Fill"};
inline constexpr std::array<std::string_view, kPerfPadCount> kPerfPadYLabels{"Gate", "Velocity", "Kick \u2194 Tops",
                                                                             ""};
inline constexpr std::array<std::string_view, kPerfPadCount> kPerfPadHints{"C3", "C#3", "D3", "D#3"};

// Ten built-in MIDI output maps for the 8 drum lanes, in the order of the noteMap parameter's choices:
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

// The MIDI note a drum lane plays: its override when that is 1..127, otherwise the kit map's note.
std::int8_t effectiveLaneMidiNote(NoteMap map, int lane, int overrideNote) noexcept CL_NONBLOCKING;

std::string midiNoteLabel(int midiNote);

} // namespace cl
