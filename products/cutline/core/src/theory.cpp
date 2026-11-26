// SPDX-License-Identifier: AGPL-3.0-or-later
#include "cl/theory.h"

#include <hba/core/note_name.h>

#include <algorithm>
#include <cstdio>

namespace cl {

namespace {
constexpr std::array<ScaleInfo, 6> kScaleTable{{
    // 0: Phrygian
    {{{0, 1, 3, 5, 7, 8, 10, 0, 0, 0, 0, 0}}, {"i", "bII", "bIII", "iv", "v\u00b0", "bVI", "bVII"}, 7},
    // 1: Natural Minor
    {{{0, 2, 3, 5, 7, 8, 10, 0, 0, 0, 0, 0}}, {"i", "ii\u00b0", "bIII", "iv", "v", "bVI", "bVII"}, 7},
    // 2: Phrygian Dominant
    {{{0, 1, 4, 5, 7, 8, 10, 0, 0, 0, 0, 0}}, {"I", "bII", "iii\u00b0", "iv", "v\u00b0", "bVI", "bvii"}, 7},
    // 3: Dorian
    {{{0, 2, 3, 5, 7, 9, 10, 0, 0, 0, 0, 0}}, {"i", "ii", "bIII", "IV", "v", "vi\u00b0", "bVII"}, 7},
    // 4: Pentatonic Minor
    {{{0, 3, 5, 7, 10, 0, 0, 0, 0, 0, 0, 0}}, {"i", "bIII", "iv", "v", "bVII"}, 5},
    // 5: Chromatic
    {{{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
     {"0", "+1", "+2", "+3", "+4", "+5", "+6", "+7", "+8", "+9", "+10", "+11"},
     12},
}};
} // namespace

const ScaleInfo& scaleInfo(Scale scale) noexcept {
    return kScaleTable[static_cast<std::size_t>(std::clamp(static_cast<int>(scale), 0, 5))];
}

std::string chordBarLabel(int degreeOffset, int root, Scale scale) {
    const auto& sc = scaleInfo(scale);
    int d = degreeOffset;
    while (d < 0)
        d += sc.count;
    while (d >= sc.count)
        d -= sc.count;
    const int semi = sc.intervals[static_cast<std::size_t>(d)];
    const int pc = ((root + semi) % 12 + 12) % 12;
    const char* arrow = (degreeOffset >= sc.count) ? "\u2191" : (degreeOffset <= -sc.count ? "\u2193" : "");
    char buf[24]{};
    std::snprintf(buf, sizeof(buf), "%s%s  %s", sc.numerals[static_cast<std::size_t>(d)], arrow,
                  kNoteNames[static_cast<std::size_t>(pc)]);
    return buf;
}

std::string midiNoteLabel(int midiNote) { return hba::core::midiNoteName(std::clamp(midiNote, 0, 127)); }

} // namespace cl
