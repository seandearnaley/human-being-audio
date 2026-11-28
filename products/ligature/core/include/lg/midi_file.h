// SPDX-License-Identifier: AGPL-3.0-or-later
// Standard MIDI files of a part, an item or the whole song, for drag-out.
#pragma once

#include "lg/document.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace lg {

inline constexpr int kMidiFilePpq = 960;

// One item (a section, or a bridge at full length). With a role: one track on that part's channel.
// Without: four tracks, one per part, on channels 1-4. Muted parts are left out.
std::vector<std::uint8_t> itemMidiFile(Document& doc, ItemRef item, std::optional<Role> role, double bpm);

// The song in strip order: each section played `passes` times, then its bridge into the next section.
std::vector<std::uint8_t> songMidiFile(Document& doc, std::optional<Role> role, double bpm, int passes = 2);

struct TimedNote {
    double startBeats = 0.0;
    double endBeats = 0.0;
    int pitch = 60;
    int velocity = 100;
    Role role = Role::chords;
};

// A free list of notes (a captured performance) as a four-track file.
std::vector<std::uint8_t> notesMidiFile(const std::vector<TimedNote>& notes, std::optional<Role> role, double bpm);

} // namespace lg
