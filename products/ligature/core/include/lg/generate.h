// SPDX-License-Identifier: AGPL-3.0-or-later
// Turns a song item (a section, or a bridge at a given length) into the notes every part plays.
// Allocates: call it off the audio thread. Deterministic for a given song.
#pragma once

#include "lg/song.h"

#include <array>
#include <cstdint>
#include <vector>

namespace lg {

struct Span {
    Chord chord;
    float start = 0.0f; // steps
    float dur = 16.0f;
    std::vector<int> voicing;
    int bass = 36;
    Analysis analysis;   // in the item's key (a bridge's departure key)
    Analysis toAnalysis; // bridges only: in the arrival key
    bool pivot = false;
    float tension = 0.2f;
};

enum NoteFlag : std::uint8_t { kNotePinned = 1u << 0u, kNoteStutter = 1u << 1u };

struct Note {
    float start = 0.0f; // steps from the item's start
    float dur = 1.0f;
    int pitch = 60;
    float velocity = 0.8f; // 0..1
    Role role = Role::chords;
    std::uint8_t flags = 0;
    int span = -1; // the chord the note belongs to
};

struct Pattern {
    ItemRef item;
    int bars = 4;
    bool bridge = false;
    Key key;
    Key toKey;
    RouteId route = RouteId::backdoor;
    std::vector<Span> spans;
    std::vector<Note> notes; // sorted by start
    StutterCells cells;      // the cells that played: the bridge route's own, plus the user's

    int totalSteps() const noexcept { return bars * kStepsPerBar; }
    int spanAt(float step) const noexcept;
};

// `barsOverride` regenerates a bridge at a squeezed length (so every part stays on the grid); user stutter cells and
// pins only apply at the full length. `partsOverride` previews other takes without touching the song.
Pattern buildPattern(const Song& song, ItemRef item, int barsOverride = 0,
                     const std::array<PartSettings, kRoleCount>* partsOverride = nullptr);

// The chords an item plays, with beats scaled to its length (a bridge's route chords are written in 8 beats).
std::vector<Chord> itemChords(const Song& song, ItemRef item, int barsOverride = 0);

// The voice-leading shape the item's last chord leaves, which the next chord voices from.
std::vector<int> closingLead(const Song& song, int section);

} // namespace lg
