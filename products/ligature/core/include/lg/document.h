// SPDX-License-Identifier: AGPL-3.0-or-later
// The editable song: every edit the editor offers, undo/redo and A/B, and a pattern cache.
// Message thread only. The plugin and the UI's fake model both wrap one of these.
#pragma once

#include "lg/generate.h"
#include "lg/palette.h"
#include "lg/song.h"

#include <StudioHistory.h>

#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace lg {

enum class Mutation : std::uint8_t { nudge, twist, rewrite };

class Document {
  public:
    Document();
    explicit Document(Song song);

    const Song& song() const noexcept { return song_; }
    std::uint64_t revision() const noexcept { return revision_; }

    // Patterns, cached until the next change. `fit` squeezes a bridge (0 = full length).
    const Pattern& pattern(ItemRef item, int fit = 0);
    Pattern previewTake(ItemRef item, Role role, int take) const; // the "other takes" thumbnails, take 1..4
    std::vector<PaletteEntry> palette(int section, int index) const;
    std::vector<Route> routes(int bridge) const;

    // Replace the whole song (presets, state restore). loadCount() tells a view that a new song arrived.
    void load(Song song, bool undoable = true);
    std::uint64_t loadCount() const noexcept { return loads_; }
    // A follower takes its leader's song: no history, and the view keeps its place (not counted as a load).
    void mirror(Song song);

    bool canUndo() const noexcept { return history_.canUndo(); }
    bool canRedo() const noexcept { return history_.canRedo(); }
    void undo();
    void redo();
    bool isSlotB() const noexcept { return history_.isSlotB(); }
    void toggleAB();
    void copyAB();

    // Chords
    void replaceChord(int section, int index, const Chord& chord); // keeps the slot's length, voicing and inversion
    void setVoicingStyle(int section, int index, VoicingStyle style);
    bool stepInversion(int section, int index, int delta); // false (and no undo step) when already at the limit
    void setLevel(int section, int index, int level);      // in-key chords take their extensions from the key
    void setVoiceLead(bool on);

    // Parts
    void setStyle(Role role, int style);
    void setDensity(Role role, float density, bool gestureStart); // one undo step per drag
    void setRegister(Role role, int reg);
    void setRate(Role role, float rate);
    void mutate(Role role, Mutation m);
    void applyTake(Role role, int take);
    void toggleMute(Role role);
    void toggleSolo(Role role);
    void togglePin(ItemRef item, const Note& note);
    void clearPins(ItemRef item, Role role);
    int pinCount(ItemRef item, Role role) const;

    // Stutter: none -> 1/32 -> 1/64 -> gate -> none
    void cycleStutter(ItemRef item, int step);

    // Bridges
    void setRoute(int bridge, RouteId route);
    void setBridgeBars(int bridge, int bars);

    // Sections
    void setSectionKey(int section, Key key); // transposes chords and pins; a mode change maps in-key chords
    void setSectionBars(int section, int bars);
    void loadProgression(int section, const Progression& progression);
    void setSectionName(int section, std::string name);
    bool addSection();
    bool removeSection(int section);

    void setBpm(double bpm);
    void setName(std::string name);

  private:
    template <typename Fn> void change(Fn&& fn, bool undoable = true);
    void changed();

    Song song_;
    std::uint64_t revision_ = 1;
    std::uint64_t loads_ = 0;
    hba::studio::History<Song, 96> history_;
    std::map<std::pair<ItemRef, int>, Pattern> cache_;
};

std::string_view mutationName(Mutation m) noexcept;

} // namespace lg
