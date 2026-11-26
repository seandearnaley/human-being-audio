// SPDX-License-Identifier: AGPL-3.0-or-later
// Music-theory scale tables, chord-bar labels, drum lane names, and MIDI note formatting.
#pragma once

#include "cl/params.h"

#include <array>
#include <string>

namespace cl {

inline constexpr std::array<const char*, 6> kDrumLaneLabels{"KICK", "SNARE", "GHOST", "CH HAT", "OP HAT", "RATCH"};
inline constexpr std::array<const char*, 12> kNoteNames{"C",  "C#", "D",  "D#", "E",  "F",
                                                        "F#", "G",  "G#", "A",  "A#", "B"};

struct ScaleInfo {
    std::array<int, 12> intervals{};
    std::array<const char*, 12> numerals{};
    int count = 7;
};

const ScaleInfo& scaleInfo(Scale scale) noexcept;
std::string chordBarLabel(int degreeOffset, int root, Scale scale);
std::string midiNoteLabel(int midiNote);

} // namespace cl
