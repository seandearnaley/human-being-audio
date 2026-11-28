// SPDX-License-Identifier: AGPL-3.0-or-later
// A chord's neighbourhood (the three palette rings) and the progression library.
#pragma once

#include "lg/song.h"

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace lg {

enum class Ring : std::uint8_t { inKey, borrowed, outside };

struct PaletteEntry {
    Chord chord;
    Ring ring = Ring::inKey;
    int sharedBefore = 0; // notes kept from the chord before
    int sharedAfter = 0;  // notes kept from the chord after
};

inline constexpr int kRingSize[3] = {6, 6, 8};

// The chords a section's chord at `index` could become: up to 6 in key, 6 borrowed or secondary, 8 outside.
std::vector<PaletteEntry> neighbourhood(const Section& section, int index);

struct ProgressionChord {
    int interval = 0; // above the section's tonic
    Quality quality = Quality::maj;
    int bassInterval = -1;
};

struct Progression {
    std::string_view name;
    std::string_view pack; // genre pack
    Mode mode = Mode::minor;
    std::vector<ProgressionChord> chords;
};

// Names a chord from the pitch classes being held (bit n = pitch class n) and the lowest one: the best-matching
// quality and root, with a slash bass when the lowest note is not the root. Empty for fewer than three notes.
std::optional<Chord> identifyChord(std::uint16_t heldMask, int lowestPc);

std::span<const Progression> progressions();
std::vector<const Progression*> progressionsFor(Mode mode);
// The progression's chords in a key, sharing `bars` evenly.
std::vector<Chord> realise(const Progression& p, Key key, int bars);

} // namespace lg
