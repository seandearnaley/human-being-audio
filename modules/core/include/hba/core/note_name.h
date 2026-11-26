// SPDX-License-Identifier: GPL-3.0-or-later
// Shared MIDI note and frequency note-name formatting helpers.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace hba::core {

inline std::string midiNoteName(int midiNote, bool useFlats = false) {
    static constexpr std::array<const char*, 12> kSharps{"C",  "C#", "D",  "D#", "E",  "F",
                                                         "F#", "G",  "G#", "A",  "A#", "B"};
    static constexpr std::array<const char*, 12> kFlats{"C",  "Db", "D",  "Eb", "E",  "F",
                                                        "Gb", "G",  "Ab", "A",  "Bb", "B"};
    const int pc = ((midiNote % 12) + 12) % 12;
    const int oct = (midiNote >= 0 ? (midiNote / 12) : ((midiNote - 11) / 12)) - 1;
    const auto& table = useFlats ? kFlats : kSharps;
    return std::string(table[static_cast<std::size_t>(pc)]) + std::to_string(oct);
}

inline std::string hzToNoteName(float hz, bool useFlats = false) {
    const double safeHz = std::max(16.0, std::isfinite(hz) ? static_cast<double>(hz) : 440.0);
    const int midi = static_cast<int>(std::lround(69.0 + 12.0 * std::log2(safeHz / 440.0)));
    return midiNoteName(midi, useFlats);
}

} // namespace hba::core
